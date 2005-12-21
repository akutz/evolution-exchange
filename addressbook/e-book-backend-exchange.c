/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2000-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <libedataserver/e-sexp.h>
#include <libedataserver/e-uid.h>
#include <e-util/e-util.h>
#include <libebook/e-address-western.h>
#include <libebook/e-contact.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-book-backend-summary.h>
#include <libedata-book/e-book-backend-cache.h>
#include <libedataserver/e-xml-hash-utils.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>

#include <e2k-context.h>
#include <e2k-propnames.h>
#include <e2k-restriction.h>
#include <e2k-uri.h>
#include <e2k-utils.h>
#include <mapi.h>
#include <exchange-account.h>
#include <exchange-hierarchy.h>
#include <e-folder-exchange.h>
#include "e-book-backend-exchange.h"
#include "exchange-component.h"

#define DEBUG
#define d(x)

#define SUMMARY_FLUSH_TIMEOUT 5000

#define PARENT_TYPE E_TYPE_BOOK_BACKEND_SYNC
static EBookBackendClass *parent_class;

struct EBookBackendExchangePrivate {
	char     *exchange_uri;
	char 	 *original_uri;
	EFolder  *folder;

	E2kRestriction *base_rn;

	ExchangeAccount *account;
	E2kContext *ctx;
	gboolean connected;
	GHashTable *ops;
	int mode;
	gboolean is_writable;
	gboolean is_cache_ready;
	gboolean marked_for_offline;

	GMutex *create_mutex;

	EBookBackendSummary *summary;
	EBookBackendCache *cache;
};

typedef struct PropMapping PropMapping;

static void subscription_notify (E2kContext *ctx, const char *uri, E2kContextChangeType type, gpointer user_data);
static void proppatch_address(PropMapping *prop_mapping, EContact *new_contact, EContact *cur_contact, E2kProperties *props);
static void proppatch_email(PropMapping *prop_mapping, EContact *new_contact, EContact *cur_contact, E2kProperties *props);
static void proppatch_date(PropMapping *prop_mapping, EContact *new_contact, EContact *cur_contact, E2kProperties *props);
static void populate_address(EContactField field, EContact *new_contact, void *data);
static void populate_date(EContactField field, EContact *new_contact, void *data);
static void proppatch_categories (PropMapping *prop_mapping, EContact *new_contact, EContact *cur_contact, E2kProperties *props);
static void populate_categories (EContactField field, EContact *new_contact, void *data);
static E2kRestriction *e_book_backend_exchange_build_restriction (const char *sexp,
							       E2kRestriction *base_rn);

static const char *contact_name (EContact *contact);
static void proppatch_im (PropMapping *prop_mapping, EContact *new_contact, EContact *cur_contact, E2kProperties *props);
static void populate_im (EContactField field, EContact *new_contact, void *data);

static GPtrArray *field_names_array;
static const char **field_names;
static int n_field_names;

static GNOME_Evolution_Addressbook_CallStatus
http_status_to_pas (E2kHTTPStatus status)
{
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return GNOME_Evolution_Addressbook_Success;

	switch (status) {
	case E2K_HTTP_UNAUTHORIZED:
		return GNOME_Evolution_Addressbook_PermissionDenied;
	case E2K_HTTP_CANT_CONNECT:
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	default:
		return GNOME_Evolution_Addressbook_OtherError;
	}
}

#define DATE_FIELD(x,z) { E_CONTACT_##x, E2K_PR_CONTACTS_##x, z, FLAG_COMPOSITE | FLAG_UNLIKEABLE, proppatch_date, populate_date }
#define EMAIL_FIELD(x,z) { E_CONTACT_##x, E2K_PR_MAPI_##x##_ADDRESS, z, FLAG_EMAIL | FLAG_COMPOSITE, proppatch_email }
#define ADDRESS_FIELD(x,z) { E_CONTACT_##x, E2K_PR_CONTACTS_##x, z, FLAG_COMPOSITE, proppatch_address, populate_address }

#define CONTACT_FIELD(x,z) { E_CONTACT_##x, E2K_PR_CONTACTS_##x, z, 0 }
#define MAPI_ID_FIELD(x,y,z) { E_CONTACT_##x, y, z, FLAG_UNLIKEABLE, NULL }
#define CAL_FIELD(x,z) { E_CONTACT_##x, E2K_PR_CALENDAR_##x, z, FLAG_UNLIKEABLE, NULL }
#define HTTPMAIL_FIELD(x,y,z) { E_CONTACT_##x, E2K_PR_HTTPMAIL_##y, z, FLAG_UNLIKEABLE | FLAG_PUT, NULL }
#define EXCHANGE_FIELD(x,y,z,w,v) { E_CONTACT_##x, E2K_PR_EXCHANGE_##y, z, FLAG_COMPOSITE, w, v }
#define IM_FIELD(x,z) { E_CONTACT_##x, E2K_PR_OUTLOOK_CONTACT_IM_ADDR, z, FLAG_COMPOSITE, proppatch_im, populate_im }

struct PropMapping {
	EContactField field;
	char *prop_name;
	char *e_book_field;
	int flags;
#define FLAG_UNLIKEABLE 0x001  /* can't use LIKE with it */
#define FLAG_COMPOSITE  0x002  /* read-only fields that can be written
                                 to only by specifying the values of
                                 individual parts (as other fields) */
#define FLAG_PUT        0x020  /* requires a PUT request */
#define FLAG_EMAIL      0x100  /* email field, so we know to invoke our magic email address handling */
	void (*composite_proppatch_func)(PropMapping *prop_mapping, EContact *new_contact, EContact *cur_contact, E2kProperties *props);
	void (*composite_populate_func)(EContactField e_book_field, EContact *new_contact, void *data);
};

static PropMapping
prop_mappings[] = {
	CONTACT_FIELD (FULL_NAME, "full_name"),
	CONTACT_FIELD (FAMILY_NAME, "family_name"),
	CONTACT_FIELD (GIVEN_NAME, "given_name"),
/*	CONTACT_FIELD (ADDITIONAL_NAME, "middle_name"), FIXME */
/*	CONTACT_FIELD (NAME_SUFFIX, "name_suffix"), FIXME */
	CONTACT_FIELD (TITLE, "title"),
	CONTACT_FIELD (ORG, "org"),
	CONTACT_FIELD (FILE_AS, "file_as"),

	CONTACT_FIELD (PHONE_CALLBACK, "callback_phone"),
	CONTACT_FIELD (PHONE_BUSINESS_FAX, "business_fax"),
	CONTACT_FIELD (PHONE_HOME_FAX, "home_fax"),
	CONTACT_FIELD (PHONE_HOME, "home_phone"),
	CONTACT_FIELD (PHONE_HOME_2, "home_phone_2"),
	CONTACT_FIELD (PHONE_ISDN, "isdn"),
	CONTACT_FIELD (PHONE_MOBILE, "mobile_phone"),
	CONTACT_FIELD (PHONE_COMPANY, "company_phone"),
	CONTACT_FIELD (PHONE_OTHER_FAX, "other_fax"),
	CONTACT_FIELD (PHONE_PAGER, "pager"),
	CONTACT_FIELD (PHONE_BUSINESS, "business_phone"),
	CONTACT_FIELD (PHONE_BUSINESS_2, "business_phone_2"),
	CONTACT_FIELD (PHONE_TELEX, "telex"),
	CONTACT_FIELD (PHONE_TTYTDD, "tty"),
	CONTACT_FIELD (PHONE_ASSISTANT, "assistant_phone"),
	CONTACT_FIELD (PHONE_CAR, "car_phone"),
	CONTACT_FIELD (PHONE_OTHER, "other_phone"),
	MAPI_ID_FIELD (PHONE_RADIO, PR_RADIO_TELEPHONE_NUMBER, "radio"),
	MAPI_ID_FIELD (PHONE_PRIMARY, PR_PRIMARY_TELEPHONE_NUMBER, "primary_phone"),

	EMAIL_FIELD (EMAIL_1, "email_1"),
	EMAIL_FIELD (EMAIL_2, "email_2"),
	EMAIL_FIELD (EMAIL_3, "email_3"),

	ADDRESS_FIELD (ADDRESS_WORK, "business_address"),
	ADDRESS_FIELD (ADDRESS_HOME, "home_address"),
	ADDRESS_FIELD (ADDRESS_OTHER, "other_address"),

	CONTACT_FIELD (HOMEPAGE_URL, "url"),
	CONTACT_FIELD (ORG_UNIT, "org_unit"),
	CONTACT_FIELD (OFFICE, "office"),
	CONTACT_FIELD (ROLE, "role"),
	CONTACT_FIELD (MANAGER, "manager"),
	CONTACT_FIELD (ASSISTANT, "assistant"),
	CONTACT_FIELD (NICKNAME, "nickname"),
	CONTACT_FIELD (SPOUSE, "spouse"),

	DATE_FIELD (BIRTH_DATE, "birth_date"),
	DATE_FIELD (ANNIVERSARY, "anniversary"),
	CAL_FIELD (FREEBUSY_URL, "fburl"),
	IM_FIELD (IM_ICQ, "icq"),
	IM_FIELD (IM_AIM, "aim"),
	IM_FIELD (IM_MSN, "msn"),
	IM_FIELD (IM_YAHOO, "yahoo"),
	IM_FIELD (IM_JABBER, "jabber"),
	IM_FIELD (IM_GROUPWISE, "nov"),

	HTTPMAIL_FIELD (NOTE, TEXT_DESCRIPTION, "note"),

	EXCHANGE_FIELD (CATEGORIES, KEYWORDS, "categories", proppatch_categories, populate_categories)
};
static int num_prop_mappings = sizeof(prop_mappings) / sizeof (prop_mappings[0]);

static const char *
e_book_backend_exchange_prop_to_exchange (char *propname)
{
	int i;

	for (i = 0; i < num_prop_mappings; i ++)
		if (prop_mappings[i].e_book_field && !strcmp (prop_mappings[i].e_book_field, propname))
			return prop_mappings[i].prop_name;

	return NULL;
}

