/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* ExchangeAccount: Handles a single configured Connector account. This
 * is strictly a model object. ExchangeStorage handles the view.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-account.h"
#include "exchange-config-listener.h"
#include "exchange-hierarchy-webdav.h"
#include "exchange-hierarchy-foreign.h"
#include "exchange-hierarchy-gal.h"
#include "exchange-permissions-dialog.h"
#include "e-folder-exchange.h"
#include "e2k-autoconfig.h"
#include "e2k-marshal.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"

#include <e-util/e-dialog-utils.h>
#include <e-util/e-passwords.h>

#include <gal/util/e-util.h>
#include <libsoup/soup-ntlm.h>
#include <libgnome/gnome-util.h>

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

extern gboolean exchange_component_interactive, exchange_component_online;
extern char *evolution_dir;

struct _ExchangeAccountPrivate {
	E2kConnection *conn;
	E2kGlobalCatalog *gc;
	GHashTable *standard_uris;
	gboolean connected;

	GPtrArray *hierarchies;
	GHashTable *hierarchies_by_folder, *foreign_hierarchies;
	GHashTable *folders;
	char *uri_authority, *http_uri_schema;
	gboolean uris_use_email;

	char *identity_name, *identity_email, *source_uri;
	char *username, *windows_domain, *password, *ad_server;
	gboolean use_basic_auth, saw_ntlm;
	int ad_limit;

	EAccountList *account_list;
	EAccount *account;

	GList *discover_datas;
	GList *connect_datas;
};

