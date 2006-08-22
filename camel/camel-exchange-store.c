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

#include <camel/camel-i18n.h>

#include "camel-exchange-store.h"
#include "camel-exchange-folder.h"
#include "camel-exchange-summary.h"
#include <camel/camel-session.h>

#define SUBFOLDER_DIR_NAME     "subfolders"
#define SUBFOLDER_DIR_NAME_LEN 10
#define d(x)


//static CamelStoreClass *parent_class = NULL;
static CamelOfflineStoreClass *parent_class = NULL;

#define CS_CLASS(so) ((CamelStoreClass *)((CamelObject *)(so))->klass)

static void construct (CamelService *service, CamelSession *session,
		       CamelProvider *provider, CamelURL *url,
		       CamelException *ex);

static GList *query_auth_types (CamelService *service, CamelException *ex);
static char  *get_name         (CamelService *service, gboolean brief);
static CamelFolder     *get_trash       (CamelStore *store,
					 CamelException *ex);

char * exchange_path_to_physical (const char *prefix, const char *vpath);
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
static gboolean		exchange_folder_subscribed (CamelStore *store, 
						const char *folder_name);
static void 		exchange_subscribe_folder (CamelStore *store, 
						const char *folder_name, 
						CamelException *ex);
static void 		exchange_unsubscribe_folder (CamelStore *store, 
						const char *folder_name, 
						CamelException *ex);

static void stub_notification (CamelObject *object, gpointer event_data, gpointer user_data);

static void
class_init (CamelExchangeStoreClass *camel_exchange_store_class)
{
	CamelServiceClass *camel_service_class =
		CAMEL_SERVICE_CLASS (camel_exchange_store_class);
	CamelStoreClass *camel_store_class =
		CAMEL_STORE_CLASS (camel_exchange_store_class);
	
	parent_class = CAMEL_OFFLINE_STORE_CLASS (camel_type_get_global_classfuncs (camel_offline_store_get_type ()));
	
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

	camel_store_class->folder_subscribed = exchange_folder_subscribed;
	camel_store_class->subscribe_folder = exchange_subscribe_folder;
	camel_store_class->unsubscribe_folder = exchange_unsubscribe_folder;
}

static void
init (CamelExchangeStore *exch, CamelExchangeStoreClass *klass)
{
	CamelStore *store = CAMEL_STORE (exch);

	exch->folders_lock = g_mutex_new ();
	exch->folders = g_hash_table_new (g_str_hash, g_str_equal);

	store->flags |= CAMEL_STORE_SUBSCRIPTIONS;
	store->flags &= ~(CAMEL_STORE_VTRASH | CAMEL_STORE_VJUNK);

	exch->connect_lock = g_mutex_new ();
}

static void
finalize (CamelExchangeStore *exch)
{
	if (exch->stub) {
		camel_object_unref (CAMEL_OBJECT (exch->stub));
		exch->stub = NULL;
	}
	
	g_free (exch->trash_name);
	
	if (exch->folders_lock)
		g_mutex_free (exch->folders_lock);

	if (exch->connect_lock)
		g_mutex_free (exch->connect_lock);
}

