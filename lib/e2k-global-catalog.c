/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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
#include "config.h"
#endif

#include "e2k-global-catalog.h"
#include "e2k-sid.h"
#include "e2k-utils.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <ldap.h>

#ifdef HAVE_LDAP_NTLM_BIND
#include "xntlm.h"
#endif

#ifdef E2K_DEBUG
static gboolean e2k_gc_debug = FALSE;
#define E2K_GC_DEBUG_MSG(x) if (e2k_gc_debug) printf x
#else
#define E2K_GC_DEBUG_MSG(x)
#endif

struct _E2kGlobalCatalogPrivate {
	LDAP *ldap;

	GPtrArray *entries;
	GHashTable *entry_cache, *server_cache;

	char *server, *user, *domain, *password;
};

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void dispose (GObject *);

static void
class_init (GObjectClass *object_class)
{
#ifdef E2K_DEBUG
	char *e2k_debug = getenv ("E2K_DEBUG");

	if (e2k_debug && atoi (e2k_debug) > 3)
		e2k_gc_debug = TRUE;
#endif

	/* For some reason, sasl_client_init (called by ldap_init
	 * below) takes a *really* long time to scan the sasl modules
	 * when running under gdb. We're not using sasl anyway, so...
	 */
	putenv("SASL_PATH=");

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
}

static void
init (GObject *object)
{
	E2kGlobalCatalog *gc = E2K_GLOBAL_CATALOG (object);

	gc->priv = g_new0 (E2kGlobalCatalogPrivate, 1);
	gc->priv->entries = g_ptr_array_new ();
	gc->priv->entry_cache = g_hash_table_new (e2k_ascii_strcase_hash,
						  e2k_ascii_strcase_equal);
	gc->priv->server_cache = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
free_entry (E2kGlobalCatalogEntry *entry)
{
	int i;

	g_free (entry->dn);
	g_free (entry->display_name);

	if (entry->sid)
		g_object_unref (entry->sid);

	g_free (entry->email);
	g_free (entry->mailbox);

	if (entry->delegates) {
		for (i = 0; i < entry->delegates->len; i++)
			g_free (entry->delegates->pdata[i]);
		g_ptr_array_free (entry->delegates, TRUE);
	}
	if (entry->delegators) {
		for (i = 0; i < entry->delegators->len; i++)
			g_free (entry->delegators->pdata[i]);
		g_ptr_array_free (entry->delegators, TRUE);
	}

	g_free (entry);
}

static void
free_server (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
	g_free (value);
}

static void
dispose (GObject *object)
{
	E2kGlobalCatalog *gc = E2K_GLOBAL_CATALOG (object);
	int i;

	if (gc->priv) {
		if (gc->priv->ldap)
			ldap_unbind (gc->priv->ldap);

		for (i = 0; i < gc->priv->entries->len; i++)
			free_entry (gc->priv->entries->pdata[i]);
		g_ptr_array_free (gc->priv->entries, TRUE);

		g_hash_table_foreach (gc->priv->server_cache, free_server, NULL);
		g_hash_table_destroy (gc->priv->server_cache);

		if (gc->priv->server)
			g_free (gc->priv->server);
		if (gc->priv->user)
			g_free (gc->priv->user);
		if (gc->priv->domain)
			g_free (gc->priv->domain);
		if (gc->priv->password) {
			memset (gc->priv->password, 0, strlen (gc->priv->password));
			g_free (gc->priv->password);
		}

		g_free (gc->priv);
		gc->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}


E2K_MAKE_TYPE (e2k_global_catalog, E2kGlobalCatalog, class_init, init, PARENT_TYPE)


#ifdef HAVE_LDAP_NTLM_BIND
static int
ntlm_bind (LDAP *ldap, const char *user, const char *domain, const char *password)
{
	LDAPMessage *msg;
	int ldap_error, msgid, err;
	char *nonce, *default_domain;
	GByteArray *ba;
	struct berval ldap_buf;

	/* Create and send NTLM request */
	ba = xntlm_negotiate ();
	ldap_buf.bv_len = ba->len;
	ldap_buf.bv_val = ba->data;
	ldap_error = ldap_ntlm_bind (ldap, "NTLM", LDAP_AUTH_NTLM_REQUEST,
				     &ldap_buf, NULL, NULL, &msgid);
	g_byte_array_free (ba, TRUE);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Failure sending first NTLM bind message: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	/* Extract challenge */
	if (ldap_result (ldap, msgid, 1, NULL, &msg) == -1) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse first NTLM bind response\n"));
		return -1;
	}
	ldap_error = ldap_parse_ntlm_bind_result (ldap, msg, &ldap_buf);
	ldap_msgfree (msg);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse NTLM bind response: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	if (!xntlm_parse_challenge (ldap_buf.bv_val, ldap_buf.bv_len,
				    &nonce, &default_domain, NULL)) {
		E2K_GC_DEBUG_MSG(("GC: Could not find nonce in NTLM bind response\n"));
		ber_memfree (ldap_buf.bv_val);

		return -1;
	}
	ber_memfree (ldap_buf.bv_val);

	/* Create and send response */
	ba = xntlm_authenticate (nonce, domain ? domain : default_domain,
				 user, password, NULL);
	ldap_buf.bv_len = ba->len;
	ldap_buf.bv_val = ba->data;
	ldap_error = ldap_ntlm_bind (ldap, "NTLM", LDAP_AUTH_NTLM_RESPONSE,
				     &ldap_buf, NULL, NULL, &msgid);
	g_byte_array_free (ba, TRUE);
	g_free (nonce);
	g_free (default_domain);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Failure sending second NTLM bind message: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	/* And get the final result */
	if (ldap_result (ldap, msgid, 1, NULL, &msg) == -1) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse second NTLM bind response\n"));
		return -1;
	}
	ldap_error = ldap_parse_result (ldap, msg, &err, NULL, NULL,
					NULL, NULL, TRUE);
	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: Could not parse second NTLM bind response: 0x%02x\n", ldap_error));
		return ldap_error;
	}

	return LDAP_SUCCESS;
}
#endif