static void
get_email_field_from_props (ExchangeAccount *account,
			    PropMapping *prop_mapping, E2kResult *result,
			    EContact *contact, char *data)
{
	char *emailtype;
	char *typeselector;

	/* here's where we do the super involved
	   conversion from local email addresses to
	   internet addresses for display in
	   evolution. */
	if (prop_mapping->field == E_CONTACT_EMAIL_1)
		typeselector = E2K_PR_MAPI_EMAIL_1_ADDRTYPE;
	else if (prop_mapping->field == E_CONTACT_EMAIL_2)
		typeselector = E2K_PR_MAPI_EMAIL_2_ADDRTYPE;
	else if (prop_mapping->field == E_CONTACT_EMAIL_3)
		typeselector = E2K_PR_MAPI_EMAIL_3_ADDRTYPE;
	else {
		g_warning ("invalid email field");
		return;
	}

	emailtype = e2k_properties_get_prop (result->props, typeselector);
	if (!emailtype) {
		g_warning ("no email info for \"%s\"", data);
		return;
	}
	if (!strcmp (emailtype, "SMTP")) {
		/* it's a normal email address, just set the value as we usually would */
		e_contact_set (contact, prop_mapping->field, data);
	}
	else if (!strcmp (emailtype, "EX")) {
		E2kGlobalCatalog *gc;
		E2kGlobalCatalogEntry *entry = NULL;

		gc = exchange_account_get_global_catalog (account);
		if (gc) {
			e2k_global_catalog_lookup (
				gc, NULL, /* FIXME: cancellable */
				E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
				data, E2K_GLOBAL_CATALOG_LOOKUP_EMAIL,
				&entry);
		}
		if (entry) {
			e_contact_set (contact, prop_mapping->field,
				       entry->email);
			e2k_global_catalog_entry_free (gc, entry);
		} else {
			g_warning ("invalid EX address");
			return;
		}
	}
	else if (emailtype[0] == '\0') {
		/* empty emailtype == no email address here */
		return;
	}
	else {
		g_warning ("email address type `%s' not handled, using the value as is", emailtype);
		e_contact_set (contact, prop_mapping->field, data);
	}

	return;
}

/* Exchange uses \r in some strings and \r\n in others. EContact always
 * wants just \n.
 */
static char *
unixify (const char *string)
{
	char *out = g_malloc (strlen (string) + 1), *d = out;
	const char *s = string;

	do {
		if (*s == '\r') {
			if (*(s + 1) != '\n')
				*d++ = '\n';
		} else
			*d++ = *s;
	} while (*s++);

	return out;
}

static EContact *
e_contact_from_props (EBookBackendExchange *be, E2kResult *result)
{
	EContact *contact;
	char *data, *body;
	const char *filename;
	E2kHTTPStatus status;
	CamelStream *stream;
	CamelMimeMessage *msg;
	CamelDataWrapper *content;
	CamelMultipart *multipart;
	CamelMimePart *part;
	int i, len;

	contact = e_contact_new ();

	e_contact_set (contact, E_CONTACT_UID, result->href);

	for (i = 0; i < num_prop_mappings; i ++) {
		data = e2k_properties_get_prop (result->props,
						prop_mappings[i].prop_name);
		if (!data)
			continue;

		if (prop_mappings[i].flags & FLAG_EMAIL) {
			get_email_field_from_props (be->priv->account,
						    &prop_mappings[i],
						    result, contact, data);
		} else if (prop_mappings[i].flags & FLAG_COMPOSITE) {
			prop_mappings[i].composite_populate_func (
				prop_mappings[i].field, contact, data);
		} else {
			char *unix_data;

			unix_data = strchr (data, '\r') ?
				unixify (data) : data;
			e_contact_set (contact, prop_mappings[i].field,
				       unix_data);
			if (unix_data != data)
				g_free (unix_data);
		}
	}

	data = e2k_properties_get_prop (result->props, E2K_PR_HTTPMAIL_HAS_ATTACHMENT);
	if (!data || !atoi(data))
		return contact;

	/* Fetch the body and parse out the photo */
	status = e2k_context_get (be->priv->ctx, NULL, result->href,
				  NULL, &body, &len);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("e_contact_from_props: %d", status);
		return contact;
	}

	stream = camel_stream_mem_new_with_buffer (body, len);
	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream);
	camel_object_unref (stream);

	content = camel_medium_get_content_object (CAMEL_MEDIUM (msg));
	if (CAMEL_IS_MULTIPART (content)) {
		multipart = (CamelMultipart *)content;
		content = NULL;

		for (i = 0; i < camel_multipart_get_number (multipart); i++) {
			part = camel_multipart_get_part (multipart, i);
			filename = camel_mime_part_get_filename (part);
			if (filename && !strncmp (filename, "ContactPicture.", 15)) {
				content = camel_medium_get_content_object (CAMEL_MEDIUM (part));
				break;
			}
		}

		if (content) {
			EContactPhoto photo;
			CamelStreamMem *stream_mem;

			stream = camel_stream_mem_new ();
			stream_mem = (CamelStreamMem *)stream;
			camel_data_wrapper_decode_to_stream (content, stream);

			photo.data = stream_mem->buffer->data;
			photo.length = stream_mem->buffer->len;
			e_contact_set (contact, E_CONTACT_PHOTO, &photo);

			camel_object_unref (stream);
		}
	}

	camel_object_unref (msg);
	return contact;
}

static char *
vcard_from_props (EBookBackendExchange *be, E2kResult *result)
{
	EContact *contact;
	char *vcard;

	contact = e_contact_from_props (be, result);
	if (!contact)
		return NULL;

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	g_object_unref (contact);

	return vcard;
}

static void
build_summary (EBookBackendExchange *be)
{
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;

	iter = e_folder_exchange_search_start (bepriv->folder, NULL,
					       field_names, n_field_names,
					       bepriv->base_rn, NULL, TRUE);
	while ((result = e2k_result_iter_next (iter))) {
		EContact *contact = e_contact_from_props (be, result);

		if (!contact) /* XXX should we error out here and destroy the summary? */
			continue;

		e_book_backend_summary_add_contact (bepriv->summary, contact);
		g_object_unref (contact);
	}
	status = e2k_result_iter_free (iter);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("build_summary: error building list\n");
		/* destroy the summary object here, so we don't try to query on it */
		g_object_unref (bepriv->summary);
		bepriv->summary = NULL;
		return;
	}

	e_book_backend_summary_save (bepriv->summary);
}

static const char *folder_props[] = {
	PR_ACCESS,
	E2K_PR_DAV_LAST_MODIFIED
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static gpointer
build_cache (EBookBackendExchange *be)
{
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kResultIter *iter;
	E2kResult *result;
	EContact *contact;

	iter = e_folder_exchange_search_start (bepriv->folder, NULL,
				       field_names, n_field_names,
				       bepriv->base_rn, NULL, TRUE);

	e_file_cache_freeze_changes (E_FILE_CACHE (bepriv->cache));
	while ((result = e2k_result_iter_next (iter))) {
		contact = e_contact_from_props (be, result);
		if (!contact)
			continue;
		e_book_backend_cache_add_contact (bepriv->cache, contact);
		g_object_unref(contact);
	}
	e_book_backend_cache_set_populated (bepriv->cache);
	bepriv->is_cache_ready=TRUE;
	e_file_cache_thaw_changes (E_FILE_CACHE (bepriv->cache));
	return NULL;
}

static gboolean
update_cache (EBookBackendExchange *be)
{
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kResultIter *iter;
	E2kResult *result;
	EContact *contact;
	const char *cache_file_name;
	time_t mod_time;
	char time_string[25];
	const struct tm *tm;
	struct stat buf;

	cache_file_name = 
		e_file_cache_get_filename (E_FILE_CACHE(bepriv->cache)); 

	stat (cache_file_name, &buf);
	mod_time = buf.st_mtime;
	tm = gmtime (&mod_time);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);

	/* build hash table from storage file */

	/* FIXME: extract the difference and build update cache with changes */ 

	iter = e_folder_exchange_search_start (bepriv->folder, NULL,
				       field_names, n_field_names,
				       bepriv->base_rn, NULL, TRUE);

	e_file_cache_freeze_changes (E_FILE_CACHE (bepriv->cache));
	while ((result = e2k_result_iter_next (iter))) {
		contact = e_contact_from_props (be, result);
		if (!contact)
			continue;
		e_book_backend_cache_add_contact (bepriv->cache, contact);
		g_object_unref(contact);
	}
	e_book_backend_cache_set_populated (bepriv->cache);
	bepriv->is_cache_ready=TRUE;
	e_file_cache_thaw_changes (E_FILE_CACHE (bepriv->cache));

	return TRUE;
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_exchange_connect (EBookBackendExchange *be)
{
	EBookBackendExchangePrivate *bepriv = be->priv;
	ExchangeHierarchy *hier;
	char *summary_path;
	char *date_prop, *access_prop;
	int access;
	E2kResult *results;
	int nresults;
	E2kHTTPStatus status;
	time_t folder_mtime;

	bepriv->account = exchange_component_get_account_for_uri (global_exchange_component, bepriv->exchange_uri);
	if (!bepriv->account)
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	bepriv->ctx = exchange_account_get_context (bepriv->account);
	if (!bepriv->ctx)
		return GNOME_Evolution_Addressbook_RepositoryOffline;

	bepriv->folder = exchange_account_get_folder (bepriv->account, bepriv->exchange_uri);
	if (!bepriv->folder)
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	g_object_ref (bepriv->folder);

	/* check permissions on the folder */
	status = e_folder_exchange_propfind (bepriv->folder, NULL,
					     folder_props, n_folder_props,
					     &results, &nresults);

	if (status != E2K_HTTP_MULTI_STATUS) {
		bepriv->connected = FALSE;
		return GNOME_Evolution_Addressbook_OtherError;
	}

	access_prop = e2k_properties_get_prop (results[0].props, PR_ACCESS);
	if (access_prop)
		access = atoi (access_prop);
	else
		access = ~0;

	if (!(access & MAPI_ACCESS_READ)) {
		bepriv->connected = FALSE;
		return GNOME_Evolution_Addressbook_PermissionDenied;
	}

	bepriv->is_writable = ((access & MAPI_ACCESS_CREATE_CONTENTS) != 0);
	e_book_backend_set_is_writable (E_BOOK_BACKEND (be),
			     		bepriv->is_writable);
	e_book_backend_notify_writable (E_BOOK_BACKEND (be),
					bepriv->is_writable);

	bepriv->base_rn = e2k_restriction_orv (
		e2k_restriction_prop_string (E2K_PR_DAV_CONTENT_CLASS,
					     E2K_RELOP_EQ,
					     "urn:content-classes:person"),
		e2k_restriction_prop_string (E2K_PR_DAV_CONTENT_CLASS,
					     E2K_RELOP_EQ,
					     "urn:content-classes:contact"),
		NULL);

	bepriv->base_rn = e2k_restriction_andv (
		bepriv->base_rn,
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		NULL);

	hier = e_folder_exchange_get_hierarchy (bepriv->folder);
	if (hier->hide_private_items) {
		bepriv->base_rn = e2k_restriction_andv (
			bepriv->base_rn,
			e2k_restriction_prop_int (E2K_PR_MAPI_SENSITIVITY,
						  E2K_RELOP_NE, 2),
			NULL);
	}

	/* once we're connected deal with the summary */
	date_prop = e2k_properties_get_prop (results[0].props, E2K_PR_DAV_LAST_MODIFIED);
	if (date_prop)
		folder_mtime = e2k_parse_timestamp (date_prop);

	else
		folder_mtime = 0;

	e2k_results_free (results, nresults);

	/* open the summary file */
	summary_path = e_folder_exchange_get_storage_file (bepriv->folder, "summary");

	bepriv->summary = e_book_backend_summary_new (summary_path, SUMMARY_FLUSH_TIMEOUT);
	if (e_book_backend_summary_is_up_to_date (bepriv->summary, folder_mtime) == FALSE
	    || e_book_backend_summary_load (bepriv->summary) == FALSE ) {
		/* generate the summary here */
		build_summary (be);
	}

	g_free (summary_path);

	/* now subscribe to our folder so we notice when it changes */
	e_folder_exchange_subscribe (bepriv->folder,
				     E2K_CONTEXT_OBJECT_CHANGED, 30,
				     subscription_notify, be);

	bepriv->connected = TRUE;
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (be), TRUE);
	return GNOME_Evolution_Addressbook_Success;
}