enum {
	CONNECTED,
	NEW_FOLDER,
	REMOVED_FOLDER,
	UPDATED_FOLDER,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void dispose (GObject *);
static void finalize (GObject *);
static void remove_hierarchy (ExchangeAccount *account, ExchangeHierarchy *hier);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* signals */
	signals[CONNECTED] =
		g_signal_new ("connected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, connected),
			      NULL, NULL,
			      e2k_marshal_NONE__NONE,
			      G_TYPE_NONE, 0);
	signals[NEW_FOLDER] =
		g_signal_new ("new_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, new_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[REMOVED_FOLDER] =
		g_signal_new ("removed_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, removed_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[UPDATED_FOLDER] =
		g_signal_new ("updated_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeAccountClass, updated_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
init (GObject *object)
{
	ExchangeAccount *account = EXCHANGE_ACCOUNT (object);

	account->priv = g_new0 (ExchangeAccountPrivate, 1);
	account->priv->hierarchies = g_ptr_array_new ();
	account->priv->hierarchies_by_folder = g_hash_table_new (NULL, NULL);
	account->priv->foreign_hierarchies = g_hash_table_new (g_str_hash, g_str_equal);
	account->priv->folders = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
free_name (gpointer name, gpointer value, gpointer data)
{
	g_free (name);
}

static void
free_folder (gpointer key, gpointer folder, gpointer data)
{
	g_object_unref (folder);
}

static void
dispose (GObject *object)
{
	ExchangeAccount *account = EXCHANGE_ACCOUNT (object);
	int i;

	if (account->priv->account) {
		g_object_unref (account->priv->account);
		account->priv->account = NULL;
	}

	if (account->priv->account_list) {
		g_object_unref (account->priv->account_list);
		account->priv->account_list = NULL;
	}

	if (account->priv->conn) {
		g_object_unref (account->priv->conn);
		account->priv->conn = NULL;
	}

	if (account->priv->gc) {
		g_object_unref (account->priv->gc);
		account->priv->gc = NULL;
	}

	if (account->priv->hierarchies) {
		for (i = 0; i < account->priv->hierarchies->len; i++)
			g_object_unref (account->priv->hierarchies->pdata[i]);
		g_ptr_array_free (account->priv->hierarchies, TRUE);
		account->priv->hierarchies = NULL;
	}

	if (account->priv->hierarchies_by_folder) {
		g_hash_table_destroy (account->priv->hierarchies_by_folder);
		account->priv->hierarchies_by_folder = NULL;
	}

	if (account->priv->foreign_hierarchies) {
		g_hash_table_foreach (account->priv->foreign_hierarchies, free_name, NULL);
		g_hash_table_destroy (account->priv->foreign_hierarchies);
		account->priv->foreign_hierarchies = NULL;
	}

	if (account->priv->folders) {
		g_hash_table_foreach (account->priv->folders, free_folder, NULL);
		g_hash_table_destroy (account->priv->folders);
		account->priv->folders = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
free_uri (gpointer name, gpointer uri, gpointer data)
{
	g_free (name);
	g_free (uri);
}

static void
finalize (GObject *object)
{
	ExchangeAccount *account = EXCHANGE_ACCOUNT (object);

	if (account->account_name)
		g_free (account->account_name);
	if (account->storage_dir)
		g_free (account->storage_dir);
	if (account->exchange_server)
		g_free (account->exchange_server);
	if (account->home_uri)
		g_free (account->home_uri);
	if (account->public_uri)
		g_free (account->public_uri);

	if (account->priv->standard_uris) {
		g_hash_table_foreach (account->priv->standard_uris,
				      free_uri, NULL);
		g_hash_table_destroy (account->priv->standard_uris);
	}

	if (account->priv->uri_authority)
		g_free (account->priv->uri_authority);
	if (account->priv->http_uri_schema)
		g_free (account->priv->http_uri_schema);

	if (account->priv->identity_name)
		g_free (account->priv->identity_name);
	if (account->priv->identity_email)
		g_free (account->priv->identity_email);
	if (account->priv->source_uri)
		g_free (account->priv->source_uri);

	if (account->priv->username)
		g_free (account->priv->username);
	if (account->priv->windows_domain)
		g_free (account->priv->windows_domain);
	if (account->priv->password) {
		memset (account->priv->password, 0,
			strlen (account->priv->password));
		g_free (account->priv->password);
	}
	if (account->priv->ad_server)
		g_free (account->priv->ad_server);

	g_free (account->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


E2K_MAKE_TYPE (exchange_account, ExchangeAccount, class_init, init, PARENT_TYPE)


void
exchange_account_rescan_tree (ExchangeAccount *account)
{
	int i;

	for (i = 0; i < account->priv->hierarchies->len; i++)
		exchange_hierarchy_rescan (account->priv->hierarchies->pdata[i]);
}

/*
 * ExchangeHierarchy folder creation/deletion/xfer notifications
 */

static void
hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
		      ExchangeAccount *account)
{
	/* This makes the cleanup easier. We just unref it each time
	 * we find it in account->priv->folders.
	 */
	g_object_ref (folder);
	g_object_ref (folder);
	g_object_ref (folder);

	g_hash_table_insert (account->priv->folders,
			     (char *)e_folder_exchange_get_path (folder),
			     folder);
	g_hash_table_insert (account->priv->folders,
			     (char *)e_folder_get_physical_uri (folder),
			     folder);
	g_hash_table_insert (account->priv->folders,
			     (char *)e_folder_exchange_get_internal_uri (folder),
			     folder);

	g_hash_table_insert (account->priv->hierarchies_by_folder, folder, hier);

	g_signal_emit (account, signals[NEW_FOLDER], 0, folder);
}

static void
hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
			  ExchangeAccount *account)
{
	g_hash_table_remove (account->priv->folders, e_folder_exchange_get_path (folder));
	g_hash_table_remove (account->priv->folders, e_folder_get_physical_uri (folder));
	g_hash_table_remove (account->priv->folders, e_folder_exchange_get_internal_uri (folder));
	g_hash_table_remove (account->priv->hierarchies_by_folder, folder);

	g_signal_emit (account, signals[REMOVED_FOLDER], 0, folder);

	if (folder == hier->toplevel)
		remove_hierarchy (account, hier);

	g_object_unref (folder);
	g_object_unref (folder);
	g_object_unref (folder);
}

static void
hierarchy_updated_folder (ExchangeHierarchy *hier, EFolder *folder,
			  ExchangeAccount *account)
{
	g_signal_emit (account, signals[UPDATED_FOLDER], 0, folder);
}


static gboolean
get_folder (ExchangeAccount *account, const char *path,
	    EFolder **folder, ExchangeHierarchy **hier)
{
	*folder = g_hash_table_lookup (account->priv->folders, path);
	if (!*folder)
		return FALSE;
	*hier = g_hash_table_lookup (account->priv->hierarchies_by_folder,
				     *folder);
	if (!*hier)
		return FALSE;
	return TRUE;
}

static gboolean
get_parent_and_name (ExchangeAccount *account, const char **path,
		     EFolder **parent, ExchangeHierarchy **hier)
{
	char *name, *parent_path;

	name = strrchr (*path + 1, '/');
	if (!name)
		return FALSE;

	parent_path = g_strndup (*path, name - *path);
	*parent = g_hash_table_lookup (account->priv->folders, parent_path);
	g_free (parent_path);

	if (!*parent)
		return FALSE;

	*hier = g_hash_table_lookup (account->priv->hierarchies_by_folder,
				     *parent);
	if (!*hier)
		return FALSE;

	*path = name + 1;
	return TRUE;
}

void
exchange_account_async_create_folder (ExchangeAccount *account,
				      const char *path, const char *type,
				      ExchangeAccountFolderCallback callback,
				      gpointer user_data)
{
	ExchangeHierarchy *hier;
	EFolder *parent;

	if (!get_parent_and_name (account, &path, &parent, &hier)) {
		callback (account, EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
			  NULL, user_data);
		return;
	}

	exchange_hierarchy_async_create_folder (hier, parent, path, type,
						callback, user_data);
}

void
exchange_account_async_remove_folder (ExchangeAccount *account,
				      const char *path,
				      ExchangeAccountFolderCallback callback,
				      gpointer user_data)
{
	ExchangeHierarchy *hier;
	EFolder *folder;

	if (!get_folder (account, path, &folder, &hier)) {
		callback (account, EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
			  NULL, user_data);
		return;
	}

	exchange_hierarchy_async_remove_folder (hier, folder,
						callback, user_data);
}

void
exchange_account_async_xfer_folder (ExchangeAccount *account,
				    const char *source_path,
				    const char *dest_path,
				    gboolean remove_source,
				    ExchangeAccountFolderCallback callback,
				    gpointer user_data)
{
	EFolder *source, *dest_parent;
	ExchangeHierarchy *source_hier, *dest_hier;

	if (!get_folder (account, source_path, &source, &source_hier) ||
	    !get_parent_and_name (account, &dest_path, &dest_parent, &dest_hier)) {
		/* Source or dest seems to not exist */
		callback (account, EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
			  NULL, user_data);
		return;
	}
	if (source_hier != dest_hier) {
		/* Can't move something between hierarchies */
		callback (account, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR,
			  NULL, user_data);
		return;
	}

	exchange_hierarchy_async_xfer_folder (source_hier, source,
					      dest_parent, dest_path,
					      remove_source,
					      callback, user_data);
}

/**
 * exchange_account_update_folder:
 * @account: the account
 * @folder: the folder to update
 *
 * Tells the shell to update the unread count on the indicated folder.
 **/
void
exchange_account_update_folder (ExchangeAccount *account, EFolder *folder)
{
	g_signal_emit (account, signals[UPDATED_FOLDER], 0, folder);
}

static void
remove_hierarchy (ExchangeAccount *account, ExchangeHierarchy *hier)
{
	int i;

	for (i = 0; i < account->priv->hierarchies->len; i++) {
		if (account->priv->hierarchies->pdata[i] == hier) {
			g_ptr_array_remove_index_fast (account->priv->hierarchies, i);
			break;
		}
	}
	g_hash_table_remove (account->priv->foreign_hierarchies,
			     hier->owner_email);
	g_signal_handlers_disconnect_by_func (hier, hierarchy_new_folder, account);
	g_signal_handlers_disconnect_by_func (hier, hierarchy_updated_folder, account);
	g_signal_handlers_disconnect_by_func (hier, hierarchy_removed_folder, account);
	g_object_unref (hier);
}

static void
setup_hierarchy (ExchangeAccount *account, ExchangeHierarchy *hier)
{
	g_ptr_array_add (account->priv->hierarchies, hier);

	g_signal_connect (hier, "new_folder",
			  G_CALLBACK (hierarchy_new_folder), account);
	g_signal_connect (hier, "updated_folder",
			  G_CALLBACK (hierarchy_updated_folder), account);
	g_signal_connect (hier, "removed_folder",
			  G_CALLBACK (hierarchy_removed_folder), account);

	exchange_hierarchy_add_to_storage (hier);
}

static void
setup_hierarchy_foreign (ExchangeAccount *account, ExchangeHierarchy *hier)
{
	g_hash_table_insert (account->priv->foreign_hierarchies,
			     (char *)hier->owner_email, hier);
	setup_hierarchy (account, hier);
}

struct discover_data {
	ExchangeAccount *account;
	ExchangeHierarchy *hier;
	char *user, *folder_name;
	ExchangeAccountFolderCallback callback;
	gpointer user_data;
	E2kGlobalCatalogLookupId lookup_id;
	gboolean cancelled;
};

static void
free_dd (struct discover_data *dd)
{
	dd->account->priv->discover_datas =
		g_list_remove (dd->account->priv->discover_datas, dd);

	g_object_unref (dd->account);
	g_free (dd->folder_name);
	g_free (dd->user);

	g_free (dd);
}

static void
discover_shared_folder_callback (ExchangeAccount *account,
				 ExchangeAccountFolderResult result,
				 EFolder *folder, gpointer user_data)
{
	struct discover_data *dd = user_data;

	if (dd->callback && !dd->cancelled)
		dd->callback (account, result, folder, dd->user_data);
	free_dd (dd);
}

static ExchangeHierarchy *
get_hierarchy_for (ExchangeAccount *account, E2kGlobalCatalogEntry *entry)
{
	ExchangeHierarchy *hier;
	char *hierarchy_name, *source;
	char *physical_uri_prefix, *internal_uri_prefix;

	hier = g_hash_table_lookup (account->priv->foreign_hierarchies,
				    entry->email);
	if (hier)
		return hier;

	/* i18n: This is the title of an "other user's folders"
	   hierarchy. Eg, "John Doe's Folders". */
	hierarchy_name = g_strdup_printf (_("%s's Folders"),
					  entry->display_name);
	source = g_strdup_printf ("exchange://%s@%s/", entry->mailbox,
				  account->exchange_server);
	physical_uri_prefix = g_strdup_printf ("exchange://%s/%s",
					       account->priv->uri_authority,
					       entry->email);
	internal_uri_prefix = exchange_account_get_foreign_uri (account, entry,
								NULL);

	hier = exchange_hierarchy_foreign_new (account, hierarchy_name,
					       physical_uri_prefix,
					       internal_uri_prefix,
					       entry->display_name,
					       entry->email, source);
	g_free (hierarchy_name);
	g_free (physical_uri_prefix);
	g_free (internal_uri_prefix);
	g_free (source);

	setup_hierarchy_foreign (account, hier);
	return hier;
}

static void
got_mailbox (E2kGlobalCatalog *gc, E2kGlobalCatalogStatus status,
	     E2kGlobalCatalogEntry *entry, gpointer data)
{
	struct discover_data *dd = data;
	ExchangeAccount *account = dd->account;

	dd->lookup_id = NULL;
	if (!entry) {
		discover_shared_folder_callback (
			NULL, ((status == E2K_GLOBAL_CATALOG_ERROR) ?
			       EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR :
			       EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST),
			NULL, dd);
		return;
	}

	dd->hier = get_hierarchy_for (account, entry);
	exchange_hierarchy_foreign_async_add_folder (
		dd->hier, dd->folder_name,
		discover_shared_folder_callback, dd);
}

void
exchange_account_async_discover_shared_folder (ExchangeAccount *account,
					       const char *user,
					       const char *folder_name,
					       ExchangeAccountFolderCallback callback,
					       gpointer user_data)
{
	struct discover_data *dd;
	char *email;

	if (!account->priv->gc) {
		callback (account,
			  EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION,
			  NULL, user_data);
		return;
	}

	dd = g_new0 (struct discover_data, 1);
	dd->account = account;
	g_object_ref (account);
	dd->callback = callback;
	dd->user_data = user_data;
	dd->user = g_strdup (user);
	dd->folder_name = g_strdup (folder_name);

	account->priv->discover_datas =
		g_list_prepend (account->priv->discover_datas, dd);

	email = strchr (user, '<');
	if (email)
		email = g_strndup (email + 1, strcspn (email + 1, ">"));
	else
		email = g_strdup (user);
	dd->hier = g_hash_table_lookup (account->priv->foreign_hierarchies,
					email);
	if (dd->hier) {
		g_free (email);
		exchange_hierarchy_foreign_async_add_folder (
			dd->hier, folder_name,
			discover_shared_folder_callback, dd);
		return;
	}

	dd->lookup_id = e2k_global_catalog_async_lookup (
		account->priv->gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL, email,
		E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
		got_mailbox, dd);
	g_free (email);
}

void
exchange_account_cancel_discover_shared_folder (ExchangeAccount *account,
						const char *user,
						const char *folder_name)
{
	struct discover_data *dd;
	GList *dds;

	for (dds = account->priv->discover_datas; dds; dds = dds->next) {
		dd = dds->data;
		if (!strcmp (dd->user, user) &&
		    !strcmp (dd->folder_name, folder_name))
			break;
	}
	if (!dds)
		return;

	if (dd->lookup_id) {
		e2k_global_catalog_cancel_lookup (account->priv->gc,
						  dd->lookup_id);
		free_dd (dd);
		return;
	}

	/* We can't actually cancel the hierarchy's attempt to get
	 * the folder, but we can remove the hierarchy if appropriate.
	 */
	if (dd->hier && exchange_hierarchy_is_empty (dd->hier))
		hierarchy_removed_folder (dd->hier, dd->hier->toplevel, account);
	dd->cancelled = TRUE;
}

void
exchange_account_async_remove_shared_folder (ExchangeAccount *account,
					     const char *path,
					     ExchangeAccountFolderCallback callback,
					     gpointer user_data)
{
	ExchangeHierarchy *hier;
	EFolder *folder;

	if (!get_folder (account, path, &folder, &hier)) {
		callback (account, EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
			  NULL, user_data);
		return;
	}

	if (!EXCHANGE_IS_HIERARCHY_FOREIGN (hier)) {
		callback (account,
			  EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION,
			  NULL, user_data);
		return;
	}

	exchange_hierarchy_async_remove_folder (hier, folder,
						callback, user_data);
}

void
exchange_account_async_open_folder (ExchangeAccount *account,
				    const char *path,
				    ExchangeAccountFolderCallback callback,
				    gpointer user_data)
{
	ExchangeHierarchy *hier;
	EFolder *folder;

	if (!get_folder (account, path, &folder, &hier)) {
		callback (account, EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
			  NULL, user_data);
		return;
	}

	exchange_hierarchy_async_scan_subtree (hier, folder,
					       callback, user_data);
}

static void
connection_redirect (E2kConnection *conn, int soup_status,
		     const char *old_uri, const char *new_uri,
		     ExchangeAccount *account)
{
	EFolder *folder;

	folder = g_hash_table_lookup (account->priv->folders, old_uri);
	if (!folder)
		return;

	g_hash_table_remove (account->priv->folders, old_uri);
	e_folder_exchange_set_internal_uri (folder, new_uri);
	g_hash_table_insert (account->priv->folders,
			     (char *)e_folder_exchange_get_internal_uri (folder),
			     folder);
}

struct connection_data {
	ExchangeAccountConnectCallback callback;
	gpointer user_data;
};

static void
finish_connection_attempts (ExchangeAccount *account)
{
	struct connection_data *cd;

	if (account->priv->conn && !account->priv->connected) {
		g_object_unref (account->priv->conn);
		account->priv->conn = NULL;
	}

	while (account->priv->connect_datas) {
		cd = account->priv->connect_datas->data;
		account->priv->connect_datas =
			g_list_remove (account->priv->connect_datas, cd);

		cd->callback (account, cd->user_data);
		g_free (cd);
	}

	g_object_unref (account);
}

static gboolean
idle_connected (gpointer account)
{
	g_signal_emit (account, signals[CONNECTED], 0);
	g_object_unref (account);
	return FALSE;
}

static void
opened_personal_hierarchy (ExchangeAccount *account,
			   ExchangeAccountFolderResult result,
			   EFolder *folder, gpointer user_data)
{
	if (result != EXCHANGE_ACCOUNT_FOLDER_OK) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not access personal folders"));
		finish_connection_attempts (account);
		return;
	}

	account->priv->connected = TRUE;
	g_object_ref (account);
	g_idle_add (idle_connected, account);

	g_signal_connect (account->priv->conn, "redirect",
			  G_CALLBACK (connection_redirect), account);

	/* And we're done */
	finish_connection_attempts (account);
}

static void
set_sf_prop (const char *propname, E2kPropType type,
	     gpointer href, gpointer user_data)
{
	ExchangeAccount *account = user_data;

	propname = strrchr (propname, ':');
	if (!propname++)
		return;

	g_hash_table_insert (account->priv->standard_uris,
			     g_strdup (propname),
			     e2k_strdup_with_trailing_slash (href));
}

static void
got_standard_folders (E2kConnection *conn, SoupMessage *msg,
		      E2kResult *results, int nresults,
		      gpointer user_data)
{
	ExchangeAccount *account = user_data;
	GByteArray *entryid;
	char *phys_uri_prefix, *dir;
	ExchangeHierarchy *hier, *personal_hier;
	struct dirent *dent;
	DIR *d;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not retrieve list of standard folders"));
		finish_connection_attempts (account);
		return;
	}

	if (nresults) {
		account->priv->standard_uris =
			g_hash_table_new (e2k_ascii_strcase_hash,
					  e2k_ascii_strcase_equal);
		e2k_properties_foreach (results[0].props, set_sf_prop, account);

		entryid = e2k_properties_get_prop (results[0].props, PR_STORE_ENTRYID);
		if (entryid)
			account->legacy_exchange_dn = g_strdup (e2k_entryid_to_dn (entryid));
	}

	if (account->priv->ad_server) {
		account->priv->gc = e2k_global_catalog_new (
			account->priv->ad_server, account->priv->ad_limit,
			account->priv->username, account->priv->windows_domain,
			account->priv->password);
	}

	/* Set up Personal Folders hierarchy */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/personal",
					   account->priv->uri_authority);
	hier = exchange_hierarchy_webdav_new (account,
					      EXCHANGE_HIERARCHY_PERSONAL,
					      _("Personal Folders"),
					      phys_uri_prefix,
					      account->home_uri,
					      account->priv->identity_name,
					      account->priv->identity_email,
					      account->priv->source_uri,
					      TRUE, "folder", -2);
	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);
	personal_hier = hier;

	/* Public Folders */
	phys_uri_prefix = g_strdup_printf ("exchange://%s/public",
					   account->priv->uri_authority);
	hier = exchange_hierarchy_webdav_new (account,
					      EXCHANGE_HIERARCHY_PUBLIC,
					      /* i18n: Outlookism */
					      _("Public Folders"),
					      phys_uri_prefix,
					      account->public_uri,
					      account->priv->identity_name,
					      account->priv->identity_email,
					      account->priv->source_uri,
					      FALSE, "public-folder", -1);
	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);

	/* Global Address List */
	phys_uri_prefix = g_strdup_printf ("activedirectory://%s/gal",
					   account->priv->uri_authority);
						     /* i18n: Outlookism */
	hier = exchange_hierarchy_gal_new (account, _("Global Address List"),
					   phys_uri_prefix);
	setup_hierarchy (account, hier);
	g_free (phys_uri_prefix);

	/* Other users' folders */
	d = opendir (account->storage_dir);
	if (!d)
		return;
	while ((dent = readdir (d))) {
		if (!strchr (dent->d_name, '@'))
			continue;
		dir = g_strdup_printf ("%s/%s", account->storage_dir,
				       dent->d_name);
		hier = exchange_hierarchy_foreign_new_from_dir (account, dir);
		g_free (dir);
		if (!hier)
			continue;

		setup_hierarchy_foreign (account, hier);
	}
	closedir (d);

	/* Scan the personal folders so we can resolve references
	 * to the Calendar, Contacts, etc even if the tree isn't
	 * opened.
	 */
	exchange_hierarchy_async_scan_subtree (personal_hier,
					       personal_hier->toplevel,
					       opened_personal_hierarchy,
					       NULL);
}

static void try_connect (ExchangeAccount *account, const char *errmsg);

static gboolean
account_moved (ExchangeAccount *account, const char *redirect_uri)
{
	E2kAutoconfig *ac;
	E2kAutoconfigResult result;
	EAccount *eaccount;

	ac = e2k_autoconfig_new (redirect_uri, account->priv->username,
				 account->priv->password,
				 !account->priv->use_basic_auth);
	e2k_autoconfig_set_gc_server (ac, account->priv->ad_server);

	result = e2k_autoconfig_check_exchange (ac);
	if (result != E2K_AUTOCONFIG_OK) {
		e2k_autoconfig_free (ac);
		return FALSE;
	}
	result = e2k_autoconfig_check_global_catalog (ac);
	if (result != E2K_AUTOCONFIG_OK) {
		e2k_autoconfig_free (ac);
		return FALSE;
	}

	eaccount = account->priv->account;

	if (eaccount->source->url && eaccount->transport->url &&
	    !strcmp (eaccount->source->url, eaccount->transport->url)) {
		g_free (eaccount->transport->url);
		eaccount->transport->url = g_strdup (ac->account_uri);
	}
	g_free (eaccount->source->url);
	eaccount->source->url = g_strdup (ac->account_uri);

	e2k_autoconfig_free (ac);

	e_account_list_change (account->priv->account_list, eaccount);
	e_account_list_save (account->priv->account_list);
	return TRUE;
}

#define OWA_55_TOP "<HTML>\r\n<!--Microsoft Outlook Web Access-->"

static void
ex55_check (SoupMessage *msg, gpointer user_data)
{
	ExchangeAccount *account = user_data;

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode) &&
	    msg->response.length > sizeof (OWA_55_TOP) &&
	    !strncmp (msg->response.body, OWA_55_TOP, sizeof (OWA_55_TOP) - 1)) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("The server '%s' is running Exchange 5.5 and is\n"
			    "therefore not compatible with Ximian Connector"),
			  account->exchange_server);
	} else {
		char *owa_uri;

		owa_uri = g_strdup_printf (account->priv->http_uri_schema,
					   account->exchange_server, "");
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not find Exchange Web Storage System at %s.\n"
			    "If OWA is running on a different path, you "
			    "must specify that in the\naccount configuration "
			    "dialog."), owa_uri);
		g_free (owa_uri);
	}
	finish_connection_attempts (account);
}