static LDAP *
get_ldap_connection (E2kGlobalCatalog *gc, const char *server, int port)
{
	int ldap_opt, ldap_error;
	LDAP *ldap;
#ifndef HAVE_LDAP_NTLM_BIND
	char *nt_name;
#endif

	E2K_GC_DEBUG_MSG(("\nGC: Connecting to ldap://%s:%d/\n", server, port));
	ldap = ldap_init (server, port);
	if (!ldap) {
		E2K_GC_DEBUG_MSG(("GC: failed\n\n"));
		g_warning ("Could not connect to ldap://%s:%d/",
			   server, port);
		return FALSE;
	}

	/* Set options */
	ldap_opt = LDAP_DEREF_ALWAYS;
	ldap_set_option (ldap, LDAP_OPT_DEREF, &ldap_opt);
	ldap_opt = gc->response_limit;
	ldap_set_option (ldap, LDAP_OPT_SIZELIMIT, &ldap_opt);
	ldap_opt = LDAP_VERSION3;
	ldap_set_option (ldap, LDAP_OPT_PROTOCOL_VERSION, &ldap_opt);

	/* authenticate */
#ifdef HAVE_LDAP_NTLM_BIND
	ldap_error = ntlm_bind (ldap, gc->priv->user, gc->priv->domain,
				gc->priv->password);
#else
	nt_name = gc->priv->domain ?
		g_strdup_printf ("%s\\%s", gc->priv->domain, gc->priv->user) :
		g_strdup (gc->priv->user);
	ldap_error = ldap_simple_bind_s (ldap, nt_name, gc->priv->password);
	g_free (nt_name);
#endif
	if (ldap_error != LDAP_SUCCESS) {
		ldap_unbind (ldap);
		ldap = NULL;
		g_warning ("LDAP authentication failed (0x%02x)", ldap_error);
	} else {
		E2K_GC_DEBUG_MSG(("GC: connected\n\n"));
	}

	return ldap;
}

/**
 * e2k_global_catalog_get_ldap:
 * @gc: the global catalog
 *
 * Connects or reconnects @gc if needed, and returns a copy of its
 * LDAP handle. The caller must not unbind the LDAP handle, and should
 * keep a reference on @gc if it wants to ensure that the handle sticks
 * around.
 *
 * Return value: an LDAP handle, or %NULL if it can't connect
 **/