static gboolean
value_changed (const char *old, const char *new)
{
	if (old == NULL)
		return new != NULL;
	else if (new == NULL)
		return old != NULL;
	else
		return strcmp (old, new) != 0;
}

static const EContactField email_fields[3] = {
	E_CONTACT_EMAIL_1,
	E_CONTACT_EMAIL_2,
	E_CONTACT_EMAIL_3
};

static const char *email1_props[] = {
	E2K_PR_MAPI_EMAIL_1_ENTRYID,
	E2K_PR_MAPI_EMAIL_1_ADDRTYPE,
	E2K_PR_MAPI_EMAIL_1_ADDRESS,
	E2K_PR_MAPI_EMAIL_1_DISPLAY_NAME,
	E2K_PR_CONTACTS_EMAIL1,
};
static const char *email2_props[] = {
	E2K_PR_MAPI_EMAIL_2_ENTRYID,
	E2K_PR_MAPI_EMAIL_2_ADDRTYPE,
	E2K_PR_MAPI_EMAIL_2_ADDRESS,
	E2K_PR_MAPI_EMAIL_2_DISPLAY_NAME,
	E2K_PR_CONTACTS_EMAIL2,
};
static const char *email3_props[] = {
	E2K_PR_MAPI_EMAIL_3_ENTRYID,
	E2K_PR_MAPI_EMAIL_3_ADDRTYPE,
	E2K_PR_MAPI_EMAIL_3_ADDRESS,
	E2K_PR_MAPI_EMAIL_3_DISPLAY_NAME,
	E2K_PR_CONTACTS_EMAIL3,
};

static const char **email_props[] = {
	email1_props, email2_props, email3_props
};

static void
proppatch_email (PropMapping *prop_mapping,
		 EContact *new_contact, EContact *cur_contact,
		 E2kProperties *props)
{
	gboolean changed;
	char *new_email, *cur_email;
	int field, prop, emaillisttype = 0;

	/* We do all three email addresses (plus some additional data)
	 * when invoked for E_CONTACT_EMAIL_1. So skip EMAIL_2
	 * and EMAIL_3.
	 */
	if (prop_mapping->field != E_CONTACT_EMAIL_1)
		return;

	for (field = 0; field < 3; field++) {
		new_email = e_contact_get (new_contact, email_fields[field]);
		cur_email = cur_contact ? e_contact_get (cur_contact, email_fields[field]) : NULL;

		if (new_email)
			emaillisttype |= (1 << field);

		changed = value_changed (cur_email, new_email);
		g_free (cur_email);

		if (!changed) {
			g_free (new_email);
			continue;
		}

		if (!new_email || !*new_email) {
			g_free (new_email);
			if (!cur_contact)
				continue;

			for (prop = 0; prop < 5; prop++) {
				e2k_properties_remove (
					props, email_props[field][prop]);
			}
			continue;
		}

		/* Clear originalentryid */
		e2k_properties_remove (props, email_props[field][0]);

		/* type is SMTP */
		e2k_properties_set_string (props, email_props[field][1],
					   g_strdup ("SMTP"));

		for (prop = 2; prop < 5; prop++) {
			e2k_properties_set_string (props,
						   email_props[field][prop],
						   g_strdup (new_email));
		}
		g_free (new_email);
	}

	e2k_properties_set_int (props, E2K_PR_MAPI_EMAIL_LIST_TYPE,
				emaillisttype);

	if (emaillisttype) {
		GPtrArray *fields;

		fields = g_ptr_array_new ();
		for (field = 0; field < 3; field++) {
			if (emaillisttype & (1 << field))
				g_ptr_array_add (fields, g_strdup_printf ("%d", field));
		}
		e2k_properties_set_int_array (props,
					      E2K_PR_MAPI_EMAIL_ADDRESS_LIST,
					      fields);
	} else if (cur_contact)
		e2k_properties_remove (props, E2K_PR_MAPI_EMAIL_ADDRESS_LIST);
}

static void
proppatch_categories (PropMapping *prop_mapping,
		      EContact *new_contact, EContact *cur_contact,
		      E2kProperties *props)
{
	GList *categories_list = NULL;
	GList *l = NULL;
	GPtrArray *prop_array = NULL;

	categories_list = e_contact_get (new_contact, E_CONTACT_CATEGORY_LIST);

	/* Dont send a NULL array to the server */
	if (!categories_list)
		return;

	prop_array = g_ptr_array_new ();

	for (l = categories_list; l; l = g_list_next (l)) {
		g_ptr_array_add (prop_array, g_strdup (l->data));
	}

	e2k_properties_set_string_array (props, prop_mapping->prop_name, prop_array);
}

static void
populate_categories (EContactField field, EContact *new_contact, void *data)
{
	GList *updated_list = NULL;
	GPtrArray *categories_list = data;
	gint i;

	for (i = 0; i < categories_list->len; i++) {
		updated_list = g_list_append (updated_list,
					      (char *)categories_list->pdata[i]);
	}
	

	e_contact_set (new_contact, E_CONTACT_CATEGORY_LIST, updated_list);

}

static void
proppatch_date (PropMapping *prop_mapping,
		EContact *new_contact, EContact *cur_contact,
		E2kProperties *props)
{
	gboolean changed;
	EContactDate *new_dt, *cur_dt;
	time_t tt;
	struct tm then;

	new_dt = e_contact_get (new_contact, prop_mapping->field);
	if (cur_contact)
		cur_dt = e_contact_get (cur_contact, prop_mapping->field);
	else
		cur_dt = NULL;

	changed = !e_contact_date_equal (cur_dt, new_dt);
	e_contact_date_free (cur_dt);

	if (!changed) {
		e_contact_date_free (new_dt);
		return;
	}

	memset (&then, 0, sizeof(then));
	then.tm_year = new_dt->year - 1900;
	then.tm_mon  = new_dt->month - 1;
	then.tm_mday = new_dt->day;
	then.tm_isdst = -1;
	tt = mktime (&then);

	e2k_properties_set_date (props, prop_mapping->prop_name,
				 e2k_make_timestamp (tt));
	e_contact_date_free (new_dt);
}

static void
populate_date(EContactField field, EContact *new_contact, void *data)
{
	char *date = (char*)data;
	time_t tt;
	struct tm *then;
	EContactDate dt;

	tt = e2k_parse_timestamp (date);

	then = gmtime (&tt);

	dt.year = then->tm_year + 1900;
	dt.month = then->tm_mon + 1;
	dt.day = then->tm_mday;
	
	e_contact_set (new_contact, field, &dt);
}

enum addressprop {
	ADDRPROP_POSTOFFICEBOX,
	ADDRPROP_STREET,
	ADDRPROP_CITY,
	ADDRPROP_STATE,
	ADDRPROP_POSTALCODE,
	ADDRPROP_COUNTRY,
	ADDRPROP_LAST
};

static char *homeaddrpropnames[] = {
	E2K_PR_CONTACTS_HOME_PO_BOX,
	E2K_PR_CONTACTS_HOME_STREET,
	E2K_PR_CONTACTS_HOME_CITY,
	E2K_PR_CONTACTS_HOME_STATE,
	E2K_PR_CONTACTS_HOME_ZIP,
	E2K_PR_CONTACTS_HOME_COUNTRY,
};