static void
connect_failed (SoupMessage *msg, ExchangeAccount *account)
{
	if (msg->errorcode == SOUP_ERROR_CANT_AUTHENTICATE) {
		g_free (account->priv->password);
		account->priv->password = NULL;
		if (account->priv->use_basic_auth && !account->priv->windows_domain) {
			try_connect (account,
				     _("Could not authenticate to server."
				       "\n\nThis probably means that your "
				       "server requires you\nto specify "
				       "the Windows domain name as part "
				       "of your\nusername (eg, "
				       "\"DOMAIN\\user\").\n\nOr you "
				       "might have just typed your "
				       "password wrong.\n\n"));
		} else if (!account->priv->use_basic_auth && !account->priv->saw_ntlm) {
			try_connect (account,
				     _("Could not authenticate to server."
				       "\n\nYou may need to use Plaintext "
				       "Password authentication.\n\nOr "
				       "you might have just typed your "
				       "password wrong.\n\n"));
		} else {
			try_connect (account,
				     _("Could not authenticate to server. "
				       "(Password incorrect?)\n\n"));
		}
		return;
	} else if (msg->errorcode == SOUP_ERROR_NOT_FOUND) {
		const char *ms_webstorage =
			soup_message_get_header (msg->response_headers,
						 "MS-WebStorage");

		if (ms_webstorage) {
			e_notice (NULL, GTK_MESSAGE_ERROR,
				  _("No mailbox for user %s on %s.\n"),
				  account->priv->username,
				  account->exchange_server);
		} else {
			char *owa_uri;

			owa_uri = g_strdup_printf (account->priv->http_uri_schema,
						   account->exchange_server, "");

			msg = e2k_soup_message_new (account->priv->conn,
						    owa_uri, SOUP_METHOD_GET);
			soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
			e2k_soup_message_queue (msg, ex55_check, account);
			return;
		}
	} else if (SOUP_ERROR_IS_REDIRECTION (msg->errorcode)) {
		const char *location =
			soup_message_get_header (msg->response_headers,
						 "Location");
		E2kUri *e2k_uri = NULL;
		gboolean moved = FALSE;

		if (location)
			e2k_uri = e2k_uri_new (location);
		if (e2k_uri) {
			if (e2k_uri->host &&
			    strcmp (e2k_uri->host, account->exchange_server))
				moved = account_moved (account, location);
			e2k_uri_free (e2k_uri);
		}

		if (!moved) {
			e_notice (NULL, GTK_MESSAGE_ERROR,
				  _("Mailbox for %s is not on this server."),
				  account->priv->username);
		}
	} else if (msg->errorcode == SOUP_ERROR_FORBIDDEN &&
		   strncmp (account->priv->http_uri_schema, "https", 5)) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not connect to server %s.\nTry using SSL?"),
			  account->exchange_server);
	} else {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not connect to server %s: %s"),
			  account->exchange_server,
			  msg->errorphrase);
	}

	finish_connection_attempts (account);
}