gpointer
e2k_global_catalog_get_ldap (E2kGlobalCatalog *gc)
{
	int err;

	g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), NULL);

	if (gc->priv->ldap) {
		ldap_get_option (gc->priv->ldap, LDAP_OPT_ERROR_NUMBER, &err);
		if (err == LDAP_SERVER_DOWN) {
			ldap_unbind (gc->priv->ldap);
			gc->priv->ldap = NULL;
		}
	}

	if (!gc->priv->ldap)
		gc->priv->ldap = get_ldap_connection (gc, gc->priv->server, 3268);
	return gc->priv->ldap;
}

/**
 * e2k_global_catalog_new:
 * @server: the GC server name
 * @response_limit: the maximum number of responses to return from a search
 * @user: username to authenticate with
 * @domain: NT domain of @user, or %NULL to autodetect.
 * @password: password to authenticate with
 *
 * Create an object for communicating with the Windows Global Catalog
 * via LDAP.
 *
 * Return value: the new E2kGlobalCatalog. (This call will always succeed.
 * If the passed-in data is bad, it will fail on a later call.)
 **/
E2kGlobalCatalog *
e2k_global_catalog_new (const char *server, int response_limit,
			const char *user, const char *domain,
			const char *password)
{
	E2kGlobalCatalog *gc;

	gc = g_object_new (E2K_TYPE_GLOBAL_CATALOG, NULL);
	gc->priv->server = g_strdup (server);
	gc->priv->user = g_strdup (user);
	gc->priv->domain = g_strdup (domain);
	gc->priv->password = g_strdup (password);
	gc->response_limit = response_limit;

	return gc;
}

static const char *
lookup_mta (E2kGlobalCatalog *gc, const char *mta_dn)
{
	char *hostname, *attrs[2], **values;
	LDAPMessage *resp;
	int ldap_error, i;

	/* Skip over "CN=Microsoft MTA," */
	mta_dn = strchr (mta_dn, ',');
	if (!mta_dn)
		return NULL;
	mta_dn++;

	hostname = g_hash_table_lookup (gc->priv->server_cache, mta_dn);
	if (hostname)
		return hostname;

	E2K_GC_DEBUG_MSG(("GC:   Finding hostname for %s\n", mta_dn));

	attrs[0] = "networkAddress";
	attrs[1] = NULL;

	ldap_error = LDAP_SERVER_DOWN;
	while (ldap_error == LDAP_SERVER_DOWN &&
	       e2k_global_catalog_get_ldap (gc)) {
		ldap_error = ldap_search_ext_s (gc->priv->ldap, mta_dn,
						LDAP_SCOPE_BASE,
						NULL, attrs, 0, NULL,
						NULL, NULL, 0, &resp);
	}

	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC:   lookup failed (0x%02x)\n", ldap_error));
		return NULL;
	}

	values = ldap_get_values (gc->priv->ldap, resp, "networkAddress");
	ldap_msgfree (resp);
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC:   entry has no networkAddress\n"));
		return NULL;
	}

	hostname = NULL;
	for (i = 0; values[i]; i++) {
		if (strstr (values[i], "_tcp")) {
			hostname = strchr (values[i], ':');
			break;
		}
	}
	if (!hostname) {
		E2K_GC_DEBUG_MSG(("GC:   host is not availble by TCP?\n"));
		ldap_value_free (values);
		return NULL;
	}

	hostname = g_strdup (hostname + 1);
	g_hash_table_insert (gc->priv->server_cache, g_strdup (mta_dn), hostname);
	ldap_value_free (values);

	E2K_GC_DEBUG_MSG(("GC:   %s\n", hostname));
	return hostname;
}

struct lookup_data {
	E2kGlobalCatalog *gc;
	int msgid, timeout_id;

	char *base, *filter;
	int scope;
	GPtrArray *attrs;

	guint32 want_flags, found_flags;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;

	E2kGlobalCatalogCallback callback;
	gpointer user_data;
};

