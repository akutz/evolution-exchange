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

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "e-book-backend-gal"
#undef DEBUG

#ifdef HAVE_CONFIG_H
#include <config.h>  
#endif

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <e2k-utils.h>
#include <e2k-global-catalog-ldap.h>
#include <exchange-account.h>
#include "exchange-component.h"

#ifdef DEBUG
#define LDAP_DEBUG
#define LDAP_DEBUG_ADD
#endif
#include <ldap.h>
#ifdef DEBUG
#undef LDAP_DEBUG
#endif

#define d(x)

#include <sys/time.h>

#include <libedataserver/e-sexp.h>
#include <libebook/e-contact.h>

#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-cache.h>
#include "e-book-backend-gal.h"

/* interval for our poll_ldap timeout */
#define LDAP_POLL_INTERVAL 20

/* timeout for ldap_result */
#define LDAP_RESULT_TIMEOUT_MILLIS 10

#define TV_TO_MILLIS(timeval) ((timeval).tv_sec * 1000 + (timeval).tv_usec / 1000)

static gchar *query_prop_to_ldap(gchar *query_prop);
static int build_query (EBookBackendGAL *bl, const char *query, char **ldap_query);

#define PARENT_TYPE E_TYPE_BOOK_BACKEND
static EBookBackendClass *parent_class;

typedef struct LDAPOp LDAPOp;

static GList *supported_fields;
static char **search_attrs;

struct _EBookBackendGALPrivate {
	char             *gal_uri;
	gboolean          connected;

	E2kGlobalCatalog *gc;
	LDAP             *ldap;
	ExchangeAccount  *account;

	EBookBackendCache *cache;
	gboolean marked_for_offline;
	GMutex		*ldap_lock;

	/* our operations */
	GStaticRecMutex op_hash_mutex;
	GHashTable *id_to_op;
	int active_ops;
	int mode;
	int poll_timeout;
};

typedef void (*LDAPOpHandler)(LDAPOp *op, LDAPMessage *res);
typedef void (*LDAPOpDtor)(LDAPOp *op);

struct LDAPOp {
	LDAPOpHandler  handler;
	LDAPOpDtor     dtor;
	EBookBackend  *backend;
	EDataBook     *book;
	EDataBookView *view;
	guint32        opid; /* the libebook operation id */
	int            id;   /* the ldap msg id */
};

static void     ldap_op_add (LDAPOp *op, EBookBackend *backend, EDataBook *book,
			     EDataBookView *view, int opid, int msgid, LDAPOpHandler handler, LDAPOpDtor dtor);
static void     ldap_op_finished (LDAPOp *op);

static gboolean poll_ldap (EBookBackendGAL *bl);

static EContact *build_contact_from_entry (EBookBackendGAL *bl, LDAPMessage *e, GList **existing_objectclasses);

static void manager_populate (EContact *contact, char **values, EBookBackendGAL *bl, E2kOperation *op);

static void last_mod_time_populate (EContact *contact, char **values, EBookBackendGAL *bl, E2kOperation *op);

struct prop_info {
	EContactField field_id;
	char *ldap_attr;
#define PROP_TYPE_STRING   0x01
#define PROP_TYPE_COMPLEX  0x02
	int prop_type;

	/* the remaining items are only used for the TYPE_COMPLEX props */

	/* used when reading from the ldap server populates EContact with the values in **values. */
	void (*populate_contact_func)(EContact *contact, char **values, EBookBackendGAL *bl, E2kOperation *op);

} prop_info[] = {

#define COMPLEX_PROP(fid,a,ctor) {fid, a, PROP_TYPE_COMPLEX, ctor}
#define STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING}


	/* name fields */
	STRING_PROP   (E_CONTACT_FULL_NAME,   "displayName" ),
	STRING_PROP   (E_CONTACT_GIVEN_NAME,  "givenName" ),
	STRING_PROP   (E_CONTACT_FAMILY_NAME, "sn" ),
	STRING_PROP   (E_CONTACT_NICKNAME,    "mailNickname" ),

	/* email addresses */
	STRING_PROP   (E_CONTACT_EMAIL_1,     "mail" ),

	/* phone numbers */
	STRING_PROP   (E_CONTACT_PHONE_BUSINESS,     "telephoneNumber"),
	STRING_PROP   (E_CONTACT_PHONE_BUSINESS_2,   "otherTelephone"),
	STRING_PROP   (E_CONTACT_PHONE_HOME,         "homePhone"),
	STRING_PROP   (E_CONTACT_PHONE_HOME_2,       "otherHomePhone"),
	STRING_PROP   (E_CONTACT_PHONE_MOBILE,       "mobile"),
	STRING_PROP   (E_CONTACT_PHONE_BUSINESS_FAX, "facsimileTelephoneNumber"), 
	STRING_PROP   (E_CONTACT_PHONE_OTHER_FAX,    "otherFacsimileTelephoneNumber"), 
	STRING_PROP   (E_CONTACT_PHONE_PAGER,        "pager"),

	/* org information */
	STRING_PROP   (E_CONTACT_ORG,                "company"),
	STRING_PROP   (E_CONTACT_ORG_UNIT,           "department"),
	STRING_PROP   (E_CONTACT_OFFICE,             "physicalDeliveryOfficeName"),
	STRING_PROP   (E_CONTACT_TITLE,              "title"),

	COMPLEX_PROP  (E_CONTACT_MANAGER,            "manager", manager_populate),

	/* FIXME: we should aggregate streetAddress, l, st, c, postalCode
	 * into business_address
	 */

	/* misc fields */
	STRING_PROP   (E_CONTACT_HOMEPAGE_URL,       "wWWHomePage"),
	STRING_PROP   (E_CONTACT_FREEBUSY_URL,       "msExchFBURL"),
	STRING_PROP   (E_CONTACT_NOTE,               "info"), 
	STRING_PROP   (E_CONTACT_FILE_AS,            "fileAs"),

	/* whenChanged is a string value, but since we need to re-format it,
	 * defining it as a complex property
	 */
	COMPLEX_PROP   (E_CONTACT_REV,                "whenChanged", last_mod_time_populate),

#undef STRING_PROP
#undef COMPLEX_PROP
};

static int num_prop_infos = sizeof(prop_info) / sizeof(prop_info[0]);

static void
book_view_notify_status (EDataBookView *view, const char *status)
{
	if (!view)
		return;
	e_data_book_view_notify_status_message (view, status);
}

static EDataBookView*
find_book_view (EBookBackendGAL *bl)
{
	EList *views = e_book_backend_get_book_views (E_BOOK_BACKEND (bl));
	EIterator *iter = e_list_get_iterator (views);
	EDataBookView *rv = NULL;

	if (e_iterator_is_valid (iter)) {
		/* just always use the first book view */
		EDataBookView *v = (EDataBookView*)e_iterator_get(iter);
		if (v)
			rv = v;
	}

	g_object_unref (iter);
	g_object_unref (views);

	return rv;
}