static char *otheraddrpropnames[] = {
	E2K_PR_CONTACTS_OTHER_PO_BOX,
	E2K_PR_CONTACTS_OTHER_STREET,
	E2K_PR_CONTACTS_OTHER_CITY,
	E2K_PR_CONTACTS_OTHER_STATE,
	E2K_PR_CONTACTS_OTHER_ZIP,
	E2K_PR_CONTACTS_OTHER_COUNTRY,
};

static char *workaddrpropnames[] = {
	E2K_PR_CONTACTS_WORK_PO_BOX,
	E2K_PR_CONTACTS_WORK_STREET,
	E2K_PR_CONTACTS_WORK_CITY,
	E2K_PR_CONTACTS_WORK_STATE,
	E2K_PR_CONTACTS_WORK_ZIP,
	E2K_PR_CONTACTS_WORK_COUNTRY,
};

static void
proppatch_address (PropMapping *prop_mapping, 
		   EContact *new_contact, EContact *cur_contact,
		   E2kProperties *props)
{
	EContactAddress *new_address, *cur_address = NULL;
	char *new_addrprops[ADDRPROP_LAST], *cur_addrprops[ADDRPROP_LAST];
	char **propnames, *value;
	int i;

	new_address = e_contact_get (new_contact, prop_mapping->field);
	if (cur_contact)
		cur_address = e_contact_get (cur_contact, prop_mapping->field);

	switch (prop_mapping->field) {
	case E_CONTACT_ADDRESS_HOME:
		propnames = homeaddrpropnames;
		break;

	case E_CONTACT_ADDRESS_WORK:
		propnames = workaddrpropnames;
		break;

	case E_CONTACT_ADDRESS_OTHER:
	default:
		propnames = otheraddrpropnames;
		break;
	}

	if (!new_address) {
		if (cur_address) {
			for (i = 0; i < ADDRPROP_LAST; i ++)
				e2k_properties_remove (props, propnames[i]);
			e_contact_address_free (cur_address);
		}
		return;
	}

	new_addrprops [ADDRPROP_POSTOFFICEBOX]		= new_address->po;
	new_addrprops [ADDRPROP_STREET]			= new_address->street;
	new_addrprops [ADDRPROP_CITY]			= new_address->locality;
	new_addrprops [ADDRPROP_STATE]			= new_address->region;
	new_addrprops [ADDRPROP_POSTALCODE]		= new_address->code;
	new_addrprops [ADDRPROP_COUNTRY]		= new_address->country;
	if (cur_address) {
		cur_addrprops [ADDRPROP_POSTOFFICEBOX]	= cur_address->po;
		cur_addrprops [ADDRPROP_STREET]		= cur_address->street;
		cur_addrprops [ADDRPROP_CITY]		= cur_address->locality;
		cur_addrprops [ADDRPROP_STATE]		= cur_address->region;
		cur_addrprops [ADDRPROP_POSTALCODE]	= cur_address->code;
		cur_addrprops [ADDRPROP_COUNTRY]	= cur_address->country;
	}

	for (i = 0; i < ADDRPROP_LAST; i ++) {
		if (!new_addrprops[i]) {
			if (cur_addrprops[i])
				e2k_properties_remove (props, propnames[i]);
			continue;
		}

		if (cur_address && cur_addrprops[i] &&
		    !strcmp (new_addrprops[i], cur_addrprops[i])) {
			/* they're the same */
			continue;
		}

		if (i == ADDRPROP_STREET && new_address->ext) {
			value = g_strdup_printf ("%s%s", new_addrprops[i],
						 new_address->ext);
		} else
			value = g_strdup (new_addrprops[i]);
		e2k_properties_set_string (props, propnames[i], value);
		g_free (value);
	}

	e_contact_address_free (new_address);
	if (cur_address)
		e_contact_address_free (cur_address);
}

static void
proppatch_im (PropMapping *prop_mapping, 
	      EContact *new_contact, EContact *cur_contact,
	      E2kProperties *props)
{
	GList *new_list;

	new_list = e_contact_get (new_contact, prop_mapping->field);
	while (new_list) {
		if (prop_mapping->field == E_CONTACT_IM_MSN) {
			e2k_properties_set_string (props, 
						   E2K_PR_OUTLOOK_CONTACT_IM_ADDR,
						   g_strdup (new_list->data));
			break;
		}
		new_list = g_list_next (new_list);
	}
}

static void
populate_im (EContactField field, EContact *new_contact, void *data)
{
	GList *im_attr_list = NULL;
	EVCardAttribute *attr;

	if (field == E_CONTACT_IM_MSN) {
		attr = e_vcard_attribute_new ("", e_contact_vcard_attribute (field));
		e_vcard_attribute_add_param_with_value (attr,
				e_vcard_attribute_param_new (EVC_TYPE), "WORK");

		e_vcard_attribute_add_value (attr, (const char *)data);

		im_attr_list = g_list_append (im_attr_list, attr);
	}

	e_contact_set_attributes (new_contact, field, im_attr_list);
}

static void
populate_address (EContactField field, EContact *new_contact, void *data)
{
	EAddressWestern *waddr;
	EContactAddress addr;

	waddr = e_address_western_parse ((const char *)data);
	addr.address_format = "us"; /* FIXME? */
	addr.po = waddr->po_box;
	addr.ext = waddr->extended;
	addr.street = waddr->street;
	addr.locality = waddr->locality;
	addr.region = waddr->region;
	addr.code = waddr->postal_code;
	addr.country = waddr->country;

	e_contact_set (new_contact, field, &addr);
	e_address_western_free (waddr);
}

static const char *
contact_name (EContact *contact)
{
	const char *contact_name;

	contact_name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_contact_get_const (contact, E_CONTACT_EMAIL_2);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_contact_get_const (contact, E_CONTACT_EMAIL_3);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_contact_get_const (contact, E_CONTACT_ORG);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_contact_get_const (contact, E_CONTACT_TITLE);
	if (contact_name && *contact_name)
		return contact_name;

	return NULL;
}

static E2kProperties *
props_from_contact (EBookBackendExchange *be,
		    EContact *contact,
		    EContact *old_contact)
{
	E2kProperties *props;
	int i;

	props = e2k_properties_new ();

	if (!old_contact) {
		const char *subject;

		subject = contact_name (contact);

		/* Set up some additional fields when creating a new contact */
		e2k_properties_set_string (
			props, E2K_PR_EXCHANGE_MESSAGE_CLASS,
			g_strdup ("IPM.Contact"));
		e2k_properties_set_string (
			props, E2K_PR_HTTPMAIL_SUBJECT,
			g_strdup (subject ? subject : ""));

		e2k_properties_set_int (props, E2K_PR_MAPI_SIDE_EFFECTS, 16);
		e2k_properties_set_int (props, PR_ACTION, 512);
		e2k_properties_set_bool (props, E2K_PR_OUTLOOK_CONTACT_JOURNAL, FALSE);
		e2k_properties_set_bool (props, E2K_PR_MAPI_SENSITIVITY, 0);
	}

	for (i = 0; i < num_prop_mappings; i ++) {
		/* handle composite attributes here (like addresses) */
		if (prop_mappings[i].flags & FLAG_COMPOSITE) {
			prop_mappings[i].composite_proppatch_func (&prop_mappings[i], contact, old_contact, props);
		} else if (prop_mappings[i].flags & FLAG_PUT) {
			continue; /* FIXME */
		} else {
			const char *new_value, *current_value;

			new_value = e_contact_get_const (contact, prop_mappings[i].field);
			if (new_value && !*new_value)
				new_value = NULL;
			current_value = old_contact ? e_contact_get_const (old_contact, prop_mappings[i].field) : NULL;
			if (current_value && !*current_value)
				current_value = NULL;

			if (value_changed (current_value, new_value)) {
				if (new_value) {
					e2k_properties_set_string (
						props,
						prop_mappings[i].prop_name,
						g_strdup (new_value));
				} else {
					e2k_properties_remove (
						props,
						prop_mappings[i].prop_name);
				}
			}
		}
	}

	if (e2k_properties_empty (props)) {
		e2k_properties_free (props);
		return NULL;
	}

	return props;
}

