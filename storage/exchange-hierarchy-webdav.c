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
#include "e2k-connection.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"

#include <e-util/e-passwords.h>
#include <e-util/e-path.h>

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
static void async_scan_subtree (ExchangeHierarchy *hier, EFolder *folder,
				ExchangeAccountFolderCallback callback,
				gpointer user_data);
static void async_create_folder (ExchangeHierarchy *hier, EFolder *parent,
				 const char *name, const char *type,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void async_remove_folder (ExchangeHierarchy *hier, EFolder *folder,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void async_xfer_folder (ExchangeHierarchy *hier, EFolder *source,
			       EFolder *dest_parent, const char *dest_name,
			       gboolean remove_source,
			       ExchangeAccountFolderCallback callback,
			       gpointer user_data);

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
	exchange_hierarchy_class->async_scan_subtree = async_scan_subtree;
	exchange_hierarchy_class->async_create_folder = async_create_folder;
	exchange_hierarchy_class->async_remove_folder = async_remove_folder;
	exchange_hierarchy_class->async_xfer_folder = async_xfer_folder;
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

struct folder_data {
	ExchangeHierarchyWebDAV *hwd;
	EFolder *source, *dest;

	ExchangeAccountFolderCallback callback;
	gpointer user_data;
};

static void
finish_folder_op (struct folder_data *fd, ExchangeAccountFolderResult result)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (fd->hwd);

	if (result == EXCHANGE_ACCOUNT_FOLDER_OK) {
		if (fd->dest)
			exchange_hierarchy_new_folder (hier, fd->dest);
		fd->callback (hier->account, result, fd->dest, fd->user_data);
		if (fd->source)
			exchange_hierarchy_removed_folder (hier, fd->source);
	} else
		fd->callback (hier->account, result, NULL, fd->user_data);

	if (fd->dest)
		g_object_unref (fd->dest);
	g_object_unref (fd->hwd);
	g_free (fd);
}

static void
created_folder (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	struct folder_data *fd = user_data;

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode))
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_OK);
	else if (msg->errorcode == SOUP_ERROR_METHOD_NOT_ALLOWED)
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS);
	else if (msg->errorcode == SOUP_ERROR_CONFLICT)
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST);
	else if (msg->errorcode == SOUP_ERROR_FORBIDDEN)
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED);
	else
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
}

static void
async_create_folder (ExchangeHierarchy *hier, EFolder *parent,
		     const char *name, const char *type,
		     ExchangeAccountFolderCallback callback,
		     gpointer user_data)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	struct folder_data *fd;
	E2kProperties *props;
	int i;

#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account)) {
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OFFLINE, NULL, user_data);
		return;
	}
#endif

	fd = g_new0 (struct folder_data, 1);
	fd->hwd = hwd;
	g_object_ref (hwd);
	fd->callback = callback;
	fd->user_data = user_data;

	for (i = 0; folder_types[i].component; i++) {
		if (!strcmp (folder_types[i].component, type))
			break;
	}
	if (!folder_types[i].component) {
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_UNKNOWN_TYPE);
		return;
	}

	fd->dest = e_folder_webdav_new (hier, NULL, parent, name, type,
					folder_types[i].contentclass, 0,
					folder_types[i].offline_supported);

	props = e2k_properties_new ();
	e2k_properties_set_string (props, E2K_PR_EXCHANGE_FOLDER_CLASS,
				   g_strdup (folder_types[i].contentclass));

	E2K_DEBUG_HINT ('S');
	e_folder_exchange_mkcol (fd->dest, props, created_folder, fd);
	e2k_properties_free (props);
}

/* This is the callback for both remove and xfer */
static void
xferred_folder (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	struct folder_data *fd = user_data;

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		if (fd->dest) {
			async_scan_subtree (EXCHANGE_HIERARCHY (fd->hwd),
					    fd->dest, NULL, NULL);
		}
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_OK);
	} else
		finish_folder_op (fd, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
}