static const char *mailbox_info_props[] = {
	E2K_PR_STD_FOLDER_CALENDAR,
	E2K_PR_STD_FOLDER_CONTACTS,
	E2K_PR_STD_FOLDER_DELETED_ITEMS,
	E2K_PR_STD_FOLDER_DRAFTS,
	E2K_PR_STD_FOLDER_INBOX,
	E2K_PR_STD_FOLDER_JOURNAL,
	E2K_PR_STD_FOLDER_NOTES,
	E2K_PR_STD_FOLDER_OUTBOX,
	E2K_PR_STD_FOLDER_SENT_ITEMS,
	E2K_PR_STD_FOLDER_TASKS,
	E2K_PR_STD_FOLDER_ROOT,
	E2K_PR_STD_FOLDER_SENDMSG,
	PR_STORE_ENTRYID
};
static const int n_mailbox_info_props = sizeof (mailbox_info_props) / sizeof (mailbox_info_props[0]);

static void
tried_connect (SoupMessage *msg, gpointer user_data)
{
	ExchangeAccount *account = user_data;

	if (SOUP_ERROR_IS_REDIRECTION (msg->errorcode)) {
		const char *location;

		location = soup_message_get_header (msg->response_headers,
						   "Location");
		if (location && strstr (location, "/owalogon.asp")) {
			if (e2k_connection_fba (account->priv->conn, msg))
				soup_message_set_error (msg, SOUP_ERROR_OK);
		}
	}

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		if (exchange_component_interactive)
			connect_failed (msg, account);
		else
			finish_connection_attempts (account);
		return;
	}

	E2K_DEBUG_HINT ('S');
	e2k_connection_propfind (account->priv->conn, account->home_uri, "0",
				 mailbox_info_props, n_mailbox_info_props,
				 got_standard_folders, account);
}