static void
get_sid_values (struct lookup_data *ld, LDAPMessage *msg)
{
	char **values;
	struct berval **bsid_values;
	E2kSidType type;

	values = ldap_get_values (ld->gc->priv->ldap, msg, "displayName");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: displayName %s\n", values[0]));
		ld->entry->display_name = g_strdup (values[0]);
		ldap_value_free (values);
	}

	bsid_values = ldap_get_values_len (ld->gc->priv->ldap, msg, "objectSid");
	if (!bsid_values)
		return;
	if (bsid_values[0]->bv_len < 2 ||
	    bsid_values[0]->bv_len != E2K_SID_BINARY_SID_LEN (bsid_values[0]->bv_val)) {
		E2K_GC_DEBUG_MSG(("GC: invalid SID\n"));
		return;
	}

	values = ldap_get_values (ld->gc->priv->ldap, msg, "objectCategory");
	if (values && values[0] && !g_ascii_strncasecmp (values[0], "CN=Group", 8))
		type = E2K_SID_TYPE_GROUP;
	else if (values && values[0] && !g_ascii_strncasecmp (values[0], "CN=Foreign", 10))
		type = E2K_SID_TYPE_WELL_KNOWN_GROUP;
	else /* FIXME? */
		type = E2K_SID_TYPE_USER;
	if (values)
		ldap_value_free (values);

	ld->entry->sid = e2k_sid_new_from_binary_sid (
		type, bsid_values[0]->bv_val, ld->entry->display_name);
	ld->found_flags |= E2K_GLOBAL_CATALOG_LOOKUP_SID;

	ldap_value_free_len (bsid_values);
}

static void
get_mail_values (struct lookup_data *ld, LDAPMessage *msg)
{
	char **values, **mtavalues;

	values = ldap_get_values (ld->gc->priv->ldap, msg, "mail");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: mail %s\n", values[0]));
		ld->entry->email = g_strdup (values[0]);
		g_hash_table_insert (ld->gc->priv->entry_cache,
				     ld->entry->email, ld->entry);
		ld->found_flags |= E2K_GLOBAL_CATALOG_LOOKUP_EMAIL;
		ldap_value_free (values);
	}

	values = ldap_get_values (ld->gc->priv->ldap, msg, "mailNickname");
	mtavalues = ldap_get_values (ld->gc->priv->ldap, msg, "homeMTA");
	if (values && mtavalues) {
		E2K_GC_DEBUG_MSG(("GC: mailNickname %s\n", values[0]));
		E2K_GC_DEBUG_MSG(("GC: homeMTA %s\n", mtavalues[0]));
		ld->entry->exchange_server = (char *)lookup_mta (ld->gc, mtavalues[0]);
		ldap_value_free (mtavalues);
		if (ld->entry->exchange_server)
			ld->entry->mailbox = g_strdup (values[0]);
		ldap_value_free (values);
		ld->found_flags |= E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX;
	}

	values = ldap_get_values (ld->gc->priv->ldap, msg, "legacyExchangeDN");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: legacyExchangeDN %s\n", values[0]));
		ld->entry->legacy_exchange_dn = g_strdup (values[0]);
		g_hash_table_insert (ld->gc->priv->entry_cache,
				     ld->entry->legacy_exchange_dn,
				     ld->entry);
		ld->found_flags |= E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN;
		ldap_value_free (values);
	}
}

static void
get_delegation_values (struct lookup_data *ld, LDAPMessage *msg)
{
	char **values;
	int i;

	values = ldap_get_values (ld->gc->priv->ldap, msg, "publicDelegates");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: publicDelegates\n"));
		ld->entry->delegates = g_ptr_array_new ();
		for (i = 0; values[i]; i++) {
			E2K_GC_DEBUG_MSG(("GC:   %s\n", values[i]));
			g_ptr_array_add (ld->entry->delegates,
					 g_strdup (values[i]));
		}
		ld->found_flags |= E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES;
		ldap_value_free (values);
	}
	values = ldap_get_values (ld->gc->priv->ldap, msg, "publicDelegatesBL");
	if (values) {
		E2K_GC_DEBUG_MSG(("GC: publicDelegatesBL\n"));
		ld->entry->delegators = g_ptr_array_new ();
		for (i = 0; values[i]; i++) {
			E2K_GC_DEBUG_MSG(("GC:   %s\n", values[i]));
			g_ptr_array_add (ld->entry->delegators,
					 g_strdup (values[i]));
		}
		ld->found_flags |= E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS;
		ldap_value_free (values);
	}
}

static void
finish_lookup (struct lookup_data *ld, E2kGlobalCatalogStatus status)
{
	g_free (ld->base);
	g_free (ld->filter);
	g_ptr_array_free (ld->attrs, TRUE);

	if (status != E2K_GLOBAL_CATALOG_OK) {
		if (!ld->entry->dn)
			g_free (ld->entry);
		ld->entry = NULL;
	}

	ld->status = status;

	if (ld->callback) {
		ld->callback (ld->gc, ld->status, ld->entry, ld->user_data);
		g_object_unref (ld->gc);
		g_free (ld);
	}
}

