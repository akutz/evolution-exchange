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

#include <string.h>

#include "e-util/e-path.h"

#include "camel-exchange-store.h"
#include "camel-exchange-folder.h"
#include <camel/camel-disco-diary.h>
#include <camel/camel-session.h>

#include <gal/util/e-util.h>

static CamelDiscoStoreClass *parent_class = NULL;

#define CS_CLASS(so) ((CamelStoreClass *)((CamelObject *)(so))->klass)

static void construct (CamelService *service, CamelSession *session,
		       CamelProvider *provider, CamelURL *url,
		       CamelException *ex);

static GList *query_auth_types (CamelService *service, CamelException *ex);
static char  *get_name         (CamelService *service, gboolean brief);
static CamelFolder     *get_trash       (CamelStore *store,
					 CamelException *ex);

static gboolean exchange_can_work_offline (CamelDiscoStore *disco_store);
static gboolean exchange_connect_online (CamelService *service, CamelException *ex);
static gboolean exchange_disconnect_online (CamelService *service, gboolean clean, CamelException *ex);

static CamelFolder *exchange_get_folder (CamelStore *store, const char *folder_name,
					 guint32 flags, CamelException *ex);

static CamelFolderInfo *exchange_get_folder_info_online (CamelStore *store, const char *top,
							 guint32 flags, CamelException *ex);

static void stub_notification (CamelObject *object, gpointer event_data, gpointer user_data);

extern void mail_note_store (CamelStore *store, gpointer storage,
			     gpointer corba_storage,
			     void (*done) (CamelStore *store,
					   CamelFolderInfo *info,
					   void *data),
			     void *data);

static void
class_init (CamelExchangeStoreClass *camel_exchange_store_class)
{
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_exchange_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_exchange_store_class);
	CamelDiscoStoreClass *camel_disco_store_class =
		CAMEL_DISCO_STORE_CLASS (camel_exchange_store_class);
	
	parent_class = CAMEL_DISCO_STORE_CLASS (camel_type_get_global_classfuncs (camel_disco_store_get_type ()));
	
	/* virtual method overload */
	camel_service_class->construct = construct;
	camel_service_class->query_auth_types = query_auth_types;
	camel_service_class->get_name = get_name;
	
	camel_store_class->get_trash = get_trash;
	camel_store_class->free_folder_info = camel_store_free_folder_info_full;
	
	camel_disco_store_class->can_work_offline = exchange_can_work_offline;
	camel_disco_store_class->connect_online = exchange_connect_online;
	camel_disco_store_class->connect_offline = exchange_connect_online;
	camel_disco_store_class->disconnect_online = exchange_disconnect_online;
	camel_disco_store_class->disconnect_offline = exchange_disconnect_online;
	camel_disco_store_class->get_folder_online = exchange_get_folder;
	camel_disco_store_class->get_folder_offline = exchange_get_folder;
	camel_disco_store_class->get_folder_resyncing = exchange_get_folder;
	camel_disco_store_class->get_folder_info_online = exchange_get_folder_info_online;
	camel_disco_store_class->get_folder_info_offline = exchange_get_folder_info_online;
	camel_disco_store_class->get_folder_info_resyncing = exchange_get_folder_info_online;
}