static void
auth_handler (SoupMessage *msg, gpointer user_data)
{
	ExchangeAccount *account = user_data;
	const GSList *headers;
	const char *challenge;

	headers = soup_message_get_header_list (msg->response_headers,
						"WWW-Authenticate");
	while (headers) {
		challenge = headers->data;

		if (!strcmp (challenge, "NTLM"))
			account->priv->saw_ntlm = TRUE;

		if (!strncmp (challenge, "NTLM ", 5) &&
		    !account->priv->windows_domain) {
			soup_ntlm_parse_challenge (challenge, NULL, &account->priv->windows_domain);
		}

		headers = headers->next;
	}
}

static void
try_connect (ExchangeAccount *account, const char *errmsg)
{
	E2kConnection *conn = account->priv->conn;
	char *key;
	SoupMessage *msg;
	gboolean remember, oldremember;

	if (!account->priv->password) {
		key = g_strdup_printf ("exchange://%s@%s",
				       account->priv->username,
				       account->exchange_server);
		if (*errmsg)
			e_passwords_forget_password ("Exchange", key);

		account->priv->password = e_passwords_get_password ("Exchange", key);
		if (!account->priv->password && exchange_component_interactive) {
			char *prompt;

			prompt = g_strdup_printf (_("%sEnter password for %s"),
						  errmsg, account->account_name);
			oldremember = remember = account->priv->account->source->save_passwd;
			account->priv->password = e_passwords_ask_password (
				_("Enter password"), "Exchange", key, prompt,
				TRUE, E_PASSWORDS_REMEMBER_FOREVER, &remember,
				NULL);
			if (remember != oldremember) {
				account->priv->account->source->save_passwd = remember;
				e_account_list_save (account->priv->account_list);			}
			g_free (prompt);
		}
		g_free (key);
		if (account->priv->password) {
			e2k_connection_set_auth (conn, account->priv->username,
						 account->priv->windows_domain,
						 account->priv->use_basic_auth ? "Basic" : "NTLM",
						 account->priv->password);
		} else {
			finish_connection_attempts (account);
			return;
		}
	}

	msg = e2k_soup_message_new (conn, account->home_uri, SOUP_METHOD_GET);
	soup_message_add_header (msg->request_headers, "Translate", "F");
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	soup_message_add_header (msg->request_headers, "Accept-Language",
				 e2k_get_accept_language ());
	if (!account->priv->windows_domain) {
		soup_message_add_error_code_handler (msg, SOUP_ERROR_UNAUTHORIZED,
						     SOUP_HANDLER_PRE_BODY,
						     auth_handler, account);
	}
	e2k_soup_message_queue (msg, tried_connect, account);
}