static struct timeval zero_tv;
static void queue_lookup (struct lookup_data *ld);

static gboolean
lookup_poll (gpointer user_data)
{
	struct lookup_data *ld = user_data;
	LDAPMessage *msg, *entry;
	int ldap_error;
	char *dn;

	ldap_error = ldap_result (ld->gc->priv->ldap, ld->msgid, TRUE,
				  &zero_tv, &msg);
	if (ldap_error == 0)
		return TRUE;

	if (ldap_error == -1) {
		if (ldap_get_option (ld->gc->priv->ldap, LDAP_OPT_ERROR_NUMBER, &ldap_error) == LDAP_OPT_SUCCESS &&
		    ldap_error == LDAP_SERVER_DOWN) {
			E2K_GC_DEBUG_MSG(("GC: server disconnected while polling. Trying again\n\n"));
			queue_lookup (ld);
			return FALSE;
		}

		E2K_GC_DEBUG_MSG(("GC: ldap error while polling\n\n"));
		finish_lookup (ld, E2K_GLOBAL_CATALOG_ERROR);
		return FALSE;
	}

	entry = ldap_first_entry (ld->gc->priv->ldap, msg);
	if (!entry) {
		E2K_GC_DEBUG_MSG(("GC: no such user\n\n"));
		finish_lookup (ld, E2K_GLOBAL_CATALOG_NO_SUCH_USER);
		ldap_msgfree (msg);
		return FALSE;
	}

	if (!ld->entry->dn) {
		dn = ldap_get_dn (ld->gc->priv->ldap, entry);
		ld->entry->dn = g_strdup (dn);
		ldap_memfree (dn);
		g_ptr_array_add (ld->gc->priv->entries, ld->entry);
		g_hash_table_insert (ld->gc->priv->entry_cache,
				     ld->entry->dn, ld->entry);
	}

	get_sid_values (ld, entry);
	get_mail_values (ld, entry);
	get_delegation_values (ld, entry);
	ldap_msgfree (msg);

	if (ld->want_flags && !ld->found_flags) {
		E2K_GC_DEBUG_MSG(("GC: no data\n\n"));
		finish_lookup (ld, E2K_GLOBAL_CATALOG_NO_DATA);
		return FALSE;
	}

	E2K_GC_DEBUG_MSG(("\n"));
	finish_lookup (ld, E2K_GLOBAL_CATALOG_OK);
	return FALSE;
}

static void
queue_lookup (struct lookup_data *ld)
{
	int ldap_error, msgid;

	if (!ld->attrs) {
		finish_lookup (ld, E2K_GLOBAL_CATALOG_OK);
		return;
	}

	ldap_error = LDAP_SERVER_DOWN;
	while (ldap_error == LDAP_SERVER_DOWN &&
	       e2k_global_catalog_get_ldap (ld->gc)) {
		ldap_error = ldap_search_ext (ld->gc->priv->ldap, ld->base,
					      ld->scope, ld->filter,
					      (char **)ld->attrs->pdata,
					      FALSE, NULL, NULL, NULL, 0,
					      &msgid);
	}

	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC: ldap_search failed: 0x%02x\n\n", ldap_error));
		finish_lookup (ld, E2K_GLOBAL_CATALOG_ERROR);
		return;
	}

	ld->msgid = msgid;
	ld->timeout_id = g_timeout_add (250, lookup_poll, ld);
}