static GNOME_Evolution_Addressbook_CallStatus
gal_connect (EBookBackendGAL *bl)
{
	EBookBackendGALPrivate *blpriv = bl->priv;

#ifdef DEBUG
	{
		int debug_level = 1;
		ldap_set_option (NULL, LDAP_OPT_DEBUG_LEVEL, &debug_level);
	}
#endif

	blpriv->gc = NULL;
	blpriv->connected = FALSE;

	blpriv->account = exchange_component_get_account_for_uri (global_exchange_component, blpriv->gal_uri);
	if (!blpriv->account)
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	blpriv->gc = exchange_account_get_global_catalog (blpriv->account);
	if (!blpriv->gc)
		return GNOME_Evolution_Addressbook_RepositoryOffline;

	g_object_ref (blpriv->gc);
	g_mutex_lock (blpriv->ldap_lock);
	blpriv->ldap = e2k_global_catalog_get_ldap (blpriv->gc, NULL);
	if (!blpriv->ldap) {
		g_mutex_unlock (blpriv->ldap_lock);
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	}
	g_mutex_unlock (blpriv->ldap_lock);

	blpriv->connected = TRUE;
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (bl), TRUE);
	return GNOME_Evolution_Addressbook_Success;
}

static gboolean
gal_reconnect (EBookBackendGAL *bl, EDataBookView *book_view, int ldap_status)
{
	g_mutex_lock (bl->priv->ldap_lock);
	if (!bl->priv->ldap) {
		g_mutex_unlock (bl->priv->ldap_lock);
		return FALSE;
	}
	g_mutex_unlock (bl->priv->ldap_lock);

	/* we need to reconnect if we were previously connected */
	if (bl->priv->connected && ldap_status == LDAP_SERVER_DOWN) {
		g_mutex_lock (bl->priv->ldap_lock);
		if (book_view)
			book_view_notify_status (book_view, _("Reconnecting to LDAP server..."));
		if (bl->priv->ldap)
			ldap_unbind (bl->priv->ldap);
		bl->priv->ldap = e2k_global_catalog_get_ldap (bl->priv->gc, NULL);
		if (book_view)
			book_view_notify_status (book_view, "");

		if (bl->priv->ldap != NULL) {
			g_mutex_unlock (bl->priv->ldap_lock);
			return TRUE;
		} else {
			g_mutex_unlock (bl->priv->ldap_lock);
			return FALSE;
		}
	}
	else {
		return FALSE;
	}
}

static void
ldap_op_add (LDAPOp *op, EBookBackend *backend,
	     EDataBook *book, EDataBookView *view,
	     int opid,
	     int msgid,
	     LDAPOpHandler handler, LDAPOpDtor dtor)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);

	op->backend = backend;
	op->book = book;
	op->view = view;
	op->opid = opid;
	op->id = msgid;
	op->handler = handler;
	op->dtor = dtor;

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	if (g_hash_table_lookup (bl->priv->id_to_op, &op->id)) {
		g_warning ("conflicting ldap msgid's");
	}

	g_hash_table_insert (bl->priv->id_to_op,
			     &op->id, op);

	bl->priv->active_ops ++;

	if (bl->priv->poll_timeout == -1)
		bl->priv->poll_timeout = g_timeout_add (LDAP_POLL_INTERVAL,
							(GSourceFunc) poll_ldap,
							bl);

	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
}

static void
ldap_op_finished (LDAPOp *op)
{
	EBookBackend *backend = op->backend;
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_remove (bl->priv->id_to_op, &op->id);

	/* should handle errors here */
	g_mutex_lock (bl->priv->ldap_lock);
	if (bl->priv->ldap)
		ldap_abandon (bl->priv->ldap, op->id);
	g_mutex_unlock (bl->priv->ldap_lock);

	op->dtor (op);

	bl->priv->active_ops--;

	if (bl->priv->active_ops == 0) {
		if (bl->priv->poll_timeout != -1)
			g_source_remove (bl->priv->poll_timeout);
		bl->priv->poll_timeout = -1;
	}
	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
}

static int
ldap_error_to_response (int ldap_error)
{
	if (ldap_error == LDAP_SUCCESS)
		return GNOME_Evolution_Addressbook_Success;
	else if (LDAP_NAME_ERROR (ldap_error))
		return GNOME_Evolution_Addressbook_ContactNotFound;
	else if (ldap_error == LDAP_INSUFFICIENT_ACCESS)
		return GNOME_Evolution_Addressbook_PermissionDenied;
	else if (ldap_error == LDAP_SERVER_DOWN)
		return GNOME_Evolution_Addressbook_RepositoryOffline;
	else if (ldap_error == LDAP_ALREADY_EXISTS)
		return GNOME_Evolution_Addressbook_ContactIdAlreadyExists;
	else
		return GNOME_Evolution_Addressbook_OtherError;
}



static void
create_contact (EBookBackend *backend,
		EDataBook    *book,
		guint32       opid,
		const char   *vcard)
{
	e_data_book_respond_create (book, opid,
				    GNOME_Evolution_Addressbook_PermissionDenied,
				    NULL);
}

static void
remove_contacts (EBookBackend *backend,
		 EDataBook    *book,
		 guint32       opid,
		 GList        *ids)
{
	e_data_book_respond_remove_contacts (book, opid,
					     GNOME_Evolution_Addressbook_PermissionDenied,
					     NULL);
}

static void
modify_contact (EBookBackend *backend,
		EDataBook    *book,
		guint32       opid,
		const char   *vcard)
{
	e_data_book_respond_modify (book, opid,
				    GNOME_Evolution_Addressbook_PermissionDenied,
				    NULL);
}

typedef struct {
	LDAPOp op;
} LDAPGetContactOp;

static void
get_contact_handler (LDAPOp *op, LDAPMessage *res)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (op->backend);
	int msg_type;

	g_mutex_lock (bl->priv->ldap_lock);
	if (!bl->priv->ldap) {
		g_mutex_unlock (bl->priv->ldap_lock);
		e_data_book_respond_get_contact (op->book, op->opid, 
					GNOME_Evolution_Addressbook_OtherError, "");
		ldap_op_finished (op);
		return;
	}
	g_mutex_unlock (bl->priv->ldap_lock);

	/* the msg_type will be either SEARCH_ENTRY (if we're
	   successful) or SEARCH_RESULT (if we're not), so we finish
	   the op after either */
	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_mutex_lock (bl->priv->ldap_lock);
		LDAPMessage *e = ldap_first_entry(bl->priv->ldap, res);
		g_mutex_unlock (bl->priv->ldap_lock);

		EContact *contact;
		char *vcard;

		if (!e) {
			g_warning ("uh, this shouldn't happen");
			e_data_book_respond_get_contact (op->book,
							 op->opid,
							 GNOME_Evolution_Addressbook_OtherError,
							 "");
			ldap_op_finished (op);
			return;
		}

		contact = build_contact_from_entry (bl, e, NULL);
		vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
		e_data_book_respond_get_contact (op->book,
						 op->opid,
						 GNOME_Evolution_Addressbook_Success,
						 vcard);
		g_free (vcard);
		g_object_unref (contact);
		ldap_op_finished (op);
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;
	
		g_mutex_lock (bl->priv->ldap_lock);
		ldap_parse_result (bl->priv->ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		g_mutex_unlock (bl->priv->ldap_lock);

		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("get_contact_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		e_data_book_respond_get_contact (op->book,
						 op->opid,
						 ldap_error_to_response (ldap_error),
						 "");
		ldap_op_finished (op);
	}
	else {
		g_warning ("unhandled result type %d returned", msg_type);
		e_data_book_respond_get_contact (op->book,
						 op->opid,
						 GNOME_Evolution_Addressbook_OtherError,
						 "");
		ldap_op_finished (op);
	}

}