static void
async_remove_folder (ExchangeHierarchy *hier, EFolder *folder,
		     ExchangeAccountFolderCallback callback,
		     gpointer user_data)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	struct folder_data *fd;

#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account)) {
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OFFLINE, NULL, user_data);
		return;
	}
#endif

	if (folder == hier->toplevel) {
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED, NULL, user_data);
		return;
	}

	fd = g_new0 (struct folder_data, 1);
	fd->hwd = hwd;
	g_object_ref (hwd);
	fd->source = folder;
	fd->callback = callback;
	fd->user_data = user_data;

	E2K_DEBUG_HINT ('S');
	e_folder_exchange_delete (folder, xferred_folder, fd);
}

static void
async_xfer_folder (ExchangeHierarchy *hier, EFolder *source,
		   EFolder *dest_parent, const char *dest_name,
		   gboolean remove_source,
		   ExchangeAccountFolderCallback callback,
		   gpointer user_data)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	struct folder_data *fd;

#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account)) {
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OFFLINE, NULL, user_data);
		return;
	}
#endif

	if (source == hier->toplevel) {
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR, NULL, user_data);
		return;
	}

	fd = g_new0 (struct folder_data, 1);
	fd->hwd = hwd;
	g_object_ref (hwd);
	fd->callback = callback;
	fd->user_data = user_data;

	if (remove_source)
		fd->source = source;
	fd->dest = e_folder_webdav_new (hier, NULL, dest_parent, dest_name,
					e_folder_get_type_string (source),
					e_folder_exchange_get_outlook_class (source),
					e_folder_get_unread_count (source),
					e_folder_get_can_sync_offline (source));

	E2K_DEBUG_HINT ('S');
	e_folder_exchange_transfer_dir (source, fd->dest, remove_source,
					xferred_folder, fd);
}

static void
rescanned (E2kConnection *conn, SoupMessage *msg,
	   E2kResult *results, int nresults, gpointer user_data)
{
	ExchangeHierarchy *hier = user_data;
	ExchangeHierarchyWebDAV *hwd = user_data;
	EFolder *folder;
	const char *prop;
	int i, unread;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode))
		return;

	for (i = 0; i < nresults; i++) {
		folder = g_hash_table_lookup (hwd->priv->folders_by_internal_path,
					      e2k_uri_path (results[i].href));
		if (!folder)
			continue;

		prop = e2k_properties_get_prop (results[i].props,
						E2K_PR_HTTPMAIL_UNREAD_COUNT);
		if (!prop)
			continue;
		unread = atoi (prop);

		if (unread != e_folder_get_unread_count (folder)) {
			e_folder_set_unread_count (folder, unread);
			exchange_hierarchy_updated_folder (hier, folder);
		}
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
	GPtrArray *hrefs;
	const char *prop = E2K_PR_HTTPMAIL_UNREAD_COUNT;

	if (
#ifdef OFFLINE_SUPPORT
	    exchange_account_is_offline (hier->account) ||
#endif
	    hier->type == EXCHANGE_HIERARCHY_PUBLIC)
		return;

	hrefs = g_ptr_array_new ();
	g_hash_table_foreach (hwd->priv->folders_by_internal_path, add_href, hrefs);
	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		return;
	}

	e_folder_exchange_bpropfind (hier->toplevel,
				     (const char **)hrefs->pdata, hrefs->len, "0",
				     &prop, 1, rescanned, hier);
	g_ptr_array_free (hrefs, TRUE);
}

