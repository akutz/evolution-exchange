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

/* ExchangeHierarchyWebDAV: class for a normal WebDAV folder hierarchy
 * in the Exchange storage. Eg, the "Personal Folders" and "Public
 * Folders" hierarchies.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-webdav.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-context.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "exchange-config-listener.h"

#include <e-util/e-passwords.h>
#include <e-util/e-path.h>
#include <libedataserver/e-source-list.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _ExchangeHierarchyWebDAVPrivate {
	GHashTable *folders_by_internal_path;
	gboolean deep_searchable;
	char *trash_path;
};

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY
static ExchangeHierarchyClass *parent_class = NULL;

static void folder_type_map_init (void);

static void finalize (GObject *object);
static gboolean is_empty (ExchangeHierarchy *hier);
static void rescan (ExchangeHierarchy *hier);
static ExchangeAccountFolderResult scan_subtree  (ExchangeHierarchy *hier,
						  EFolder *folder);
static ExchangeAccountFolderResult create_folder (ExchangeHierarchy *hier,
						  EFolder *parent,
						  const char *name,
						  const char *type);
static ExchangeAccountFolderResult remove_folder (ExchangeHierarchy *hier,
						  EFolder *folder);
static ExchangeAccountFolderResult xfer_folder   (ExchangeHierarchy *hier,
						  EFolder *source,
						  EFolder *dest_parent,
						  const char *dest_name,
						  gboolean remove_source);

static void hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
				  gpointer user_data);
static void hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
				      gpointer user_data);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *exchange_hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	folder_type_map_init ();

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	exchange_hierarchy_class->is_empty = is_empty;
	exchange_hierarchy_class->rescan = rescan;
	exchange_hierarchy_class->scan_subtree = scan_subtree;
	exchange_hierarchy_class->create_folder = create_folder;
	exchange_hierarchy_class->remove_folder = remove_folder;
	exchange_hierarchy_class->xfer_folder = xfer_folder;
}

static void
init (GObject *object)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (object);

	hwd->priv = g_new0 (ExchangeHierarchyWebDAVPrivate, 1);
	hwd->priv->folders_by_internal_path = g_hash_table_new (g_str_hash, g_str_equal);

	g_signal_connect (object, "new_folder",
			  G_CALLBACK (hierarchy_new_folder), NULL);
	g_signal_connect (object, "removed_folder",
			  G_CALLBACK (hierarchy_removed_folder), NULL);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (object);

	g_hash_table_destroy (hwd->priv->folders_by_internal_path);
	g_free (hwd->priv->trash_path);
	g_free (hwd->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_webdav, ExchangeHierarchyWebDAV, class_init, init, PARENT_TYPE)


typedef struct {
	char *contentclass, *component;
	gboolean offline_supported;
} ExchangeFolderType;

static ExchangeFolderType folder_types[] = {
	{ "IPF.Note", "mail", FALSE },
	{ "IPF.Contact", "contacts", FALSE },
	{ "IPF.Appointment", "calendar", FALSE },
	{ "IPF.Task", "tasks", FALSE },
	{ NULL, NULL }
};
static GHashTable *folder_type_map;

static void
folder_type_map_init (void)
{
	int i;

	folder_type_map = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; folder_types[i].contentclass; i++) {
		g_hash_table_insert (folder_type_map,
				     folder_types[i].contentclass,
				     &folder_types[i]);
	}
}

/* We maintain the folders_by_internal_path hash table by listening
 * to our own signal emissions. (This lets ExchangeHierarchyForeign
 * remove its folders by just calling exchange_hierarchy_removed_folder.)
 */
