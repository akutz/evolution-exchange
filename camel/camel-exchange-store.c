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

/* camel-exchange-store.c: class for a exchange store */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-util/e-path.h"

#include "camel-exchange-store.h"
#include "camel-exchange-folder.h"
#include <camel/camel-session.h>

#include <gal/util/e-util.h>

static CamelStoreClass *parent_class = NULL;

#define CS_CLASS(so) ((CamelStoreClass *)((CamelObject *)(so))->klass)

static void construct (CamelService *service, CamelSession *session,
		       CamelProvider *provider, CamelURL *url,
		       CamelException *ex);

static GList *query_auth_types (CamelService *service, CamelException *ex);
static char  *get_name         (CamelService *service, gboolean brief);
static CamelFolder     *get_trash       (CamelStore *store,
					 CamelException *ex);

static gboolean exchange_connect (CamelService *service, CamelException *ex);
static gboolean exchange_disconnect (CamelService *service, gboolean clean, CamelException *ex);

static CamelFolder *exchange_get_folder (CamelStore *store, const char *folder_name,
					 guint32 flags, CamelException *ex);

static CamelFolderInfo *exchange_get_folder_info (CamelStore *store, const char *top,
						  guint32 flags, CamelException *ex);

static CamelFolderInfo *exchange_create_folder (CamelStore *store,
						const char *parent_name,
						const char *folder_name,
						CamelException *ex);
static void             exchange_delete_folder (CamelStore *store,
						const char *folder_name,
						CamelException *ex);
static void             exchange_rename_folder (CamelStore *store,
						const char *old_name,
						const char *new_name,
						CamelException *ex);

static void stub_notification (CamelObject *object, gpointer event_data, gpointer user_data);

static void
class_init (CamelExchangeStoreClass *camel_exchange_store_class)
{
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_exchange_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_exchange_store_class);
	
	parent_class = CAMEL_STORE_CLASS (camel_type_get_global_classfuncs (camel_store_get_type ()));
	
	/* virtual method overload */
	camel_service_class->construct = construct;
	camel_service_class->query_auth_types = query_auth_types;
	camel_service_class->get_name = get_name;
	camel_service_class->connect = exchange_connect;
	camel_service_class->disconnect = exchange_disconnect;
	
	camel_store_class->get_trash = get_trash;
	camel_store_class->free_folder_info = camel_store_free_folder_info_full;
	camel_store_class->get_folder = exchange_get_folder;
	camel_store_class->get_folder_info = exchange_get_folder_info;
	camel_store_class->create_folder = exchange_create_folder;
	camel_store_class->delete_folder = exchange_delete_folder;
	camel_store_class->rename_folder = exchange_rename_folder;
}

static void
init (CamelExchangeStore *exch, CamelExchangeStoreClass *klass)
{
	CamelStore *store = CAMEL_STORE (exch);

	exch->folders_lock = g_mutex_new ();
	exch->folders = g_hash_table_new (g_str_hash, g_str_equal);

	store->flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);
}

static void
finalize (CamelExchangeStore *exch)
{
	if (exch->stub) {
		camel_object_unref (CAMEL_OBJECT (exch->stub));
		exch->stub = NULL;
	}
	if (exch->folders_lock)
		g_mutex_free (exch->folders_lock);
}

CamelType
camel_exchange_store_get_type (void)
{
	static CamelType camel_exchange_store_type = CAMEL_INVALID_TYPE;

	if (!camel_exchange_store_type) {
		camel_exchange_store_type = camel_type_register (
			CAMEL_STORE_TYPE,
			"CamelExchangeStore",
			sizeof (CamelExchangeStore),
			sizeof (CamelExchangeStoreClass),
			(CamelObjectClassInitFunc) class_init,
			NULL,
			(CamelObjectInitFunc) init,
			(CamelObjectFinalizeFunc) finalize);
	}

	return camel_exchange_store_type;
}

static void
construct (CamelService *service, CamelSession *session,
	   CamelProvider *provider, CamelURL *url, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (service);
	char *p;
	
	CAMEL_SERVICE_CLASS (parent_class)->construct (service, session, provider, url, ex);

	exch->base_url = camel_url_to_string (url, CAMEL_URL_HIDE_ALL);
	/* Strip path */
	p = strstr (exch->base_url, "//");
	if (p) {
		p = strchr (p + 2, '/');
		if (p)
			*p = '\0';
	}

	exch->storage_path = camel_session_get_storage_path (session, service, ex);
}

