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

#define DEBUG
#undef DEBUG_TIMERS

#include "config.h"  
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <e-util/e-sexp.h>
#include <e-util/e-xml-hash-utils.h>
#include <ebook/e-card-simple.h>

#include "e2k-connection.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "mapi.h"
#include "exchange-component.h"
#include "exchange-account.h"
#include "exchange-hierarchy.h"
#include "e-folder-exchange.h"
#include "pas-backend-exchange.h"
#include "pas/pas-book.h"
#include "pas/pas-card-cursor.h"
#include "pas/pas-backend-card-sexp.h"
#include "pas/pas-backend-summary.h"

#include <gal/util/e-util.h>

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#define SUMMARY_FLUSH_TIMEOUT 5000

#define PARENT_TYPE PAS_TYPE_BACKEND
static PASBackendClass *parent_class;

typedef struct _PASBackendExchangeCursorPrivate PASBackendExchangeCursorPrivate;
typedef struct _PASBackendExchangeBookView PASBackendExchangeBookView;
typedef struct _PASBackendExchangeChangeContext PASBackendExchangeChangeContext;

struct _PASBackendExchangePrivate {
	char     *exchange_uri;
	EFolder  *folder;

	E2kRestriction *base_rn;

	ExchangeAccount *account;
	E2kConnection *conn;
	gboolean connected;
	EList    *book_views;

	EList    *supported_fields;

	PASBackendSummary *summary;
};

struct _PASBackendExchangeCursorPrivate {
	PASBackend *backend;
	PASBook    *book;

	GList      *elements;
	long       num_elements;
};

struct _PASBackendExchangeBookView {
	PASBookView               *book_view;
	PASBackendExchangePrivate *bepriv;
	gchar                     *search;
	PASBackendCardSExp        *card_sexp;
	GHashTable                *contact_hash;
	int                        num_contacts;
	gchar                     *change_id;
	PASBackendExchangeChangeContext *change_context;
#ifdef DEBUG_TIMERS
	GTimer *search_timer;
#endif
};

struct _PASBackendExchangeChangeContext {
	GHashTable *id_hash; /* hash built up of existing ids (in the
				folder), so we can tell if cards were
				deleted */
	GList *add_cards;
	GList *add_ids;
	GList *mod_cards;
	GList *mod_ids;
	GList *del_ids;
};

typedef struct PropMapping PropMapping;

static void subscription_notify (E2kConnection *conn, const char *uri, E2kConnectionChangeType type, gpointer user_data);
static void proppatch_address(PropMapping *prop_mapping, ECardSimple *new_card, ECardSimple *cur_card, E2kProperties *props);
static void proppatch_email(PropMapping *prop_mapping, ECardSimple *new_card, ECardSimple *cur_card, E2kProperties *props);
static void proppatch_date(PropMapping *prop_mapping, ECardSimple *new_card, ECardSimple *cur_card, E2kProperties *props);
static void populate_date(ECardSimpleField simple_field, ECardSimple *new_card, void *data);
#ifdef ENABLE_CATEGORIES
static void proppatch_categories(PropMapping *prop_mapping, ECardSimple *new_card, ECardSimple *cur_card, E2kProperties *props);
static void populate_categories(ECardSimpleField simple_field, ECardSimple *new_card, void *data);
#endif
static E2kRestriction *pas_backend_exchange_build_restriction (const char *sexp,
							       E2kRestriction *base_rn);

static ECardSimple* e_card_simple_from_props (ExchangeAccount *account, E2kConnection *conn, E2kResult *result);

static const char *contact_name (ECardSimple *card);

static GPtrArray *field_names_array;
static const char **field_names;
static int n_field_names;

static GNOME_Evolution_Addressbook_BookListener_CallStatus
soup_error_to_pas (guint soup_code)
{
	if (SOUP_ERROR_IS_SUCCESSFUL (soup_code))
		return GNOME_Evolution_Addressbook_BookListener_Success;

	switch (soup_code) {
	case SOUP_ERROR_CANT_AUTHENTICATE:
	case SOUP_ERROR_CANT_AUTHENTICATE_PROXY:
		return GNOME_Evolution_Addressbook_BookListener_AuthenticationFailed;
	case SOUP_ERROR_CANT_CONNECT:
	case SOUP_ERROR_CANT_CONNECT_PROXY:
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;
	case SOUP_ERROR_IO:
	case SOUP_ERROR_MALFORMED:
	case SOUP_ERROR_CANCELLED:
	default:
		return GNOME_Evolution_Addressbook_BookListener_OtherError;
	}
}

static gboolean
free_cards (char *uri, gpointer bleah,
	    gpointer foo)
{
	g_free (uri);
	return TRUE;
}

static void
exchange_book_view_free(PASBackendExchangeBookView *view, void *closure)
{
	g_free (view->search);
	g_object_unref (view->card_sexp);

	g_hash_table_foreach_remove (view->contact_hash,
				     (GHRFunc)free_cards, NULL);
	g_hash_table_destroy (view->contact_hash);

	g_free (view);
}