static void
get_contact_dtor (LDAPOp *op)
{
	LDAPGetContactOp *get_contact_op = (LDAPGetContactOp*)op;

	g_free (get_contact_op);
}

static void
get_contact (EBookBackend *backend,
	     EDataBook    *book,
	     guint32       opid,
	     const char   *id)
{
	LDAPGetContactOp *get_contact_op;
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);
	LDAP *ldap = bl->priv->ldap;
	int get_contact_msgid;
	EDataBookView *book_view;
	int ldap_error;

	switch (bl->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			EContact *contact = e_book_backend_cache_get_contact (bl->priv->cache, id);
			gchar *vcard_str;

			if (!contact) {
				e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_OtherError, "");
				return;
			}

			vcard_str = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

			e_data_book_respond_get_contact (book,
							 opid,
							 GNOME_Evolution_Addressbook_Success,
							 vcard_str);
			g_free (vcard_str);
			g_object_unref (contact);
			return;
		}

		e_data_book_respond_get_contact(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, "");
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE : 	
		g_mutex_lock (bl->priv->ldap_lock);
		if (!ldap) {
			g_mutex_unlock (bl->priv->ldap_lock);
			e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_OtherError, "");
			return;
		}
		g_mutex_unlock (bl->priv->ldap_lock);

		get_contact_op = g_new0 (LDAPGetContactOp, 1);

		book_view = find_book_view (bl);

		do {	
			g_mutex_lock (bl->priv->ldap_lock);
			ldap_error = ldap_search_ext (ldap, id,
						      LDAP_SCOPE_BASE,
						      "(objectclass=*)",
						      search_attrs, 0, NULL, NULL,
						      NULL, /* XXX timeout */
						      1, &get_contact_msgid);
			g_mutex_unlock (bl->priv->ldap_lock);
		} while (gal_reconnect (bl, book_view, ldap_error));

		if (ldap_error == LDAP_SUCCESS) {
			ldap_op_add ((LDAPOp*)get_contact_op, backend, book,
				     book_view, opid, get_contact_msgid,
				     get_contact_handler, get_contact_dtor);
		}
		else {
			e_data_book_respond_get_contact (book,
							 opid,
							 ldap_error_to_response (ldap_error),
							 "");
			get_contact_dtor ((LDAPOp*)get_contact_op);
		}
	}
}

typedef struct {
	LDAPOp op;
	GList *contacts;
} LDAPGetContactListOp;

static void
contact_list_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp*)op;
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (op->backend);
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *e;
	int msg_type;

	g_mutex_lock (bl->priv->ldap_lock);
	if (!ldap) {
		g_mutex_unlock (bl->priv->ldap_lock);
		e_data_book_respond_get_contact_list (op->book, op->opid, GNOME_Evolution_Addressbook_OtherError, NULL);
		ldap_op_finished (op);
		return;
	}
	g_mutex_unlock (bl->priv->ldap_lock);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_mutex_lock (bl->priv->ldap_lock);	
		e = ldap_first_entry(ldap, res);
		g_mutex_unlock (bl->priv->ldap_lock);

		while (NULL != e) {
			EContact *contact = build_contact_from_entry (bl, e, NULL);
			char *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);

			d(printf ("vcard = %s\n", vcard));
 
			contact_list_op->contacts = g_list_append (contact_list_op->contacts,
								   vcard);

			g_free (vcard);
			g_object_unref (contact);
			g_mutex_lock (bl->priv->ldap_lock);
			e = ldap_next_entry(ldap, e);
			g_mutex_unlock (bl->priv->ldap_lock);
		}
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;

		g_mutex_lock (bl->priv->ldap_lock);
		ldap_parse_result (ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		g_mutex_unlock (bl->priv->ldap_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("contact_list_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		d(printf ("search returned %d\n", ldap_error));

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_SearchTimeLimitExceeded,
							      contact_list_op->contacts);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_SearchSizeLimitExceeded,
							      contact_list_op->contacts);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_Success,
							      contact_list_op->contacts);
		else
			e_data_book_respond_get_contact_list (op->book,
							      op->opid,
							      GNOME_Evolution_Addressbook_OtherError,
							      contact_list_op->contacts);

		ldap_op_finished (op);
	}
	else {
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_respond_get_contact_list (op->book,
						      op->opid,
						      GNOME_Evolution_Addressbook_OtherError,
						      NULL);
		ldap_op_finished (op);
	}

}

static void
contact_list_dtor (LDAPOp *op)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp*)op;

	g_free (contact_list_op);
}


static void
get_contact_list (EBookBackend *backend,
		  EDataBook    *book,
		  guint32       opid,
		  const char   *query)
{
	LDAPGetContactListOp *contact_list_op;
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);
	LDAP *ldap = bl->priv->ldap;
	GNOME_Evolution_Addressbook_CallStatus status;
	int contact_list_msgid;
	EDataBookView *book_view;
	int ldap_error;
	char *ldap_query;

	switch (bl->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			GList *contacts;
			GList *vcard_strings = NULL;
			GList *l;

			contacts = e_book_backend_cache_get_contacts (bl->priv->cache, query);

			for (l = contacts; l; l = g_list_next (l)) {
				EContact *contact = l->data;
				vcard_strings = g_list_prepend (vcard_strings, e_vcard_to_string (E_VCARD (contact),
								EVC_FORMAT_VCARD_30));
				g_object_unref (contact);
			}

			g_list_free (contacts);
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success, vcard_strings);
			return;
		}
		
		e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_RepositoryOffline,
						      NULL);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		g_mutex_lock (bl->priv->ldap_lock);
		if (!ldap) {
			g_mutex_unlock (bl->priv->ldap_lock);
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}
		g_mutex_unlock (bl->priv->ldap_lock);

		contact_list_op = g_new0 (LDAPGetContactListOp, 1);
		book_view = find_book_view (bl);

		status = build_query (bl, query, &ldap_query);
		if (status != GNOME_Evolution_Addressbook_Success || !ldap_query) {
			e_data_book_respond_get_contact_list (book, opid, status, NULL);
			return;
		}

		d(printf ("getting contact list with filter: %s\n", ldap_query));

		do {	
			g_mutex_lock (bl->priv->ldap_lock);
			ldap_error = ldap_search_ext (bl->priv->ldap, LDAP_ROOT_DSE,
						      LDAP_SCOPE_SUBTREE,
						      ldap_query,
						      search_attrs, 0, NULL, NULL,
						      NULL, /* XXX timeout */
						      LDAP_NO_LIMIT, &contact_list_msgid);
			g_mutex_unlock (bl->priv->ldap_lock);
		} while (gal_reconnect (bl, book_view, ldap_error));

		g_free (ldap_query);

		if (ldap_error == LDAP_SUCCESS) {
			ldap_op_add ((LDAPOp*)contact_list_op, backend, book,
				     book_view, opid, contact_list_msgid,
				     contact_list_handler, contact_list_dtor);
		}
		else {
			e_data_book_respond_get_contact_list (book,
							      opid,
							      ldap_error_to_response (ldap_error),
							      NULL);
			contact_list_dtor ((LDAPOp*)contact_list_op);
		}
	}
}