static void
hierarchy_new_folder (ExchangeHierarchy *hier, EFolder *folder,
		      gpointer user_data)
{
	const char *internal_uri = e_folder_exchange_get_internal_uri (folder);
	char *mf_path;

	g_hash_table_insert (EXCHANGE_HIERARCHY_WEBDAV (hier)->priv->folders_by_internal_path,
			     (char *)e2k_uri_path (internal_uri), folder);

	mf_path = e_folder_exchange_get_storage_file (folder, "connector-metadata.xml");
	e_folder_exchange_save_to_file (folder, mf_path);
	g_free (mf_path);
}

static void
hierarchy_removed_folder (ExchangeHierarchy *hier, EFolder *folder,
			  gpointer user_data)
{
	const char *internal_uri = e_folder_exchange_get_internal_uri (folder);
	char *mf_path;

	g_hash_table_remove (EXCHANGE_HIERARCHY_WEBDAV (hier)->priv->folders_by_internal_path,
			     (char *)e2k_uri_path (internal_uri));

	mf_path = e_folder_exchange_get_storage_file (folder, "connector-metadata.xml");
	unlink (mf_path);
	g_free (mf_path);

	e_path_rmdir (hier->account->storage_dir,
		      e_folder_exchange_get_path (folder));
}

static gboolean
is_empty (ExchangeHierarchy *hier)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);

	/* 1, not 0, because there will always be an entry for toplevel */
	return g_hash_table_size (hwd->priv->folders_by_internal_path) == 1;
}

static EFolder *
e_folder_webdav_new (ExchangeHierarchy *hier, const char *internal_uri,
		     EFolder *parent, const char *name, const char *type,
		     const char *outlook_class, int unread,
		     gboolean offline_supported)
{
	EFolder *folder;
	char *real_type, *http_uri, *physical_uri;

	if (hier->type == EXCHANGE_HIERARCHY_PUBLIC &&
	    !strstr (type, "/public"))
		real_type = g_strdup_printf ("%s/public", type);
	else if (hier->type == EXCHANGE_HIERARCHY_FOREIGN &&
		 !strcmp (type, "calendar"))
		real_type = g_strdup ("calendar/public"); /* Hack */
	else
		real_type = g_strdup (type);

	if (strchr (name, '/')) {
		char *fixed_name, *p;

		/* We can't have a '/' in the path, so we replace it with
		 * a '\' and just hope the user doesn't have another
		 * folder with that name.
		 */
		fixed_name = g_strdup (name);
		for (p = fixed_name; *p; p++) {
			if (*p == '/')
				*p = '\\';
		}

		physical_uri = e2k_uri_concat (e_folder_get_physical_uri (parent), fixed_name);
		g_free (fixed_name);
	} else
		physical_uri = e2k_uri_concat (e_folder_get_physical_uri (parent), name);

	if (internal_uri) {
		folder = e_folder_exchange_new (hier, name,
						real_type, outlook_class,
						physical_uri, internal_uri);
	} else {
		http_uri = e2k_uri_concat (e_folder_exchange_get_internal_uri (parent), name);
		folder = e_folder_exchange_new (hier, name,
						real_type, outlook_class,
						physical_uri, http_uri);
		g_free (http_uri);
	}
	g_free (physical_uri);
	g_free (real_type);

	if (unread && hier->type != EXCHANGE_HIERARCHY_PUBLIC)
		e_folder_set_unread_count (folder, unread);
	if (offline_supported)
		e_folder_set_can_sync_offline (folder, offline_supported);

	/* FIXME: set is_stock */

	return folder;
}