static void
view_destroy(GObject *object, gpointer data)
{
	PASBook            *book = (PASBook *)data;
	PASBackendExchange *be;
	EIterator          *iter;

	be = PAS_BACKEND_EXCHANGE(pas_book_get_backend(book));
	for (iter = e_list_get_iterator (be->priv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		const PASBackendExchangeBookView *view = e_iterator_get (iter);
		if (view->book_view == PAS_BOOK_VIEW(object)) {
			e_iterator_delete (iter);
			break;
		}
	}
	g_object_unref (iter);

	bonobo_object_unref (BONOBO_OBJECT (book));
}

static void
build_summary (PASBackendExchange *be)
{
	PASBackendExchangePrivate *bepriv = be->priv;
	E2kResult *results;
	int count;

	if (e_folder_exchange_search_sync (bepriv->folder,
					   field_names, n_field_names,
					   FALSE, bepriv->base_rn, NULL,
					   &results, &count)
	    != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("build_summary: error building list\n");
		/* destroy the summary object here, so we don't try to query on it */
		g_object_unref (bepriv->summary);
		bepriv->summary = NULL;
		return;
	}
	else {
		int i;

		for (i = 0; i < count; i ++) {
			ECardSimple *simple = e_card_simple_from_props (bepriv->account, bepriv->conn, &results[i]);
			char *vcard;

			if (!simple) /* XXX should we error out here and destroy the summary? */
				continue;

			e_card_simple_sync_card(simple);

			vcard = e_card_simple_get_vcard(simple);
			pas_backend_summary_add_card (bepriv->summary, vcard);
			g_free (vcard);
			g_object_unref (simple);
		}
		e2k_results_free (results, count);

		pas_backend_summary_save (bepriv->summary);
	}
}

static const char *folder_props[] = {
	PR_ACCESS,
	E2K_PR_DAV_LAST_MODIFIED
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static GNOME_Evolution_Addressbook_BookListener_CallStatus
pas_backend_exchange_connect (PASBackendExchange *be)
{
	PASBackendExchangePrivate *bepriv = be->priv;
	ExchangeHierarchy *hier;
	char *summary_filename, *summary_path;
	char *dirname;
	char *date_prop, *access_prop;
	int access;
	E2kResult *results;
	int nresults;
	int status;
	time_t folder_mtime;
	int i;

	bepriv->account = exchange_component_get_account_for_uri (bepriv->exchange_uri);
	if (!bepriv->account)
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;
	bepriv->conn = exchange_account_get_connection (bepriv->account);
	if (!bepriv->conn)
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;

	bepriv->folder = exchange_account_get_folder (bepriv->account, bepriv->exchange_uri);
	if (!bepriv->folder)
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;
	g_object_ref (bepriv->folder);

	/* check permissions on the folder */
	E2K_DEBUG_HINT ('A');
	status = e_folder_exchange_propfind_sync (bepriv->folder, "0",
						  folder_props, n_folder_props,
						  &results, &nresults);

	if (status != SOUP_ERROR_DAV_MULTISTATUS) {
		bepriv->connected = FALSE;
		return GNOME_Evolution_Addressbook_BookListener_OtherError;
	}

	access_prop = e2k_properties_get_prop (results[0].props, PR_ACCESS);
	if (access_prop)
		access = atoi (access_prop);
	else
		access = ~0;

	if (!(access & MAPI_ACCESS_READ)) {
		bepriv->connected = FALSE;
		return GNOME_Evolution_Addressbook_BookListener_PermissionDenied;
	}

	pas_backend_set_is_writable (PAS_BACKEND (be),
				     (access & MAPI_ACCESS_CREATE_CONTENTS) != 0);

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

	/* ensure the path to our change summary file exists */
	dirname = g_strdup_printf ("%s/Addressbook", bepriv->account->storage_dir);
	e_mkdir_hier (dirname, S_IRWXU);
	summary_filename = g_strdup (e2k_uri_path (bepriv->exchange_uri));
	for (i = 1; i < strlen (summary_filename); i ++) {
		if (summary_filename[i] == '/')
			summary_filename[i] = '_';
	}
	summary_path = g_strconcat (dirname, summary_filename, ".summary", NULL);

	bepriv->summary = pas_backend_summary_new (summary_path, SUMMARY_FLUSH_TIMEOUT);
	if (pas_backend_summary_is_up_to_date (bepriv->summary, folder_mtime) == FALSE
	    || pas_backend_summary_load (bepriv->summary) == FALSE ) {
		/* generate the summary here */
		build_summary (be);
	}

	g_free (dirname);
	g_free (summary_filename);
	g_free (summary_path);

	/* now subscribe to our folder so we notice when it changes */
	e_folder_exchange_subscribe (bepriv->folder,
				     E2K_CONNECTION_OBJECT_CHANGED, 30,
				     subscription_notify, be);

	bepriv->connected = TRUE;
	pas_backend_set_is_loaded (PAS_BACKEND (be), TRUE);
	return GNOME_Evolution_Addressbook_BookListener_Success;
}

#define DATE_FIELD(x,z) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_CONTACTS_##x, z, FLAG_COMPOSITE | FLAG_UNLIKEABLE, proppatch_date, populate_date }
#define EMAIL_FIELD(x,z) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_MAPI_##x##_ADDRESS, z, FLAG_EMAIL | FLAG_COMPOSITE, proppatch_email }
#define ADDRESS_FIELD(x,z) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_CONTACTS_##x, z, FLAG_COMPOSITE, proppatch_address, NULL }

#define CONTACT_FIELD(x,z) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_CONTACTS_##x, z, 0 }
#define MAPI_ID_FIELD(x,y,z) { E_CARD_SIMPLE_FIELD_##x, y, z, FLAG_UNLIKEABLE, NULL }
#define CAL_FIELD(x,z) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_CALENDAR_##x, z, FLAG_UNLIKEABLE, NULL }
#define HTTPMAIL_FIELD(x,y,z) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_HTTPMAIL_##y, z, FLAG_UNLIKEABLE | FLAG_PUT, NULL }
#define EXCHANGE_FIELD(x,y,z,w,v) { E_CARD_SIMPLE_FIELD_##x, E2K_PR_EXCHANGE_##y, z, FLAG_COMPOSITE, w, v }

struct PropMapping {
	ECardSimpleField simple_field;
	char *prop_name;
	char *pas_field;
	int flags;
#define FLAG_UNLIKEABLE 0x001  /* can't use LIKE with it */
#define FLAG_COMPOSITE  0x002  /* read-only fields that can be written
                                 to only by specifying the values of
                                 individual parts (as other fields) */
#define FLAG_PUT        0x020  /* requires a PUT request */
#define FLAG_EMAIL      0x100  /* email field, so we know to invoke our magic email address handling */
	void (*composite_proppatch_func)(PropMapping *prop_mapping, ECardSimple *new_card, ECardSimple *cur_card, E2kProperties *props);
	void (*composite_populate_func)(ECardSimpleField pas_field, ECardSimple *new_card, void *data);
};

static PropMapping
prop_mappings[] = {
	CONTACT_FIELD (FULL_NAME, "full_name"),
	CONTACT_FIELD (FAMILY_NAME, "family_name"),
	CONTACT_FIELD (GIVEN_NAME, "given_name"),
	CONTACT_FIELD (ADDITIONAL_NAME, "middle_name"),
	CONTACT_FIELD (NAME_SUFFIX, "name_suffix"),
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

	EMAIL_FIELD (EMAIL, "email"),
	EMAIL_FIELD (EMAIL_2, "email_2"),
	EMAIL_FIELD (EMAIL_3, "email_3"),

	ADDRESS_FIELD (ADDRESS_BUSINESS, "business_address"),
	ADDRESS_FIELD (ADDRESS_HOME, "home_address"),
	ADDRESS_FIELD (ADDRESS_OTHER, "other_address"),

	CONTACT_FIELD (URL, "url"),
	CONTACT_FIELD (ORG_UNIT, "org_unit"),
	CONTACT_FIELD (OFFICE, "office"),
	CONTACT_FIELD (ROLE, "role"),
	CONTACT_FIELD (MANAGER, "manager"),
	CONTACT_FIELD (ASSISTANT, "assistant"),
	CONTACT_FIELD (NICKNAME, "nickname"),
	CONTACT_FIELD (SPOUSE, "spouse"),

	DATE_FIELD (BIRTH_DATE, "birth_date"),
	DATE_FIELD (ANNIVERSARY, "anniversary"),
	CAL_FIELD (FBURL, "fburl"),

	HTTPMAIL_FIELD (NOTE, TEXT_DESCRIPTION, "note"),

#ifdef ENABLE_CATEGORIES
	/* this doesn't work at the moment */
	EXCHANGE_FIELD (CATEGORIES, KEYWORDS, "categories", proppatch_categories, populate_categories)
#endif
};
static int num_prop_mappings = sizeof(prop_mappings) / sizeof (prop_mappings[0]);

static const char *
pas_backend_exchange_prop_to_exchange (char *propname)
{
	int i;

	for (i = 0; i < num_prop_mappings; i ++)
		if (prop_mappings[i].pas_field && !strcmp (prop_mappings[i].pas_field, propname))
			return prop_mappings[i].prop_name;

	return NULL;
}

static void
get_email_field_from_props (ExchangeAccount *account, E2kConnection *conn,
			    PropMapping *prop_mapping, E2kResult *result,
			    ECardSimple *simple, char *data)
{
	char *emailtype;
	char *typeselector;

	/* here's where we do the super involved
	   conversion from local email addresses to
	   internet addresses for display in
	   evolution. */
	if (prop_mapping->simple_field == E_CARD_SIMPLE_FIELD_EMAIL)
		typeselector = E2K_PR_MAPI_EMAIL_ADDRTYPE;
	else if (prop_mapping->simple_field == E_CARD_SIMPLE_FIELD_EMAIL_2)
		typeselector = E2K_PR_MAPI_EMAIL_2_ADDRTYPE;
	else if (prop_mapping->simple_field == E_CARD_SIMPLE_FIELD_EMAIL_3)
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
		e_card_simple_set (simple, prop_mapping->simple_field, 
				   data);
	}
	else if (!strcmp (emailtype, "EX")) {
		E2kGlobalCatalog *gc;
		E2kGlobalCatalogEntry *entry = NULL;

		gc = exchange_account_get_global_catalog (account);
		if (gc) {
			e2k_global_catalog_lookup (
				gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
				data, E2K_GLOBAL_CATALOG_LOOKUP_EMAIL,
				&entry);
		}
		if (entry) {
			e_card_simple_set (simple, prop_mapping->simple_field,
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
		e_card_simple_set (simple, prop_mapping->simple_field, 
				   data);
	}

	return;
}

/* Exchange uses \r in some strings and \r\n in others. ECard always
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

static ECardSimple*
e_card_simple_from_props (ExchangeAccount *account, E2kConnection *conn,
			  E2kResult *result)
{
	ECard *card;
	ECardSimple *simple;
	int i;

	card = e_card_new ("");
	simple = e_card_simple_new (card);
	g_object_unref (card);

	e_card_simple_set_id (simple, result->href);

	for (i = 0; i < num_prop_mappings; i ++) {
		char *data = e2k_properties_get_prop (result->props, prop_mappings[i].prop_name);
		if (data) {
			if (prop_mappings[i].flags & FLAG_EMAIL) {
				get_email_field_from_props (account, conn, &prop_mappings[i], result, simple, data);
			}
			else if (prop_mappings[i].flags & FLAG_COMPOSITE && prop_mappings[i].composite_populate_func) {
				prop_mappings[i].composite_populate_func (prop_mappings[i].simple_field, simple, (void*)data);
			}
			else {
				char *unix_data = data;

				if (strchr (unix_data, '\r'))
					unix_data = unixify (data);
				e_card_simple_set (simple, prop_mappings[i].simple_field, 
						   unix_data);
				if (unix_data != data)
					g_free (unix_data);
			}
		}
	}

	return simple;
}

typedef struct {
	PASBackendExchangePrivate *bepriv;
	PASBook *book;
	char *uri;
	char *put_body;
	ECardSimple *new_card;
	ECardSimple *current_card;
} ProppatchClosure;

static int
do_put (E2kConnection *conn, const char *uri,
	const char *subject, const char *put_body)
{
	int status;
	char *put_request;

	put_request = g_strdup_printf ("content-class: urn:content-classes:person\r\n"
				       "MIME-Version: 1.0\r\n"
				       "Content-Type: text/plain;\r\n"
				       "\tcharset=\"utf-8\"\r\n"
				       "Content-Transfer-Encoding: 8bit\r\n"
				       "Subject: %s\r\n"
				       "\r\n%s",
				       subject ? subject : "",
				       put_body);

	E2K_DEBUG_HINT ('A');
	status = e2k_connection_put_sync (conn, uri,
					  "message/rfc822", put_request, strlen (put_request));

	g_free (put_request);

	return status;
}

static gboolean
vcard_matches_search (const PASBackendExchangeBookView *view, char *vcard_string)
{
	/* If this is not a search context view, it doesn't match be default */
	if (view->card_sexp == NULL)
		return FALSE;

	return pas_backend_card_sexp_match_vcard (view->card_sexp, vcard_string);
}

static void
proppatch_create_cb (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	ProppatchClosure *closure = user_data;
	PASBackendExchangePrivate *bepriv = closure->bepriv;
	PASBook *book = closure->book;
	ECardSimple *new_card = closure->new_card;
	char *uri = closure->uri;
	char *put_body = closure->put_body;
	int errorcode = msg->errorcode;
	EIterator *iter;

	g_free (closure);

	if (put_body && errorcode == SOUP_ERROR_DAV_MULTISTATUS) {
		/* do the PUT request if we're supposed to */
		int status = do_put (conn, uri, contact_name (new_card), put_body);
		if (status != SOUP_ERROR_OK)
			errorcode = status;
	}

	for (iter = e_list_get_iterator (bepriv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		const PASBackendExchangeBookView *v = e_iterator_get (iter);

		pas_book_view_notify_complete (v->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);
	}
	g_object_unref (iter);

	if (errorcode == SOUP_ERROR_DAV_MULTISTATUS) {
		/* the card was added, let's let the views know about it */
		char *new_vcard;

		e_card_simple_set_id (new_card, uri);

		new_vcard = e_card_simple_get_vcard_assume_utf8 (new_card);

		for (iter = e_list_get_iterator (bepriv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
			const PASBackendExchangeBookView *view = e_iterator_get (iter);

			bonobo_object_ref (BONOBO_OBJECT (view->book_view));

			if (vcard_matches_search (view, new_vcard))
				pas_book_view_notify_add_1 (view->book_view, new_vcard);

			/* and make sure to add it to the hashtable so
                           we'll ignore the updates the exchange
                           server might send about its addition */
			g_hash_table_insert (view->contact_hash,
					     g_strdup (uri),
					     GINT_TO_POINTER (1));

			bonobo_object_unref (BONOBO_OBJECT (view->book_view));
		}
		g_object_unref (iter);

		pas_book_respond_create (book, GNOME_Evolution_Addressbook_BookListener_Success,
					 uri);

		pas_backend_summary_add_card (bepriv->summary, new_vcard);

		g_free (new_vcard);
	}
	else
		pas_book_respond_create (book, GNOME_Evolution_Addressbook_BookListener_OtherError,
					 "");

	g_free (uri);
	g_free (put_body);
	g_object_unref (new_card);
}

static void
proppatch_modify_cb (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	ProppatchClosure *closure = user_data;
	PASBackendExchangePrivate *bepriv = closure->bepriv;
	PASBook *book = closure->book;
	ECardSimple *new_card = closure->new_card;
	ECardSimple *current_card = closure->current_card;
	char *uri = closure->uri;
	char *put_body = closure->put_body;
	int errorcode = msg->errorcode;
	EIterator *iter;

	g_free (closure);

	if (put_body && errorcode == SOUP_ERROR_DAV_MULTISTATUS) {
		/* do the PUT request if we're supposed to */
		int status = do_put (conn, uri, contact_name (new_card), put_body);
		if (status != SOUP_ERROR_OK)
			errorcode = status;
	}

	for (iter = e_list_get_iterator (bepriv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		const PASBackendExchangeBookView *v = e_iterator_get (iter);

		pas_book_view_notify_complete (v->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);
	}
	g_object_unref (iter);

	if (errorcode == SOUP_ERROR_DAV_MULTISTATUS) {
		/* the card was modified, let's let the views know about it */
		char *new_vcard, *current_vcard;

		new_vcard = e_card_simple_get_vcard_assume_utf8 (new_card);
		current_vcard = e_card_simple_get_vcard_assume_utf8 (current_card);

		for (iter = e_list_get_iterator (bepriv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
			gboolean old_match, new_match;
			const PASBackendExchangeBookView *view = e_iterator_get (iter);

			bonobo_object_ref (BONOBO_OBJECT (view->book_view));

			old_match = vcard_matches_search (view, current_vcard);
			new_match = vcard_matches_search (view, new_vcard);
			if (old_match && new_match)
				pas_book_view_notify_change_1 (view->book_view, new_vcard);
			else if (new_match)
				pas_book_view_notify_add_1 (view->book_view, new_vcard);
			else /* if (old_match) */
				pas_book_view_notify_remove_1 (view->book_view, e_card_simple_get_id (new_card));

			bonobo_object_unref (BONOBO_OBJECT (view->book_view));
		}
		g_object_unref (iter);

		pas_book_respond_modify (book, GNOME_Evolution_Addressbook_BookListener_Success);

		pas_backend_summary_remove_card (bepriv->summary, e_card_simple_get_id (new_card));
		pas_backend_summary_add_card (bepriv->summary, new_vcard);

		g_free (new_vcard);
		g_free (current_vcard);
	}
	else
		pas_book_respond_modify (book, GNOME_Evolution_Addressbook_BookListener_OtherError);

	g_free (uri);
	g_free (put_body);
	g_object_unref (new_card);
	g_object_unref (current_card);
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

static const ECardSimpleField email_fields[3] = {
	E_CARD_SIMPLE_FIELD_EMAIL,
	E_CARD_SIMPLE_FIELD_EMAIL_2,
	E_CARD_SIMPLE_FIELD_EMAIL_3
};

static const char *email1_props[] = {
	E2K_PR_MAPI_EMAIL_ENTRYID,
	E2K_PR_MAPI_EMAIL_ADDRTYPE,
	E2K_PR_MAPI_EMAIL_ADDRESS,
	E2K_PR_MAPI_EMAIL_DISPLAY_NAME,
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
		 ECardSimple *new_card, ECardSimple *cur_card,
		 E2kProperties *props)
{
	gboolean changed;
	char *new_email, *cur_email;
	int field, prop, emaillisttype = 0;

	/* We do all three email addresses (plus some additional data)
	 * when invoked for E_CARD_SIMPLE_FIELD_EMAIL. So skip EMAIL_2
	 * and EMAIL_3.
	 */
	if (prop_mapping->simple_field != E_CARD_SIMPLE_FIELD_EMAIL)
		return;

	for (field = 0; field < 3; field++) {
		new_email = e_card_simple_get (new_card, email_fields[field]);
		cur_email = cur_card ? e_card_simple_get (cur_card, email_fields[field]) : NULL;

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
			if (!cur_card)
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
	} else if (cur_card)
		e2k_properties_remove (props, E2K_PR_MAPI_EMAIL_ADDRESS_LIST);
}

#ifdef ENABLE_CATEGORIES
static void
proppatch_categories (PropMapping *prop_mapping,
		      ECardSimple *new_card, ECardSimple *cur_card,
		      E2kProperties *props)
{
	gboolean changed;
	char *new_categories, *cur_categories = NULL;
	EList *categories;
	EIterator *iter;
	GPtrArray *array = NULL;

	new_categories = e_card_simple_get (new_card, simple_field);
	if (cur_card)
		cur_categories = e_card_simple_get (cur_card, simple_field);

	changed = value_changed (cur_categories, new_categories);
	g_free (new_categories);
	g_free (cur_categories);

	if (!changed)
		return;


#ifdef DEBUG
	printf ("CATEGORIES = %s\n", new_categories);
#endif

	g_object_get ((new_card->card),
			"category_list", &categories,
			NULL);

	iter = e_list_get_iterator (categories);
	while (e_iterator_is_valid (iter)) {
		const char *category = e_iterator_get (iter);

		if (!array)
			array = g_ptr_array_new ();
		g_ptr_array_add (array, g_strdup (category));
		e_iterator_next (iter);
	}
	g_object_unref (iter);

	e2k_properties_set_string_array (props, prop_mapping->prop_name, array);
}

static void
populate_categories(ECardSimpleField simple_field, ECardSimple *new_card, void *data)
{
	GSList *list = data;
	GSList *l;
	EList *categories;

	categories = e_list_new ((EListCopyFunc) g_strdup, 
				 (EListFreeFunc) g_free,
				 NULL);

	for (l = list; l; l = l->next) {
		e_list_append (categories, l->data);
	}

	g_object_set ((new_card->card),
			"category_list", categories,
			NULL);

#ifdef DEBUG 
	{
		char *category_text = e_card_simple_get (new_card, E_CARD_SIMPLE_FIELD_CATEGORIES);
		printf ("populated categories = %s\n", category_text);
		g_free (category_text);
	}
#endif

	g_object_unref (categories);
}
#endif

static void
proppatch_date (PropMapping *prop_mapping,
		ECardSimple *new_card, ECardSimple *cur_card,
		E2kProperties *props)
{
	gboolean changed;
	char *new_date, *cur_date = NULL;
	ECardDate *date;
	time_t tt;
	struct tm then;

	new_date = e_card_simple_get (new_card, prop_mapping->simple_field);
	if (cur_card)
		cur_date = e_card_simple_get (cur_card, prop_mapping->simple_field);

	changed = value_changed (cur_date, new_date);
	g_free (cur_date);
	g_free (new_date);

	if (!changed)
		return;

	g_object_get ((new_card->card),
			e_card_simple_get_ecard_field (new_card, prop_mapping->simple_field), &date,
			NULL);
	if (!date)
		return;

	memset (&then, 0, sizeof(then));
	then.tm_year = date->year - 1900;
	then.tm_mon  = date->month - 1;
	then.tm_mday = date->day;
	then.tm_isdst = -1;
	tt = mktime (&then);

	e2k_properties_set_date (props, prop_mapping->prop_name,
				 e2k_make_timestamp (tt));
}

static void
populate_date(ECardSimpleField simple_field, ECardSimple *new_card, void *data)
{
	char *date = (char*)data;
	time_t tt;
	struct tm *then;
	ECardDate ecard_date;

	tt = e2k_parse_timestamp (date);

	then = gmtime (&tt);
	
	ecard_date.year = then->tm_year + 1900;
	ecard_date.month = then->tm_mon + 1;
	ecard_date.day = then->tm_mday;
	
	g_object_set ((new_card->card),
			e_card_simple_get_ecard_field(new_card, simple_field), &ecard_date,
			NULL);
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
		   ECardSimple *new_card, ECardSimple *cur_card,
		   E2kProperties *props)
{
	const ECardDeliveryAddress *new_address, *cur_address = NULL;
	char *new_addrprops[ADDRPROP_LAST], *cur_addrprops[ADDRPROP_LAST];
	char **propnames, *value;
	int i;

	switch (prop_mapping->simple_field) {
	case E_CARD_SIMPLE_FIELD_ADDRESS_HOME:
		new_address = e_card_simple_get_delivery_address (
			new_card, E_CARD_SIMPLE_ADDRESS_ID_HOME);
		if (cur_card) {
			cur_address = e_card_simple_get_delivery_address (
				cur_card, E_CARD_SIMPLE_ADDRESS_ID_HOME);
		}
		propnames = homeaddrpropnames;
		break;

	case E_CARD_SIMPLE_FIELD_ADDRESS_BUSINESS:
		new_address = e_card_simple_get_delivery_address (
			new_card, E_CARD_SIMPLE_ADDRESS_ID_BUSINESS);
		if (cur_card) {
			cur_address = e_card_simple_get_delivery_address (
				cur_card, E_CARD_SIMPLE_ADDRESS_ID_BUSINESS);
		}
		propnames = workaddrpropnames;
		break;

	case E_CARD_SIMPLE_FIELD_ADDRESS_OTHER:
	default:
		new_address = e_card_simple_get_delivery_address (
			new_card, E_CARD_SIMPLE_ADDRESS_ID_OTHER);
		if (cur_card) {
			cur_address = e_card_simple_get_delivery_address (
				cur_card, E_CARD_SIMPLE_ADDRESS_ID_OTHER);
		}
		propnames = otheraddrpropnames;
		break;
	}

	if (!new_address) {
		if (cur_address) {
			for (i = 0; i < ADDRPROP_LAST; i ++)
				e2k_properties_remove (props, propnames[i]);
		}
		return;
	}

	new_addrprops [ADDRPROP_POSTOFFICEBOX]		= new_address->po;
	new_addrprops [ADDRPROP_STREET]			= new_address->street;
	new_addrprops [ADDRPROP_CITY]			= new_address->city;
	new_addrprops [ADDRPROP_STATE]			= new_address->region;
	new_addrprops [ADDRPROP_POSTALCODE]		= new_address->code;
	new_addrprops [ADDRPROP_COUNTRY]		= new_address->country;
	if (cur_address) {
		cur_addrprops [ADDRPROP_POSTOFFICEBOX]	= cur_address->po;
		cur_addrprops [ADDRPROP_STREET]		= cur_address->street;
		cur_addrprops [ADDRPROP_CITY]		= cur_address->city;
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
	}
}

static const char *
contact_name (ECardSimple *card)
{
	const char *contact_name;

	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_FULL_NAME);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_FILE_AS);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_EMAIL);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_EMAIL_2);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_EMAIL_3);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_ORG);
	if (contact_name && *contact_name)
		return contact_name;
	contact_name = e_card_simple_get_const (card, E_CARD_SIMPLE_FIELD_TITLE);
	if (contact_name && *contact_name)
		return contact_name;

	return NULL;
}

static void
pas_backend_exchange_proppatch_card (PASBackendExchangePrivate *bepriv,
				     const char *uri,
				     PASBook *book,
				     ECardSimple *new_card,
				     ECardSimple *current_card)
{
	int i;
	E2kProperties *props;
	char *put_body = NULL;
	ProppatchClosure *closure;
	EIterator *iter;

	props = e2k_properties_new ();

	if (!current_card) {
		const char *subject;

		subject = contact_name (new_card);

		/* Set up some additional fields when creating a new card */
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
			prop_mappings[i].composite_proppatch_func (&prop_mappings[i], new_card, current_card, props);
		} else {
			char *new_value, *current_value;
			gboolean changed;

			new_value = e_card_simple_get (new_card, prop_mappings[i].simple_field);
			if (new_value && !*new_value) {
				g_free (new_value);
				new_value = NULL;
			}
			current_value = current_card ? e_card_simple_get (current_card, prop_mappings[i].simple_field) : NULL;
			if (current_value && !*current_value) {
				g_free (current_value);
				current_value = NULL;
			}

			changed = value_changed (current_value, new_value);
			g_free (current_value);

			if (changed) {
				if (prop_mappings[i].flags & FLAG_PUT)
					put_body = e2k_lf_to_crlf (new_value);
				else if (new_value) {
					e2k_properties_set_string (
						props, prop_mappings[i].prop_name,
						new_value);
					new_value = NULL;
				} else {
					e2k_properties_remove (
						props,
						prop_mappings[i].prop_name);
				}
			}

			g_free (new_value);
		}
	}

	if (e2k_properties_empty (props)) {
		e2k_properties_free (props);
		if (current_card)
			pas_book_respond_modify (book, GNOME_Evolution_Addressbook_BookListener_OtherError);
		else
			pas_book_respond_create (book, GNOME_Evolution_Addressbook_BookListener_OtherError, "");
		return;
	}

	closure = g_new0 (ProppatchClosure, 1);
	closure->bepriv = bepriv;
	closure->book = book;
	closure->uri = g_strdup(uri);
	closure->put_body = put_body;
	closure->new_card = new_card;
	if (new_card)
		g_object_ref (new_card);
	closure->current_card = current_card;
	if (current_card)
		g_object_ref (current_card);

	for (iter = e_list_get_iterator (bepriv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		const PASBackendExchangeBookView *v = e_iterator_get (iter);
		char *message;
		if (current_card)
			message = g_strdup_printf (_("Modifying %s"), uri);
		else
			message = g_strdup_printf (_("Creating %s"), uri);

		pas_book_view_notify_status_message (v->book_view,
						     message);
		g_free (message);
	}
	g_object_unref (iter);

	E2K_DEBUG_HINT ('A');
	if (current_card) {
		e2k_connection_proppatch (bepriv->conn, uri, props, FALSE,
					  proppatch_modify_cb, closure);
	} else {
		e2k_connection_proppatch (bepriv->conn, uri, props, TRUE,
					  proppatch_create_cb, closure);
	}

	e2k_properties_free (props);
}

static void
pas_backend_exchange_process_create_card (PASBackend           *backend,
					  PASBook              *book,
					  PASCreateCardRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBackendExchangePrivate *bepriv = be->priv;
	ECard *new_ecard;
	ECardSimple *new_card;
	const char *name, *folder_uri;
	char *uri, *contact_name_enc, *contact_basename;
	int status;
	int num;

	new_ecard = e_card_new (req->vcard);
	new_card = e_card_simple_new (new_ecard);
	g_object_unref (new_ecard);

	/* figure out the right uri to be using */
	name = contact_name (new_card);
	if (!name)
		name = "No Subject";

	folder_uri = e_folder_exchange_get_internal_uri (bepriv->folder);

	contact_name_enc = e2k_uri_encode (name, NULL);
	contact_basename = e2k_uri_concat (folder_uri, contact_name_enc);
	g_free (contact_name_enc);

	for (num = 1; ; num++) {
		char *uri_data;
		int uri_len;

		if (num == 1)
			uri = g_strdup_printf ("%s.EML", contact_basename);
		else
			uri = g_strdup_printf ("%s-%d.EML", contact_basename, num);

		E2K_DEBUG_HINT ('A');
		status = e2k_connection_get_sync (bepriv->conn, uri, &uri_data, &uri_len);
		if (status != SOUP_ERROR_OK)
			break;
		g_free (uri_data);
	}
	g_free (contact_basename);

	pas_backend_exchange_proppatch_card (bepriv, uri, book, new_card, NULL);

	g_object_unref (new_card);

	g_free (uri);
}





static void
pas_backend_exchange_process_modify_card (PASBackend           *backend,
					  PASBook              *book,
					  PASModifyCardRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBackendExchangePrivate *bepriv = be->priv;
	ECard *ecard;
	ECardSimple *new_card, *current_card = NULL;
	int status;
	E2kResult *results;
	int nresults;

	ecard = e_card_new (req->vcard);
	new_card = e_card_simple_new (ecard);
	g_object_unref (ecard);

	E2K_DEBUG_HINT ('A');
	status = e2k_connection_propfind_sync (bepriv->conn, e_card_simple_get_id (new_card), "0",
					       field_names,  n_field_names,
					       &results, &nresults);
	if (status == SOUP_ERROR_DAV_MULTISTATUS) {
		current_card = e_card_simple_from_props (bepriv->account, bepriv->conn, &results[0]);
		e2k_results_free (results, nresults);
	}


	pas_backend_exchange_proppatch_card (bepriv, e_card_simple_get_id (new_card), book, new_card, current_card);

	g_object_unref (new_card);
	if (current_card)
		g_object_unref (current_card);
}



typedef struct {
	PASBackendExchangePrivate *bepriv;
	PASBook *book;
	char *uri;
} RemoveCardClosure;

static void
remove_card_cb (E2kConnection *conn, SoupMessage *msg, gpointer data)
{
	RemoveCardClosure *closure = data;
	PASBook *book = closure->book;
	PASBackendExchangePrivate *bepriv = closure->bepriv;
	char *uri = closure->uri;
	EIterator *iter;

	g_free (closure);

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {

		for (iter = e_list_get_iterator (bepriv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
			const PASBackendExchangeBookView *view = e_iterator_get (iter);
			char *orig_uri;
			gpointer bleah;

			bonobo_object_ref (BONOBO_OBJECT (view->book_view));

			/* if it's in our contact hash (and therefore
                           matched the search for the view), remove it
                           from the hashtable and signal the view to
                           remove it from the UI */
			if (g_hash_table_lookup_extended (view->contact_hash, uri,
							  (gpointer*)&orig_uri, &bleah)) {

				pas_book_view_notify_remove_1 (view->book_view, uri);

				g_hash_table_remove (view->contact_hash, orig_uri);
				g_free (orig_uri);
			}

			bonobo_object_unref (BONOBO_OBJECT (view->book_view));
		}
		g_object_unref (iter);

		pas_backend_summary_remove_card (bepriv->summary, uri);
	}

	g_free (uri);

	pas_book_respond_remove (book, soup_error_to_pas (msg->errorcode));
}

static void
pas_backend_exchange_process_remove_cards (PASBackend           *backend,
					   PASBook              *book,
					   PASRemoveCardsRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBackendExchangePrivate *bepriv = be->priv;
	RemoveCardClosure *closure;

	/* since we don't report "bulk-removes" in our static
	   capabilities, the id list will always contain 1 element. */
	closure = g_new (RemoveCardClosure, 1);
	closure->bepriv = bepriv;
	closure->book = book;
	closure->uri = g_strdup (req->ids->data);

	E2K_DEBUG_HINT ('A');
	e2k_connection_delete (bepriv->conn, closure->uri, remove_card_cb, closure);
}




static long
get_length(PASCardCursor *cursor, gpointer data)
{
	PASBackendExchangeCursorPrivate *cursor_data = (PASBackendExchangeCursorPrivate *) data;

	return cursor_data->num_elements;
}

static char *
get_nth(PASCardCursor *cursor, long n, gpointer data)
{
	PASBackendExchangeCursorPrivate *cursor_data = (PASBackendExchangeCursorPrivate *) data;

	g_return_val_if_fail (n < cursor_data->num_elements, NULL);

	return (char*)g_list_nth_data (cursor_data->elements, n);
}

static void
cursor_destroy(GObject *object, gpointer data)
{
	CORBA_Environment ev;
	GNOME_Evolution_Addressbook_Book corba_book;
	PASBackendExchangeCursorPrivate *cursor_data = (PASBackendExchangeCursorPrivate *) data;

	corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(cursor_data->book));

	CORBA_exception_init(&ev);

	GNOME_Evolution_Addressbook_Book_unref(corba_book, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("cursor_destroy: Exception unreffing "
			  "corba book.\n");
	}

	CORBA_exception_free(&ev);

	/* free the exchange specific cursor information */
	g_list_foreach (cursor_data->elements, (GFunc)g_free, NULL);
	g_list_free (cursor_data->elements);

	g_free(cursor_data);
}

static void
pas_backend_exchange_build_cards_list(PASBackendExchange *be,
				      PASBackendExchangeCursorPrivate *cursor_data,
				      char *search)
{
	PASBackendExchangePrivate *bepriv = be->priv;
	E2kRestriction *rn;
	E2kResult *results;
	int count;

	rn = pas_backend_exchange_build_restriction (search, bepriv->base_rn);

	E2K_DEBUG_HINT ('A');
	if (e_folder_exchange_search_sync (bepriv->folder,
					   field_names, n_field_names,
					   FALSE, rn, NULL,
					   &results, &count)
	    != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("pas_backend_exchange_changes: error building list\n");
		e2k_restriction_unref (rn);
		return;
	} else {
		int i;

		e2k_restriction_unref (rn);

		for (i = 0; i < count; i ++) {
			ECardSimple *simple = e_card_simple_from_props (bepriv->account, bepriv->conn, &results[i]);

			if (!simple)
				continue;

			e_card_simple_sync_card(simple);

			cursor_data->elements = g_list_prepend (cursor_data->elements, e_card_simple_get_vcard(simple));

			g_object_unref (simple);
		}
		e2k_results_free (results, count);

		cursor_data->num_elements = g_list_length (cursor_data->elements);
		cursor_data->elements = g_list_reverse (cursor_data->elements);
	}
}

static void
pas_backend_exchange_process_get_cursor (PASBackend          *backend,
					 PASBook             *book,
					 PASGetCursorRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	CORBA_Environment ev;
	PASBackendExchangeCursorPrivate *cursor_data;
	PASCardCursor *cursor;
	GNOME_Evolution_Addressbook_Book corba_book;

	cursor_data = g_new0(PASBackendExchangeCursorPrivate, 1);
	cursor_data->backend = backend;
	cursor_data->book = book;

	pas_backend_exchange_build_cards_list(be, cursor_data, req->search);

	corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(book));

	CORBA_exception_init(&ev);

	GNOME_Evolution_Addressbook_Book_ref(corba_book, &ev);
	
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_warning("pas_backend_exchange_process_get_cursor: Exception reffing "
			  "corba book.\n");
	}

	CORBA_exception_free(&ev);
	
	cursor = pas_card_cursor_new(get_length,
				     get_nth,
				     cursor_data);

	g_signal_connect (cursor, "destroy",
			  G_CALLBACK (cursor_destroy), cursor_data);
	
	pas_book_respond_get_cursor (book,
				     GNOME_Evolution_Addressbook_BookListener_Success,
				     cursor);
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
		rn = e2k_restriction_orv (
			e2k_restriction_content (
				pas_backend_exchange_prop_to_exchange ("full_name"),
				flags, str),
			e2k_restriction_content (
				pas_backend_exchange_prop_to_exchange ("family_name"),
				flags, str),
			NULL);
	} else {
		exchange_prop =
			pas_backend_exchange_prop_to_exchange (propname);
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
pas_backend_exchange_build_restriction (const char *query,
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



typedef struct {
	PASBackendExchange *be;
	PASBackendExchangeBookView *view;
	E2kRestriction *rn;
} RefreshData;

static gboolean
delete_cards (char *uri, gpointer bleah,
	      PASBookView *view)
{
	pas_book_view_notify_remove_1 (view, uri);
	g_free (uri);
	return TRUE;
}

static void
got_new_changed_objects (E2kConnection *conn, SoupMessage *msg,
			 E2kResult *results, int count,
			 gpointer user_data)
{
	RefreshData *rd = user_data;
	PASBackendExchangeBookView *view = rd->view;
	GList *modified_cards = NULL, *added_cards = NULL;
	CORBA_Environment ev;

	/* Exchange returns 400 Bad Request if there are no matches. */
	if (msg->errorcode == SOUP_ERROR_BAD_REQUEST)
		goto done;

	if (msg->errorcode == SOUP_ERROR_DAV_MULTISTATUS) {
		int i;

		/* fill our list with the objects returned */
		for (i = 0; i < count; i++) {
			ECardSimple *card;

			card = e_card_simple_from_props (rd->be->priv->account, conn, &results[i]);
			if (!card)
				continue;
			e_card_simple_sync_card(card);

			if (g_hash_table_lookup (view->contact_hash, results[i].href)) {
				modified_cards = g_list_append (modified_cards, e_card_simple_get_vcard (card));
			}
			else {
				g_hash_table_insert (view->contact_hash,
						     g_strdup (results[i].href),
						     GINT_TO_POINTER(1));

				added_cards = g_list_append (added_cards, e_card_simple_get_vcard (card));
			}

			g_object_unref (card);
		}

		if (count < view->num_contacts + g_list_length (added_cards)) {
			int i;
			GHashTable *new_contact_hash = g_hash_table_new (g_str_hash, g_str_equal);

			/* loop through the uids we got from this
                           query, removing them from the hash table
                           and adding them to the new hash table. */
			for (i = 0; i < count; i ++) {
				char *orig_uri;
				gpointer bleah;

				if (g_hash_table_lookup_extended (view->contact_hash,
								  results[i].href,
								  (gpointer*)&orig_uri, &bleah)) {
					g_hash_table_insert (new_contact_hash, orig_uri, GINT_TO_POINTER(1));
					g_hash_table_remove (view->contact_hash, orig_uri);
				}
			}

			g_hash_table_foreach_remove (view->contact_hash,
						     (GHRFunc)delete_cards, view->book_view);

			g_hash_table_destroy (view->contact_hash);
			view->contact_hash = new_contact_hash;
		}

		if (added_cards) {
			pas_book_view_notify_add (view->book_view, added_cards);
			view->num_contacts += g_list_length (added_cards);

			g_list_foreach (added_cards, (GFunc)g_free, NULL);
			g_list_free (added_cards);
		}
		if (modified_cards) {
			pas_book_view_notify_change (view->book_view, modified_cards);

			g_list_foreach (modified_cards, (GFunc)g_free, NULL);
			g_list_free (modified_cards);
		}
	}

 done:

	pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);
	CORBA_exception_init(&ev);
	bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
	CORBA_exception_free(&ev);

	e2k_restriction_unref (rd->rn);
	g_free (rd);
}

static void
pas_backend_exchange_refresh_view (PASBackendExchange *be,
				   PASBackendExchangeBookView *view)
{
	PASBackendExchangePrivate *bepriv = be->priv;
	RefreshData *rd;
	CORBA_Environment ev;

	/* if this isn't a search context, we don't need to worry about refreshing */
	if (!view->search)
		return;

	rd = g_new (RefreshData, 1);
	rd->be = be;
	rd->view = view;
	rd->rn = pas_backend_exchange_build_restriction (view->search,
							 bepriv->base_rn);

	CORBA_exception_init(&ev);
	bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
	CORBA_exception_free(&ev);

	/* execute the query */
	E2K_DEBUG_HINT ('A');
	e_folder_exchange_search (bepriv->folder,
				  field_names, n_field_names,
				  FALSE, rd->rn, NULL,
				  got_new_changed_objects, rd);
}

static void
subscription_notify (E2kConnection *conn, const char *uri,
		     E2kConnectionChangeType type, gpointer user_data)
{
	PASBackendExchange *be = user_data;
	EIterator *iter;

	for (iter = e_list_get_iterator (be->priv->book_views); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		PASBackendExchangeBookView *view = (gpointer)e_iterator_get(iter);
		pas_backend_exchange_refresh_view (be, view);
	}
	g_object_unref (iter);
}



typedef struct {
	PASBackendExchange *be;
	PASBackendExchangeBookView *view;
} SearchClosure;

static void
search_cb (E2kConnection *conn, SoupMessage *msg, 
	   E2kResult *results, int count,
	   gpointer user_data)
{
	SearchClosure *closure = user_data;
	PASBackendExchange *be = closure->be;
	PASBackendExchangeBookView *view = closure->view;
	int i;
	GList *cards = NULL;
	CORBA_Environment ev;

	g_free (closure);

#ifdef DEBUG_TIMERS
	g_message ("Elapsed time before search_cb was called for '%s' = %.5f seconds", view->search, g_timer_elapsed (view->search_timer, NULL));
#endif

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		/* XXX error condition here */
		goto done;
	}

	/* fill our list with the objects returned */
	for (i = 0; i < count; i++) {
		char *prop;
		ECardSimple *card;
#ifdef DEBUG_TIMERS
		GTimer *timer;

		timer = g_timer_new();
		g_timer_start (timer);
#endif
		card = e_card_simple_from_props (be->priv->account, conn, &results[i]);

#ifdef DEBUG_TIMERS
		g_message ("Elapsed time creating card from  '%s' = %.5f seconds", results[i].href,
			   g_timer_elapsed (timer, NULL));
		g_timer_destroy (timer);
#endif

		if (!card)
			continue;

		e_card_simple_sync_card(card);
		cards = g_list_append (cards, e_card_simple_get_vcard (card));

		prop = e2k_properties_get_prop (results[i].props, "DAV:getlastmodified");

		g_hash_table_insert (view->contact_hash, g_strdup (results[i].href), GINT_TO_POINTER(1));
		view->num_contacts ++;
		g_object_unref (card);
	}

#ifdef DEBUG_TIMERS
	g_message ("Total elapsed time for search '%s' = %.5f seconds", view->search, g_timer_elapsed (view->search_timer, NULL));
	g_timer_destroy (view->search_timer);
#endif

	if (cards) {
		pas_book_view_notify_add (view->book_view, cards);
			
		g_list_foreach (cards, (GFunc)g_free, NULL);
		g_list_free (cards);
	}

 done:
	pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);

	CORBA_exception_init(&ev);
	bonobo_object_release_unref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
	CORBA_exception_free(&ev);
}