ExchangeAccountFolderResult
exchange_hierarchy_webdav_parse_folders (ExchangeHierarchyWebDAV *hwd,
					 SoupMessage *msg,
					 EFolder *parent,
					 E2kResult *results,
					 int nresults,
					 GPtrArray **folders_p)
{
	GPtrArray *folders;
	EFolder *folder;
	ExchangeFolderType *folder_type;
	const char *prop, *outlook_class;
	char *name;
	int i, unread;
	gboolean hassubs;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		if (msg->errorcode == SOUP_ERROR_NOT_FOUND)
			return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
		else if (msg->errorcode == SOUP_ERROR_CANT_AUTHENTICATE)
			return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
		else
			return EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
	}

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

	folders = g_ptr_array_new ();

	for (i = 0; i < nresults; i++) {
		name = e2k_properties_get_prop (results[i].props,
						E2K_PR_DAV_DISPLAY_NAME);
		if (!name)
			continue;

		prop = e2k_properties_get_prop (results[i].props,
						E2K_PR_HTTPMAIL_UNREAD_COUNT);
		unread = prop ? atoi (prop) : 0;
		prop = e2k_properties_get_prop (results[i].props,
						E2K_PR_DAV_HAS_SUBS);
		hassubs = prop && atoi (prop);

		outlook_class = e2k_properties_get_prop (results[i].props,
							 E2K_PR_EXCHANGE_FOLDER_CLASS);
		folder_type = NULL;
		if (outlook_class)
			folder_type = g_hash_table_lookup (folder_type_map, outlook_class);
		if (!folder_type)
			folder_type = &folder_types[0]; /* mail */
		if (!outlook_class)
			outlook_class = folder_type->contentclass;

		folder = e_folder_webdav_new (EXCHANGE_HIERARCHY (hwd),
					      results[i].href, parent,
					      name, folder_type->component,
					      outlook_class, unread,
					      folder_type->offline_supported);
		if (hwd->priv->trash_path && !strcmp (e2k_uri_path (results[i].href), hwd->priv->trash_path))
			e_folder_set_custom_icon (folder, "evolution-trash");
		if (hassubs)
			e_folder_exchange_set_has_subfolders (folder, TRUE);

		g_ptr_array_add (folders, folder);
	}

	*folders_p = folders;
	return EXCHANGE_ACCOUNT_FOLDER_OK;
}

struct scan_master_data {
	ExchangeHierarchyWebDAV *hwd;
	EFolder *top;
	ExchangeAccountFolderCallback callback;
	gpointer user_data;
	int pending;
};

struct scan_folder_data {
	struct scan_master_data *smd;
	EFolder *parent;
};

static void async_scan_one_subtree (struct scan_master_data *smd, EFolder *folder);

static void
got_subtree (E2kConnection *conn, SoupMessage *msg,
	     E2kResult *results, int nresults,
	     gpointer user_data)
{
	struct scan_folder_data *sfd = user_data;
	struct scan_master_data *smd = sfd->smd;
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (smd->hwd);
	ExchangeAccountFolderResult result;
	GPtrArray *folders;
	EFolder *folder;
	int i;

	result = exchange_hierarchy_webdav_parse_folders (
		sfd->smd->hwd, msg, sfd->parent, results, nresults, &folders);
	g_object_unref (sfd->parent);
	g_free (sfd);
	smd->pending--;

	if (result == EXCHANGE_ACCOUNT_FOLDER_OK) {
		for (i = 0; i < folders->len; i++) {
			folder = folders->pdata[i];

			if (smd->hwd->priv->deep_searchable &&
			    e_folder_exchange_get_has_subfolders (folder)) {
				e_folder_exchange_set_has_subfolders (folder, FALSE);
				async_scan_one_subtree (smd, folder);
			}
			exchange_hierarchy_new_folder (hier, folder);
			g_object_unref (folder);
		}
		g_ptr_array_free (folders, TRUE);
	}

	if (smd->pending)
		return;

	exchange_hierarchy_scanned_folder (hier, smd->top);
	if (smd->callback) {
		smd->callback (hier->account, result,
			       smd->top, smd->user_data);
	}
	g_object_unref (smd->hwd);
	g_object_unref (smd->top);
	g_free (smd);
}