/**
 * exchange_account_async_connect:
 * @account: an #ExchangeAccount
 * @callback: callback to call after success/failure
 * @user_data: data to pass to @callback.
 *
 * This attempts to connect to @account. If the shell has enabled user
 * interaction, then it will prompt for a password if needed. After
 * succeeding or failing, it will call @callback, which can call
 * exchange_account_get_connection() to see if it succeeded.
 **/
void
exchange_account_async_connect (ExchangeAccount *account,
				ExchangeAccountConnectCallback callback,
				gpointer user_data)
{
	struct connection_data *cd;

	if (account->priv->connected) {
		callback (account, user_data);
		return;
	}

	cd = g_new0 (struct connection_data, 1);
	cd->callback = callback;
	cd->user_data = user_data;

	account->priv->connect_datas =
		g_list_prepend (account->priv->connect_datas, cd);

	if (account->priv->connect_datas->next) {
		/* We're already trying to connect */
		return;
	}
	g_object_ref (account);

	account->priv->conn = e2k_connection_new (account->home_uri);
	if (!account->priv->conn) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not create connection for %s"),
			  account->home_uri);
		finish_connection_attempts (account);
		return;
	}

	try_connect (account, "");
}


/**
 * exchange_account_get_connection:
 * @account: an #ExchangeAccount
 *
 * Return value: @account's #E2kConnection, if it is connected and
 * online, or %NULL if not.
 **/