static void
do_summary_query (PASBackendExchange         *be,
		  PASBackendExchangeBookView *view)
{
	GPtrArray *ids = pas_backend_summary_search (be->priv->summary, view->search);
	GList   *cards = NULL;
	gint    card_count = 0, card_threshold = 20, card_threshold_max = 3000;
	int i;

	for (i = 0; i < ids->len; i ++) {
		char *id = g_ptr_array_index (ids, i);
		char *vcard;

		vcard = pas_backend_summary_get_summary_vcard (be->priv->summary,
							       id);

		if (vcard) {
			cards = g_list_prepend (cards, vcard);
			card_count ++;

			/* If we've accumulated a number of checks, pass them off to the client. */
			if (card_count >= card_threshold) {
				pas_book_view_notify_add (view->book_view, cards);
				/* Clean up the handed-off data. */
				g_list_foreach (cards, (GFunc)g_free, NULL);
				g_list_free (cards);
				cards = NULL;
				card_count = 0;

				/* Yeah, this scheme is overly complicated.  But I like it. */
				if (card_threshold < card_threshold_max) {
					card_threshold = MIN (2*card_threshold, card_threshold_max);
				}
			}
		}
		else
			continue; /* XXX */
	}

	g_ptr_array_free (ids, TRUE);

	if (card_count)
		pas_book_view_notify_add (view->book_view, cards);

	pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);

	g_list_foreach (cards, (GFunc)g_free, NULL);
	g_list_free (cards);
}