#define IS_RFC2254_CHAR(c) ((c) == '*' || (c) =='\\' || (c) == '(' || (c) == ')' || (c) == '\0')
static char *
rfc2254_escape(char *str)
{
	int i;
	int len = strlen(str);
	int newlen = 0;

	for (i = 0; i < len; i ++) {
		if (IS_RFC2254_CHAR(str[i]))
			newlen += 3;
		else
			newlen ++;
	}

	if (len == newlen) {
		return g_strdup (str);
	}
	else {
		char *newstr = g_malloc0 (newlen + 1);
		int j = 0;
		for (i = 0; i < len; i ++) {
			if (IS_RFC2254_CHAR(str[i])) {
				sprintf (newstr + j, "\\%02x", str[i]);
				j+= 3;
			}
			else {
				newstr[j++] = str[i];
			}
		}
		return newstr;
	}
}

static ESExpResult *
func_and(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	GString *string;
	int i;

	/* Check for short circuit */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_BOOL &&
		    argv[i]->value.bool == FALSE) {
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = FALSE;
			return r;
		} else if (argv[i]->type == ESEXP_RES_UNDEFINED)
			return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

	string = g_string_new("(&");
	for (i = 0; i < argc; i ++) {
		if (argv[i]->type != ESEXP_RES_STRING)
			continue;
		g_string_append(string, argv[i]->value.string);
	}
	g_string_append(string, ")");

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = string->str;
	g_string_free(string, FALSE);

	return r;
}

static ESExpResult *
func_or(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	GString *string;
	int i;

	/* Check for short circuit */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_BOOL &&
		    argv[i]->value.bool == TRUE) {
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = TRUE;
			return r;
		} else if (argv[i]->type == ESEXP_RES_UNDEFINED)
			return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

	string = g_string_new("(|");
	for (i = 0; i < argc; i ++) {
		if (argv[i]->type != ESEXP_RES_STRING)
			continue;
		g_string_append(string, argv[i]->value.string);
	}
	g_string_append(string, ")");

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = string->str;
	g_string_free(string, FALSE);

	return r;
}

static ESExpResult *
func_not(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;

	if (argc != 1 ||
	    (argv[0]->type != ESEXP_RES_STRING &&
	     argv[0]->type != ESEXP_RES_BOOL))
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);

	if (argv[0]->type == ESEXP_RES_STRING) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup_printf ("(!%s)",
						   argv[0]->value.string);
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = !argv[0]->value.bool;
	}

	return r;
}

static gchar *
query_prop_to_ldap(gchar *query_prop)
{
	int i;

	if (!strcmp (query_prop, "email"))
		query_prop = "email_1";

	for (i = 0; i < num_prop_infos; i ++)
		if (!strcmp (query_prop, e_contact_field_name (prop_info[i].field_id)))
			return prop_info[i].ldap_attr;

	return NULL;
}


static ESExpResult *
func_contains(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	char *propname, *ldap_attr, *str;

	if (argc != 2 ||
	    argv[0]->type != ESEXP_RES_STRING ||
	    argv[1]->type != ESEXP_RES_STRING)
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!strcmp(propname, "x-evolution-any-field")) {
		/* This gui does (contains "x-evolution-any-field" ""),
		 * when you hit "Clear". We want that to be empty. But
		 * other "any field contains" searches should give an
		 * error.
		 */
		if (strlen(str) == 0) {
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = FALSE;
		} else
			r = e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
		return r;
	}

	ldap_attr = query_prop_to_ldap(argv[0]->value.string);
	if (!ldap_attr) {
		/* Attribute doesn't exist, so it can't possibly match */
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
		return r;
	}

	/* AD doesn't do substring indexes, so we only allow
	 * (contains FIELD ""), meaning "FIELD exists".
	 */
	if (strlen(str) == 0) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup_printf ("(%s=*)", ldap_attr);
	} else if (!strcmp(propname, "file_as")) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup_printf ("(|(displayName=%s*)(sn=%s*)(%s=%s*))", str, str, ldap_attr, str);
	} else 
		r = e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	return r;
}

static ESExpResult *
func_is_or_begins_with(ESExp *f, int argc, ESExpResult **argv, gboolean exact)
{
	ESExpResult *r;
	char *propname, *str, *ldap_attr, *star, *filter;

	if (argc != 2
	    || argv[0]->type != ESEXP_RES_STRING
	    || argv[1]->type != ESEXP_RES_STRING)
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);

	propname = argv[0]->value.string;
	str = rfc2254_escape(argv[1]->value.string);
	star = exact ? "" : "*";

	if (!exact && strlen (str) == 0 && strcmp(propname, "file_as")) {
		/* Can't do (beginswith FIELD "") */
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

	/* We use the query "(beginswith fileas "")" while building cache for
	 * GAL offline, where we try to retrive all the contacts and store it 
	 * locally. Retrieving *all* the contacts may not be possible in case 
	 * of large number of contacts and huge data, (for the same reason
	 * we don't support empty queries in GAL when online.) In such cases 
	 * cache may not be complete.
	 */
	if (!strcmp(propname, "file_as")) {
		filter = g_strdup_printf("(displayName=%s%s)", str, star);
		goto done;
	}

	ldap_attr = query_prop_to_ldap(propname);
	if (!ldap_attr) {
		g_free (str);

		/* Property doesn't exist, so it can't ever match */
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
		return r;
	}

	if (!strcmp (propname, "full_name")) {
		char *first, *last, *space;

		space = strchr (str, ' ');
		if (space && space > str) {
			if (*(space - 1) == ',') {
				first = g_strdup (space + 1);
				last = g_strndup (str, space - str - 1);
			} else {
				first = g_strndup (str, space - str);
				last = g_strdup (space + 1);
			}
			filter = g_strdup_printf("(|(displayName=%s%s)(sn=%s%s)(givenName=%s%s)(&(givenName=%s%s)(sn=%s%s)))",
						 str, star, str, star,
						 str, star, first, star,
						 last, star);
			g_free (first);
			g_free (last);
		} else {
			filter = g_strdup_printf("(|(displayName=%s%s)(sn=%s%s)(givenName=%s%s)(mailNickname=%s%s))",
						 str, star, str, star,
						 str, star, str, star);
		}
	} else 
		filter = g_strdup_printf("(%s=%s%s)", ldap_attr, str, star);

 done:
	g_free (str);

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = filter;
	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	return func_is_or_begins_with(f, argc, argv, TRUE);
}