static GByteArray *
build_message (const char *from_name, const char *from_email,
	       const char *subject, const char *note, EContactPhoto *photo)
{
	CamelDataWrapper *wrapper = NULL;
	CamelContentType *type;
	CamelMimeMessage *msg;
	CamelInternetAddress *from;
	CamelStream *stream;
	CamelMimePart *text_part, *photo_part;
	GByteArray *buffer;

	msg = camel_mime_message_new ();
	camel_medium_add_header (CAMEL_MEDIUM (msg), "content-class",
				 "urn:content-classes:person");
	camel_mime_message_set_subject (msg, subject);
	camel_medium_add_header (CAMEL_MEDIUM (msg), "X-MS-Has-Attach", "yes");

	from = camel_internet_address_new ();
	camel_internet_address_add (from, from_name, from_email);
	camel_mime_message_set_from (msg, from);
	camel_object_unref (from);

	/* Create the body */
	if (note) {
		stream = camel_stream_mem_new_with_buffer (note, strlen (note));
		wrapper = camel_data_wrapper_new ();
		camel_data_wrapper_construct_from_stream (wrapper, stream);
		camel_object_unref (stream);
		
		type = camel_content_type_new ("text", "plain");
		camel_content_type_set_param (type, "charset", "UTF-8");
		camel_data_wrapper_set_mime_type_field (wrapper, type);
		camel_content_type_unref (type);
	}
	text_part = NULL;
	
	if (note && photo)
		text_part = camel_mime_part_new ();
	else if (note)
		text_part = CAMEL_MIME_PART (msg);

	if (text_part) {
		camel_medium_set_content_object (CAMEL_MEDIUM (text_part), wrapper);
		camel_mime_part_set_encoding (text_part, CAMEL_TRANSFER_ENCODING_8BIT);
	}
	if (photo) {
		CamelMultipart *multipart;
		GByteArray *photo_ba;
		GdkPixbufLoader *loader;
		GdkPixbufFormat *format;
		const char *content_type, *extension;
		char **list, *filename;

		/* Determine the MIME type of the photo */
		loader = gdk_pixbuf_loader_new ();
		gdk_pixbuf_loader_write (loader, photo->data, photo->length, NULL);
		gdk_pixbuf_loader_close (loader, NULL);

		format = gdk_pixbuf_loader_get_format (loader);
		g_object_unref (loader);

		if (format) {
			list = gdk_pixbuf_format_get_mime_types (format);
			content_type = list[0];
			list = gdk_pixbuf_format_get_extensions (format);
			extension = list[0];
		} else {
			content_type = "application/octet-stream";
			extension = "dat";
		}
		filename = g_strdup_printf ("ContactPicture.%s", extension);

		/* Build the MIME part */
		photo_ba = g_byte_array_new ();
		g_byte_array_append (photo_ba, photo->data, photo->length);
		stream = camel_stream_mem_new_with_byte_array (photo_ba);

		wrapper = camel_data_wrapper_new ();
		camel_data_wrapper_construct_from_stream (wrapper, stream);
		camel_object_unref (stream);
		camel_data_wrapper_set_mime_type (wrapper, content_type);

		photo_part = camel_mime_part_new ();
		camel_medium_set_content_object (CAMEL_MEDIUM (photo_part),
						 wrapper);
		camel_mime_part_set_encoding (photo_part, CAMEL_TRANSFER_ENCODING_BASE64);
		camel_mime_part_set_description (photo_part, filename);
		camel_mime_part_set_filename (photo_part, filename);

		g_free (filename);

		/* Build the multipart */
		multipart = camel_multipart_new ();
		camel_multipart_set_boundary (multipart, NULL);
		if (text_part) {
			camel_multipart_add_part (multipart, text_part);
			camel_object_unref (text_part);
		}
		camel_multipart_add_part (multipart, photo_part);
		camel_object_unref (photo_part);

		camel_medium_set_content_object (CAMEL_MEDIUM (msg),
						 CAMEL_DATA_WRAPPER (multipart));
		camel_object_unref (multipart);
	}

	buffer = g_byte_array_new();
	stream = camel_stream_mem_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream), buffer);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (msg), stream);
	camel_object_unref (stream);
	camel_object_unref (msg);

	return buffer;
}

static E2kHTTPStatus
do_put (EBookBackendExchange *be, EDataBook *book,
	const char *uri, const char *subject,
	const char *note, EContactPhoto *photo)
{
	ExchangeHierarchy *hier;
	E2kHTTPStatus status;
	GByteArray *body;

	hier = e_folder_exchange_get_hierarchy (be->priv->folder);
	body = build_message (hier->owner_name, hier->owner_email,
			      subject, note, photo);
	status = e2k_context_put (be->priv->ctx, NULL, uri, "message/rfc822",
				  body->data, body->len, NULL);
	g_byte_array_free (body, TRUE);

	return status;
}

static gboolean
test_name (E2kContext *ctx, const char *name, gpointer summary)
{
	return !e_book_backend_summary_check_contact (summary, name);
}

static EBookBackendSyncStatus
e_book_backend_exchange_create_contact (EBookBackendSync  *backend,
					EDataBook         *book,
					guint32            opid,
					const char        *vcard,
					EContact         **contact)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kProperties *props;
	const char *name;
	E2kHTTPStatus status;
	char *location = NULL, *note;
	EContactPhoto *photo;

	d(printf("ebbe_create_contact(%p, %p, %s)\n", backend, book, vcard));

	g_mutex_lock (bepriv->create_mutex);

	switch (bepriv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		*contact = NULL;
		g_mutex_unlock (bepriv->create_mutex);
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	
	case GNOME_Evolution_Addressbook_MODE_REMOTE:	
		*contact = e_contact_new_from_vcard (vcard);
		props = props_from_contact (be, *contact, NULL);

		/* figure out the right uri to be using */
		name = contact_name (*contact);
		if (!name)
			name = "No Subject";

		note = e_contact_get (*contact, E_CONTACT_NOTE);
		photo = e_contact_get (*contact, E_CONTACT_PHOTO);

		status = e_folder_exchange_proppatch_new (bepriv->folder, NULL, name,
							  test_name, bepriv->summary,
							  props, &location, NULL);

		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
			e_contact_set (*contact, E_CONTACT_UID, location);

			if (note || photo) {
				/* Do the PUT request. */
				status = do_put (be, book, location,
						 contact_name (*contact),
						 note, photo);
			}
			g_free (location);
		}

		if (note)
			g_free (note);
		if (photo)
			e_contact_photo_free (photo);

		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
			e_book_backend_summary_add_contact (bepriv->summary,
							    *contact);
			e_book_backend_cache_add_contact (bepriv->cache, *contact);
			g_mutex_unlock (bepriv->create_mutex);
			return GNOME_Evolution_Addressbook_Success;
		} else {
			g_object_unref (*contact);
			*contact = NULL;
			g_mutex_unlock (bepriv->create_mutex);
			return http_status_to_pas (status);
		}
	default:
		break;
	}
	g_mutex_unlock (bepriv->create_mutex);
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_exchange_modify_contact (EBookBackendSync  *backend,
					EDataBook         *book,
					guint32 	  opid,
					const char        *vcard,
					EContact         **contact)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	EContact *old_contact;
	const char *uri;
	E2kHTTPStatus status;
	E2kResult *results;
	int nresults;
	E2kProperties *props;

	d(printf("ebbe_modify_contact(%p, %p, %s)\n", backend, book, vcard));

	switch (bepriv->mode) {
	
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		*contact = NULL;
		return GNOME_Evolution_Addressbook_RepositoryOffline;

	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		*contact = e_contact_new_from_vcard (vcard);
		uri = e_contact_get_const (*contact, E_CONTACT_UID);

		status = e2k_context_propfind (bepriv->ctx, NULL, uri,
					       field_names, n_field_names,
					       &results, &nresults);
		if (status == E2K_HTTP_CANCELLED) {
			g_object_unref (book);
			g_object_unref (*contact);
			*contact = NULL;
			return GNOME_Evolution_Addressbook_OtherError;
		}

		if (status == E2K_HTTP_MULTI_STATUS && nresults > 0)
			old_contact = e_contact_from_props (be, &results[0]);
		else
			old_contact = NULL;

		props = props_from_contact (be, *contact, old_contact);
		status = e2k_context_proppatch (bepriv->ctx, NULL, uri,
						props, FALSE, NULL);

		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
			/* Do the PUT request if we need to. */
			char *old_note, *new_note;
			EContactPhoto *old_photo, *new_photo;
			gboolean changed;

			old_note = e_contact_get (old_contact, E_CONTACT_NOTE);
			old_photo = e_contact_get (old_contact, E_CONTACT_PHOTO);
			new_note = e_contact_get (*contact, E_CONTACT_NOTE);
			new_photo = e_contact_get (*contact, E_CONTACT_PHOTO);

			if ((old_note && !new_note) ||
		    	    (new_note && !old_note) ||
		    	    (old_note && new_note &&
		     	strcmp (old_note, new_note) != 0))
				changed = TRUE;
			else if ((old_photo && !new_photo) ||
				 (new_photo && !old_photo) ||
			 	(old_photo && new_photo &&
			  	((old_photo->length != new_photo->length) ||
			   	(memcmp (old_photo->data, new_photo->data, 
					 old_photo->length) != 0))))
					changed = TRUE;
			else
				changed = FALSE;

			if (changed) {
				status = do_put (be, book, uri,
						 contact_name (*contact),
						 new_note, new_photo);
			}

			g_free (old_note);
			g_free (new_note);
			if (old_photo)
				e_contact_photo_free (old_photo);
			if (new_photo)
				e_contact_photo_free (new_photo);
		}

		if (old_contact)
			g_object_unref (old_contact);

		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
			e_book_backend_summary_remove_contact (bepriv->summary,
							       uri);
			e_book_backend_summary_add_contact (bepriv->summary,
							    *contact);
			e_book_backend_cache_remove_contact (bepriv->cache, uri);
			e_book_backend_cache_add_contact (bepriv->cache, *contact);
			
			return GNOME_Evolution_Addressbook_Success;
		} else {
			g_object_unref (*contact);
			*contact = NULL;
			return http_status_to_pas (status);
		}

	default:
		break;
	}
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_exchange_remove_contacts (EBookBackendSync  *backend,
					 EDataBook         *book,
					 guint32 	   opid,
					 GList             *id_list,
					 GList            **removed_ids)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	const char *uri;
	E2kHTTPStatus status;
	GList *l;
	EBookBackendSyncStatus ret_status = GNOME_Evolution_Addressbook_Success;

	 /* Remove one or more contacts */
	d(printf("ebbe_remove_contact(%p, %p, %s)\n", backend, book, (char*)id_list->data));

	switch (bepriv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		*removed_ids = NULL;
		return GNOME_Evolution_Addressbook_RepositoryOffline; 
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		for (l = id_list; l; l = l->next) {
			uri = l->data;
			status = e2k_context_delete (bepriv->ctx, NULL, uri);
			if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
				e_book_backend_summary_remove_contact (
							bepriv->summary, uri);
				e_book_backend_cache_remove_contact (bepriv->cache, uri);
				*removed_ids = g_list_append (
						*removed_ids, g_strdup (uri));
			} else 
				ret_status = http_status_to_pas (status);
		}
		return ret_status;
	
	default:
		break;
	}
	return GNOME_Evolution_Addressbook_Success;
}

static ESExpResult *
func_not (ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;

	if (argc != 1 || argv[0]->type != ESEXP_RES_UNDEFINED) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);
	r->value.string = (char *)
		e2k_restriction_not ((E2kRestriction *)argv[0]->value.string, TRUE);

	return r;
}