extern CamelServiceAuthType camel_exchange_password_authtype;
extern CamelServiceAuthType camel_exchange_ntlm_authtype;

static GList *
query_auth_types (CamelService *service, CamelException *ex)
{
	return g_list_prepend (g_list_prepend (NULL, &camel_exchange_password_authtype),
			       &camel_exchange_ntlm_authtype);
}

static char *
get_name (CamelService *service, gboolean brief)
{
	if (brief) {
		return g_strdup_printf (_("Exchange server %s"),
					service->url->host);
	} else {
		return g_strdup_printf (_("Exchange account for %s on %s"),
					service->url->user,
					service->url->host);
	}
}

#define EXCHANGE_STOREINFO_VERSION 1

static gboolean
exchange_connect (CamelService *service, CamelException *ex)
{
	CamelExchangeStore *store = CAMEL_EXCHANGE_STORE (service);
	char *real_user, *socket_path;
	
	if (!store->storage_path)
		return FALSE;
	
	real_user = strpbrk (service->url->user, "\\/");
	if (real_user)
		real_user++;
	else
		real_user = service->url->user;
	socket_path = g_strdup_printf ("/tmp/.exchange-%s/%s@%s",
				       g_get_user_name (),
				       real_user, service->url->host);
	e_filename_make_safe (strchr (socket_path + 5, '/') + 1);
	
	store->stub = camel_stub_new (socket_path, _("Evolution Exchange backend process"), ex);
	g_free (socket_path);
	if (!store->stub) 
		return FALSE;

	/* Initialize the stub connection */
	if (!camel_stub_send (store->stub, NULL, CAMEL_STUB_CMD_CONNECT,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_END)) {
		/* The user cancelled the connection attempt. */
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     "Cancelled");
		camel_object_unref (CAMEL_OBJECT (store->stub));
		store->stub = NULL;
		return FALSE;
	}

	camel_object_hook_event (CAMEL_OBJECT (store->stub), "notification",
				 stub_notification, store);
	
	return TRUE;
}

static gboolean
exchange_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (service);

	if (exch->stub) {
		camel_object_unref (CAMEL_OBJECT (exch->stub));
		exch->stub = NULL;
	}

	g_free (exch->trash_name);
	exch->trash_name = NULL;

	return TRUE;
}

#define RETURN_VAL_IF_NOT_CONNECTED(service, ex, val) \
	if (!camel_service_connect ((CamelService *)service, ex)) \
		return val;

static CamelFolder *
exchange_get_folder (CamelStore *store, const char *folder_name,
		     guint32 camel_flags, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	CamelFolder *folder;
	char *folder_dir;

	RETURN_VAL_IF_NOT_CONNECTED (store, ex, NULL);

	g_mutex_lock (exch->folders_lock);
	folder = g_hash_table_lookup (exch->folders, folder_name);
	if (folder) {
		/* This shouldn't actually happen, it should be caught
		 * by the store-level cache...
		 */
		g_mutex_unlock (exch->folders_lock);
		camel_object_ref (CAMEL_OBJECT (folder));
		return folder;
	}

	folder = (CamelFolder *)camel_object_new (CAMEL_EXCHANGE_FOLDER_TYPE);
	g_hash_table_insert (exch->folders, g_strdup (folder_name), folder);
	g_mutex_unlock (exch->folders_lock);

	folder_dir = e_path_to_physical (exch->storage_path, folder_name);
	if (!camel_exchange_folder_construct (folder, store, folder_name,
					      camel_flags, folder_dir,
					      exch->stub, ex)) {
		g_free (folder_dir);
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}
	g_free (folder_dir);

	/* If you move messages into a folder you haven't visited yet, it
	 * may create and then unref the folder. That's a waste. So don't
	 * let that happen. Probably not the best fix... 
	 */
	camel_object_ref (CAMEL_OBJECT (folder));

	return folder;
}

static CamelFolder *
get_trash (CamelStore *store, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);

	RETURN_VAL_IF_NOT_CONNECTED (store, ex, NULL);

	if (!exch->trash_name) {
		if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_TRASH_NAME,
				      CAMEL_STUB_ARG_RETURN,
				      CAMEL_STUB_ARG_STRING, &exch->trash_name,
				      CAMEL_STUB_ARG_END))
			return NULL;
	}

	return CS_CLASS (store)->get_folder (store, exch->trash_name, 0, ex);
}