static void
init (CamelExchangeStore *exch, CamelExchangeStoreClass *klass)
{
	exch->folders = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
finalize (CamelExchangeStore *exch)
{
	if (exch->stub) {
		camel_object_unref (CAMEL_OBJECT (exch->stub));
		exch->stub = NULL;
	}
	if (exch->note_idle)
		g_source_remove (exch->note_idle);
}

CamelType
camel_exchange_store_get_type (void)
{
	static CamelType camel_exchange_store_type = CAMEL_INVALID_TYPE;

	if (!camel_exchange_store_type) {
		camel_exchange_store_type = camel_type_register (
			CAMEL_DISCO_STORE_TYPE,
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

static gboolean
note_store (gpointer store)
{
	mail_note_store (store, NULL, NULL, NULL, NULL);
	CAMEL_EXCHANGE_STORE (store)->note_idle = 0;
	return FALSE;
}

static gboolean
exchange_can_work_offline (CamelDiscoStore *disco_store)
{
	return FALSE;
}

#define EXCHANGE_STOREINFO_VERSION 1

static gboolean
exchange_connect_online (CamelService *service, CamelException *ex)
{
	CamelExchangeStore *store = CAMEL_EXCHANGE_STORE (service);
	CamelDiscoStore *disco_store = CAMEL_DISCO_STORE (service);
	char *real_user, *socket_path, *path;
	
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

	path = g_strdup_printf ("%s/journal", store->storage_path);
	disco_store->diary = camel_disco_diary_new (disco_store, path, ex);
	g_free (path);
	if (!disco_store->diary) {
		camel_object_unref (CAMEL_OBJECT (store->stub));
		store->stub = NULL;
		return FALSE;
	}
	
	camel_object_hook_event (CAMEL_OBJECT (store->stub), "notification",
				 stub_notification, store);
	
	/* FIXME: Ick. Do something nicer in 1.2 */
	store->note_idle = g_idle_add (note_store, service);
	return TRUE;
}

static gboolean
exchange_disconnect_online (CamelService *service, gboolean clean, CamelException *ex)
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

static CamelFolder *
exchange_get_folder (CamelStore *store, const char *folder_name,
		     guint32 camel_flags, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	CamelFolder *folder;
	char *name, *folder_dir;

	folder = g_hash_table_lookup (exch->folders, folder_name);
	if (folder) {
		/* This shouldn't actually happen, it should be caught
		 * by the store-level cache...
		 */
		camel_object_ref (CAMEL_OBJECT (folder));
		return folder;
	}

	folder = (CamelFolder *)camel_object_new (CAMEL_EXCHANGE_FOLDER_TYPE);
	name = g_strdup (folder_name);
	g_hash_table_insert (exch->folders, name, folder);

	folder_dir = e_path_to_physical (exch->storage_path, folder_name);
	if (!camel_exchange_folder_construct (folder, store, folder_name,
					      folder_dir, exch->stub, ex)) {
		g_hash_table_remove (exch->folders, name);
		g_free (name);
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
make_folder_info (CamelExchangeStore *exch, char *name, char *uri)
{
	CamelFolderInfo *info;

	info = g_new0 (CamelFolderInfo, 1);
	info->name = name;
	info->url = uri;

	info->path = strstr (uri, "://");
	if (info->path) {
		info->path = strchr (info->path + 3, '/');
		if (info->path) {
			info->path = g_strdup (info->path);
			info->full_name = g_strdup (info->path + 1);
			camel_url_decode (info->full_name);
		}
	}

	info->unread_message_count = -1;

	return info;
}

static CamelFolderInfo *
exchange_get_folder_info_online (CamelStore *store, const char *top,
				 guint32 flags, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	GPtrArray *folders, *folder_names, *folder_uris;
	CamelFolderInfo *info;
	int i;

	/* If the backend crashed, don't keep returning an error
	 * each time auto-send/recv runs.
	 */
	if (camel_stub_marshal_eof (exch->stub->cmd))
		return NULL;

	if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_FOLDER_INFO,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_STRINGARRAY, &folder_names,
			      CAMEL_STUB_ARG_STRINGARRAY, &folder_uris,
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
					 folder_uris->pdata[i]);
		g_ptr_array_add (folders, info);
	}
	g_ptr_array_free (folder_names, TRUE);
	g_ptr_array_free (folder_uris, TRUE);

	info = camel_folder_info_build (folders, NULL, '/', TRUE);
	g_ptr_array_free (folders, TRUE);
	return info;
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		folder = g_hash_table_lookup (exch->folders, folder_name);
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

		info = make_folder_info (exch, name, uri);
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

		info = make_folder_info (exch, name, uri);
		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_deleted", info);
		camel_folder_info_free (info);
		break;
	}

	case CAMEL_STUB_RETVAL_FOLDER_RENAMED:
	{
		/* FIXME */
		break;
	}

	default:
		g_assert_not_reached ();
		break;
	}
}
