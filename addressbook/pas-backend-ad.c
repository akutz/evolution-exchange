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
#define G_LOG_DOMAIN "pas-backend-ad"

#include "config.h"  
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lber.h>

#include "e2k-utils.h"
#include "exchange-component.h"
#include "exchange-account.h"

#ifdef DEBUG
#define LDAP_DEBUG
#define LDAP_DEBUG_ADD
#endif
#include <ldap.h>
#ifdef DEBUG
#undef LDAP_DEBUG
#endif

#include <e-util/e-sexp.h>
#include <ebook/e-card-simple.h>

#include "pas-backend-ad.h"
#include "pas/pas-backend-card-sexp.h"
#include "pas/pas-book.h"
#include "pas/pas-card-cursor.h"


/* interval for our poll_ldap timeout */
#define LDAP_POLL_INTERVAL 20

/* timeout for ldap_result */
#define LDAP_RESULT_TIMEOUT_MILLIS 0

/* smart grouping stuff */
#define GROUPING_INITIAL_SIZE 1
#define GROUPING_MAXIMUM_SIZE 200

/* the next two are in milliseconds */
#define GROUPING_MINIMUM_WAIT 0  /* we never send updates faster than this, to avoid totally spamming the UI */
#define GROUPING_MAXIMUM_WAIT 250 /* we always send updates (if there are pending cards) when we hit this */

#define TV_TO_MILLIS(timeval) ((timeval).tv_sec * 1000 + (timeval).tv_usec / 1000)

#ifdef DEBUG
static gboolean debug = FALSE;
#define DEBUGGING debug
#else
#define DEBUGGING 0
#endif

#define PARENT_TYPE PAS_TYPE_BACKEND
static PASBackendClass *parent_class;

typedef struct _PASBackendADBookView PASBackendADBookView;
typedef struct LDAPOp LDAPOp;

struct _PASBackendADPrivate {
	char     *ad_uri;
	gboolean connected;
	gchar    *ldap_binddn;
	EList    *book_views;

	E2kGlobalCatalog *gc;

	EList    *supported_fields;

	char     **search_attrs;

	/* whether or not there's a request in process on our LDAP* */
	LDAPOp *current_op;
	GList *pending_ops;
	int op_idle;
};

struct _PASBackendADBookView {
	PASBookView           *book_view;
	PASBackendADPrivate   *blpriv;
	gchar                 *search;
	PASBackendCardSExp    *card_sexp;
	int                    search_timeout;
	int                    search_msgid;
	LDAPOp                *search_op;

        /* grouping stuff */

        GList    *pending_adds;        /* the cards we're sending */
        int       num_pending_adds;    /* the number waiting to be sent */
        int       target_pending_adds; /* the cutoff that forces a flush to the client, if it happens before the timeout */
        int       num_sent_this_time;  /* the number of cards we sent to the client before the most recent timeout */
        int       num_sent_last_time;  /* the number of cards we sent to the client before the previous timeout */
        glong     grouping_time_start;

        /* used by poll_ldap to only send the status messages once */
        gboolean notified_receiving_results;
};

typedef gboolean (*LDAPOpHandler)(PASBackend *backend, LDAPOp *op);
typedef void (*LDAPOpDtor)(PASBackend *backend, LDAPOp *op);

struct LDAPOp {
	LDAPOpHandler handler;
	LDAPOpDtor    dtor;
	PASBackend    *backend;
	PASBook       *book;
	PASBookView   *view;
};

static void     ldap_op_init (LDAPOp *op, PASBackend *backend, PASBook *book, PASBookView *view, LDAPOpHandler handler, LDAPOpDtor dtor);
static void     ldap_op_process_current (PASBackend *backend);
static void     ldap_op_process (LDAPOp *op);
static void     ldap_op_restart (LDAPOp *op);
static gboolean ldap_op_process_on_idle (PASBackend *backend);
static void     ldap_op_finished (LDAPOp *op);

static ECardSimple *build_card_from_entry (PASBackendAD *bl, LDAPMessage *e);

static void manager_populate (ECardSimple *card, char **values, PASBackendAD *bl);

struct prop_info {
	ECardSimpleField field_id;
	char *query_prop;
	char *ldap_attr;
#define PROP_TYPE_STRING   0x01
#define PROP_TYPE_COMPLEX  0x02
	int prop_type;

	/* the remaining items are only used for the TYPE_COMPLEX props */