E2kConnection *
exchange_account_get_connection (ExchangeAccount *account)
{
	return account->priv->conn;
}

/**
 * exchange_account_get_global_catalog:
 * @account: an #ExchangeAccount
 *
 * Return value: @account's #E2kGlobalCatalog, if it is connected and
 * online, or %NULL if not.
 **/
E2kGlobalCatalog *
exchange_account_get_global_catalog (ExchangeAccount *account)
{
	return account->priv->gc;
}

/**
 * exchange_account_get_standard_uri:
 * @account: an #ExchangeAccount
 * @item: the short name of the standard URI
 *
 * Looks up the value of one of the standard URIs on @account.
 * Supported values for @item are:
 *   "calendar", "contacts", "deleteditems", "drafts", "inbox",
 *   "journal", "notes", "outbox", "sentitems", "tasks", and
 *   "sendmsg" (the special mail submission URI)
 *
 * Return value: the value of the standard URI, or %NULL if the
 * account is not connected or the property is invalid or not
 * defined on @account.
 **/
const char *
exchange_account_get_standard_uri (ExchangeAccount *account, const char *item)
{
	if (!account->priv->standard_uris)
		return NULL;
	return g_hash_table_lookup (account->priv->standard_uris, item);
}

/**
 * exchange_account_get_standard_uri_for:
 * @account: an #ExchangeAccount
 * @home_uri: the home URI of a user
 * @std_uri_prop: the %E2K_PR_STD_FOLDER property to look up
 *
 * Looks up the URI of a folder in another user's mailbox.
 *
 * Return value: the URI of the folder, or %NULL if either the folder
 * doesn't exist or the user doesn't have permission to access it.
 **/
char *
exchange_account_get_standard_uri_for (ExchangeAccount *account,
				       const char *home_uri,
				       const char *std_uri_prop)
{
	char *foreign_uri, *prop;
	int status, nresults;
	E2kResult *results;

	foreign_uri = e2k_uri_concat (home_uri, "NON_IPM_SUBTREE");
	status = e2k_connection_propfind_sync (account->priv->conn,
					       foreign_uri, "0",
					       &std_uri_prop, 1,
					       &results, &nresults);
	g_free (foreign_uri);

	if (!SOUP_ERROR_IS_SUCCESSFUL (status) || nresults == 0)
		return NULL;

	prop = e2k_properties_get_prop (results[0].props, std_uri_prop);
	if (prop)
		foreign_uri = e2k_strdup_with_trailing_slash (prop);
	else
		foreign_uri = NULL;
	e2k_results_free (results, nresults);

	return foreign_uri;
}

/**
 * exchange_account_get_foreign_uri:
 * @account: an #ExchangeAccount
 * @entry: an #E2kGlobalCatalogEntry with mailbox data
 * @std_uri_prop: the %E2K_PR_STD_FOLDER property to look up, or %NULL
 *
 * Looks up the URI of a folder in another user's mailbox. If
 * @std_uri_prop is %NULL, the URI for the top level of the user's
 * mailbox is returned.
 *
 * Return value: the URI of the folder, or %NULL if either the folder
 * doesn't exist or the user doesn't have permission to access it.
 **/
char *
exchange_account_get_foreign_uri (ExchangeAccount *account,
				  E2kGlobalCatalogEntry *entry,
				  const char *std_uri_prop)
{
	char *home_uri, *foreign_uri;

	if (account->priv->uris_use_email) {
		char *mailbox;

		mailbox = g_strndup (entry->email, strcspn (entry->email, "@"));
		home_uri = g_strdup_printf (account->priv->http_uri_schema,
					    entry->exchange_server, mailbox);
		g_free (mailbox);
	} else {
		home_uri = g_strdup_printf (account->priv->http_uri_schema,
					    entry->exchange_server,
					    entry->mailbox);
	}
	if (!std_uri_prop)
		return home_uri;

	foreign_uri = exchange_account_get_standard_uri_for (account,
							     home_uri,
							     std_uri_prop);
	g_free (home_uri);

	return foreign_uri;
}