/* Note: steals @name and @uri */
static CamelFolderInfo *
make_folder_info (CamelExchangeStore *exch, char *name, char *uri,
		  int unread_count, int flags)
{
	CamelFolderInfo *info;
	const char *path;

	path = strstr (uri, "://");
	if (!path)
		return NULL;
	path = strchr (path + 3, '/');
	if (!path)
		return NULL;

	info = g_new0 (CamelFolderInfo, 1);
	info->name = name;
	info->uri = uri;
	info->full_name = g_strdup (path + 1);
	info->unread = unread_count;

	if (flags & CAMEL_STUB_FOLDER_NOSELECT)
		info->flags = CAMEL_FOLDER_NOSELECT;

	return info;
}

static CamelFolderInfo *
postprocess_tree (CamelFolderInfo *info)
{
	CamelFolderInfo *sibling;

	if (info->child)
		info->child = postprocess_tree (info->child);
	if (info->next)
		info->next = postprocess_tree (info->next);

	/* If the node still has children, keep it */
	if (info->child)
		return info;
	info->flags |= CAMEL_FOLDER_NOCHILDREN;

	/* If it's a mail folder (not noselect), keep it */
	if (!(info->flags & CAMEL_FOLDER_NOSELECT))
		return info;

	/* Otherwise delete it and return its sibling */
	sibling = info->next;
	info->next = NULL;
	camel_folder_info_free (info);
	return sibling;
}
			

static CamelFolderInfo *
exchange_get_folder_info (CamelStore *store, const char *top,
			  guint32 flags, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	GPtrArray *folders, *folder_names, *folder_uris;
	GArray *unread_counts;
	GByteArray *folder_flags;
	CamelFolderInfo *info;
	int i;

	RETURN_VAL_IF_NOT_CONNECTED (store, ex, NULL);

	/* If the backend crashed, don't keep returning an error
	 * each time auto-send/recv runs.
	 */
	if (camel_stub_marshal_eof (exch->stub->cmd))
		return NULL;

	if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_FOLDER_INFO,
			      CAMEL_STUB_ARG_STRING, top,
			      CAMEL_STUB_ARG_UINT32, (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE) != 0,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_STRINGARRAY, &folder_names,
			      CAMEL_STUB_ARG_STRINGARRAY, &folder_uris,
			      CAMEL_STUB_ARG_UINT32ARRAY, &unread_counts,
			      CAMEL_STUB_ARG_BYTEARRAY, &folder_flags,
			      CAMEL_STUB_ARG_END))
		return NULL;
	if (!folder_names) {
		/* This means the storage hasn't finished scanning yet.
		 * We return NULL for now and will emit folder_created
		 * events later.
		 */
		return NULL;
	}

	folders = g_ptr_array_new ();
	for (i = 0; i < folder_names->len; i++) {
		info = make_folder_info (exch, folder_names->pdata[i],
					 folder_uris->pdata[i],
					 g_array_index (unread_counts, int, i),
					 folder_flags->data[i]);
		if (info)
			g_ptr_array_add (folders, info);
	}
	g_ptr_array_free (folder_names, TRUE);
	g_ptr_array_free (folder_uris, TRUE);
	g_array_free (unread_counts, TRUE);
	g_byte_array_free (folder_flags, TRUE);

	info = camel_folder_info_build (folders, top, '/', TRUE);
	if (info)
		info = postprocess_tree (info);
	g_ptr_array_free (folders, TRUE);

	return info;
}

static CamelFolderInfo *
exchange_create_folder (CamelStore *store, const char *parent_name,
			const char *folder_name, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	char *folder_uri;
	guint32 unread_count, flags;
	CamelFolderInfo *info;

	if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_CREATE_FOLDER,
			      CAMEL_STUB_ARG_FOLDER, parent_name,
			      CAMEL_STUB_ARG_STRING, folder_name,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_STRING, &folder_uri,
			      CAMEL_STUB_ARG_UINT32, &unread_count,
			      CAMEL_STUB_ARG_UINT32, &flags,
			      CAMEL_STUB_ARG_END))
		return NULL;

	info = make_folder_info (exch, g_strdup (folder_name),
				 folder_uri, unread_count, flags);
	info->flags |= CAMEL_FOLDER_NOCHILDREN;
	return info;
}