	/* used when reading from the ldap server populates ECard with the values in **values. */
	void (*populate_ecard_func)(ECardSimple *card, char **values, PASBackendAD *bl);

} prop_info[] = {

#define COMPLEX_PROP(fid,q,a,ctor) {fid, q, a, PROP_TYPE_COMPLEX, ctor}
#define STRING_PROP(fid,q,a) {fid, q, a, PROP_TYPE_STRING}


	/* name fields */
	STRING_PROP (E_CARD_SIMPLE_FIELD_FULL_NAME,   "full_name", "displayName" ),
	STRING_PROP (E_CARD_SIMPLE_FIELD_GIVEN_NAME,  "given_name", "givenName" ),
	STRING_PROP (E_CARD_SIMPLE_FIELD_FAMILY_NAME, "family_name", "sn" ),

	/* email addresses */
	STRING_PROP (E_CARD_SIMPLE_FIELD_EMAIL, "email", "mail"),

	/* phone numbers */
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_BUSINESS,     "business_phone", "telephoneNumber"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_2,   "business_phone_2", "otherTelephone"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_HOME,         "home_phone", "homePhone"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_HOME_2,       "home_phone_2", "otherHomePhone"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_MOBILE,       "mobile_phone", "mobile"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_BUSINESS_FAX, "business_fax", "facsimileTelephoneNumber"), 
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_OTHER_FAX,    "other_fax", "otherFacsimileTelephoneNumber"), 
	STRING_PROP (E_CARD_SIMPLE_FIELD_PHONE_PAGER,        "pager", "pager"),

	/* org information */
	STRING_PROP (E_CARD_SIMPLE_FIELD_ORG,       "org",       "company"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_ORG_UNIT,  "org_unit",  "department"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_OFFICE,    "office",    "physicalDeliveryOfficeName"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_TITLE,     "title",     "title"),

	COMPLEX_PROP(E_CARD_SIMPLE_FIELD_MANAGER,   "manager",   "manager", manager_populate),

	/* FIXME: we should aggregate streetAddress, l, st, c, postalCode
	 * into business_address
	 */

	/* misc fields */
	STRING_PROP (E_CARD_SIMPLE_FIELD_URL,         "url", "wWWHomePage"),
	STRING_PROP (E_CARD_SIMPLE_FIELD_NOTE,        "note", "info"), 
	STRING_PROP (E_CARD_SIMPLE_FIELD_FBURL,       "fburl", "msExchFBURL"),

#undef STRING_PROP
#undef COMPLEX_PROP
};

static int num_prop_infos = sizeof(prop_info) / sizeof(prop_info[0]);

static void
view_destroy (GObject *object, gpointer data)
{
	PASBook           *book = (PASBook *)data;
	PASBackendAD      *bl;
	EIterator         *iter;

	bl = PAS_BACKEND_AD (pas_book_get_backend(book));

	iter = e_list_get_iterator (bl->priv->book_views);

	while (e_iterator_is_valid (iter)) {
		PASBackendADBookView *view = (PASBackendADBookView *)e_iterator_get (iter);

		if (view->book_view == PAS_BOOK_VIEW(object)) {
			GNOME_Evolution_Addressbook_Book corba_book;
			CORBA_Environment ev;

			if (view->search_timeout != 0) {
				/* we have a search running on the
				   ldap connection.  remove the idle
				   handler and anbandon the msg id */
				g_source_remove(view->search_timeout);
				if (view->search_msgid != -1) {
					LDAP *ldap;
					ldap = e2k_global_catalog_get_ldap (bl->priv->gc);
					if (ldap) {
						ldap_abandon_ext (ldap,
								  view->search_msgid,
								  NULL, NULL);
					}
				}
			}

			/* If the search op is the current op, finish
			 * it. Else if it's still pending, remove it
			 * from the list and nuke it ourselves.
			 */
			if (view->search_op) {
				if (view->search_op == bl->priv->current_op)
					ldap_op_finished (view->search_op);
				else if (g_list_find (bl->priv->pending_ops, view->search_op)) {
					bl->priv->pending_ops = g_list_remove (
						bl->priv->pending_ops,
						view->search_op);
					view->search_op->dtor (
						view->search_op->backend,
						view->search_op);
				}
			}

			/* free up the view structure */
			g_free (view->search);
			g_object_unref (view->card_sexp);
			g_free (view);

			/* and remove it from our list */
			e_iterator_delete (iter);

			/* unref the book now */
			corba_book = bonobo_object_corba_objref(BONOBO_OBJECT(book));

			CORBA_exception_init(&ev);

			GNOME_Evolution_Addressbook_Book_unref(corba_book, &ev);
	
			if (ev._major != CORBA_NO_EXCEPTION) {
				g_warning("view_destroy: Exception unreffing "
					  "corba book.\n");
			}

			CORBA_exception_free(&ev);
			break;
		}

		e_iterator_next (iter);
	}

	g_object_unref (iter);

}