static struct lookup_data *
setup_lookup_data (E2kGlobalCatalog *gc, E2kGlobalCatalogLookupType type,
		   const char *key, guint32 lookup_flags,
		   E2kGlobalCatalogCallback callback, gpointer user_data)
{
	struct lookup_data *ld;

	E2K_GC_DEBUG_MSG(("\nGC: looking up info for %s\n", key));

	ld = g_new0 (struct lookup_data, 1);
	ld->gc = gc;
	if (callback) {
		g_object_ref (ld->gc);
		ld->callback = callback;
		ld->user_data = user_data;
	}

	ld->entry = g_hash_table_lookup (gc->priv->entry_cache, key);
	if (!ld->entry)
		ld->entry = g_new0 (E2kGlobalCatalogEntry, 1);

	switch (type) {
	case E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL:
		ld->filter = g_strdup_printf ("(mail=%s)", key);
		ld->base = g_strdup (LDAP_ROOT_DSE);
		ld->scope = LDAP_SCOPE_SUBTREE;
		break;

	case E2K_GLOBAL_CATALOG_LOOKUP_BY_DN:
		ld->filter = NULL;
		ld->base = g_strdup (key);
		ld->scope = LDAP_SCOPE_BASE;
		break;

	case E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN:
		ld->filter = g_strdup_printf ("(legacyExchangeDN=%s)", key);
		ld->base = g_strdup (LDAP_ROOT_DSE);
		ld->scope = LDAP_SCOPE_SUBTREE;
		break;
	}

	ld->attrs = g_ptr_array_new ();
	if (!ld->entry->display_name)
		g_ptr_array_add (ld->attrs, "displayName");
	if (!ld->entry->email) {
		g_ptr_array_add (ld->attrs, "mail");
		if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_EMAIL)
			ld->want_flags |= E2K_GLOBAL_CATALOG_LOOKUP_EMAIL;
	}
	if (!ld->entry->legacy_exchange_dn) {
		g_ptr_array_add (ld->attrs, "legacyExchangeDN");
		if (lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN)
			ld->want_flags |= E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN;
	}

	if ((lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_SID) && !ld->entry->sid) {
		g_ptr_array_add (ld->attrs, "objectSid");
		g_ptr_array_add (ld->attrs, "objectCategory");
		ld->want_flags |= E2K_GLOBAL_CATALOG_LOOKUP_SID;
	}
	if ((lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX) && !ld->entry->mailbox) {
		g_ptr_array_add (ld->attrs, "mailNickname");
		g_ptr_array_add (ld->attrs, "homeMTA");
		ld->want_flags |= E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX;
	}
	if ((lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES) &&
	    !ld->entry->delegates)
		g_ptr_array_add (ld->attrs, "publicDelegates");
	if ((lookup_flags & E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS) &&
	    !ld->entry->delegators)
		g_ptr_array_add (ld->attrs, "publicDelegatesBL");
	g_ptr_array_add (ld->attrs, NULL);

	return ld;
}

/**
 * e2k_global_catalog_async_lookup:
 * @gc: the global catalog
 * @type: the type of information in @key
 * @key: email address or DN to look up
 * @lookup_flags: the information to look up
 * @callback: the callback to invoke after finding the user
 * @user_data: data to pass to callback
 *
 * Asynchronously look up the indicated user in the global catalog and
 * return the requested information to the callback.
 *
 * Return value: a cookie that can be passed to
 * e2k_global_catalog_cancel_lookup()
 **/
E2kGlobalCatalogLookupId
e2k_global_catalog_async_lookup (E2kGlobalCatalog *gc,
				 E2kGlobalCatalogLookupType type,
				 const char *key, guint32 lookup_flags,
				 E2kGlobalCatalogCallback callback,
				 gpointer user_data)
{
	struct lookup_data *ld;

	if (!E2K_IS_GLOBAL_CATALOG (gc) || !key) {
		callback (gc, E2K_GLOBAL_CATALOG_ERROR, NULL, user_data);
		g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), NULL);
		g_return_val_if_fail (key != NULL, NULL);
	}

	ld = setup_lookup_data (gc, type, key, lookup_flags,
				callback, user_data);
	queue_lookup (ld);
	return ld;
}

/**
 * e2k_global_catalog_cancel_lookup:
 * @gc: the global catalog
 * @lookup_id: the return value from the e2k_global_catalog_async_lookup()
 * call to cancel
 *
 * This cancels the lookup identified by @lookup_id, which must be an
 * active lookup. The callback will not be called.
 **/
void
e2k_global_catalog_cancel_lookup (E2kGlobalCatalog *gc,
				  E2kGlobalCatalogLookupId lookup_id)
{
	struct lookup_data *ld = lookup_id;

	g_source_remove (ld->timeout_id);
	ldap_abandon (gc->priv->ldap, ld->msgid);

	ld->callback = NULL;
	finish_lookup (ld, -1);
	g_object_unref (ld->gc);
	g_free (ld);
}

/**
 * e2k_global_catalog_lookup:
 * @gc: the global catalog
 * @type: the type of information in @key
 * @key: email address or DN to look up
 * @lookup_flags: the information to look up
 * @entry_p: pointer to a variable to return the entry in.
 *
 * Synchronously look up the indicated user in the global catalog and
 * return their information in *@entry_p.
 *
 * Return value: the status of the lookup
 **/