static ExchangeAccountFolderResult
create_folder (ExchangeHierarchy *hier, EFolder *parent,
	       const char *name, const char *type)
{
	EFolder *dest;
	E2kProperties *props;
	E2kHTTPStatus status;
	char *permanent_url = NULL;
	int i;

#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account))
		return EXCHANGE_ACCOUNT_FOLDER_OFFLINE;
#endif

	for (i = 0; folder_types[i].component; i++) {
		if (!strcmp (folder_types[i].component, type))
			break;
	}
	if (!folder_types[i].component)
		return EXCHANGE_ACCOUNT_FOLDER_UNKNOWN_TYPE;

	dest = e_folder_webdav_new (hier, NULL, parent, name, type,
				    folder_types[i].contentclass, 0,
				    folder_types[i].offline_supported);

	props = e2k_properties_new ();
	e2k_properties_set_string (props, E2K_PR_EXCHANGE_FOLDER_CLASS,
				   g_strdup (folder_types[i].contentclass));

	status = e_folder_exchange_mkcol (dest, NULL, props,
					  &permanent_url);
	e2k_properties_free (props);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		e_folder_exchange_set_permanent_uri (dest, permanent_url);
		g_free (permanent_url);
		exchange_hierarchy_new_folder (hier, dest);
		g_object_unref (dest);
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	}

	g_object_unref (dest);
	if (status == E2K_HTTP_METHOD_NOT_ALLOWED)
		return EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS;
	else if (status == E2K_HTTP_CONFLICT)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	else if (status == E2K_HTTP_FORBIDDEN)
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
	else
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
}

static ExchangeAccountFolderResult
remove_folder (ExchangeHierarchy *hier, EFolder *folder)
{
	E2kHTTPStatus status;

#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account))
		return EXCHANGE_ACCOUNT_FOLDER_OFFLINE;
#endif

	if (folder == hier->toplevel)
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;

	status = e_folder_exchange_delete (folder, NULL);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		exchange_hierarchy_removed_folder (hier, folder);
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	} else
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
}

static ExchangeAccountFolderResult
xfer_folder (ExchangeHierarchy *hier, EFolder *source,
	     EFolder *dest_parent, const char *dest_name,
	     gboolean remove_source)
{
	E2kHTTPStatus status;
	EFolder *dest;
	char *permanent_url = NULL;
	char *conf_key_cal="/apps/evolution/calendar/sources";
	char *conf_key_tasks="/apps/evolution/tasks/sources";
	char *conf_key_contacts="/apps/evolution/addressbook/sources";
	ESourceList *cal_source_list, *task_source_list, *cont_source_list;
	const char *folder_type, *physical_uri;

#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account))
		return EXCHANGE_ACCOUNT_FOLDER_OFFLINE;
#endif

	if (source == hier->toplevel)
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;

	/* Remove the ESource of the source folder, in case of rename/move */

	if (hier->type != EXCHANGE_HIERARCHY_GAL && remove_esource) {
		folder_type = e_folder_get_type_string (source);
		physical_uri = e_folder_get_physical_uri (source);
		
		if ((strcmp (folder_type, "calendar") == 0) ||
		    (strcmp (folder_type, "calendar/public") == 0)) {
			cal_source_list = e_source_list_new_for_gconf (
						gconf_client_get_default (),
						conf_key_cal);
			remove_esource (hier->account, conf_key_cal,
					physical_uri, &cal_source_list,
					FALSE, FALSE);
			e_source_list_sync (cal_source_list, NULL);
			g_object_unref (cal_source_list);
		}
		else if ((strcmp (folder_type, "tasks") == 0) ||
			 (strcmp (folder_type, "tasks/public") == 0)){
			task_source_list = e_source_list_new_for_gconf (
						gconf_client_get_default (),
						conf_key_tasks);
			remove_esource (hier->account, conf_key_tasks,
					physical_uri, &task_source_list,
					FALSE, FALSE);
			e_source_list_sync (task_source_list, NULL);
			g_object_unref (task_source_list);
		}
		else if ((strcmp (folder_type, "contacts") == 0) ||
			 (strcmp (folder_type, "contacts/public") == 0)) {
			cont_source_list = e_source_list_new_for_gconf (
						gconf_client_get_default (),
						conf_key_contacts);
			remove_esource (hier->account, conf_key_contacts,
					physical_uri, &cont_source_list, 
					FALSE, TRUE);
			e_source_list_sync (cont_source_list, NULL);
			g_object_unref (cont_source_list);
		}
	}

	dest = e_folder_webdav_new (hier, NULL, dest_parent, dest_name,
				    e_folder_get_type_string (source),
				    e_folder_exchange_get_outlook_class (source),
				    e_folder_get_unread_count (source),
				    e_folder_get_can_sync_offline (source));

	status = e_folder_exchange_transfer_dir (source, NULL, dest,
						 remove_source,
						 &permanent_url);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		if (permanent_url)
			e_folder_exchange_set_permanent_uri (dest, permanent_url);
		if (remove_source)
			exchange_hierarchy_removed_folder (hier, source);
		exchange_hierarchy_new_folder (hier, dest);
		scan_subtree (hier, dest);
		g_object_unref (dest);
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	} else {
		g_object_unref (dest);
		if (status == E2K_HTTP_FORBIDDEN ||
		    status == E2K_HTTP_UNAUTHORIZED)
			return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
		else
			return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
	}
}