static void
pas_backend_exchange_search (PASBackendExchange *be,
			     PASBackendExchangeBookView *view,
			     gboolean completion_search)
{
	PASBackendExchangePrivate *bepriv = be->priv;

	if (completion_search && pas_backend_summary_is_summary_query (bepriv->summary, view->search)) {
		/* only do the summary query if it's a completion
		   search, since we lose all benefit of the summary if
		   we turn around and query the exchange server */
		do_summary_query (be, view);
	}
	else {
		E2kRestriction *rn;
		SearchClosure *closure;
		CORBA_Environment ev;

		closure = g_new (SearchClosure, 1);
		closure->be = be;
		closure->view = view;

		CORBA_exception_init(&ev);
		bonobo_object_dup_ref(bonobo_object_corba_objref(BONOBO_OBJECT(view->book_view)), &ev);
		CORBA_exception_free(&ev);

		pas_book_view_notify_status_message (view->book_view, _("Searching..."));

#ifdef DEBUG_TIMERS
		view->search_timer = g_timer_new ();
		g_timer_start (view->search_timer);
#endif
		/* execute the query */
		rn = pas_backend_exchange_build_restriction (view->search,
							     bepriv->base_rn);

		E2K_DEBUG_HINT ('A');
		e_folder_exchange_search (bepriv->folder,
					  field_names, n_field_names,
					  FALSE, rn, NULL,
					  search_cb, closure);
		e2k_restriction_unref (rn);
	}
}