static ESExpResult *
func_and_or (ESExp *f, int argc, ESExpResult **argv, void *and)
{
	ESExpResult *r;
	E2kRestriction *rn;
	GPtrArray *rns;
	int i;

	rns = g_ptr_array_new ();

	for (i = 0; i < argc; i ++) {
		if (argv[i]->type != ESEXP_RES_UNDEFINED) {
			g_ptr_array_free (rns, TRUE);
			for (i = 0; i < argc; i++) {
				if (argv[i]->type == ESEXP_RES_UNDEFINED)
					g_free (argv[i]->value.string);
			}

			e_sexp_fatal_error (f, "parse error");
			return NULL;
		}

		g_ptr_array_add (rns, argv[i]->value.string);
	}

	if (and)
		rn = e2k_restriction_and (rns->len, (E2kRestriction **)rns->pdata, TRUE);
	else
		rn = e2k_restriction_or (rns->len, (E2kRestriction **)rns->pdata, TRUE);
	g_ptr_array_free (rns, TRUE);

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);
	r->value.string = (char *)rn;
	return r;
}

static ESExpResult *
func_match (struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	ESExpResult *r;
	E2kRestriction *rn;
	char *propname, *str;
	const char *exchange_prop;
	guint flags = GPOINTER_TO_UINT (data);

	if (argc != 2 ||
	    argv[0]->type != ESEXP_RES_STRING ||
	    argv[1]->type != ESEXP_RES_STRING) {
		e_sexp_fatal_error (f, "parse error");
		return NULL;
	}

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!strcmp (propname, "x-evolution-any-field")) {
		GPtrArray *rns;
		int i;

		rns = g_ptr_array_new ();
		for (i = 0; i < num_prop_mappings; i ++) {
			if (prop_mappings[i].flags & FLAG_UNLIKEABLE)
				continue;

			exchange_prop = prop_mappings[i].prop_name;
			if (!*str)
				rn = e2k_restriction_exist (exchange_prop);
			else
				rn = e2k_restriction_content (exchange_prop, flags, str);
			g_ptr_array_add (rns, rn);
		}

		rn = e2k_restriction_or (rns->len, (E2kRestriction **)rns->pdata, TRUE);
		g_ptr_array_free (rns, TRUE);
	} else if (!strcmp (propname, "full_name") && flags == E2K_FL_PREFIX) {
		if (!*str) {
			rn = e2k_restriction_orv (
				e2k_restriction_exist (
					e_book_backend_exchange_prop_to_exchange ("full_name")),
				e2k_restriction_exist (
					e_book_backend_exchange_prop_to_exchange ("family_name")),
				NULL);
		}
		else {
			rn = e2k_restriction_orv (
				e2k_restriction_content (
					e_book_backend_exchange_prop_to_exchange ("full_name"),
					flags, str),
				e2k_restriction_content (
					e_book_backend_exchange_prop_to_exchange ("family_name"),
					flags, str),
				e2k_restriction_content (
					e_book_backend_exchange_prop_to_exchange ("nickname"),
					flags, str),
				NULL);
		}
	} else if (!strcmp (propname, "email")) {
		if (!*str) {
			rn = e2k_restriction_orv (
				e2k_restriction_exist (E2K_PR_MAPI_EMAIL_1_ADDRESS),
				e2k_restriction_exist (E2K_PR_MAPI_EMAIL_2_ADDRESS),
				e2k_restriction_exist (E2K_PR_MAPI_EMAIL_3_ADDRESS),
				NULL);
		}
		else {	
			rn = e2k_restriction_orv (
				e2k_restriction_content (E2K_PR_MAPI_EMAIL_1_ADDRESS, flags, str),
				e2k_restriction_content (E2K_PR_MAPI_EMAIL_2_ADDRESS, flags, str),
				e2k_restriction_content (E2K_PR_MAPI_EMAIL_3_ADDRESS, flags, str),
				NULL);
		}
	} else {
		exchange_prop =
			e_book_backend_exchange_prop_to_exchange (propname);
		if (!exchange_prop) {
			/* FIXME. Do something better in 1.6 */
			e_sexp_fatal_error (f, "no prop");
			return NULL;
		}

		if (!*str)
			rn = e2k_restriction_exist (exchange_prop);
		else
			rn = e2k_restriction_content (exchange_prop, flags, str);
	}

	r = e_sexp_result_new (f, ESEXP_RES_UNDEFINED);
	r->value.string = (char *)rn;
	return r;
}

static struct {
	char *name;
	ESExpFunc *func;
	guint flags;
} symbols[] = {
	{ "and", func_and_or, TRUE },
	{ "or", func_and_or, FALSE },
	{ "not", func_not, 0 },
	{ "contains", func_match, E2K_FL_SUBSTRING },
	{ "is", func_match, E2K_FL_FULLSTRING },
	{ "beginswith", func_match, E2K_FL_PREFIX },
	{ "endswith", func_match, E2K_FL_SUFFIX },
};

static E2kRestriction *
e_book_backend_exchange_build_restriction (const char *query,
					E2kRestriction *base_rn)
{
	ESExp *sexp;
	ESExpResult *r;
	E2kRestriction *rn;
	int i;

	sexp = e_sexp_new ();

	for (i = 0; i < sizeof (symbols) / sizeof (symbols[0]); i++) {
		e_sexp_add_function (sexp, 0, symbols[i].name,
				     symbols[i].func,
				     GUINT_TO_POINTER (symbols[i].flags));
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);
	if (r && r->type == ESEXP_RES_UNDEFINED)
		rn = (E2kRestriction *)r->value.string;
	else {
		g_warning ("conversion to exchange restriction failed");
		rn = NULL;
	}

	e_sexp_result_free (sexp, r);
	e_sexp_unref (sexp);

	if (base_rn) {
		e2k_restriction_ref (base_rn);
		rn = e2k_restriction_andv (rn, base_rn, NULL);
	}

	return rn;
}

static void
notify_remove (gpointer id, gpointer value, gpointer backend)
{
	EBookBackendExchange *be = backend;

	e_book_backend_notify_remove (backend, id);
	e_book_backend_summary_remove_contact (be->priv->summary, id);
}

static void
subscription_notify (E2kContext *ctx, const char *uri,
		     E2kContextChangeType type, gpointer user_data)
{
	EBookBackendExchange *be = user_data;
	EBookBackendExchangePrivate *bepriv = be->priv;
	EBookBackend *backend = user_data;
	GHashTable *unseen_ids;
	GPtrArray *ids;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	EContact *contact;
	const char *uid;
	int i;

	bonobo_object_ref (be);

	unseen_ids = g_hash_table_new (g_str_hash, g_str_equal);
	ids = e_book_backend_summary_search (bepriv->summary,
					     "(contains \"x-evolution-any-field\" \"\")");
	for (i = 0; i < ids->len; i++) {
		g_hash_table_insert (unseen_ids, ids->pdata[i],
				     GUINT_TO_POINTER (1));
	}

	/* FIXME: don't search everything */

	/* execute the query */
	iter = e_folder_exchange_search_start (bepriv->folder, NULL,
					       field_names, n_field_names,
					       bepriv->base_rn, NULL, TRUE);
	while ((result = e2k_result_iter_next (iter))) {
		contact = e_contact_from_props (be, result);
		if (!contact)
			continue;
		uid = e_contact_get_const (contact, E_CONTACT_UID);

		g_hash_table_remove (unseen_ids, uid);
		e_book_backend_notify_update (backend, contact);

		e_book_backend_summary_remove_contact (bepriv->summary, uid);
		e_book_backend_summary_add_contact (bepriv->summary, contact);
		g_object_unref (contact);
	}
	status = e2k_result_iter_free (iter);

	if (status == E2K_HTTP_MULTI_STATUS)
		g_hash_table_foreach (unseen_ids, notify_remove, be);
	g_hash_table_destroy (unseen_ids);
	bonobo_object_unref (be);
}


static EBookBackendSyncStatus
e_book_backend_exchange_get_contact_list (EBookBackendSync  *backend,
					  EDataBook         *book,
					  guint32 	     opid,
					  const char        *query,
					  GList            **contacts)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	char *vcard;
	GList *vcard_list = NULL, *temp, *offline_contacts;
	

	d(printf("ebbe_get_contact_list(%p, %p, %s)\n", backend, book, query));

	switch (bepriv->mode) {
	
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		/* FIXME */
		offline_contacts = e_book_backend_cache_get_contacts (bepriv->cache,
							      query);
		temp = offline_contacts;
		for (; offline_contacts != NULL; 
		       offline_contacts = g_list_next(offline_contacts)) {
			vcard_list = g_list_append ( 
					vcard_list, 
					e_vcard_to_string (
						E_VCARD (offline_contacts->data), 
						EVC_FORMAT_VCARD_30));
			g_object_unref (offline_contacts->data);
		}

	    	*contacts = vcard_list;
		if (temp)
			g_list_free (temp);
		return GNOME_Evolution_Addressbook_Success;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		rn = e_book_backend_exchange_build_restriction (query, 
								bepriv->base_rn);
		iter = e_folder_exchange_search_start (bepriv->folder, NULL,
					       field_names, n_field_names,
					       rn, NULL, TRUE);
		e2k_restriction_unref (rn);

		*contacts = NULL;
		while ((result = e2k_result_iter_next (iter))) {
			vcard = vcard_from_props (be, result);
			if (!vcard)
				continue;
			*contacts = g_list_prepend (*contacts, vcard);
		}
		status = e2k_result_iter_free (iter);

		return http_status_to_pas (status);

	default:
		break;
	}
	return GNOME_Evolution_Addressbook_Success;
}