static void
add_href (gpointer path, gpointer folder, gpointer hrefs)
{
	if (!strcmp (e_folder_get_type_string (folder), "noselect"))
		return;

	g_ptr_array_add (hrefs, path);
}

static void
rescan (ExchangeHierarchy *hier)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	const char *prop = E2K_PR_HTTPMAIL_UNREAD_COUNT;
	GPtrArray *hrefs;
	E2kResultIter *iter;
	E2kResult *result;
	EFolder *folder;
	int unread;

	if (
#ifdef OFFLINE_SUPPORT
	    exchange_account_is_offline (hier->account) ||
#endif
	    hier->type == EXCHANGE_HIERARCHY_PUBLIC)
		return;

	hrefs = g_ptr_array_new ();
	g_hash_table_foreach (hwd->priv->folders_by_internal_path,
			      add_href, hrefs);
	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		return;
	}

	g_object_ref (hier);
	iter = e_folder_exchange_bpropfind_start (hier->toplevel, NULL,
						  (const char **)hrefs->pdata,
						  hrefs->len,
						  &prop, 1);
	g_ptr_array_free (hrefs, TRUE);

	while ((result = e2k_result_iter_next (iter))) {
		folder = g_hash_table_lookup (hwd->priv->folders_by_internal_path,
					      e2k_uri_path (result->href));
		if (!folder)
			continue;

		prop = e2k_properties_get_prop (result->props,
						E2K_PR_HTTPMAIL_UNREAD_COUNT);
		if (!prop)
			continue;
		unread = atoi (prop);

		if (unread != e_folder_get_unread_count (folder)) {
			e_folder_set_unread_count (folder, unread);
			exchange_hierarchy_updated_folder (hier, folder);
		}
	}
	e2k_result_iter_free (iter);
	g_object_unref (hier);
}

ExchangeAccountFolderResult
exchange_hierarchy_webdav_status_to_folder_result (E2kHTTPStatus status)
{
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	else if (status == E2K_HTTP_NOT_FOUND)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	else if (status == E2K_HTTP_UNAUTHORIZED)
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
	else
		return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
}