/**
 * exchange_account_get_folder:
 * @account: an #ExchangeAccount
 * @path_or_uri: the shell path or URI referring to the folder
 *
 * Return value: an #EFolder corresponding to the indicated
 * folder.
 **/
EFolder *
exchange_account_get_folder (ExchangeAccount *account,
			     const char *path_or_uri)
{
	return g_hash_table_lookup (account->priv->folders, path_or_uri);
}

static int
folder_comparator (const void *a, const void *b)
{
	EFolder **fa = (EFolder **)a;
	EFolder **fb = (EFolder **)b;

	return strcmp (e_folder_exchange_get_path (*fa),
		       e_folder_exchange_get_path (*fb));
}

static void
add_folder (gpointer key, gpointer value, gpointer folders)
{
	EFolder *folder = value;

	/* Each folder appears under three different keys, but
	 * we only want to add it to the results array once. So
	 * we only add when we see the "path" key.
	 */
	if (!strcmp (key, e_folder_exchange_get_path (folder)))
		g_ptr_array_add (folders, folder);
}

/**
 * exchange_account_get_folders:
 * @account: an #ExchangeAccount
 *
 * Return an array of folders (sorted such that parents will occur
 * before children). If the caller wants to keep up to date with the
 * list of folders, he should also listen to %new_folder and
 * %removed_folder signals.
 *
 * Return value: an array of folders. The array should be freed with
 * g_ptr_array_free().
 **/
GPtrArray *
exchange_account_get_folders (ExchangeAccount *account)
{
	GPtrArray *folders;

	folders = g_ptr_array_new ();
	g_hash_table_foreach (account->priv->folders, add_folder, folders);

	qsort (folders->pdata, folders->len,
	       sizeof (EFolder *), folder_comparator);

	return folders;
}	

/**
 * exchange_account_new:
 * @adata: an #EAccount
 *
 * An #ExchangeAccount is essentially an #E2kConnection with
 * associated configuration.
 *
 * Return value: a new #ExchangeAccount corresponding to @adata
 **/
ExchangeAccount *
exchange_account_new (EAccountList *account_list, EAccount *adata)
{
	ExchangeAccount *account;
	char *enc_user, *mailbox;
	const char *param, *proto, *owa_path, *pf_server;
	E2kUri *uri;

	uri = e2k_uri_new (adata->source->url);
	if (!uri) {
		g_warning ("Could not parse exchange uri '%s'",
			   adata->source->url);
		return NULL;
	}

	account = g_object_new (EXCHANGE_TYPE_ACCOUNT, NULL);
	account->priv->account_list = account_list;
	g_object_ref (account_list);
	account->priv->account = adata;
	g_object_ref (adata);

	account->account_name = g_strdup (adata->name);

	account->storage_dir = g_strdup_printf ("%s/exchange/%s@%s",
						evolution_dir,
						uri->user, uri->host);
	account->account_filename = strrchr (account->storage_dir, '/') + 1;
	e_filename_make_safe (account->account_filename);

	/* Identity info */
	account->priv->identity_name = g_strdup (adata->id->name);
	account->priv->identity_email = g_strdup (adata->id->address);

	/* URI, etc, info */
	enc_user = e2k_uri_encode (uri->user, "@/;:");
	account->priv->uri_authority = g_strdup_printf ("%s@%s", enc_user,
							uri->host);
	g_free (enc_user);

	account->priv->source_uri = g_strdup_printf ("exchange://%s/", account->priv->uri_authority);

	account->priv->username = g_strdup (uri->user);
	account->priv->windows_domain = g_strdup (uri->domain);
	account->exchange_server = g_strdup (uri->host);
	if (uri->authmech && !strcmp (uri->authmech, "Basic"))
		account->priv->use_basic_auth = TRUE;
	param = e2k_uri_get_param (uri, "ad_server");
	if (param && *param) {
		account->priv->ad_server = g_strdup (param);
		param = e2k_uri_get_param (uri, "ad_limit");
		if (param)
			account->priv->ad_limit = atoi (param);
	}

	owa_path = e2k_uri_get_param (uri, "owa_path");
	if (!owa_path || !*owa_path)
		owa_path = "exchange";
	else if (*owa_path == '/')
		owa_path++;

	pf_server = e2k_uri_get_param (uri, "pf_server");
	if (!pf_server || !*pf_server)
		pf_server = uri->host;

	proto = e2k_uri_get_param (uri, "use_ssl") ? "https" : "http";
	if (uri->port != 0) {
		account->priv->http_uri_schema =
			g_strdup_printf ("%s://%%s:%d/%s/%%s/",
					 proto, uri->port, owa_path);
		account->public_uri =
			g_strdup_printf ("%s://%s:%d/public",
					 proto, pf_server, uri->port);
	} else {
		account->priv->http_uri_schema =
			g_strdup_printf ("%s://%%s/%s/%%s/", proto, owa_path);
		account->public_uri =
			g_strdup_printf ("%s://%s/public", proto, pf_server);
	}

	param = e2k_uri_get_param (uri, "mailbox");
	if (!param || !*param)
		param = uri->user;
	else if (!g_ascii_strncasecmp (param, account->priv->identity_email, strlen (param)))
		account->priv->uris_use_email = TRUE;
	mailbox = e2k_uri_encode (param, "/");
	account->home_uri = g_strdup_printf (account->priv->http_uri_schema,
					     uri->host, mailbox);
	g_free (mailbox);

	param = e2k_uri_get_param (uri, "filter");
	if (param)
		account->filter_inbox = TRUE;

	e2k_uri_free (uri);

	return account;
}