E2kGlobalCatalogStatus
e2k_global_catalog_lookup (E2kGlobalCatalog *gc,
			   E2kGlobalCatalogLookupType type,
			   const char *key, guint32 lookup_flags,
			   E2kGlobalCatalogEntry **entry_p)
{
	struct lookup_data *ld;
	E2kGlobalCatalogStatus status;

	g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), E2K_GLOBAL_CATALOG_ERROR);
	g_return_val_if_fail (key != NULL, E2K_GLOBAL_CATALOG_ERROR);

	ld = setup_lookup_data (gc, type, key, lookup_flags, NULL, NULL);
	ld->status = -1;
	queue_lookup (ld);
	while (ld->status == -1)
		g_main_context_iteration (NULL, TRUE);

	*entry_p = ld->entry;
	status = ld->status;

	g_free (ld);
	return status;
}


static const char *
lookup_controlling_ad_server (E2kGlobalCatalog *gc, const char *dn)
{
	char *hostname, *attrs[2], **values, *ad_dn;
	LDAPMessage *resp;
	int ldap_error;

	while (g_ascii_strncasecmp (dn, "DC=", 3) != 0) {
		dn = strchr (dn, ',');
		if (!dn)
			return NULL;
		dn++;
	}

	hostname = g_hash_table_lookup (gc->priv->server_cache, dn);
	if (hostname)
		return hostname;

	E2K_GC_DEBUG_MSG(("GC:   Finding AD server for %s\n", dn));

	attrs[0] = "masteredBy";
	attrs[1] = NULL;

	ldap_error = LDAP_SERVER_DOWN;
	while (ldap_error == LDAP_SERVER_DOWN &&
	       e2k_global_catalog_get_ldap (gc)) {
		ldap_error = ldap_search_ext_s (gc->priv->ldap, dn,
						LDAP_SCOPE_BASE,
						NULL, attrs, 0, NULL,
						NULL, NULL, 0, &resp);
	}

	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC:   ldap_search failed: 0x%02x\n", ldap_error));
		return NULL;
	}

	values = ldap_get_values (gc->priv->ldap, resp, "masteredBy");
	ldap_msgfree (resp);
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC:   no known AD server\n\n"));
		return NULL;
	}

	/* Skip over "CN=NTDS Settings," */
	ad_dn = strchr (values[0], ',');
	if (!ad_dn) {
		E2K_GC_DEBUG_MSG(("GC:   bad dn %s\n\n", values[0]));
		ldap_value_free (values);
		return NULL;
	}
	ad_dn++;

	attrs[0] = "dNSHostName";
	attrs[1] = NULL;

	ldap_error = LDAP_SERVER_DOWN;
	while (ldap_error == LDAP_SERVER_DOWN &&
	       e2k_global_catalog_get_ldap (gc)) {
		ldap_error = ldap_search_ext_s (gc->priv->ldap, ad_dn,
						LDAP_SCOPE_BASE,
						NULL, attrs, 0, NULL,
						NULL, NULL, 0, &resp);
	}
	ldap_value_free (values);

	if (ldap_error != LDAP_SUCCESS) {
		E2K_GC_DEBUG_MSG(("GC:   ldap_search failed: 0x%02x\n\n", ldap_error));
		return NULL;
	}

	values = ldap_get_values (gc->priv->ldap, resp, "dNSHostName");
	ldap_msgfree (resp);
	if (!values) {
		E2K_GC_DEBUG_MSG(("GC:   entry has no dNSHostName\n\n"));
		return NULL;
	}

	hostname = g_strdup (values[0]);
	ldap_value_free (values);

	g_hash_table_insert (gc->priv->server_cache, g_strdup (dn), hostname);

	E2K_GC_DEBUG_MSG(("GC:   %s\n", hostname));
	return hostname;
}