static GNOME_Evolution_Addressbook_BookListener_CallStatus
pas_backend_ad_connect (PASBackendAD *bl)
{
	PASBackendADPrivate *blpriv = bl->priv;
	ExchangeAccount *account;

#ifdef DEBUG
	{
		int debug_level = 1;
		ldap_set_option (NULL, LDAP_OPT_DEBUG_LEVEL, &debug_level);
	}
#endif

	blpriv->gc = NULL;
	blpriv->connected = FALSE;

	account = exchange_component_get_account_for_uri (blpriv->ad_uri);
	if (!account)
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;
	blpriv->gc = exchange_account_get_global_catalog (account);
	if (!blpriv->gc)
		return GNOME_Evolution_Addressbook_BookListener_RepositoryOffline;

	g_object_ref (blpriv->gc);

	blpriv->connected = TRUE;
	pas_backend_set_is_loaded (PAS_BACKEND (bl), TRUE);
	return GNOME_Evolution_Addressbook_BookListener_Success;
}

static ECardSimple *
search_for_dn (PASBackendAD *bl, const char *dn)
{
	LDAP *ldap;
	LDAPMessage *res, *e;
	ECardSimple *result = NULL;
	int ldap_error;

	ldap_error = LDAP_SERVER_DOWN;
	while (ldap_error == LDAP_SERVER_DOWN &&
	       (ldap = e2k_global_catalog_get_ldap (bl->priv->gc))) {
		ldap_error = ldap_search_s (ldap, dn, LDAP_SCOPE_BASE,
					    "(objectclass=*)",
					    bl->priv->search_attrs,
					    0, &res);
	}

	if (ldap_error == LDAP_SUCCESS) {
		e = ldap_first_entry (ldap, res);
		while (NULL != e) {
			if (!strcmp (ldap_get_dn (ldap, e), dn)) {
				result = build_card_from_entry (bl, e);
				break;
			}
			e = ldap_next_entry (ldap, e);
		}

		ldap_msgfree(res);
	}

	return result;
}

static void
ldap_op_init (LDAPOp *op, PASBackend *backend,
	      PASBook *book, PASBookView *view,
	      LDAPOpHandler handler, LDAPOpDtor dtor)
{
	op->backend = backend;
	op->book = book;
	op->view = view;
	op->handler = handler;
	op->dtor = dtor;
}

static void
ldap_op_process_current (PASBackend *backend)
{
	PASBackendAD *bl = PAS_BACKEND_AD (backend);
	LDAPOp *op = bl->priv->current_op;

	if (!bl->priv->connected) {
		if (op->view)
			pas_book_view_notify_status_message (op->view, _("Connecting to LDAP server..."));
		pas_backend_ad_connect(bl);
	}

	if (bl->priv->connected) {
		if (op->handler (backend, op))
			ldap_op_finished (op);
	}
	else {
		if (op->view) {
			pas_book_view_notify_status_message (op->view, _("Unable to connect to LDAP server."));
			pas_book_view_notify_complete (op->view, GNOME_Evolution_Addressbook_BookViewListener_Success); /* XXX bleah */
		}

		ldap_op_finished (op);
	}
}

static void
ldap_op_process (LDAPOp *op)
{
	PASBackendAD *bl = PAS_BACKEND_AD (op->backend);

	if (bl->priv->current_op) {
		/* operation in progress.  queue this op for later and return. */
		if (op->view)
			pas_book_view_notify_status_message (op->view, _("Waiting for connection to LDAP server..."));
		bl->priv->pending_ops = g_list_append (bl->priv->pending_ops, op);
	}
	else {
		/* nothing going on, do this op now */
		bl->priv->current_op = op;
		ldap_op_process_current (op->backend);
	}
}

static gboolean
ldap_op_process_on_idle (PASBackend *backend)
{
	PASBackendAD *bl = PAS_BACKEND_AD (backend);

	bl->priv->op_idle = 0;

	ldap_op_process_current (backend);

	return FALSE;
}

static void
ldap_op_restart (LDAPOp *op)
{
	PASBackend *backend = op->backend;
	PASBackendAD *bl = PAS_BACKEND_AD (backend);

	g_return_if_fail (op == bl->priv->current_op);

	bl->priv->op_idle = g_idle_add((GSourceFunc)ldap_op_process_on_idle, backend);
}