static ESExpResult *
func_beginswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	return func_is_or_begins_with(f, argc, argv, FALSE);
}

static ESExpResult *
func_endswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	/* We don't allow endswith searches */
	return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
}

/* 'builtin' functions */
static struct {
	char *name;
	ESExpFunc *func;
} symbols[] = {
	{ "and", func_and },
	{ "or", func_or },
	{ "not", func_not },
	{ "contains", func_contains },
	{ "is", func_is },
	{ "beginswith", func_beginswith },
	{ "endswith", func_endswith },
};

static int
build_query (EBookBackendGAL *bl, const char *query, char **ldap_query)
{
	ESExp *sexp;
	ESExpResult *r;
	int i, retval;

	sexp = e_sexp_new();

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		e_sexp_add_function(sexp, 0, symbols[i].name,
				    symbols[i].func, NULL);
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);

	if (r->type == ESEXP_RES_STRING) {
		if (!strcmp (r->value.string, "(mail=*)")) {
			/* If the query is empty, 
			 * don't search for all the contats 
			 */ 
			*ldap_query = NULL;
			retval = GNOME_Evolution_Addressbook_QueryRefused;
		}
		else {
			*ldap_query = g_strdup_printf ("(&(mail=*)(!(msExchHideFromAddressLists=TRUE))%s)", r->value.string);
			retval = GNOME_Evolution_Addressbook_Success;
		}
	} else if (r->type == ESEXP_RES_BOOL) {
		/* If it's FALSE, that means "no matches". If it's TRUE
		 * that means "everything matches", but we don't support
		 * that, so it also means "no matches".
		 */
		*ldap_query = NULL;
		retval = GNOME_Evolution_Addressbook_Success;
	} else {
		/* Bad query */
		*ldap_query = NULL;
		retval = GNOME_Evolution_Addressbook_QueryRefused;
	}

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	return retval;
}

static void
manager_populate(EContact *contact, char **values, EBookBackendGAL *bl, E2kOperation *op)
{
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;

	status = e2k_global_catalog_lookup (bl->priv->gc,
					    op,
					    E2K_GLOBAL_CATALOG_LOOKUP_BY_DN,
					    values[0], 0, &entry);
	if (status != E2K_GLOBAL_CATALOG_OK)
		return;

	e_contact_set (contact, E_CONTACT_MANAGER,
		       entry->display_name);
	e2k_global_catalog_entry_free (bl->priv->gc, entry);
}

#define G_STRNDUP(str, len) g_strndup(str, len); \
				str += len;

static char *
get_time_stamp (char *serv_time_str)
{
	char *input_str = serv_time_str, *result_str = NULL;
	char *year, *month, *date, *hour, *minute, *second, *zone;

	/* input time string will be of the format 20050419162256.0Z
	 * out put string shd be of the format 2005-04-19T16:22:56.0Z
	 * ("%04d-%02d-%02dT%02d:%02d:%02dZ") 
	 */

	/* FIXME : Add a check for proper input string */
	year = G_STRNDUP(input_str, 4)
	month = G_STRNDUP(input_str, 2)
	date = G_STRNDUP(input_str, 2)
	hour = G_STRNDUP(input_str, 2)
	minute = G_STRNDUP(input_str, 2)
	second = G_STRNDUP(input_str, 2)
	input_str ++; // parse over the dot
	zone = G_STRNDUP(input_str, 1)

	result_str = g_strdup_printf ("%s-%s-%sT%s:%s:%s.%sZ", 
		year, month, date, hour, minute, second, zone);

	printf ("rev time : %s\n", result_str);

	g_free (year);
	g_free (month);
	g_free (date);
	g_free (hour);
	g_free (minute);
	g_free (second);
	g_free (zone);

	return result_str;
}

static void
last_mod_time_populate (EContact *contact, char **values,
			EBookBackendGAL *bl, E2kOperation *op)
{
	char *time_str;

	/* FIXME: Some better way to do this  */
	time_str = get_time_stamp (values[0]);
	if (time_str)
		e_contact_set (contact, E_CONTACT_REV, time_str);
	g_free (time_str);
}


typedef struct {
	LDAPOp op;
	EDataBookView *view;

	/* used to detect problems with start/stop_book_view racing */
	gboolean aborted;
	/* used by search_handler to only send the status messages once */
	gboolean notified_receiving_results;
} LDAPSearchOp;

static EContact *
build_contact_from_entry (EBookBackendGAL *bl, LDAPMessage *e, GList **existing_objectclasses)
{
	LDAP *ldap = bl->priv->ldap;
	EContact *contact = e_contact_new ();
	char *dn;
	char *attr;
	BerElement *ber = NULL;

	g_mutex_lock (bl->priv->ldap_lock);
	dn = ldap_get_dn(ldap, e);
	g_mutex_unlock (bl->priv->ldap_lock);

	e_contact_set (contact, E_CONTACT_UID, dn);
	ldap_memfree (dn);

	g_mutex_lock (bl->priv->ldap_lock);
	attr = ldap_first_attribute (ldap, e, &ber);
	g_mutex_unlock (bl->priv->ldap_lock);

	while (attr) {
		int i;
		struct prop_info *info = NULL;
		char **values;

		if (existing_objectclasses && !g_ascii_strcasecmp (attr, "objectclass")) {
			g_mutex_lock (bl->priv->ldap_lock);
			values = ldap_get_values (ldap, e, attr);
			g_mutex_unlock (bl->priv->ldap_lock);
			for (i = 0; values[i]; i ++)
				*existing_objectclasses = g_list_append (*existing_objectclasses, g_strdup (values[i]));

			ldap_value_free (values);
		}
		else {
			for (i = 0; i < num_prop_infos; i ++)
				if (!g_ascii_strcasecmp (attr, prop_info[i].ldap_attr)) {
					info = &prop_info[i];
					break;
				}

			d(printf ("attr = %s, ", attr));
			d(printf ("info = %p\n", info));

			if (info) {
				if (1) {
					g_mutex_lock (bl->priv->ldap_lock);
					values = ldap_get_values (ldap, e, attr);
					g_mutex_unlock (bl->priv->ldap_lock);

					if (values) {
						if (info->prop_type & PROP_TYPE_STRING) {
							d(printf ("value = %s\n", values[0]));
							/* if it's a normal property just set the string */
							if (values[0])
								e_contact_set (contact, info->field_id, values[0]);
						}
						else if (info->prop_type & PROP_TYPE_COMPLEX) {
							/* if it's a list call the contact-populate function,
							   which calls g_object_set to set the property */
							info->populate_contact_func(contact, values, bl, NULL);
						}

						ldap_value_free (values);
					}
				}
			}
		}

		ldap_memfree (attr);
		g_mutex_lock (bl->priv->ldap_lock);
		attr = ldap_next_attribute (ldap, e, ber);
		g_mutex_unlock (bl->priv->ldap_lock);
	}

	if (ber)
		ber_free (ber, 0);

	return contact;
}