CamelType
camel_exchange_store_get_type (void)
{
	static CamelType camel_exchange_store_type = CAMEL_INVALID_TYPE;

	if (!camel_exchange_store_type) {
		camel_exchange_store_type = camel_type_register (
			camel_offline_store_get_type (),
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

/* Use this to ensure that the camel session is online and we are connected
   too. Also returns the current status of the store */
gboolean
camel_exchange_store_connected (CamelExchangeStore *store, CamelException *ex)
{
	CamelService *service;
	CamelSession *session;

	g_return_val_if_fail (CAMEL_IS_EXCHANGE_STORE (store), FALSE);

	service = CAMEL_SERVICE (store);
	session = service->session;

	if (service->status != CAMEL_SERVICE_CONNECTED &&
	    camel_session_is_online (session) &&
	    !camel_service_connect (service, ex)) {
		return FALSE;
	}

	return CAMEL_OFFLINE_STORE (store)->state != CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL;
}

/* This has been now removed from evolution/e-util. So implemented this here.
 * Also note that this is similar to the call in e2k-path.c. The name of the 
 * function has been changed to avoid any conflicts.
 */
char *
exchange_path_to_physical (const char *prefix, const char *vpath)
{
	const char *p, *newp;
	char *dp;
	char *ppath;
	int ppath_len;
	int prefix_len;

	while (*vpath == '/')
		vpath++;
	if (!prefix)
		prefix = "";

	/* Calculate the length of the real path. */
	ppath_len = strlen (vpath);
	ppath_len++;	/* For the ending zero.  */

	prefix_len = strlen (prefix);
	ppath_len += prefix_len;
	ppath_len++;	/* For the separating slash.  */

	/* Take account of the fact that we need to translate every
	 * separator into `subfolders/'.
	 */
	p = vpath;
	while (1) {
		newp = strchr (p, '/');
		if (newp == NULL)
			break;

		ppath_len += SUBFOLDER_DIR_NAME_LEN;
		ppath_len++; /* For the separating slash.  */

		/* Skip consecutive slashes.  */
		while (*newp == '/')
			newp++;

		p = newp;
	};

	ppath = g_malloc (ppath_len);
	dp = ppath;

	memcpy (dp, prefix, prefix_len);
	dp += prefix_len;
	*(dp++) = '/';

	/* Copy the mangled path.  */
	p = vpath;
 	while (1) {
		newp = strchr (p, '/');
		if (newp == NULL) {
			strcpy (dp, p);
			break;
		}

		memcpy (dp, p, newp - p + 1); /* `+ 1' to copy the slash too.  */
		dp += newp - p + 1;

		memcpy (dp, SUBFOLDER_DIR_NAME, SUBFOLDER_DIR_NAME_LEN);
		dp += SUBFOLDER_DIR_NAME_LEN;

		*(dp++) = '/';

		/* Skip consecutive slashes.  */
		while (*newp == '/')
			newp++;

		p = newp;
	}

	return ppath;
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

	if (!(exch->storage_path = camel_session_get_storage_path (session, service, ex)))
		return;
	
	if (camel_url_get_param (url, "filter_junk"))
		CAMEL_STORE (service)->flags |= CAMEL_STORE_VJUNK;
	
	exch->stub = NULL;
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

/* SURF : Picked this from gal/util/e-util.c */
/* This only makes a filename safe for usage as a filename.  
	It still may have shell meta-characters in it. */
static void
e_filename_make_safe (gchar *string)
{
	gchar *p, *ts;
	gunichar c;
	
	g_return_if_fail (string != NULL);
	p = string;

	while(p && *p) {
		c = g_utf8_get_char (p);
		ts = p;
		p = g_utf8_next_char (p);
		if (!g_unichar_isprint(c) || ( c < 0xff && strchr (" /'\"`&();|<>$%{}!", c&0xff ))) {
			while (ts<p) 	
				*ts++ = '_';
		}
	}
}

static void
camel_exchange_get_password (CamelService *service, CamelException *ex)
{
	CamelSession *session = camel_service_get_session (service);
	char *prompt, *account_name;

	if (!service->url->passwd) {
		/* This is a hack to use the same string being used in 
		   exchange-account.c:get_password */
		account_name = g_strdup_printf ("%s@%s", service->url->user, service->url->host);
		prompt = g_strdup_printf (_("%sEnter password for %s"), "", account_name);

		service->url->passwd = camel_session_get_password (session, 
					service, "Exchange", prompt, "password", CAMEL_SESSION_PASSWORD_SECRET, ex);
		g_free (account_name);
		g_free (prompt);
	}
}

static void
camel_exchange_forget_password (CamelService *service, CamelException *ex)
{
	CamelSession *session = camel_service_get_session (service);

	if (service->url->passwd) {
		camel_session_forget_password (session, 
					       service, "Exchange", 
					       "password", ex);
		g_free (service->url->passwd);
		service->url->passwd = NULL;
	}
}

static gboolean
exchange_connect (CamelService *service, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (service);
	char *real_user, *socket_path;
	gchar *password = NULL;
	guint32 connect_status;
	gboolean online_mode = FALSE;

	/* This lock is only needed for offline operation. exchange_connect
	   is called many times in offline to ensure we are connected atleast
	   to the mail stub. Think twice before changing anything here.*/
	
	g_mutex_lock (exch->connect_lock);

	online_mode = camel_session_is_online (service->session);
	
	if (exch->stub == NULL) {
		real_user = strpbrk (service->url->user, "\\/");
		if (real_user)
			real_user++;
		else
			real_user = service->url->user;
		socket_path = g_strdup_printf ("/tmp/.exchange-%s/%s@%s",
					       g_get_user_name (),
					       real_user, service->url->host);
		e_filename_make_safe (strchr (socket_path + 5, '/') + 1);
		
		exch->stub = camel_stub_new (socket_path, _("Evolution Exchange backend process"), ex);
		g_free (socket_path);
		if (!exch->stub) {
			g_mutex_unlock (exch->connect_lock);
			return FALSE;
		}

		camel_object_hook_event (CAMEL_OBJECT (exch->stub), "notification",
					 stub_notification, exch);
	} else if (!online_mode && exch->stub_connected) {
		g_mutex_unlock (exch->connect_lock);
		return TRUE;
	}

	if (online_mode) {
		camel_exchange_get_password (service, ex);
		if (camel_exception_is_set (ex)) {
			g_mutex_unlock (exch->connect_lock);
			return FALSE;
		}
		password = service->url->passwd;
	}

	/* Initialize the stub connection */
	if (!camel_stub_send (exch->stub, NULL, CAMEL_STUB_CMD_CONNECT,
			      CAMEL_STUB_ARG_STRING, password,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_UINT32, &connect_status,
			      CAMEL_STUB_ARG_END)) {
		/* The user cancelled the connection attempt. */
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     "Cancelled");
		camel_object_unref (exch->stub);
		exch->stub = NULL;
		g_mutex_unlock (exch->connect_lock);
		return FALSE;
	}

	if (!connect_status) {
		camel_exchange_forget_password (service, ex);
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Could not authenticate to server. "
				       "(Password incorrect?)\n\n"));
		g_mutex_unlock (exch->connect_lock);
		return FALSE;
	} else {
		exch->stub_connected = TRUE;
	}

	g_mutex_unlock (exch->connect_lock);
	
	return TRUE;
}

static gboolean
exchange_disconnect (CamelService *service, gboolean clean, CamelException *ex)
{
	return TRUE;
}

/* Even if we are disconnected, we need to exchange_connect
   to get the offline data */
#define RETURN_VAL_IF_NOT_CONNECTED(store, ex, val)\
	if (!camel_exchange_store_connected(store, ex) && \
	    !exchange_connect (CAMEL_SERVICE (store), ex)) \
		return val; 

static CamelFolder *
exchange_get_folder (CamelStore *store, const char *folder_name,
		     guint32 flags, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	CamelFolder *folder;
	char *folder_dir;
	
	RETURN_VAL_IF_NOT_CONNECTED (exch, ex, NULL);

	folder_dir = exchange_path_to_physical (exch->storage_path, folder_name);
	
	if (!camel_exchange_store_connected (exch, ex)) {
		if (!folder_dir || access (folder_dir, F_OK) != 0) {
			g_free (folder_dir);
			camel_exception_setv (ex, CAMEL_EXCEPTION_STORE_NO_FOLDER,
					      _("No such folder %s"), folder_name);
			return NULL;
		}
	}
	
	g_mutex_lock (exch->folders_lock);
	folder = g_hash_table_lookup (exch->folders, folder_name);
	if (folder) {
		/* This shouldn't actually happen, it should be caught
		 * by the store-level cache...
		 */
		g_mutex_unlock (exch->folders_lock);
		camel_object_ref (CAMEL_OBJECT (folder));
		g_free (folder_dir);
		return folder;
	}

	folder = (CamelFolder *)camel_object_new (CAMEL_EXCHANGE_FOLDER_TYPE);
	g_hash_table_insert (exch->folders, g_strdup (folder_name), folder);
	g_mutex_unlock (exch->folders_lock);
	
	if (!camel_exchange_folder_construct (folder, store, folder_name,
					      flags, folder_dir, ((CamelOfflineStore *) store)->state,
					      exch->stub, ex)) {
		gchar *key;
		g_mutex_lock (exch->folders_lock);
		if (g_hash_table_lookup_extended (exch->folders, folder_name,
						  (gpointer *) &key, NULL)) {
			g_hash_table_remove (exch->folders, key);
			g_free (key);
		}
		g_mutex_unlock (exch->folders_lock);
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

static gboolean
exchange_folder_subscribed (CamelStore *store, const char *folder_name)
{
	d(printf ("is subscribed folder : %s\n", folder_name));
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	guint32 is_subscribed;
	
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		return FALSE;
	}

	if (!camel_stub_send (exch->stub, NULL, CAMEL_STUB_CMD_IS_SUBSCRIBED_FOLDER,
			      CAMEL_STUB_ARG_FOLDER, folder_name,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_UINT32, &is_subscribed,
			      CAMEL_STUB_ARG_END)) {
		return FALSE;
	}

	return is_subscribed ? TRUE : FALSE;
}

static void
exchange_subscribe_folder (CamelStore *store, const char *folder_name,
				CamelException *ex)
{
	d(printf ("subscribe folder : %s\n", folder_name));
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	
	if (!camel_exchange_store_connected (exch, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot subscribe folder in offline mode."));
		return;
	}

	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_SUBSCRIBE_FOLDER,
			      CAMEL_STUB_ARG_FOLDER, folder_name,
			      CAMEL_STUB_ARG_END);
}

static void
exchange_unsubscribe_folder (CamelStore *store, const char *folder_name,
				CamelException *ex)
{
	d(printf ("unsubscribe folder : %s\n", folder_name));
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	
	if (!camel_exchange_store_connected (exch, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot unsubscribe folder in offline mode."));
		return;
	}

	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_UNSUBSCRIBE_FOLDER,
			      CAMEL_STUB_ARG_FOLDER, folder_name,
			      CAMEL_STUB_ARG_END);
}


static CamelFolder *
get_trash (CamelStore *store, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	
	RETURN_VAL_IF_NOT_CONNECTED (exch, ex, NULL);

	if (!exch->trash_name) {
		if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_TRASH_NAME,
				      CAMEL_STUB_ARG_RETURN,
				      CAMEL_STUB_ARG_STRING, &exch->trash_name,
				      CAMEL_STUB_ARG_END))
			return NULL;
	}

	return camel_store_get_folder (store, exch->trash_name, 0, ex);
}