static void
exchange_delete_folder (CamelStore *store, const char *folder_name,
			CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);

	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_DELETE_FOLDER,
			 CAMEL_STUB_ARG_FOLDER, folder_name,
			 CAMEL_STUB_ARG_END);
}

static void
exchange_rename_folder (CamelStore *store, const char *old_name,
			const char *new_name, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);

	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_RENAME_FOLDER,
			 CAMEL_STUB_ARG_FOLDER, old_name,
			 CAMEL_STUB_ARG_FOLDER, new_name,
			 CAMEL_STUB_ARG_END);
}

static void
stub_notification (CamelObject *object, gpointer event_data, gpointer user_data)
{
	CamelStub *stub = CAMEL_STUB (object);
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (user_data);
	guint32 retval = GPOINTER_TO_UINT (event_data);

	switch (retval) {
	case CAMEL_STUB_RETVAL_NEW_MESSAGE:
	{
		CamelExchangeFolder *folder;
		char *folder_name, *uid, *headers;
		guint32 flags, size;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1 ||
		    camel_stub_marshal_decode_uint32 (stub->status, &flags) == -1 ||
		    camel_stub_marshal_decode_uint32 (stub->status, &size) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &headers) == -1)
			return;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder) {
			camel_exchange_folder_add_message (folder, uid, flags,
							   size, headers);
		}

		g_free (folder_name);
		g_free (uid);
		g_free (headers);
		break;
	}

	case CAMEL_STUB_RETVAL_REMOVED_MESSAGE:
	{
		CamelExchangeFolder *folder;
		char *folder_name, *uid;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1)
			return;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_exchange_folder_remove_message (folder, uid);

		g_free (folder_name);
		g_free (uid);
		break;
	}

	case CAMEL_STUB_RETVAL_CHANGED_MESSAGE:
	{
		CamelExchangeFolder *folder;
		char *folder_name, *uid;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_exchange_folder_uncache_message (folder, uid);

		g_free (folder_name);
		g_free (uid);
		break;
	}

	case CAMEL_STUB_RETVAL_CHANGED_FLAGS:
	{
		CamelExchangeFolder *folder;
		char *folder_name, *uid;
		guint32 flags;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1 ||
		    camel_stub_marshal_decode_uint32 (stub->status, &flags) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_exchange_folder_update_message_flags (folder, uid, flags);

		g_free (folder_name);
		g_free (uid);
		break;
	}

	case CAMEL_STUB_RETVAL_CHANGED_TAG:
	{
		CamelExchangeFolder *folder;
		char *folder_name, *uid, *name, *value;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &value) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_exchange_folder_update_message_tag (folder, uid, name, value);

		g_free (folder_name);
		g_free (uid);
		g_free (name);
		g_free (value);
		break;
	}

	case CAMEL_STUB_RETVAL_FREEZE_FOLDER:
	{
		CamelFolder *folder;
		char *folder_name;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_folder_freeze (folder);

		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_RETVAL_THAW_FOLDER:
	{
		CamelFolder *folder;
		char *folder_name;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_folder_thaw (folder);

		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_RETVAL_FOLDER_CREATED:
	{
		CamelFolderInfo *info;
		char *name, *uri;

		if (camel_stub_marshal_decode_string (stub->status, &name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uri) == -1)
			break;

		info = make_folder_info (exch, name, uri, -1, 0);
		info->flags |= CAMEL_FOLDER_NOCHILDREN;
		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_created", info);
		camel_folder_info_free (info);
		break;
	}

	case CAMEL_STUB_RETVAL_FOLDER_DELETED:
	{
		CamelFolderInfo *info;
		char *name, *uri;

		if (camel_stub_marshal_decode_string (stub->status, &name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uri) == -1)
			break;

		info = make_folder_info (exch, name, uri, -1, 0);
		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_deleted", info);
		camel_folder_info_free (info);
		break;
	}

	case CAMEL_STUB_RETVAL_FOLDER_RENAMED:
	{
		char *new_name, *new_uri;
		CamelRenameInfo reninfo;

		if (camel_stub_marshal_decode_folder (stub->status, &reninfo.old_base) == -1 ||
		    camel_stub_marshal_decode_folder (stub->status, &new_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &new_uri) == -1)
			break;

		reninfo.new = make_folder_info (exch, new_name, new_uri, -1, 0);
		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_renamed", &reninfo);
		camel_folder_info_free (reninfo.new);
		g_free (reninfo.old_base);
		break;
	}

	default:
		g_assert_not_reached ();
		break;
	}
}