static void
ldap_op_finished (LDAPOp *op)
{
	PASBackend *backend = op->backend;
	PASBackendAD *bl = PAS_BACKEND_AD (backend);

	g_return_if_fail (op == bl->priv->current_op);

	op->dtor (backend, op);

	if (bl->priv->pending_ops) {
		bl->priv->current_op = bl->priv->pending_ops->data;
		bl->priv->pending_ops = g_list_remove (bl->priv->pending_ops, bl->priv->current_op);

		bl->priv->op_idle = g_idle_add((GSourceFunc)ldap_op_process_on_idle, backend);
	}
	else {
		bl->priv->current_op = NULL;
		if (bl->priv->op_idle) {
			g_source_remove (bl->priv->op_idle);
			bl->priv->op_idle = 0;
		}
	}
}


static void
pas_backend_ad_process_create_card (PASBackend *backend,
				    PASBook    *book,
				    PASCreateCardRequest *req)
{
	pas_book_respond_create (book, GNOME_Evolution_Addressbook_BookListener_PermissionDenied, "");
}

static void
pas_backend_ad_process_remove_cards (PASBackend *backend,
				     PASBook    *book,
				     PASRemoveCardsRequest *req)
{
	pas_book_respond_remove (book, GNOME_Evolution_Addressbook_BookListener_PermissionDenied);
}

static void
pas_backend_ad_process_modify_card (PASBackend *backend,
				    PASBook    *book,
				    PASModifyCardRequest *req)
{
	pas_book_respond_modify (book, GNOME_Evolution_Addressbook_BookListener_PermissionDenied);
}

static void
pas_backend_ad_process_get_vcard (PASBackend *backend,
				  PASBook    *book,
				  PASGetVCardRequest *req)
{
	PASBackendAD *bl;
	ECardSimple *simple;

	bl = PAS_BACKEND_AD (pas_book_get_backend (book));

	simple = search_for_dn (bl, req->id);

	if (simple) {
		char *vcard;
		vcard = e_card_simple_get_vcard_assume_utf8 (simple);
		pas_book_respond_get_vcard (book,
					    GNOME_Evolution_Addressbook_BookListener_Success,
					    vcard);
		g_object_unref (simple);
		g_free (vcard);
	}
	else {
		pas_book_respond_get_vcard (book,
					    GNOME_Evolution_Addressbook_BookListener_CardNotFound,
					    "");
	}
}


static void
pas_backend_ad_process_get_cursor (PASBackend *backend,
				   PASBook    *book,
				   PASGetCursorRequest *req)
{
	PASCardCursor *cursor;

	cursor = pas_card_cursor_new (NULL, NULL, NULL);	
	pas_book_respond_get_cursor (book,
				     GNOME_Evolution_Addressbook_BookListener_PermissionDenied,
				     cursor);
	bonobo_object_unref (BONOBO_OBJECT (cursor));
}


/* List property functions */