/* Note: steals @name and @uri */
static CamelFolderInfo *
make_folder_info (CamelExchangeStore *exch, char *name, char *uri,
		  int unread_count, int flags)
{
	d(printf ("make folder info : %s flags : %d\n", name, flags));
	CamelFolderInfo *info;
	const char *path;
	gchar **components;
	char *new_uri;

	path = strstr (uri, "://");
	if (!path)
		return NULL;
	path = strstr (path + 3, "/;");
	if (!path)
		return NULL;

	components = g_strsplit (uri, "/;", 2);
	if (components[0] && components[1])
		new_uri = g_strdup_printf ("%s/%s", components[0], components[1]);
	else
		new_uri = g_strdup (uri);
	g_strfreev (components);

	d(printf ("new_uri is : %s\n", new_uri));
	info = g_new0 (CamelFolderInfo, 1);
	info->name = name;
	info->uri = new_uri;
	info->full_name = g_strdup (path + 2);
	info->unread = unread_count;

	if (flags & CAMEL_STUB_FOLDER_NOSELECT)
		info->flags = CAMEL_FOLDER_NOSELECT;

	if (flags & CAMEL_STUB_FOLDER_SYSTEM)
		info->flags |= CAMEL_FOLDER_SYSTEM;
		
	if (flags & CAMEL_STUB_FOLDER_TYPE_INBOX)
		info->flags |= CAMEL_FOLDER_TYPE_INBOX;

	if (flags & CAMEL_STUB_FOLDER_SUBSCRIBED) {
		info->flags |= CAMEL_FOLDER_SUBSCRIBED;
		d(printf ("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMmark as subscribed\n"));
	}
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
exchange_get_folder_info (CamelStore *store, const char *top, guint32 flags, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);
	GPtrArray *folders, *folder_names, *folder_uris;
	GArray *unread_counts;
	GArray *folder_flags;
	CamelFolderInfo *info;
	guint32 store_flags = 0;
	int i;
#if 0	
	if (((CamelOfflineStore *) store)->state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot get folder info in offline mode."));
		return NULL;
	}
#endif	
	
	/* If the backend crashed, don't keep returning an error
	 * each time auto-send/recv runs.
	 */

	RETURN_VAL_IF_NOT_CONNECTED (exch, ex, NULL);

	if (camel_stub_marshal_eof (exch->stub->cmd))
		return NULL;

	if (flags & CAMEL_STORE_FOLDER_INFO_RECURSIVE)
		store_flags |= CAMEL_STUB_STORE_FOLDER_INFO_RECURSIVE;
	if (flags & CAMEL_STORE_FOLDER_INFO_SUBSCRIBED)
		store_flags |= CAMEL_STUB_STORE_FOLDER_INFO_SUBSCRIBED;

	if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_FOLDER_INFO,
			      CAMEL_STUB_ARG_STRING, top,
			      CAMEL_STUB_ARG_UINT32, store_flags,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_STRINGARRAY, &folder_names,
			      CAMEL_STUB_ARG_STRINGARRAY, &folder_uris,
			      CAMEL_STUB_ARG_UINT32ARRAY, &unread_counts,
			      CAMEL_STUB_ARG_UINT32ARRAY, &folder_flags,
			      CAMEL_STUB_ARG_END)) {
		return NULL;
	}
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
					 g_array_index (folder_flags, int, i));
		if (info)
			g_ptr_array_add (folders, info);
	}
	g_ptr_array_free (folder_names, TRUE);
	g_ptr_array_free (folder_uris, TRUE);
	g_array_free (unread_counts, TRUE);
	g_array_free (folder_flags, TRUE);

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
	
	if (!camel_exchange_store_connected (exch, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot create folder in offline mode."));
		return NULL;
	}

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

	if (!camel_exchange_store_connected (exch, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot delete folder in offline mode."));
		return;
	}

	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_DELETE_FOLDER,
			 CAMEL_STUB_ARG_FOLDER, folder_name,
			 CAMEL_STUB_ARG_END);
}