static void
pas_backend_exchange_process_get_book_view (PASBackend            *backend,
					    PASBook               *book,
					    PASGetBookViewRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBookView       *book_view;
	PASBackendExchangeBookView *view;

	if (!be->priv->connected) {
		/* try to connect if we aren't connected, and return an error if it fails */
		pas_backend_exchange_connect (be);

		if (!be->priv->connected)
			pas_book_respond_get_book_view (book,
							GNOME_Evolution_Addressbook_BookListener_RepositoryOffline,
							NULL);
		return;
	}

	g_return_if_fail (req->listener != NULL);

	book_view = pas_book_view_new (req->listener);

	bonobo_object_ref(BONOBO_OBJECT(book));
	g_signal_connect (book_view, "destroy",
			  G_CALLBACK (view_destroy), book);

	view = g_new0 (PASBackendExchangeBookView, 1);
	view->book_view = book_view;
	view->search = g_strdup (req->search);
	view->card_sexp = pas_backend_card_sexp_new (view->search);
	view->bepriv = be->priv;
	view->contact_hash = g_hash_table_new (g_str_hash, g_str_equal);
	view->num_contacts = 0;
	view->change_id = NULL;
	view->change_context = NULL;	

	e_list_append (be->priv->book_views, view);

	pas_book_respond_get_book_view (book,
		(book_view != NULL
		 ? GNOME_Evolution_Addressbook_BookListener_Success 
		 : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		book_view);

	/* actually perform the query */
	pas_backend_exchange_search (be, view, FALSE);
}

static void
pas_backend_exchange_process_get_completion_view (PASBackend                  *backend,
						  PASBook                     *book,
						  PASGetCompletionViewRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBookView       *book_view;
	PASBackendExchangeBookView *view;

	/* we don't need to be connected for this to work, if we have a summary */
	if (!be->priv->summary) {

		pas_book_respond_get_book_view (book,
						GNOME_Evolution_Addressbook_BookListener_RepositoryOffline,
						NULL);
		return;
	}

	g_return_if_fail (req->listener != NULL);

	book_view = pas_book_view_new (req->listener);

	bonobo_object_ref(BONOBO_OBJECT(book));
	g_signal_connect (book_view, "destroy",
			  G_CALLBACK (view_destroy), book);

	view = g_new0 (PASBackendExchangeBookView, 1);
	view->book_view = book_view;
	view->search = g_strdup (req->search);
	view->card_sexp = pas_backend_card_sexp_new (view->search);
	view->bepriv = be->priv;
	view->contact_hash = g_hash_table_new (g_str_hash, g_str_equal);
	view->num_contacts = 0;
	view->change_id = NULL;
	view->change_context = NULL;	

	e_list_append (be->priv->book_views, view);

	pas_book_respond_get_completion_view (book,
		(book_view != NULL
		 ? GNOME_Evolution_Addressbook_BookListener_Success 
		 : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		book_view);

	/* actually perform the query */
	pas_backend_exchange_search (be, view, TRUE);
}

static void
pas_backend_exchange_changes_foreach_key (const char *key, gpointer user_data)
{
	PASBackendExchangeChangeContext *ctx = user_data;

	if (!g_hash_table_lookup (ctx->id_hash, key))
		ctx->del_ids = g_list_append (ctx->del_ids, g_strdup (key));
}

static void
pas_backend_exchange_changes (PASBackendExchange	*be,
			      PASBook         		*book,
			      const PASBackendExchangeBookView *cnstview)
{
	PASBackendExchangePrivate *bepriv = be->priv;
	PASBackendExchangeBookView *view = (PASBackendExchangeBookView *)cnstview;
	PASBackendExchangeChangeContext *ctx = cnstview->change_context;
	char    *filename, *dirname;
	EXmlHash *ehash;
	GList *i, *v;
	E2kResult *results;
	int count;
	int e2k_err;

	/* ensure the path to our change db's exists */
	dirname = g_strdup_printf ("%s/Addressbook", bepriv->account->storage_dir);
	e_mkdir_hier (dirname, S_IRWXU);
	filename = g_strdup_printf ("%s/%s.changes", dirname, view->change_id);
	ehash = e_xmlhash_new (filename);
	g_free (dirname);
	g_free (filename);

	E2K_DEBUG_HINT ('A');
	e2k_err = e_folder_exchange_search_sync (bepriv->folder,
						 field_names, n_field_names,
						 FALSE, bepriv->base_rn, NULL,
						 &results, &count);

	if (e2k_err != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("pas_backend_exchange_changes: error building list (err = %d)\n", e2k_err);
		e_xmlhash_destroy (ehash);
		pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);
		return;
	} else {
		int i;

		for (i = 0; i < count; i ++) {
			ECard *card;
			ECardSimple *simple = e_card_simple_from_props (bepriv->account, bepriv->conn, &results[i]);
			char *vcard_string;
			char *vcard;

			if (!simple)
				continue;

			vcard = e_card_simple_get_vcard (simple);
			card = e_card_new (vcard);
			g_free (vcard);
			g_object_unref (simple);

			/* Remove fields the user can't change
			 * and can change without the rest of the
			 * card changing 
			 */
			g_object_set ((card), "last_use", NULL, "use_score", 0.0, NULL);
			vcard_string = e_card_get_vcard_assume_utf8 (card);
			g_object_unref (card);

			g_hash_table_insert (ctx->id_hash, g_strdup (results[i].href), GINT_TO_POINTER(1));

			/* check what type of change has occurred, if any */
			switch (e_xmlhash_compare (ehash, results[i].href, vcard_string)) {
			case E_XMLHASH_STATUS_SAME:
				break;
			case E_XMLHASH_STATUS_NOT_FOUND:
				ctx->add_cards = g_list_append (ctx->add_cards, 
								vcard_string);
				ctx->add_ids = g_list_append (ctx->add_ids, g_strdup(results[i].href));
				break;
			case E_XMLHASH_STATUS_DIFFERENT:
				ctx->mod_cards = g_list_append (ctx->mod_cards, 
								vcard_string);
				ctx->mod_ids = g_list_append (ctx->mod_ids, g_strdup(results[i].href));
				break;
			}
		}
		e2k_results_free (results, count);
	}

	e_xmlhash_foreach_key (ehash, (EXmlHashFunc)pas_backend_exchange_changes_foreach_key, view->change_context);

	/* Send the changes */
	if (ctx->add_cards != NULL)
		pas_book_view_notify_add (view->book_view, ctx->add_cards);
		
	if (ctx->mod_cards != NULL)
		pas_book_view_notify_change (view->book_view, ctx->mod_cards);

	for (v = ctx->del_ids; v != NULL; v = v->next){
		char *id = v->data;
		pas_book_view_notify_remove_1 (view->book_view, id);
	}
		
	pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success);

	/* Update the hash */
	for (i = ctx->add_ids, v = ctx->add_cards; i != NULL; i = i->next, v = v->next){
		char *id = i->data;
		char *vcard = v->data;

		e_xmlhash_add (ehash, id, vcard);
		g_free (i->data);
		g_free (v->data);		
	}	
	for (i = ctx->mod_ids, v = ctx->mod_cards; i != NULL; i = i->next, v = v->next){
		char *id = i->data;
		char *vcard = v->data;

		e_xmlhash_add (ehash, id, vcard);
		g_free (i->data);
		g_free (v->data);		
	}	
	for (i = ctx->del_ids; i != NULL; i = i->next){
		char *id = i->data;

		e_xmlhash_remove (ehash, id);
		g_free (i->data);
	}

	e_xmlhash_write (ehash);
  	e_xmlhash_destroy (ehash);
}