EFolder *
exchange_hierarchy_webdav_parse_folder (ExchangeHierarchyWebDAV *hwd,
					EFolder *parent,
					E2kResult *result)
{
	EFolder *folder;
	ExchangeFolderType *folder_type;
	const char *name, *prop, *outlook_class, *permanenturl;
	int unread;
	gboolean hassubs;

	/* It's possible to have a localized inbox folder named, eg,
	 * "Innboks", with children whose URIs go through "Inbox"
	 * instead. (See bugzilla 27065.) This is probably related to
	 * the IMAP "INBOX" convention. Anyway, the important bit is
	 * that you can't know a folder's parent URI just by looking
	 * at its own URI. Since we only ever scan one folder at a
	 * time here, we just keep track of what the parent was. If we
	 * were going to read multiple folders at once, we could deal
	 * with this by fetching DAV:parentname.
	 */

	name = e2k_properties_get_prop (result->props,
					E2K_PR_DAV_DISPLAY_NAME);
	if (!name)
		return NULL;

	prop = e2k_properties_get_prop (result->props,
					E2K_PR_HTTPMAIL_UNREAD_COUNT);
	unread = prop ? atoi (prop) : 0;
	prop = e2k_properties_get_prop (result->props,
					E2K_PR_DAV_HAS_SUBS);
	hassubs = prop && atoi (prop);

	outlook_class = e2k_properties_get_prop (result->props,
						 E2K_PR_EXCHANGE_FOLDER_CLASS);
	folder_type = NULL;
	if (outlook_class)
		folder_type = g_hash_table_lookup (folder_type_map, outlook_class);
	if (!folder_type)
		folder_type = &folder_types[0]; /* mail */
	if (!outlook_class)
		outlook_class = folder_type->contentclass;

	permanenturl = e2k_properties_get_prop (result->props,
						E2K_PR_EXCHANGE_PERMANENTURL);

	folder = e_folder_webdav_new (EXCHANGE_HIERARCHY (hwd),
				      result->href, parent,
				      name, folder_type->component,
				      outlook_class, unread,
				      folder_type->offline_supported);
	if (hwd->priv->trash_path && !strcmp (e2k_uri_path (result->href), hwd->priv->trash_path))
		e_folder_set_custom_icon (folder, "stock_delete");
	if (hassubs)
		e_folder_exchange_set_has_subfolders (folder, TRUE);
	if (permanenturl)
		e_folder_exchange_set_permanent_uri (folder, permanenturl);

	return folder;
}

static const char *folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_HTTPMAIL_UNREAD_COUNT,
	E2K_PR_DAV_DISPLAY_NAME,
	E2K_PR_EXCHANGE_PERMANENTURL,
	E2K_PR_DAV_HAS_SUBS
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static ExchangeAccountFolderResult
scan_subtree (ExchangeHierarchy *hier, EFolder *parent)
{
	static E2kRestriction *folders_rn;
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	GSList *subtrees = NULL;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	EFolder *folder;

	if (!folders_rn) {
		folders_rn =
			e2k_restriction_andv (
				e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
							   E2K_RELOP_EQ, TRUE),
				e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
							   E2K_RELOP_EQ, FALSE),
				NULL);
	}

	iter = e_folder_exchange_search_start (parent, NULL,
					       folder_props, n_folder_props,
					       folders_rn, NULL, TRUE);
	while ((result = e2k_result_iter_next (iter))) {
		folder = exchange_hierarchy_webdav_parse_folder (hwd, parent, result);
		if (!folder)
			continue;

		if (hwd->priv->deep_searchable &&
		    e_folder_exchange_get_has_subfolders (folder)) {
			e_folder_exchange_set_has_subfolders (folder, FALSE);
			subtrees = g_slist_prepend (subtrees, folder);
		}
		exchange_hierarchy_new_folder (hier, folder);
		g_object_unref (folder);
	}
	status = e2k_result_iter_free (iter);

	while (subtrees) {
		folder = subtrees->data;
		subtrees = g_slist_remove (subtrees, folder);
		scan_subtree (hier, folder);
	}

	return exchange_hierarchy_webdav_status_to_folder_result (status);
}

struct scan_offline_data {
	ExchangeHierarchy *hier;
	ExchangeHierarchyWebDAVScanCallback callback;
	gpointer user_data;
	GPtrArray *badpaths;
};