static void
exchange_rename_folder (CamelStore *store, const char *old_name,
			const char *new_name, CamelException *ex)
{
	CamelExchangeStore *exch = CAMEL_EXCHANGE_STORE (store);

	if (!camel_exchange_store_connected (exch, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, _("Cannot rename folder in offline mode."));
		return;
	}

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
		CamelMessageInfo *info;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1)
			return;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder && (info = camel_folder_summary_uid (((CamelFolder *)folder)->summary, uid))) {
			camel_message_info_free (info);
			camel_exchange_folder_remove_message (folder, uid);
		}

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

	case CAMEL_STUB_RETVAL_CHANGED_FLAGS_EX:
	{
		CamelExchangeFolder *folder;
		char *folder_name, *uid;
		guint32 flags;
		guint32 mask;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uid) == -1 ||
		    camel_stub_marshal_decode_uint32 (stub->status, &flags) == -1 ||
		    camel_stub_marshal_decode_uint32 (stub->status, &mask) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder)
			camel_exchange_folder_update_message_flags_ex (folder, uid,
								       flags, mask);

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
					    "folder_subscribed", info);
		camel_folder_info_free (info);
		break;
	}

	case CAMEL_STUB_RETVAL_FOLDER_DELETED:
	{
		CamelFolderInfo *info;
		CamelFolder *folder;
		char *name, *uri;

		if (camel_stub_marshal_decode_string (stub->status, &name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &uri) == -1)
			break;
		
		info = make_folder_info (exch, name, uri, -1, 0);

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, info->full_name);
		if (folder) {
			g_hash_table_remove (exch->folders, info->full_name);
			camel_object_unref (CAMEL_OBJECT (folder));
		}
		g_mutex_unlock (exch->folders_lock);

		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_unsubscribed", info);
		camel_folder_info_free (info);
		break;
	}

	case CAMEL_STUB_RETVAL_FOLDER_RENAMED:
	{
		char *new_name, *new_uri;
		CamelFolder *folder;
		CamelRenameInfo reninfo;

		if (camel_stub_marshal_decode_folder (stub->status, &reninfo.old_base) == -1 ||
		    camel_stub_marshal_decode_folder (stub->status, &new_name) == -1 ||
		    camel_stub_marshal_decode_string (stub->status, &new_uri) == -1)
			break;

		reninfo.new = make_folder_info (exch, new_name, new_uri, -1, 0);
		
		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, reninfo.old_base);
		if (folder) {
			g_hash_table_remove (exch->folders, reninfo.old_base);
			camel_object_unref (CAMEL_OBJECT (folder));
		}
		g_mutex_unlock (exch->folders_lock);
		
		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_renamed", &reninfo);
		camel_folder_info_free (reninfo.new);
		g_free (reninfo.old_base);
		break;
	}
	
	case CAMEL_STUB_RETVAL_FOLDER_SET_READONLY:
	{
		CamelFolder *folder;
		gchar *folder_name;
		guint32 readonly;

		if (camel_stub_marshal_decode_folder (stub->status, &folder_name) == -1 ||
		    camel_stub_marshal_decode_uint32 (stub->status, &readonly) == -1)
			break;

		g_mutex_lock (exch->folders_lock);
		folder = g_hash_table_lookup (exch->folders, folder_name);
		g_mutex_unlock (exch->folders_lock);
		if (folder) {
			camel_exchange_summary_set_readonly (folder->summary, readonly ? TRUE : FALSE);
		}

		g_free (folder_name);
		break;
	}

	default:
		g_assert_not_reached ();
		break;
	}
}