static const char *folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_HTTPMAIL_UNREAD_COUNT,
	E2K_PR_DAV_DISPLAY_NAME,
	E2K_PR_DAV_HAS_SUBS
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static void
async_scan_one_subtree (struct scan_master_data *smd, EFolder *folder)
{
	struct scan_folder_data *sfd;
	static E2kRestriction *folders_rn;

	if (!folders_rn) {
		folders_rn =
			e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
						   E2K_RELOP_EQ, FALSE);
	}

	sfd = g_new (struct scan_folder_data, 1);
	sfd->smd = smd;
	sfd->parent = folder;
	g_object_ref (sfd->parent);
	smd->pending++;

	E2K_DEBUG_HINT ('S');
	e_folder_exchange_search (folder, folder_props, n_folder_props,
				  TRUE, folders_rn, NULL, got_subtree, sfd);
}

static void
async_scan_subtree_online (ExchangeHierarchy *hier, EFolder *folder,
			   ExchangeAccountFolderCallback callback,
			   gpointer user_data)
{
	ExchangeHierarchyWebDAV *hwd = EXCHANGE_HIERARCHY_WEBDAV (hier);
	struct scan_master_data *smd;

	smd = g_new (struct scan_master_data, 1);
	smd->hwd = hwd;
	g_object_ref (smd->hwd);
	smd->top = folder;
	g_object_ref (smd->top);
	smd->callback = callback;
	smd->user_data = user_data;
	smd->pending = 0;

	async_scan_one_subtree (smd, folder);
}

#ifdef OFFLINE_SUPPORT
static void
scan_cb (ExchangeHierarchy *hier, EFolder *folder, gpointer data)
{
	ExchangeFolderType *folder_type;

	folder_type = g_hash_table_lookup (folder_type_map, e_folder_get_type_string (folder));
	if (folder_type && folder_type->offline_supported)
		e_folder_set_can_sync_offline (folder, TRUE);

	exchange_hierarchy_new_folder (hier, folder);
}

static void
async_scan_subtree_offline (ExchangeHierarchy *hier, EFolder *folder,
			    ExchangeAccountFolderCallback callback,
			    gpointer user_data)
{
	if (folder == hier->toplevel) {
		exchange_hierarchy_webdav_offline_scan_subtree (hier, scan_cb, NULL);
		exchange_hierarchy_scanned_folder (hier, folder);
	}
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OK, NULL, user_data);
}
#endif

static void
async_scan_subtree (ExchangeHierarchy *hier, EFolder *folder,
		    ExchangeAccountFolderCallback callback,
		    gpointer user_data)
{
#ifdef OFFLINE_SUPPORT
	if (exchange_account_is_offline (hier->account))
		async_scan_subtree_offline (hier, folder, callback, user_data);
	else
#endif
		async_scan_subtree_online (hier, folder, callback, user_data);
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
				     gboolean deep_searchable,
				     const char *toplevel_icon,
				     int sorting_priority)
{
	EFolder *toplevel;

	hwd->priv->deep_searchable = deep_searchable;

	toplevel = e_folder_exchange_new (EXCHANGE_HIERARCHY (hwd),
					  hierarchy_name,
					  "noselect", NULL, 
					  physical_uri_prefix,
					  internal_uri_prefix);
	e_folder_set_custom_icon (toplevel, toplevel_icon);
	e_folder_set_sorting_priority (toplevel, sorting_priority);
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
			       gboolean deep_searchable,
			       const char *toplevel_icon,
			       int sorting_priority)
{
	ExchangeHierarchy *hier;

	hier = g_object_new (EXCHANGE_TYPE_HIERARCHY_WEBDAV, NULL);

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hier),
					     account, type, hierarchy_name,
					     physical_uri_prefix,
					     internal_uri_prefix,
					     owner_name, owner_email,
					     source_uri, deep_searchable,
					     toplevel_icon, sorting_priority);
	return hier;
}