static gboolean
poll_ldap (EBookBackendGAL *bl)
{
	LDAP           *ldap = bl->priv->ldap;
	int            rc;
	LDAPMessage    *res;
	struct timeval timeout;

	g_mutex_lock (bl->priv->ldap_lock);
	if (!ldap) {
		g_mutex_unlock (bl->priv->ldap_lock);
		bl->priv->poll_timeout = -1;
		return FALSE;
	}
	g_mutex_unlock (bl->priv->ldap_lock);

	if (!bl->priv->active_ops) {
		g_warning ("poll_ldap being called for backend with no active operations");
		bl->priv->poll_timeout = -1;
		return FALSE;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = LDAP_RESULT_TIMEOUT_MILLIS * 1000;

	g_mutex_lock (bl->priv->ldap_lock);
	rc = ldap_result (ldap, LDAP_RES_ANY, 0, &timeout, &res);
	g_mutex_unlock (bl->priv->ldap_lock);
	if (rc != 0) {/* rc == 0 means timeout exceeded */
		if (rc == -1) {
			EDataBookView *book_view = find_book_view (bl);
			d(printf ("ldap_result returned -1, restarting ops\n"));

			gal_reconnect (bl, book_view, LDAP_SERVER_DOWN);
#if 0
			if (bl->priv->connected)
				restart_ops (bl);
#endif
		}
		else {
			int msgid = ldap_msgid (res);
			LDAPOp *op;

			g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
			op = g_hash_table_lookup (bl->priv->id_to_op, &msgid);

			d(printf ("looked up msgid %d, got op %p\n", msgid, op));

			if (op)
				op->handler (op, res);
			else
				g_warning ("unknown operation, msgid = %d", msgid);

			/* XXX should the call to op->handler be
			   protected by the lock? */
			g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);

			ldap_msgfree(res);
		}
	}

	return TRUE;
}

static void
ldap_search_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*)op;
	EDataBookView *view = search_op->view;
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (op->backend);
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *e;
	int msg_type;

	d(printf ("ldap_search_handler (%p)\n", view));

	g_mutex_lock (bl->priv->ldap_lock);
	if (!ldap) {
		g_mutex_unlock (bl->priv->ldap_lock);
		e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_OtherError);
		ldap_op_finished (op);
		return;
	}
	g_mutex_unlock (bl->priv->ldap_lock);

	if (!search_op->notified_receiving_results) {
		search_op->notified_receiving_results = TRUE;
		book_view_notify_status (op->view, _("Receiving LDAP search results..."));
	}

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_mutex_lock (bl->priv->ldap_lock);
		e = ldap_first_entry(ldap, res);
		g_mutex_unlock (bl->priv->ldap_lock);

		while (NULL != e) {
			EContact *contact = build_contact_from_entry (bl, e, NULL);

			e_data_book_view_notify_update (view, contact);

			g_object_unref (contact);

			g_mutex_lock (bl->priv->ldap_lock);
			e = ldap_next_entry(ldap, e);
			g_mutex_unlock (bl->priv->ldap_lock);
		}
	}
	else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		char *ldap_error_msg;
		int ldap_error;

		g_mutex_lock (bl->priv->ldap_lock);
		ldap_parse_result (ldap, res, &ldap_error,
				   NULL, &ldap_error_msg, NULL, NULL, 0);
		g_mutex_unlock (bl->priv->ldap_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("ldap_search_handler: %02X (%s), additional info: %s",
				   ldap_error,
				   ldap_err2string (ldap_error), ldap_error_msg);
		}
		ldap_memfree (ldap_error_msg);

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_SearchTimeLimitExceeded);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_SearchSizeLimitExceeded);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);
		else
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_OtherError);

		ldap_op_finished (op);
	}
	else {
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_OtherError);
		ldap_op_finished (op);

	}
}

static void
ldap_search_dtor (LDAPOp *op)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*) op;

	d(printf ("ldap_search_dtor (%p)\n", search_op->view));

	/* unhook us from our EDataBookView */
	g_object_set_data (G_OBJECT (search_op->view), "EBookBackendGAL.BookView::search_op", NULL);

	bonobo_object_unref (search_op->view);

	g_free (search_op);
}

static void
start_book_view (EBookBackend  *backend,
		 EDataBookView *view)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);
	GNOME_Evolution_Addressbook_CallStatus status;
	GList *contacts;
	char *ldap_query;
	int ldap_err;
	int search_msgid;
	int view_limit;
	GList *l;

	switch (bl->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (!(bl->priv->marked_for_offline && bl->priv->cache)) {
			e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_RepositoryOffline);
			return;
		}

		contacts = e_book_backend_cache_get_contacts (bl->priv->cache,
							      e_data_book_view_get_card_query (view));

		for (l = contacts; l; l = g_list_next (l)) {
			EContact *contact = l->data;
			e_data_book_view_notify_update (view, contact);
			g_object_unref (contact);
		}

		g_list_free (contacts);

		e_data_book_view_notify_complete (view, GNOME_Evolution_Addressbook_Success);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		g_mutex_lock (bl->priv->ldap_lock);
		if (!bl->priv->ldap) {
			g_mutex_unlock (bl->priv->ldap_lock);
			e_data_book_view_notify_complete (view,
							  GNOME_Evolution_Addressbook_InvalidQuery);
			return;
		}
		g_mutex_unlock (bl->priv->ldap_lock);

		/* we start at 1 so the user sees stuff as it appears and we
		   aren't left waiting for more cards to show up, if the
		   connection is slow. */
		e_data_book_view_set_thresholds (view, 1, 3000);

		view_limit = e_data_book_view_get_max_results (view);
		if (view_limit == -1 || view_limit > bl->priv->gc->response_limit)
			view_limit = bl->priv->gc->response_limit;

		d(printf ("start_book_view (%p)\n", view));

		status = build_query (bl, e_data_book_view_get_card_query (view),
				      &ldap_query);

		if (status != GNOME_Evolution_Addressbook_Success || !ldap_query) {
			e_data_book_view_notify_complete (view, status);
			if (ldap_query)
				g_free (ldap_query);
			return;
		}

		do {
			book_view_notify_status (view, _("Searching..."));

			g_mutex_lock (bl->priv->ldap_lock);
			ldap_err = ldap_search_ext (bl->priv->ldap, LDAP_ROOT_DSE,
						    LDAP_SCOPE_SUBTREE,
						    ldap_query,
						    search_attrs, 0,
						    NULL, /* XXX */
						    NULL, /* XXX */
						    NULL, /* XXX timeout */
						    view_limit,
						    &search_msgid);
			g_mutex_unlock (bl->priv->ldap_lock);
		} while (gal_reconnect (bl, view, ldap_err));

		g_free (ldap_query);

		if (ldap_err != LDAP_SUCCESS) {
			book_view_notify_status (view, ldap_err2string(ldap_err));
			return;
		}
		else if (search_msgid == -1) {
			book_view_notify_status (view,
						 _("Error performing search"));
			return;
		}
		else {
			LDAPSearchOp *op = g_new0 (LDAPSearchOp, 1);

			d(printf ("adding search_op (%p, %d)\n", view, search_msgid));

			op->view = view;

			bonobo_object_ref (view);

			ldap_op_add ((LDAPOp*)op, E_BOOK_BACKEND (bl), NULL, view,
				     0, search_msgid,
				     ldap_search_handler, ldap_search_dtor);

			g_object_set_data (G_OBJECT (view), "EBookBackendGAL.BookView::search_op", op);
		}
	}
}