static void
e_book_backend_exchange_start_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	const char *query = e_data_book_view_get_card_query (book_view);
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	EContact *contact;
	GList *temp_list, *contacts;

	d(printf("ebbe_start_book_view(%p, %p)\n", backend, book_view));

	bonobo_object_ref (book_view);
	e_data_book_view_notify_status_message (book_view, _("Searching..."));

	switch (bepriv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (!bepriv->cache) {
			e_data_book_view_notify_complete (book_view, 
					GNOME_Evolution_Addressbook_Success);
			return;
		}
		contacts = e_book_backend_cache_get_contacts (bepriv->cache, 
							      query);
		temp_list = contacts;
		for (; contacts != NULL; contacts = g_list_next(contacts)) {
			/* FIXME: Need muex here?
			g_mutex_lock (closure->mutex);
			stopped = closure->stopped;
			g_mutex_unlock (closure->mutex);
			if (stopped) 

			for (;contacts != NULL; 
			      contacts = g_list_next (contacts))
				g_object_unref (contacts->data);
			break;
			} 
			*/
			e_data_book_view_notify_update (book_view, 
							E_CONTACT(contacts->data));
			g_object_unref (contacts->data);
		}
		//if (!stopped)
		e_data_book_view_notify_complete (book_view, 
					GNOME_Evolution_Addressbook_Success);
		if (temp_list)
			 g_list_free (temp_list);
		bonobo_object_unref (book_view);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		if (!be->priv->ctx) {
			e_book_backend_notify_auth_required (backend);
			e_data_book_view_notify_complete (book_view,
						GNOME_Evolution_Addressbook_AuthenticationRequired);
			bonobo_object_unref (book_view);
			return;
		}

		/* execute the query */
		rn = e_book_backend_exchange_build_restriction (query, 
							bepriv->base_rn);

		iter = e_folder_exchange_search_start (bepriv->folder, NULL,
					       field_names, n_field_names,
					       rn, NULL, TRUE);
		e2k_restriction_unref (rn);
		
		while ((result = e2k_result_iter_next (iter))) {
			contact = e_contact_from_props (be, result);
			if (contact) {
				e_data_book_view_notify_update (book_view, 
								contact);
				g_object_unref (contact);
			}
		}
		status = e2k_result_iter_free (iter);

		e_data_book_view_notify_complete (book_view,
						  http_status_to_pas (status));
		bonobo_object_unref (book_view);

	default:
		break;
	}
}

static void
e_book_backend_exchange_stop_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kOperation *op;

	d(printf("ebbe_stop_book_view(%p, %p)\n", backend, book_view));

	op = g_hash_table_lookup (bepriv->ops, book_view);
	if (op) {
		g_hash_table_remove (bepriv->ops, book_view);
		e2k_operation_cancel (op);
	}
}

typedef struct {
	EXmlHash *ehash;
	GHashTable *seen_ids;
	GList *changes;
} EBookBackendExchangeChangeContext;

static void
free_change (gpointer change, gpointer user_data)
{
	CORBA_free (change);
}

static void
find_deleted_ids (const char *id, const char *vcard, gpointer user_data)
{
	EBookBackendExchangeChangeContext *ctx = user_data;

	if (!g_hash_table_lookup (ctx->seen_ids, id)) {
		ctx->changes = g_list_prepend (
			ctx->changes,
			e_book_backend_change_delete_new (id));
		e_xmlhash_remove (ctx->ehash, id);
	}
}

static EBookBackendSyncStatus
e_book_backend_exchange_get_changes (EBookBackendSync  *backend,
				     EDataBook         *book,
				     guint32 		opid,
				     const char        *change_id,
				     GList            **changes)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	EBookBackendExchangeChangeContext *ctx;
	char *filename, *path, *vcard;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;

	d(printf("ebbe_get_changes(%p, %p, %s)\n", backend, book, change_id));

	switch (bepriv->mode) {
	
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		*changes = NULL;
		return GNOME_Evolution_Addressbook_RepositoryOffline;

	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		ctx = g_new0 (EBookBackendExchangeChangeContext, 1);
		ctx->seen_ids = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, NULL);

		/* open the changes file */
		filename = g_strdup_printf ("%s.changes", change_id);
		path = e_folder_exchange_get_storage_file (bepriv->folder, 
							   filename);
		ctx->ehash = e_xmlhash_new (path);
		g_free (path);
		g_free (filename);

		iter = e_folder_exchange_search_start (bepriv->folder, NULL,
					       field_names, n_field_names,
					       bepriv->base_rn, NULL, TRUE);

		while ((result = e2k_result_iter_next (iter))) {
			vcard = vcard_from_props (be, result);
			if (!vcard)
				continue;

			g_hash_table_insert (ctx->seen_ids, 
					     g_strdup (result->href),
				     	     GINT_TO_POINTER (1));

			/* Check what type of change has occurred, if any. */
			switch (e_xmlhash_compare (ctx->ehash, result->href, 
						   vcard)) {
			case E_XMLHASH_STATUS_SAME:
				break;

			case E_XMLHASH_STATUS_NOT_FOUND:
				e_xmlhash_add (ctx->ehash, result->href, vcard);
				ctx->changes = g_list_prepend (
					ctx->changes,
					e_book_backend_change_add_new (vcard));
				break;

			case E_XMLHASH_STATUS_DIFFERENT:
				e_xmlhash_add (ctx->ehash, result->href, vcard);
				ctx->changes = g_list_prepend (
					ctx->changes,
					e_book_backend_change_modify_new (vcard));
				break;
			}

			g_free (vcard);
		}
		status = e2k_result_iter_free (iter);

		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
			g_warning ("e_book_backend_exchange_changes: error building list (err = %d)\n", status);
			g_list_foreach (ctx->changes, free_change, NULL);
			ctx->changes = NULL;
		} else {
			e_xmlhash_foreach_key (ctx->ehash, find_deleted_ids, ctx);
			e_xmlhash_write (ctx->ehash);
		}
  		e_xmlhash_destroy (ctx->ehash);
		g_hash_table_destroy (ctx->seen_ids);
		g_free (ctx);

		return http_status_to_pas (status);

	default:
		break;
	}
	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_exchange_get_contact (EBookBackendSync  *backend,
				     EDataBook         *book,
				     guint32            opid,
				     const char        *id,
				     char             **vcard)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	EContact *contact;

	d(printf("ebbe_get_contact(%p, %p, %s)\n", backend, book, id));

	be = E_BOOK_BACKEND_EXCHANGE (e_data_book_get_backend (book));

	switch (bepriv->mode) {
	
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		contact = e_book_backend_cache_get_contact (bepriv->cache,
							    id);
		if (contact) {
			*vcard =  e_vcard_to_string (E_VCARD (contact), 
						     EVC_FORMAT_VCARD_30);
			g_object_unref (contact);
			return GNOME_Evolution_Addressbook_Success;
		}
		else {
			*vcard = g_strdup ("");
			return GNOME_Evolution_Addressbook_ContactNotFound;
		}
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		/* XXX finish this */

	default:
		break;
	}

	return GNOME_Evolution_Addressbook_OtherError;
}


static void
e_book_backend_exchange_authenticate_user (EBookBackend *backend,
					   EDataBook        *book,
					   guint32 	     opid,
					   const char       *user,
					   const char       *password,
					   const char       *auth_method)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	ExchangeAccountResult result;
	ExchangeAccount *account;

	d(printf("ebbe_authenticate_user(%p, %p, %s, %s, %s)\n", backend, book, user, password, auth_method));

	switch (bepriv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		e_book_backend_notify_writable (E_BOOK_BACKEND (backend), FALSE);
		e_book_backend_notify_connection_status (E_BOOK_BACKEND (backend), FALSE);
		e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success);
		return;
			
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		
		account = exchange_component_get_account_for_uri (global_exchange_component, NULL);
		/* FIXME : Check for failures */
		if (!exchange_account_get_context (account)) {
			if(!exchange_account_connect (account, password, &result)) {
				e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_AuthenticationFailed);
				return;
			}
		}
		if (!bepriv->connected)
			e_book_backend_exchange_connect (be);
		if (e_book_backend_cache_is_populated (bepriv->cache)) {
			if (bepriv->is_writable)
				g_thread_create ((GThreadFunc) update_cache, 
						  be, FALSE, NULL);
		}
		else if (bepriv->is_writable || bepriv->marked_for_offline){ 
			/* for personal books we always cache*/
			g_thread_create ((GThreadFunc) build_cache, be, FALSE, NULL);
		}
		e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success);
		return;

	default:
		break;
	}
	e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success);
	return;
}

static void
e_book_backend_exchange_get_supported_auth_methods (EBookBackend *backend,
						    EDataBook *book,
						    guint32 opid)
{
	GList *auth_methods = NULL;
	char *auth_method;

	d(printf ("ebbe_get_supported_auth_methods (%p, %p)\n", backend, book));

	auth_method = g_strdup_printf ("plain/password");
	auth_methods = g_list_append (auth_methods, auth_method);
	e_data_book_respond_get_supported_auth_methods (book, opid,
				GNOME_Evolution_Addressbook_Success,
				auth_methods);

	g_free (auth_method);
	g_list_free (auth_methods);
}