static E2kGlobalCatalogStatus
do_delegate_op (E2kGlobalCatalog *gc, int op,
		const char *self_dn, const char *delegate_dn)
{
	LDAP *ldap;
	LDAPMod *mods[2], mod;
	const char *ad_server;
	char *values[2];
	int ldap_error;

	g_return_val_if_fail (E2K_IS_GLOBAL_CATALOG (gc), E2K_GLOBAL_CATALOG_ERROR);
	g_return_val_if_fail (self_dn != NULL, E2K_GLOBAL_CATALOG_ERROR);
	g_return_val_if_fail (delegate_dn != NULL, E2K_GLOBAL_CATALOG_ERROR);

	ad_server = lookup_controlling_ad_server (gc, self_dn);
	if (!ad_server)
		return E2K_GLOBAL_CATALOG_ERROR;

	ldap = get_ldap_connection (gc, ad_server, LDAP_PORT);
	if (!ldap)
		return E2K_GLOBAL_CATALOG_ERROR;

	mod.mod_op = op;
	mod.mod_type = "publicDelegates";
	mod.mod_values = values;
	values[0] = (char *)delegate_dn;
	values[1] = NULL;

	mods[0] = &mod;
	mods[1] = NULL;
	ldap_error = ldap_modify_ext_s (ldap, self_dn, mods, NULL, NULL);
	ldap_unbind (ldap);

	switch (ldap_error) {
	case LDAP_SUCCESS:
		E2K_GC_DEBUG_MSG(("\n"));
		return E2K_GLOBAL_CATALOG_OK;

	case LDAP_NO_SUCH_OBJECT:
		E2K_GC_DEBUG_MSG(("GC: no such user\n\n"));
		return E2K_GLOBAL_CATALOG_NO_SUCH_USER;

	case LDAP_NO_SUCH_ATTRIBUTE:
		E2K_GC_DEBUG_MSG(("GC: no such delegate\n\n"));
		return E2K_GLOBAL_CATALOG_NO_DATA;

	case LDAP_CONSTRAINT_VIOLATION:
		E2K_GC_DEBUG_MSG(("GC: bad delegate\n\n"));
		return E2K_GLOBAL_CATALOG_BAD_DATA;

	case LDAP_TYPE_OR_VALUE_EXISTS:
		E2K_GC_DEBUG_MSG(("GC: delegate already exists\n\n"));
		return E2K_GLOBAL_CATALOG_EXISTS;

	default:
		E2K_GC_DEBUG_MSG(("GC: ldap_modify failed: 0x%02x\n\n", ldap_error));
		return E2K_GLOBAL_CATALOG_ERROR;
	}
}

/**
 * e2k_global_catalog_add_delegate:
 * @gc: the global catalog
 * @self_dn: Active Directory DN of the user to add a delegate to
 * @delegate_dn: Active Directory DN of the new delegate
 *
 * Attempts to make @delegate_dn a delegate of @self_dn.
 *
 * Return value: %E2K_GLOBAL_CATALOG_OK on success,
 * %E2K_GLOBAL_CATALOG_NO_SUCH_USER if @self_dn is invalid,
 * %E2K_GLOBAL_CATALOG_BAD_DATA if @delegate_dn is invalid,
 * %E2K_GLOBAL_CATALOG_EXISTS if @delegate_dn is already a delegate,
 * %E2K_GLOBAL_CATALOG_ERROR on other errors.
 **/
E2kGlobalCatalogStatus
e2k_global_catalog_add_delegate (E2kGlobalCatalog *gc,
				 const char *self_dn,
				 const char *delegate_dn)
{
	E2K_GC_DEBUG_MSG(("\nGC: adding %s as delegate for %s\n", delegate_dn, self_dn));

	return do_delegate_op (gc, LDAP_MOD_ADD, self_dn, delegate_dn);
}

/**
 * e2k_global_catalog_remove_delegate:
 * @gc: the global catalog
 * @self_dn: Active Directory DN of the user to remove a delegate from
 * @delegate_dn: Active Directory DN of the delegate to remove
 *
 * Attempts to remove @delegate_dn as a delegate of @self_dn.
 *
 * Return value: %E2K_GLOBAL_CATALOG_OK on success,
 * %E2K_GLOBAL_CATALOG_NO_SUCH_USER if @self_dn is invalid,
 * %E2K_GLOBAL_CATALOG_NO_DATA if @delegate_dn is not a delegate of @self_dn,
 * %E2K_GLOBAL_CATALOG_ERROR on other errors.
 **/
E2kGlobalCatalogStatus
e2k_global_catalog_remove_delegate (E2kGlobalCatalog *gc,
				    const char *self_dn,
				    const char *delegate_dn)
{
	E2K_GC_DEBUG_MSG(("\nGC: removing %s as delegate for %s\n", delegate_dn, self_dn));

	return do_delegate_op (gc, LDAP_MOD_DELETE, self_dn, delegate_dn);
}