static void
stop_book_view (EBookBackend  *backend,
		EDataBookView *view)
{
	LDAPSearchOp *op;

	d(printf ("stop_book_view (%p)\n", view));

	op = g_object_get_data (G_OBJECT (view), "EBookBackendGAL.BookView::search_op");
	if (op)
		ldap_op_finished ((LDAPOp*)op);
}

static void
get_changes (EBookBackend *backend,
	     EDataBook    *book,
	     guint32 	   opid,
	     const char   *change_id)
{
	/* FIXME: implement */
}

static void
generate_cache_handler (LDAPOp *op, LDAPMessage *res)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (op->backend);
	LDAP *ldap = bl->priv->ldap;
	LDAPMessage *e;
	gint msg_type;

	g_mutex_lock (bl->priv->ldap_lock);
	if (!ldap) {
		g_mutex_unlock (bl->priv->ldap_lock);
		ldap_op_finished (op);
		return;
	}
	g_mutex_unlock (bl->priv->ldap_lock);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_mutex_lock (bl->priv->ldap_lock);
		e = ldap_first_entry(ldap, res);
		g_mutex_unlock (bl->priv->ldap_lock);

		while (e != NULL) {
			EContact *contact = build_contact_from_entry (bl, e, NULL);

			contact_list_op->contacts = g_list_prepend (contact_list_op->contacts, contact);
			g_mutex_lock (bl->priv->ldap_lock);
			e = ldap_next_entry(ldap, e);
			g_mutex_unlock (bl->priv->ldap_lock);
		}
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		GList *l;

		e_file_cache_clean (E_FILE_CACHE (bl->priv->cache));
		
		for (l = contact_list_op->contacts; l; l = g_list_next (l)) {
			EContact *contact = l->data;
			e_book_backend_cache_add_contact (bl->priv->cache, contact);
		}

		e_book_backend_cache_set_populated (bl->priv->cache);
		ldap_op_finished (op);
	}
}

static void
generate_cache_dtor (LDAPOp *op)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	GList *l;

	for (l = contact_list_op->contacts; l; l = g_list_next (l)) {
		g_object_unref (l->data);
	}

	g_list_free (contact_list_op->contacts);
	g_free (contact_list_op);
}

static void
generate_cache (EBookBackendGAL *book_backend_gal)
{
	LDAPGetContactListOp *contact_list_op = g_new0 (LDAPGetContactListOp, 1);
	EBookBackendGALPrivate *priv;
	gchar *ldap_query;
	gint contact_list_msgid;
	gint ldap_error;

	priv = book_backend_gal->priv;

	g_mutex_lock (priv->ldap_lock);
	if (!priv->ldap) {
		g_mutex_unlock (priv->ldap_lock);
		g_free (contact_list_op);
		return;
	}
	g_mutex_unlock (priv->ldap_lock);

	build_query (book_backend_gal, 
		     "(beginswith \"file_as\" \"\")", &ldap_query);

	do {	
		g_mutex_lock (priv->ldap_lock);
		ldap_error = ldap_search_ext (priv->ldap, LDAP_ROOT_DSE,
					      LDAP_SCOPE_SUBTREE,
					      ldap_query,
					      NULL, 0, NULL, NULL,
					      NULL, /* XXX timeout */
					      LDAP_NO_LIMIT, &contact_list_msgid);
		g_mutex_unlock (priv->ldap_lock);
	} while (gal_reconnect (book_backend_gal, NULL, ldap_error));

	g_free (ldap_query);

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add ((LDAPOp*) contact_list_op, (EBookBackend *) book_backend_gal, NULL /* book */,
			     NULL /* book_view */, 0 /* opid */, contact_list_msgid,
			     generate_cache_handler, generate_cache_dtor);
	} else {
		generate_cache_dtor ((LDAPOp *) contact_list_op);
	}
}

static void
authenticate_user (EBookBackend *backend,
		   EDataBook    *book,
		   guint32       opid,
		   const char   *user,
		   const char   *passwd,
		   const char   *auth_method)
{
	/* We should not be here */
	e_data_book_respond_authenticate_user (book,
					       opid,
					       GNOME_Evolution_Addressbook_UnsupportedAuthenticationMethod);
}

static void
ldap_cancel_op(void *key, void *value, void *data)
{
	EBookBackendGAL *bl = data;
	LDAPOp *op = value;

	/* ignore errors, its only best effort? */
	g_mutex_lock (bl->priv->ldap_lock);
	if (bl->priv->ldap)
		ldap_abandon_ext (bl->priv->ldap, op->id, NULL, NULL);
	g_mutex_unlock (bl->priv->ldap_lock);
}

static GNOME_Evolution_Addressbook_CallStatus
cancel_operation (EBookBackend *backend, EDataBook *book)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);

	g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_foreach (bl->priv->id_to_op, ldap_cancel_op, bl);
	g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);

	return GNOME_Evolution_Addressbook_Success;
}

static void
set_mode (EBookBackend *backend, int mode)
{
	EBookBackendGAL *be = E_BOOK_BACKEND_GAL (backend);
	EBookBackendGALPrivate *bepriv;
	ExchangeAccountResult result;

	bepriv = be->priv;

	if (bepriv->mode == mode)
		return;

	bepriv->mode = mode;

	/* Cancel all running operations */
	cancel_operation (backend, NULL);

	if (e_book_backend_is_loaded (backend)) {
		if (mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
			if (bepriv->account) {
			if (exchange_account_set_offline (bepriv->account)) {
				e_book_backend_set_is_writable (backend, FALSE);
				e_book_backend_notify_writable (backend, FALSE);
				e_book_backend_notify_connection_status (backend, FALSE);
			}
			}
		} else if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
			if (exchange_account_connect (bepriv->account, NULL, &result)) {
				e_book_backend_set_is_writable (backend, FALSE);
				e_book_backend_notify_writable (backend, FALSE);
				e_book_backend_notify_connection_status (backend, TRUE);

				if (e_book_backend_is_loaded (backend)) {
					gal_connect (be);
					e_book_backend_notify_auth_required (backend);

					if (bepriv->marked_for_offline && bepriv->cache)
						generate_cache (be);
				}
			}
		}
	}
}

static void
get_supported_fields (EBookBackend *backend,
		      EDataBook    *book,
		      guint32 	    opid)