static void
pas_backend_exchange_process_get_changes (PASBackend           *backend,
					  PASBook              *book,
					  PASGetChangesRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBookView       *book_view;
	PASBackendExchangeBookView *view;
	PASBackendExchangeChangeContext *ctx;

	g_return_if_fail (req->listener != NULL);

	bonobo_object_ref(BONOBO_OBJECT(book));

	book_view = pas_book_view_new (req->listener);

	g_signal_connect (book_view, "destroy",
			  G_CALLBACK (view_destroy), book);

	pas_book_respond_get_changes (book,
		   (book_view != NULL
		    ? GNOME_Evolution_Addressbook_BookListener_Success 
		    : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		   book_view);

	view = g_new0 (PASBackendExchangeBookView, 1);
	view->book_view = book_view;
	view->change_id = g_strdup (req->change_id);
	view->search = NULL;
	ctx = g_new0 (PASBackendExchangeChangeContext, 1);
	ctx->id_hash = g_hash_table_new (g_str_hash, g_str_equal);
	ctx->add_cards = NULL;
	ctx->add_ids = NULL;
	ctx->mod_cards = NULL;
	ctx->mod_ids = NULL;
	ctx->del_ids = NULL;

	view->change_context = ctx;
	
	e_list_append (be->priv->book_views, view);

	pas_backend_exchange_changes (be, book, view);
}

static void
pas_backend_exchange_process_check_connection (PASBackend                *backend,
					       PASBook                   *book,
					       PASCheckConnectionRequest *req)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);

	pas_book_report_connection (book, be->priv->connected);
}