static EBookBackendSyncStatus
e_book_backend_exchange_get_supported_fields (EBookBackendSync  *backend,
					      EDataBook         *book,
					      guint32 		 opid,
					      GList            **methods)
{
	int i;

	d(printf("ebbe_get_supported_fields(%p, %p)\n", backend, book));

	*methods = NULL;
	for (i = 0; i < num_prop_mappings; i ++) {
		if (prop_mappings[i].e_book_field) {
			*methods = g_list_prepend (*methods,
					g_strdup (e_contact_field_name(prop_mappings[i].field)));
		}
	}

	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_exchange_get_required_fields (EBookBackendSync *backend,
					  EDataBook *book,
					  guint32 opid,
					  GList **fields_out)
{
	GList *fields = NULL;

	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FILE_AS)));
	*fields_out = fields;
	return GNOME_Evolution_Addressbook_Success;


}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_exchange_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	E2kOperation *op;

	d(printf("ebbe_cancel_operation(%p, %p)\n", backend, book));

	op = g_hash_table_lookup (bepriv->ops, book);
	if (op) {
		e2k_operation_cancel (op);
		return GNOME_Evolution_Addressbook_Success;
	} else
		return GNOME_Evolution_Addressbook_CouldNotCancel;
}	

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_exchange_load_source (EBookBackend *backend,
				     ESource      *source,
				     gboolean      only_if_exists)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	const char *offline;

	g_return_val_if_fail (bepriv->connected == FALSE, GNOME_Evolution_Addressbook_OtherError);

	d(printf("ebbe_load_source(%p, %p[%s])\n", backend, source, e_source_peek_name (source)));

	offline = e_source_get_property (source, "offline_sync");
	if (offline  && g_str_equal (offline, "1"))
		bepriv->marked_for_offline = TRUE;

	if (bepriv->mode ==  GNOME_Evolution_Addressbook_MODE_LOCAL &&  
	    !bepriv->marked_for_offline ) {
		return GNOME_Evolution_Addressbook_OfflineUnavailable;
	}

	bepriv->exchange_uri = e_source_get_uri (source);
	if (bepriv->exchange_uri == NULL)
		return  GNOME_Evolution_Addressbook_OtherError;
	bepriv->original_uri = g_strdup (bepriv->exchange_uri);

	if (bepriv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		e_book_backend_set_is_writable (backend, FALSE);
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
		if (!e_book_backend_cache_exists (bepriv->original_uri))
			return GNOME_Evolution_Addressbook_OfflineUnavailable;
	}
	bepriv->cache = e_book_backend_cache_new (bepriv->original_uri);

	/* Once aunthentication in address book works this can be removed */
	if (bepriv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		return GNOME_Evolution_Addressbook_Success;
	}

	// writable property will be set in authenticate_user callback
	e_book_backend_set_is_writable (E_BOOK_BACKEND(backend), FALSE);
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (be), TRUE);
	e_book_backend_notify_connection_status (E_BOOK_BACKEND (be), TRUE);

	if (e_book_backend_cache_is_populated (bepriv->cache)) {
		if (bepriv->is_writable)
			g_thread_create ((GThreadFunc) update_cache, 
					  be, FALSE, NULL);
	}
	else if (bepriv->is_writable || bepriv->marked_for_offline){ 
		/* for personal books we always cache*/
		g_thread_create ((GThreadFunc) build_cache, be, FALSE, NULL);
	}

	return GNOME_Evolution_Addressbook_Success;
}

static EBookBackendSyncStatus
e_book_backend_exchange_remove (EBookBackendSync *backend, EDataBook *book, guint32 opid)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	ExchangeAccountFolderResult result;
	const char *uri;

	d(printf("ebbe_remove(%p, %p)\n", backend, book));
	uri = e_folder_exchange_get_internal_uri (bepriv->folder);
	result = exchange_account_remove_folder (bepriv->account, uri);
	if (result == EXCHANGE_ACCOUNT_FOLDER_OK)
		return GNOME_Evolution_Addressbook_Success;
	else if (result == EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST)
		return GNOME_Evolution_Addressbook_NoSuchBook;
	else if (result == EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION)
		return GNOME_Evolution_Addressbook_PermissionDenied;
	else if (result == EXCHANGE_ACCOUNT_FOLDER_OFFLINE)
		return GNOME_Evolution_Addressbook_OfflineUnavailable;
	else if (result == EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED)
		return GNOME_Evolution_Addressbook_PermissionDenied;
	else
		return GNOME_Evolution_Addressbook_OtherError;
}

static char *
e_book_backend_exchange_get_static_capabilites (EBookBackend *backend)
{
	return g_strdup("net,bulk-removes,do-initial-query,cache-completions,no-contactlist-option");
}

static gboolean
e_book_backend_exchange_construct (EBookBackendExchange *backend)
{
	g_assert (backend != NULL);
	g_assert (E_IS_BOOK_BACKEND_EXCHANGE (backend));

	if (! e_book_backend_construct (E_BOOK_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

static void
e_book_backend_exchange_set_mode (EBookBackend *backend, int mode)
{
	EBookBackendExchange *be = E_BOOK_BACKEND_EXCHANGE (backend);
	EBookBackendExchangePrivate *bepriv = be->priv;
	ExchangeAccount *account;

	bepriv->mode = mode;
	/* if (e_book_backend_is_loaded (backend)) { */
	if (mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		e_book_backend_set_is_writable (backend, FALSE);
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
		/* FIXME : free context ? */
	} else if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
		e_book_backend_set_is_writable (backend, bepriv->is_writable);
		e_book_backend_notify_writable (backend, bepriv->is_writable);
		e_book_backend_notify_connection_status (backend, TRUE);
		account = exchange_component_get_account_for_uri (global_exchange_component, NULL);
		if (!exchange_account_get_context (account))
			e_book_backend_notify_auth_required (backend);
	}
	/*}*/
}

/**
 * e_book_backend_exchange_new:
 *
 * Creates a new #EBookBackendExchange.
 *
 * Return value: the new #EBookBackendExchange.
 */
EBookBackend *
e_book_backend_exchange_new (void)
{
	EBookBackendExchange *backend;

	backend = g_object_new (e_book_backend_exchange_get_type (), NULL);

	if (! e_book_backend_exchange_construct (backend)) {
		g_object_unref (backend);

		return NULL;
	}

	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_exchange_dispose (GObject *object)
{
	EBookBackendExchange *be;

	be = E_BOOK_BACKEND_EXCHANGE (object);

	if (be->priv) {
		if (be->priv->folder) {
			e_folder_exchange_unsubscribe (be->priv->folder);
			g_object_unref (be->priv->folder);
		}

		if (be->priv->exchange_uri)
			g_free (be->priv->exchange_uri);

		if (be->priv->original_uri)
			g_free (be->priv->original_uri);

		if (be->priv->account)
			g_object_unref (be->priv->account);

		if (be->priv->ops)
			g_hash_table_destroy (be->priv->ops);

		if (be->priv->cache)
			g_object_unref (be->priv->cache);

		if (be->priv->create_mutex)
			g_mutex_free (be->priv->create_mutex);
			

		g_free (be->priv);
		be->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);	
}

static void
e_book_backend_exchange_class_init (EBookBackendExchangeClass *klass)
{
	GObjectClass  *object_class = (GObjectClass *) klass;
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (klass);
	EBookBackendSyncClass *sync_class = E_BOOK_BACKEND_SYNC_CLASS (klass);
	int i;

	parent_class = g_type_class_ref (e_book_backend_get_type ());

	/* Static initialization */
	field_names_array = g_ptr_array_new ();
	g_ptr_array_add (field_names_array, E2K_PR_DAV_UID);
	g_ptr_array_add (field_names_array, E2K_PR_DAV_LAST_MODIFIED);
	g_ptr_array_add (field_names_array, E2K_PR_DAV_CREATION_DATE);
	g_ptr_array_add (field_names_array, E2K_PR_MAPI_EMAIL_1_ADDRTYPE);
	g_ptr_array_add (field_names_array, E2K_PR_MAPI_EMAIL_2_ADDRTYPE);
	g_ptr_array_add (field_names_array, E2K_PR_MAPI_EMAIL_3_ADDRTYPE);
	g_ptr_array_add (field_names_array, E2K_PR_HTTPMAIL_HAS_ATTACHMENT);
	for (i = 0; i < num_prop_mappings; i ++)
		g_ptr_array_add (field_names_array, prop_mappings[i].prop_name);
	field_names = (const char **)field_names_array->pdata;
	n_field_names = field_names_array->len;

	/* Set the virtual methods. */
	backend_class->load_source             = e_book_backend_exchange_load_source;
	backend_class->get_static_capabilities = e_book_backend_exchange_get_static_capabilites;
	backend_class->start_book_view         = e_book_backend_exchange_start_book_view;
	backend_class->stop_book_view          = e_book_backend_exchange_stop_book_view;
	backend_class->cancel_operation        = e_book_backend_exchange_cancel_operation;
	backend_class->set_mode			= e_book_backend_exchange_set_mode;
	backend_class->get_supported_auth_methods = e_book_backend_exchange_get_supported_auth_methods;
	backend_class->authenticate_user     = e_book_backend_exchange_authenticate_user;

	sync_class->remove_sync                = e_book_backend_exchange_remove;
	sync_class->create_contact_sync        = e_book_backend_exchange_create_contact;
	sync_class->remove_contacts_sync       = e_book_backend_exchange_remove_contacts;
	sync_class->modify_contact_sync        = e_book_backend_exchange_modify_contact;
	sync_class->get_contact_sync           = e_book_backend_exchange_get_contact;
	sync_class->get_contact_list_sync      = e_book_backend_exchange_get_contact_list;
	sync_class->get_changes_sync           = e_book_backend_exchange_get_changes;
	sync_class->get_supported_fields_sync  = e_book_backend_exchange_get_supported_fields;
	sync_class->get_required_fields_sync   = e_book_backend_exchange_get_required_fields;

	object_class->dispose = e_book_backend_exchange_dispose;
}

static void
e_book_backend_exchange_init (EBookBackendExchange *backend)
{
	EBookBackendExchangePrivate *priv;

	priv            	= g_new0 (EBookBackendExchangePrivate, 1);
	priv->ops       	= g_hash_table_new (NULL, NULL);
	priv->is_cache_ready 	= FALSE;
	priv->marked_for_offline= FALSE;
	priv->cache		= NULL;
	priv->original_uri 	= NULL;
	priv->is_writable 	= TRUE;

	priv->create_mutex      = g_mutex_new ();

	backend->priv 		= priv;
}

E2K_MAKE_TYPE (e_book_backend_exchange, EBookBackendExchange, e_book_backend_exchange_class_init, e_book_backend_exchange_init, PARENT_TYPE)