static gboolean
scan_offline_cb (const char *physical_path, const char *path, gpointer data)
{
	struct scan_offline_data *sod = data;
	EFolder *folder;
	char *mf_name;

	mf_name = g_build_filename (physical_path, "connector-metadata.xml", NULL);
	folder = e_folder_exchange_new_from_file (sod->hier, mf_name);
	if (!folder) {
		unlink (mf_name);
		if (!sod->badpaths)
			sod->badpaths = g_ptr_array_new ();
		g_ptr_array_add (sod->badpaths, g_strdup (path));
		return TRUE;
	}
	g_free (mf_name);

	sod->callback (sod->hier, folder, sod->user_data);
	g_object_unref (folder);

	return TRUE;
}

/**
 * exchange_hierarchy_webdav_offline_scan_subtree:
 * @hier: a (webdav) hierarchy
 * @callbackb: a callback
 * @user_data: data for @cb
 *
 * Scans the offline folder tree cache for @hier and calls @cb
 * with each folder successfully constructed from offline data
 **/
void
exchange_hierarchy_webdav_offline_scan_subtree (ExchangeHierarchy *hier,
						ExchangeHierarchyWebDAVScanCallback callback,
						gpointer user_data)
{
	struct scan_offline_data sod;
	const char *path;
	char *dir, *prefix;
	int i;

	sod.hier = hier;
	sod.callback = callback;
	sod.user_data = user_data;
	sod.badpaths = NULL;

	path = e_folder_exchange_get_path (hier->toplevel);
	prefix = e2k_strdup_with_trailing_slash (path);
	dir = e_path_to_physical (hier->account->storage_dir, prefix);
	g_free (prefix);
	e_path_find_folders (dir, scan_offline_cb, &sod);

	if (sod.badpaths) {
		for (i = 0; i < sod.badpaths->len; i++) {
			e_path_rmdir (dir, sod.badpaths->pdata[i]);
			g_free (sod.badpaths->pdata[i]);
		}
		g_ptr_array_free (sod.badpaths, TRUE);
	}

	g_free (dir);
}

void
exchange_hierarchy_webdav_construct (ExchangeHierarchyWebDAV *hwd,
				     ExchangeAccount *account,
				     ExchangeHierarchyType type,
				     const char *hierarchy_name,
				     const char *physical_uri_prefix,
				     const char *internal_uri_prefix,
				     const char *owner_name,
				     const char *owner_email,
				     const char *source_uri,
				     gboolean deep_searchable)
{
	EFolder *toplevel;

	hwd->priv->deep_searchable = deep_searchable;

	toplevel = e_folder_exchange_new (EXCHANGE_HIERARCHY (hwd),
					  hierarchy_name,
					  "noselect", NULL, 
					  physical_uri_prefix,
					  internal_uri_prefix);
	e_folder_set_custom_icon (toplevel, "stock_folder");
	e_folder_exchange_set_has_subfolders (toplevel, TRUE);
	exchange_hierarchy_construct (EXCHANGE_HIERARCHY (hwd),
				      account, type, toplevel,
				      owner_name, owner_email, source_uri);
	g_object_unref (toplevel);

	if (type == EXCHANGE_HIERARCHY_PERSONAL) {
		const char *trash_uri;

		trash_uri = exchange_account_get_standard_uri (account, "deleteditems");
		if (trash_uri)
			hwd->priv->trash_path = e2k_strdup_with_trailing_slash (e2k_uri_path (trash_uri));
	}
}

ExchangeHierarchy *
exchange_hierarchy_webdav_new (ExchangeAccount *account,
			       ExchangeHierarchyType type,
			       const char *hierarchy_name,
			       const char *physical_uri_prefix,
			       const char *internal_uri_prefix,
			       const char *owner_name,
			       const char *owner_email,
			       const char *source_uri,
			       gboolean deep_searchable)
{
	ExchangeHierarchy *hier;

	hier = g_object_new (EXCHANGE_TYPE_HIERARCHY_WEBDAV, NULL);

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hier),
					     account, type, hierarchy_name,
					     physical_uri_prefix,
					     internal_uri_prefix,
					     owner_name, owner_email,
					     source_uri, deep_searchable);
	return hier;
}