static void
pas_backend_exchange_process_get_vcard (PASBackend         *backend,
					PASBook            *book,
					PASGetVCardRequest *req)
{
	PASBackendExchange *be;

	be = PAS_BACKEND_EXCHANGE (pas_book_get_backend (book));

	/* XXX finish this */
}


static void
pas_backend_exchange_process_authenticate_user (PASBackend                 *backend,
						PASBook                    *book,
						PASAuthenticateUserRequest *req)
{
	/* don't need anything here */
}

static void
pas_backend_exchange_process_get_supported_fields (PASBackend                   *backend,
						   PASBook                      *book,
						   PASGetSupportedFieldsRequest *req)

{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBackendExchangePrivate *bepriv = be->priv;

	if (bepriv->supported_fields == NULL) {
		int i;
		bepriv->supported_fields = e_list_new ((EListCopyFunc)g_strdup, (EListFreeFunc)g_free, NULL);

		for (i = 0; i < num_prop_mappings; i ++)
			if (prop_mappings[i].pas_field)
				e_list_append (bepriv->supported_fields, prop_mappings[i].pas_field);
	}

	pas_book_respond_get_supported_fields (book,
					       GNOME_Evolution_Addressbook_BookListener_Success,
					       bepriv->supported_fields);
}

static GNOME_Evolution_Addressbook_BookListener_CallStatus
pas_backend_exchange_load_uri (PASBackend             *backend,
			       const char             *uri)
{
	PASBackendExchange *be = PAS_BACKEND_EXCHANGE (backend);
	PASBackendExchangePrivate *bepriv = be->priv;

	g_assert (be->priv->connected == FALSE);

	bepriv->exchange_uri = g_strdup (uri);

	return pas_backend_exchange_connect (be);
}