{
	e_data_book_respond_get_supported_fields (book,
						  opid,
						  GNOME_Evolution_Addressbook_Success,
						  supported_fields);
}

static void
get_required_fields (EBookBackend *backend,
		     EDataBook *book,
		     guint32 opid)
{
	GList *fields = NULL;
	
	fields = g_list_append (fields, (gchar *) e_contact_field_name (E_CONTACT_FILE_AS));
	e_data_book_respond_get_required_fields (book,
						  opid,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
	g_list_free (fields);

}

static void
get_supported_auth_methods (EBookBackend *backend,
			    EDataBook    *book,
			    guint32       opid)

{
	e_data_book_respond_get_supported_auth_methods (book,
							opid,
							GNOME_Evolution_Addressbook_Success,
							NULL);
}

static GNOME_Evolution_Addressbook_CallStatus
load_source (EBookBackend *backend,
	     ESource      *source,
	     gboolean      only_if_exists)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (backend);
	GNOME_Evolution_Addressbook_CallStatus result;
	const char *host;
	const char *offline;
	char *uri;

	g_return_val_if_fail (bl->priv->connected == FALSE, GNOME_Evolution_Addressbook_OtherError);

	uri = e_source_get_uri (source);

	offline = e_source_get_property (source, "offline_sync");
	if (offline && g_str_equal (offline, "1"))
		bl->priv->marked_for_offline = TRUE;

	host = uri + sizeof ("gal://") - 1;
	if (strncmp (uri, "gal://", host - uri))
		return FALSE;

	bl->priv->gal_uri = uri;

	if (bl->priv->cache) {
		g_object_unref (bl->priv->cache);
		bl->priv->cache = NULL;
	}

	bl->priv->cache = e_book_backend_cache_new (uri);

	if (bl->priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		/* Offline */

		e_book_backend_set_is_loaded (backend, TRUE);
		e_book_backend_set_is_writable (backend, FALSE);
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);

		if (!bl->priv->marked_for_offline)
			return GNOME_Evolution_Addressbook_RepositoryOffline;

		return GNOME_Evolution_Addressbook_Success;
	}

	/* Online */

	result = gal_connect (bl);
	if (result != GNOME_Evolution_Addressbook_Success)
		return result;

	if (bl->priv->marked_for_offline)
		generate_cache (bl);

	return result;
}

static void
remove_gal (EBookBackend *backend, EDataBook *book, guint32 opid)
{
	e_data_book_respond_remove (book, opid, GNOME_Evolution_Addressbook_PermissionDenied);
}

static char *
get_static_capabilities (EBookBackend *backend)
{
	return g_strdup("net");
}

/**
 * e_book_backend_gal_new:
 *
 * Creates a new #EBookBackendGAL.
 *
 * Return value: the new #EBookBackendGAL.
 */
EBookBackend *
e_book_backend_gal_new (void)
{
	EBookBackendGAL *backend;

	backend = g_object_new (E_TYPE_BOOK_BACKEND_GAL, NULL);
	if (!e_book_backend_construct (E_BOOK_BACKEND (backend))) {
		g_object_unref (backend);

		return NULL;
	}

	return E_BOOK_BACKEND (backend);
}

static gboolean
call_dtor (int msgid, LDAPOp *op, gpointer data)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (op->backend);

	g_mutex_lock (bl->priv->ldap_lock);
	ldap_abandon (bl->priv->ldap, op->id);
	g_mutex_unlock (bl->priv->ldap_lock);

	op->dtor (op);

	return TRUE;
}

static void
dispose (GObject *object)
{
	EBookBackendGAL *bl = E_BOOK_BACKEND_GAL (object);

	if (bl->priv) {
		g_static_rec_mutex_lock (&bl->priv->op_hash_mutex);
		g_hash_table_foreach_remove (bl->priv->id_to_op, (GHRFunc)call_dtor, NULL);
		g_hash_table_destroy (bl->priv->id_to_op);
		g_static_rec_mutex_unlock (&bl->priv->op_hash_mutex);
		g_static_rec_mutex_free (&bl->priv->op_hash_mutex);

		if (bl->priv->poll_timeout != -1) {
			d(printf ("removing timeout\n"));
			g_source_remove (bl->priv->poll_timeout);
		}

		g_mutex_lock (bl->priv->ldap_lock);
		if (bl->priv->ldap)
			ldap_unbind (bl->priv->ldap);
		g_mutex_unlock (bl->priv->ldap_lock);

		if (bl->priv->gc)
			g_object_unref (bl->priv->gc);
		if (bl->priv->cache)
			g_object_unref (bl->priv->cache);

		if (bl->priv->ldap_lock)
			g_mutex_free (bl->priv->ldap_lock);

		g_free (bl->priv->gal_uri);
		g_free (bl->priv);
		bl->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
class_init (EBookBackendGALClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *backend_class = E_BOOK_BACKEND_CLASS (klass);
	int i;

	parent_class = g_type_class_peek_parent (klass);

	/* Set the virtual methods. */
	backend_class->load_source                = load_source;
	backend_class->remove                     = remove_gal;
	backend_class->get_static_capabilities    = get_static_capabilities;

	backend_class->create_contact             = create_contact;
	backend_class->remove_contacts            = remove_contacts;
	backend_class->modify_contact             = modify_contact;
	backend_class->get_contact                = get_contact;
	backend_class->get_contact_list           = get_contact_list;
	backend_class->start_book_view            = start_book_view;
	backend_class->stop_book_view             = stop_book_view;
	backend_class->get_changes                = get_changes;
	backend_class->authenticate_user          = authenticate_user;
	backend_class->get_supported_fields       = get_supported_fields;
	backend_class->set_mode      		  = set_mode;
	backend_class->get_required_fields        = get_required_fields;
	backend_class->get_supported_auth_methods = get_supported_auth_methods;
	backend_class->cancel_operation           = cancel_operation;

	object_class->dispose = dispose;

	/* Set up static data */
	supported_fields = NULL;
	for (i = 0; i < num_prop_infos; i++) {
		supported_fields = g_list_append (supported_fields,
						  (char *)e_contact_field_name (prop_info[i].field_id));
	}
	supported_fields = g_list_append (supported_fields, "file_as");

	search_attrs = g_new (char *, num_prop_infos + 1);
	for (i = 0; i < num_prop_infos; i++)
		search_attrs[i] = prop_info[i].ldap_attr;
	search_attrs[num_prop_infos] = NULL;
}

static void
init (EBookBackendGAL *backend)
{
	EBookBackendGALPrivate *priv;

	priv                         = g_new0 (EBookBackendGALPrivate, 1);

	priv->id_to_op         	     = g_hash_table_new (g_int_hash, g_int_equal);
	priv->poll_timeout     	     = -1;
	priv->ldap_lock		     = g_mutex_new ();

	g_static_rec_mutex_init (&priv->op_hash_mutex);

	backend->priv = priv;
}

E2K_MAKE_TYPE (e_book_backend_gal, EBookBackendGAL, class_init, init, PARENT_TYPE)