static void
manager_populate(ECardSimple *card, char **values, PASBackendAD *bl)
{
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;

	status = e2k_global_catalog_lookup (bl->priv->gc,
					    E2K_GLOBAL_CATALOG_LOOKUP_BY_DN,
					    values[0], 0, &entry);
	if (status != E2K_GLOBAL_CATALOG_OK)
		return;

	e_card_simple_set (card, E_CARD_SIMPLE_FIELD_MANAGER,
			   entry->display_name);
	e2k_global_catalog_entry_free (bl->priv->gc, entry);
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

	for (i = 0; i < num_prop_infos; i ++)
		if (!strcmp (query_prop, prop_info[i].query_prop))
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

	if (!exact && strlen (str) == 0) {
		/* Can't do (beginswith FIELD "") */
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

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
			filter = g_strdup_printf("(|(displayName=%s%s)(sn=%s%s)(givenName=%s%s))",
						 str, star, str, star,
						 str, star);
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
pas_backend_ad_build_query (PASBackendAD *bl, char *evo_query, char **ldap_query)
{
	ESExp *sexp;
	ESExpResult *r;
	int i, retval;

	sexp = e_sexp_new();

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		e_sexp_add_function(sexp, 0, symbols[i].name,
				    symbols[i].func, NULL);
	}

	e_sexp_input_text(sexp, evo_query, strlen(evo_query));
	e_sexp_parse(sexp);

	r = e_sexp_eval(sexp);

	if (r->type == ESEXP_RES_STRING) {
		*ldap_query = g_strdup_printf ("(&(mail=*)(!(msExchHideFromAddressLists=TRUE))%s)", r->value.string);
		retval = GNOME_Evolution_Addressbook_BookViewListener_Success;
	} else if (r->type == ESEXP_RES_BOOL) {
		/* If it's FALSE, that means "no matches". If it's TRUE
		 * that means "everything matches", but we don't support
		 * that, so it also means "no matches".
		 */
		*ldap_query = NULL;
		retval = GNOME_Evolution_Addressbook_BookViewListener_Success;
	} else {
		/* Bad query */
		*ldap_query = NULL;
		retval = GNOME_Evolution_Addressbook_BookViewListener_QueryRefused;
	}

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	return retval;
}


typedef struct {
	LDAPOp op;
	char *ldap_query;
	PASBackendAD *bl;
	PASBackendADBookView *view;
} LDAPSearchOp;

static ECardSimple *
build_card_from_entry (PASBackendAD *bl, LDAPMessage *e)
{
	LDAP *ldap = e2k_global_catalog_get_ldap (bl->priv->gc);
	ECard *ecard = E_CARD(g_object_new(e_card_get_type(), NULL));
	ECardSimple *card = e_card_simple_new (ecard);
	char *dn = ldap_get_dn(ldap, e);
	char *attr, *name;
	BerElement *ber = NULL;

        if (DEBUGGING)
		printf ("build_card_from_entry, dn = %s\n", dn);

	e_card_simple_set_id (card, dn);

	for (attr = ldap_first_attribute (ldap, e, &ber); attr;
	     attr = ldap_next_attribute (ldap, e, ber)) {
		int i;
		struct prop_info *info = NULL;

		for (i = 0; i < num_prop_infos; i ++)
			if (!g_ascii_strcasecmp (attr, prop_info[i].ldap_attr))
				info = &prop_info[i];

		if (info) {
			char **values;
			values = ldap_get_values (ldap, e, attr);

			if (values) {
				if (info->prop_type & PROP_TYPE_STRING) {
					if (DEBUGGING)
						printf ("%s: %s\n", attr, values[0]);

					e_card_simple_set (card, info->field_id, values[0]);
				}
				else if (info->prop_type & PROP_TYPE_COMPLEX) {
					if (DEBUGGING)
						printf ("%s (complex property): ", attr);
					info->populate_ecard_func(card, values, bl);
					if (DEBUGGING)
						printf ("\n");
				}

				ldap_value_free (values);
			}
		}

		ldap_memfree (attr);
	}

	name = e_card_simple_get (card, E_CARD_SIMPLE_FIELD_FULL_NAME);
	if (name) {
		e_card_simple_set (card, E_CARD_SIMPLE_FIELD_FILE_AS, name);
		g_free (name);
	}

	e_card_simple_sync_card (card);

	g_object_unref (ecard);
	if (ber)
		ber_free (ber, 0);
	ldap_memfree (dn);

	return card;
}

static void
send_pending_adds (PASBackendADBookView *view)
{
	view->num_sent_this_time += view->num_pending_adds;
	pas_book_view_notify_add (view->book_view, view->pending_adds);
	g_list_foreach (view->pending_adds, (GFunc)g_free, NULL);
	view->pending_adds = NULL;
	view->num_pending_adds = 0;
}

#define CHECK_CANCEL if (bl->priv->current_op != (LDAPOp *)op) goto out

static gboolean
poll_ldap (LDAPSearchOp *op)
{
	PASBackendADBookView *view = op->view;
	PASBackendAD   *bl = op->bl;
	LDAP           *ldap = e2k_global_catalog_get_ldap (bl->priv->gc);
	int            rc;
	LDAPMessage    *res, *e;
	static int received = 0;
	GTimeVal cur_time;
	glong cur_millis;
	struct timeval timeout;
	gboolean done = FALSE;

	g_object_ref (bl);
	bonobo_object_ref (BONOBO_OBJECT (view->book_view));

	timeout.tv_sec = 0;
	timeout.tv_usec = LDAP_RESULT_TIMEOUT_MILLIS * 1000;

	if (!view->notified_receiving_results) {
		view->notified_receiving_results = TRUE;
		pas_book_view_notify_status_message (view->book_view, _("Receiving LDAP search results..."));
		CHECK_CANCEL;
	}

	if (ldap)
		rc = ldap_result (ldap, view->search_msgid, 0, &timeout, &res);
	else
		rc = -1;
	if (rc != 0) {/* rc == 0 means timeout exceeded */
		if (rc == -1 && received == 0) {
			pas_book_view_notify_status_message (view->book_view, _("Restarting search."));
			CHECK_CANCEL;

			/* connection went down and we never got any. */
			bl->priv->connected = FALSE;

			/* this will reopen the connection */
			ldap_op_restart ((LDAPOp*)op);
			goto out;
		}

		if (rc != LDAP_RES_SEARCH_ENTRY) {
			ldap_msgfree(res);

			view->search_timeout = 0;
			if (view->num_pending_adds)
				send_pending_adds (view);
			pas_book_view_notify_complete (view->book_view, GNOME_Evolution_Addressbook_BookViewListener_Success); /* XXX bleah */
			CHECK_CANCEL;

			ldap_op_finished ((LDAPOp*)op);
			received = 0;
			goto out;
		}

		received = 1;

		e = ldap_first_entry(ldap, res);

		while (NULL != e) {
			ECardSimple *card = build_card_from_entry (bl, e);
			CHECK_CANCEL;

			view->pending_adds = g_list_append (view->pending_adds,
							    e_card_simple_get_vcard_assume_utf8 (card));
			view->num_pending_adds ++;

			g_object_unref (card);

			e = ldap_next_entry(ldap, e);
		}

		ldap_msgfree(res);
	}

	g_get_current_time (&cur_time);
	cur_millis = TV_TO_MILLIS (cur_time);

	if (cur_millis - view->grouping_time_start > GROUPING_MINIMUM_WAIT) {

		if (view->num_pending_adds >= view->target_pending_adds) {
			send_pending_adds (view);
			CHECK_CANCEL;
		}

		if (cur_millis - view->grouping_time_start > GROUPING_MAXIMUM_WAIT) {
			GTimeVal new_start;

			if (view->num_pending_adds) {
				send_pending_adds (view);
				CHECK_CANCEL;
			}
			view->target_pending_adds = MIN (GROUPING_MAXIMUM_SIZE,
							 (view->num_sent_this_time + view->num_sent_last_time) / 2);
			view->target_pending_adds = MAX (view->target_pending_adds, 1);

#ifdef PERFORMANCE_SPEW
			printf ("num sent this time %d, last time %d, target pending adds set to %d\n",
				view->num_sent_this_time,
				view->num_sent_last_time,
				view->target_pending_adds);
#endif
			g_get_current_time (&new_start);
			view->grouping_time_start = TV_TO_MILLIS (new_start); 
			view->num_sent_last_time = view->num_sent_this_time;
			view->num_sent_this_time = 0;
		}
	}

	done = TRUE;

 out:
	bonobo_object_unref (BONOBO_OBJECT (view->book_view));
	g_object_unref (bl);
	return done;
}

static gboolean
ldap_search_handler (PASBackend *backend, LDAPOp *op)
{
	LDAP *ldap;
	LDAPSearchOp *search_op = (LDAPSearchOp*) op;
	PASBackendAD *bl = PAS_BACKEND_AD (backend);
	PASBackendADBookView *view = search_op->view;
	int ldap_err;
	GTimeVal search_start;

	if (view)
		pas_book_view_notify_status_message (op->view, _("Searching..."));

	view->pending_adds = NULL;
	view->num_pending_adds = 0;
	view->target_pending_adds = GROUPING_INITIAL_SIZE;

	g_get_current_time (&search_start);
	view->grouping_time_start = TV_TO_MILLIS (search_start);
	view->num_sent_last_time = 0;
	view->num_sent_this_time = 0;
	view->notified_receiving_results = FALSE;

	ldap_err = LDAP_SERVER_DOWN;
	while (ldap_err == LDAP_SERVER_DOWN &&
	       (ldap = e2k_global_catalog_get_ldap (bl->priv->gc))) {
		ldap_err = ldap_search_ext (ldap, LDAP_ROOT_DSE,
					    LDAP_SCOPE_SUBTREE,
					    search_op->ldap_query,
					    bl->priv->search_attrs, 0,
					    NULL, NULL, 
					    NULL, -1,
					    &view->search_msgid);
	}

	if (ldap_err != LDAP_SUCCESS) {
		pas_book_view_notify_status_message (view->book_view, ldap_err2string(ldap_err));
		return TRUE; /* act synchronous in this case */
	}

	if (view->search_msgid == -1) {
		pas_book_view_notify_status_message (view->book_view, ldap_err2string(ldap_err));
		return TRUE; /* act synchronous in this case */
	}
	else {
		view->search_timeout = g_timeout_add (LDAP_POLL_INTERVAL,
						      (GSourceFunc) poll_ldap,
						      search_op);
	}

	/* we're async */
	return FALSE;
}

static void
ldap_search_dtor (PASBackend *backend, LDAPOp *op)
{
	LDAPSearchOp *search_op = (LDAPSearchOp*) op;

	g_free (search_op->ldap_query);
	g_free (search_op);
}

static void
pas_backend_ad_search (PASBackendAD  	*bl,
		       PASBook         	*book,
		       PASBackendADBookView *view)
{
	char *ldap_query;
	LDAPSearchOp *op;
	int result;

	result = pas_backend_ad_build_query(bl, view->search, &ldap_query);

	if (ldap_query == NULL) {
		pas_book_view_notify_complete (view->book_view, result);
		return;
	}

	op = g_new (LDAPSearchOp, 1);

	ldap_op_init ((LDAPOp*)op, PAS_BACKEND(bl), book, view->book_view, ldap_search_handler, ldap_search_dtor);

	op->ldap_query = ldap_query;
	op->view = view;
	op->bl = bl;

	/* keep track of the search op so we can delete it from the
           list if the view is destroyed */
	view->search_op = (LDAPOp*)op;

	ldap_op_process ((LDAPOp*)op);
}

static void
ad_get_view (PASBackend *backend,
	     PASBook    *book,
	     const char *search,
	     GNOME_Evolution_Addressbook_BookViewListener listener)
{
	PASBackendAD *bl = PAS_BACKEND_AD (backend);
	PASBookView       *book_view;
	PASBackendADBookView *view;

	book_view = pas_book_view_new (listener);

	bonobo_object_ref(BONOBO_OBJECT(book));
	g_signal_connect (book_view, "destroy",
			  G_CALLBACK (view_destroy), book);

	view = g_new0(PASBackendADBookView, 1);
	view->book_view = book_view;
	view->search = g_strdup (search);
	view->card_sexp = pas_backend_card_sexp_new (view->search);
	view->blpriv = bl->priv;

	e_list_append(bl->priv->book_views, view);

	pas_book_respond_get_book_view (book,
		(book_view != NULL
		 ? GNOME_Evolution_Addressbook_BookListener_Success 
		 : GNOME_Evolution_Addressbook_BookListener_CardNotFound /* XXX */),
		book_view);

	pas_backend_ad_search (bl, book, view);

	bonobo_object_unref (BONOBO_OBJECT (book_view));
}

static void
pas_backend_ad_process_get_book_view (PASBackend *backend,
				      PASBook    *book,
				      PASGetBookViewRequest *req)
{
	ad_get_view (backend, book, req->search, req->listener);
}

static void
pas_backend_ad_process_get_completion_view (PASBackend *backend,
					    PASBook    *book,
					    PASGetCompletionViewRequest *req)
{
	ad_get_view (backend, book, req->search, req->listener); 
}

static void
pas_backend_ad_process_get_changes (PASBackend *backend,
				    PASBook    *book,
				    PASGetChangesRequest *req)
{
	/* FIXME: return an error */
}

static void
pas_backend_ad_process_check_connection (PASBackend *backend,
					 PASBook    *book,
					 PASCheckConnectionRequest *req)
{
	PASBackendAD *bl = PAS_BACKEND_AD (backend);

	pas_book_report_connection (book, bl->priv->connected);
}

static void
pas_backend_ad_process_authenticate_user (PASBackend *backend,
					  PASBook    *book,
					  PASAuthenticateUserRequest *req)
{
	pas_book_respond_authenticate_user (book, GNOME_Evolution_Addressbook_BookListener_Success);
}

static void
pas_backend_ad_process_get_supported_fields (PASBackend *backend,
					     PASBook    *book,
					     PASGetSupportedFieldsRequest *req)

{
	PASBackendAD *bl = PAS_BACKEND_AD (backend);

	pas_book_respond_get_supported_fields (book,
					       GNOME_Evolution_Addressbook_BookListener_Success,
					       bl->priv->supported_fields);
}

static GNOME_Evolution_Addressbook_BookListener_CallStatus
pas_backend_ad_load_uri (PASBackend             *backend,
			 const char             *uri)
{
	PASBackendAD *bl = PAS_BACKEND_AD (backend);
	const char *host;

	g_assert (bl->priv->connected == FALSE);

	host = uri + sizeof ("activedirectory://") - 1;
	if (strncmp (uri, "activedirectory://", host - uri))
		return FALSE;

	bl->priv->ad_uri = g_strdup(uri);
	return pas_backend_ad_connect (bl);
}

/* Get_uri handler for the addressbook LDAP backend */
static const char *
pas_backend_ad_get_uri (PASBackend *backend)
{
	PASBackendAD *bl;

	bl = PAS_BACKEND_AD (backend);
	return bl->priv->ad_uri;
}

static char *
pas_backend_ad_get_static_capabilites (PASBackend *backend)
{
	return g_strdup("net");
}

static gboolean
pas_backend_ad_construct (PASBackendAD *backend)
{
	g_assert (backend != NULL);
	g_assert (PAS_IS_BACKEND_AD (backend));

	if (! pas_backend_construct (PAS_BACKEND (backend)))
		return FALSE;

	return TRUE;
}

/**
 * pas_backend_ad_new:
 */
PASBackend *
pas_backend_ad_new (void)
{
	PASBackendAD *backend;

#ifdef DEBUG
	char *e2k_debug = getenv ("E2K_DEBUG");

	if (e2k_debug && strchr (e2k_debug, 'g'))
		debug = TRUE;
#endif

	backend = g_object_new (pas_backend_ad_get_type (), NULL);

	if (! pas_backend_ad_construct (backend)) {
		g_object_unref (backend);

		return NULL;
	}

	return PAS_BACKEND (backend);
}

static void
call_dtor (LDAPOp *op, gpointer data)
{
	op->dtor (op->backend, op);
}

static void
pas_backend_ad_dispose (GObject *object)
{
	PASBackendAD *bl;

	bl = PAS_BACKEND_AD (object);

	if (bl->priv) {
		g_list_foreach (bl->priv->pending_ops, (GFunc)call_dtor, NULL);
		g_list_free (bl->priv->pending_ops);

		g_object_unref (bl->priv->book_views);
		g_object_unref (bl->priv->supported_fields);
		g_free (bl->priv->search_attrs);

		if (bl->priv->gc)
			g_object_unref (bl->priv->gc);

		if (bl->priv->op_idle)
			g_source_remove (bl->priv->op_idle);

		g_free (bl->priv);
		bl->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
pas_backend_ad_class_init (PASBackendADClass *klass)
{
	GObjectClass  *object_class = (GObjectClass *) klass;
	PASBackendClass *pas_backend_class;

	parent_class = g_type_class_ref (pas_backend_get_type ());

	pas_backend_class = PAS_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	pas_backend_class->load_uri                = pas_backend_ad_load_uri;
	pas_backend_class->get_uri                 = pas_backend_ad_get_uri;
	pas_backend_class->get_static_capabilities = pas_backend_ad_get_static_capabilites;

	pas_backend_class->create_card             = pas_backend_ad_process_create_card;
	pas_backend_class->remove_cards            = pas_backend_ad_process_remove_cards;
	pas_backend_class->modify_card             = pas_backend_ad_process_modify_card;
	pas_backend_class->check_connection        = pas_backend_ad_process_check_connection;
	pas_backend_class->get_vcard               = pas_backend_ad_process_get_vcard;
	pas_backend_class->get_cursor              = pas_backend_ad_process_get_cursor;
	pas_backend_class->get_book_view           = pas_backend_ad_process_get_book_view;
	pas_backend_class->get_completion_view     = pas_backend_ad_process_get_completion_view;
	pas_backend_class->get_changes             = pas_backend_ad_process_get_changes;
	pas_backend_class->authenticate_user       = pas_backend_ad_process_authenticate_user;
	pas_backend_class->get_supported_fields    = pas_backend_ad_process_get_supported_fields;

	object_class->dispose = pas_backend_ad_dispose;
}

static void
pas_backend_ad_init (PASBackendAD *backend)
{
	PASBackendADPrivate *priv;
	int i;

	priv = g_new0 (PASBackendADPrivate, 1);

	priv->supported_fields = e_list_new (NULL, NULL, NULL);
	for (i = 0; i < num_prop_infos; i ++) {
		e_list_append (priv->supported_fields, prop_info[i].query_prop);
	}
	e_list_append (priv->supported_fields, "file_as");

	priv->search_attrs = g_new (char*, num_prop_infos+1);
	for (i = 0; i < num_prop_infos; i ++) {
		priv->search_attrs[i] = prop_info[i].ldap_attr;
	}
	priv->search_attrs[num_prop_infos] = NULL;

	priv->book_views = e_list_new (NULL, NULL, NULL);

	backend->priv = priv;
}

E2K_MAKE_TYPE (pas_backend_ad, PASBackendAD, pas_backend_ad_class_init, pas_backend_ad_init, PARENT_TYPE)