/* Get_uri handler for the addressbook exchange backend */
static const char *
pas_backend_exchange_get_uri (PASBackend *backend)
{
	PASBackendExchange *be;

	be = PAS_BACKEND_EXCHANGE (backend);
	return be->priv->exchange_uri;
}

static char *
pas_backend_exchange_get_static_capabilites (PASBackend *backend)
{
	return g_strdup("net,do-initial-query,cache-completions");
}

static gboolean
pas_backend_exchange_construct (PASBackendExchange *backend)
{
	g_assert (backend != NULL);
	g_assert (PAS_IS_BACKEND_EXCHANGE (backend));

	if (! pas_backend_construct (PAS_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * pas_backend_exchange_new:
 */
PASBackend *
pas_backend_exchange_new (void)
{
	PASBackendExchange *backend;

	backend = g_object_new (pas_backend_exchange_get_type (), NULL);

	if (! pas_backend_exchange_construct (backend)) {
		g_object_unref (backend);

		return NULL;
	}

	return PAS_BACKEND (backend);
}

static void
pas_backend_exchange_dispose (GObject *object)
{
	PASBackendExchange *be;

	be = PAS_BACKEND_EXCHANGE (object);

	if (be->priv) {
		if (be->priv->folder) {
			e_folder_exchange_unsubscribe (be->priv->folder);
			g_object_unref (be->priv->folder);
		}

		if (be->priv->exchange_uri)
			g_free (be->priv->exchange_uri);

		if (be->priv->account)
			g_object_unref (be->priv->account);

		if (be->priv->book_views)
			g_object_unref (be->priv->book_views);

		g_free (be->priv);
		be->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);	
}

static void
pas_backend_exchange_class_init (PASBackendExchangeClass *klass)
{
	GObjectClass  *object_class = (GObjectClass *) klass;
	PASBackendClass *pas_backend_class;
	int i;

	parent_class = g_type_class_ref (pas_backend_get_type ());

	pas_backend_class = PAS_BACKEND_CLASS (klass);

	/* Static initialization */
	field_names_array = g_ptr_array_new ();
	g_ptr_array_add (field_names_array, E2K_PR_DAV_UID);
	g_ptr_array_add (field_names_array, E2K_PR_DAV_LAST_MODIFIED);
	g_ptr_array_add (field_names_array, E2K_PR_DAV_CREATION_DATE);
	g_ptr_array_add (field_names_array, E2K_PR_MAPI_EMAIL_ADDRTYPE);
	g_ptr_array_add (field_names_array, E2K_PR_MAPI_EMAIL_2_ADDRTYPE);
	g_ptr_array_add (field_names_array, E2K_PR_MAPI_EMAIL_3_ADDRTYPE);
	for (i = 0; i < num_prop_mappings; i ++)
		g_ptr_array_add (field_names_array, prop_mappings[i].prop_name);
	field_names = (const char **)field_names_array->pdata;
	n_field_names = field_names_array->len;

	/* Set the virtual methods. */
	pas_backend_class->load_uri                = pas_backend_exchange_load_uri;
	pas_backend_class->get_uri                 = pas_backend_exchange_get_uri;
	pas_backend_class->get_static_capabilities = pas_backend_exchange_get_static_capabilites;


	pas_backend_class->create_card             = pas_backend_exchange_process_create_card;
	pas_backend_class->remove_cards            = pas_backend_exchange_process_remove_cards;
	pas_backend_class->modify_card             = pas_backend_exchange_process_modify_card;
	pas_backend_class->check_connection        = pas_backend_exchange_process_check_connection;
	pas_backend_class->get_vcard               = pas_backend_exchange_process_get_vcard;
	pas_backend_class->get_cursor              = pas_backend_exchange_process_get_cursor;
	pas_backend_class->get_book_view           = pas_backend_exchange_process_get_book_view;
	pas_backend_class->get_completion_view     = pas_backend_exchange_process_get_completion_view;
	pas_backend_class->get_changes             = pas_backend_exchange_process_get_changes;
	pas_backend_class->authenticate_user       = pas_backend_exchange_process_authenticate_user;
	pas_backend_class->get_supported_fields    = pas_backend_exchange_process_get_supported_fields;

	object_class->dispose = pas_backend_exchange_dispose;
}

static void
pas_backend_exchange_init (PASBackendExchange *backend)
{
	PASBackendExchangePrivate *priv;

	priv            = g_new0 (PASBackendExchangePrivate, 1);

	backend->priv = priv;
	priv->book_views = e_list_new (NULL, (EListFreeFunc) exchange_book_view_free, NULL);
}

E2K_MAKE_TYPE (pas_backend_exchange, PASBackendExchange, pas_backend_exchange_class_init, pas_backend_exchange_init, PARENT_TYPE)

