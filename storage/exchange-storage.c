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

/* ExchangeStorage: EvolutionStorage subclass that talks to an
 * ExchangeAccount
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-storage.h"
#include "exchange-account.h"
#include "exchange-hierarchy.h"
#include "exchange-permissions-dialog.h"
#include "e-folder-exchange.h"

#include <e-util/e-dialog-utils.h>

#include <stdlib.h>
#include <string.h>

struct _ExchangeStoragePrivate {
	ExchangeAccount *account;
	guint idle_id;
};

#define PARENT_TYPE EVOLUTION_TYPE_STORAGE
static EvolutionStorageClass *parent_class = NULL;

static void finalize (GObject *);

static void create_folder (EvolutionStorage *storage,
			   const Bonobo_Listener listener,
			   const char *path,
			   const char *type,
			   const char *description,
			   const char *parent_physical_uri);
static void remove_folder (EvolutionStorage *storage,
			   const Bonobo_Listener listener,
			   const char *path,
			   const char *physical_uri);
static void xfer_folder   (EvolutionStorage *storage,
			   const Bonobo_Listener listener,
			   const char *source_path,
			   const char *destination_path,
			   gboolean remove_source);
static void open_folder   (EvolutionStorage *storage,
			   const Bonobo_Listener listener,
			   const char *path);

static void discover_shared_folder        (EvolutionStorage *storage,
					   Bonobo_Listener listener,
					   const char *user,
					   const char *folder_name);
static void cancel_discover_shared_folder (EvolutionStorage *storage,
					   const char *user,
					   const char *folder_name);
static void remove_shared_folder          (EvolutionStorage *storage,
					   Bonobo_Listener listener,
					   const char *path);

static void show_folder_properties (EvolutionStorage *storage,
				    const char *path,
				    guint item,
				    gulong parent_window_id);

static void
class_init (GObjectClass *object_class)
{
	EvolutionStorageClass *storage_class =
		EVOLUTION_STORAGE_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	storage_class->create_folder = create_folder;
	storage_class->remove_folder = remove_folder;
	storage_class->xfer_folder = xfer_folder;
	storage_class->open_folder = open_folder;
	storage_class->discover_shared_folder = discover_shared_folder;
	storage_class->cancel_discover_shared_folder = cancel_discover_shared_folder;
	storage_class->remove_shared_folder = remove_shared_folder;
	storage_class->show_folder_properties = show_folder_properties;
}

static void
init (GObject *object)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (object);

	estorage->priv = g_new0 (ExchangeStoragePrivate, 1);
}

static void
finalize (GObject *object)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (object);

	if (estorage->priv->idle_id)
		g_source_remove (estorage->priv->idle_id);

	g_free (estorage->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


E2K_MAKE_TYPE (exchange_storage, ExchangeStorage, class_init, init, PARENT_TYPE)

static void
account_new_folder (ExchangeAccount *account, EFolder *folder,
		    EvolutionStorage *storage)
{
	const char *path = e_folder_exchange_get_path (folder);

	evolution_storage_new_folder (storage, path,
				      e_folder_get_name (folder),
				      e_folder_get_type_string (folder),
				      e_folder_get_physical_uri (folder),
				      e_folder_get_description (folder),
				      e_folder_get_custom_icon_name (folder),
				      e_folder_get_unread_count (folder),
				      e_folder_get_can_sync_offline (folder),
				      e_folder_get_sorting_priority (folder));
	if (e_folder_exchange_get_has_subfolders (folder)) {
		evolution_storage_has_subfolders (storage, path,
						  _("Searching..."));
	}
}

static void
account_removed_folder (ExchangeAccount *account, EFolder *folder,
			EvolutionStorage *storage)
{
	const char *path = e_folder_exchange_get_path (folder);

	evolution_storage_removed_folder (storage, path);
}

static void
account_updated_folder (ExchangeAccount *account, EFolder *folder,
			EvolutionStorage *storage)
{
	const char *path = e_folder_exchange_get_path (folder);
	int unread = e_folder_get_unread_count (folder);

	evolution_storage_update_folder (storage, path, unread);
}

static GNOME_Evolution_Storage_Result
account_result_to_storage_result (ExchangeAccountFolderResult result)
{
	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
		return GNOME_Evolution_Storage_OK;
	case EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS:
		return GNOME_Evolution_Storage_ALREADY_EXISTS;
	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		return GNOME_Evolution_Storage_DOES_NOT_EXIST;
	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		return GNOME_Evolution_Storage_PERMISSION_DENIED;
	case EXCHANGE_ACCOUNT_FOLDER_OFFLINE:
	case EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION:
		return GNOME_Evolution_Storage_UNSUPPORTED_OPERATION;
	default:
		return GNOME_Evolution_Storage_GENERIC_ERROR;
	}
}

static void
listener_notify (ExchangeAccount *account, ExchangeAccountFolderResult result,
		 EFolder *folder, gpointer user_data)
{
	Bonobo_Listener listener = user_data;
	GNOME_Evolution_Storage_Result storage_result;
	CORBA_Environment ev;
	CORBA_any any;

	storage_result = account_result_to_storage_result (result);

	any._type = TC_GNOME_Evolution_Storage_Result;
	any._value = &storage_result;

	CORBA_exception_init (&ev);
	Bonobo_Listener_event (listener, "result", &any, &ev);
	CORBA_exception_free (&ev);

	bonobo_object_release_unref (listener, NULL);
}

static void
create_folder (EvolutionStorage *storage, Bonobo_Listener listener,
	       const char *path, const char *type,
	       const char *description,
	       const char *parent_physical_uri) 
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	listener = bonobo_object_dup_ref (listener, NULL);
	exchange_account_async_create_folder (estorage->priv->account,
					      path, type,
					      listener_notify, listener);
}

static void
remove_folder (EvolutionStorage *storage, Bonobo_Listener listener,
	       const char *path, const char *physical_uri)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	listener = bonobo_object_dup_ref (listener, NULL);
	exchange_account_async_remove_folder (estorage->priv->account, path,
					      listener_notify, listener);
}

static void
xfer_folder (EvolutionStorage *storage, Bonobo_Listener listener,
	     const char *source_path, const char *dest_path,
	     gboolean remove_source)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	listener = bonobo_object_dup_ref (listener, NULL);
	exchange_account_async_xfer_folder (estorage->priv->account,
					    source_path, dest_path,
					    remove_source,
					    listener_notify, listener);
}

static void
connected_cb (ExchangeAccount *account, gpointer listener)
{
	listener_notify (account,
			 (exchange_account_get_connection (account) ?
			  EXCHANGE_ACCOUNT_FOLDER_OK :
			  EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR),
			 NULL, listener);
}

static void
open_folder (EvolutionStorage *storage, Bonobo_Listener listener,
	     const char *path)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	listener = bonobo_object_dup_ref (listener, NULL);

	if (!strcmp (path, "/")) {
		exchange_account_async_connect (estorage->priv->account,
						connected_cb, listener);
	} else {
		exchange_account_async_open_folder (estorage->priv->account, path,
						    listener_notify, listener);
	}
}

static void
shared_folder_listener_notify (ExchangeAccount *account,
			       ExchangeAccountFolderResult result,
			       EFolder *folder, gpointer user_data)
{
	Bonobo_Listener listener = user_data;
	GNOME_Evolution_Storage_FolderResult folder_result;
	CORBA_Environment ev;
	CORBA_any any;

	folder_result.result = account_result_to_storage_result (result);
	if (result == EXCHANGE_ACCOUNT_FOLDER_OK && folder)
		folder_result.path = CORBA_string_dup (e_folder_exchange_get_path (folder));
	else
		folder_result.path = CORBA_string_dup ("");

	any._type = TC_GNOME_Evolution_Storage_FolderResult;
	any._value = &folder_result;

	CORBA_exception_init (&ev);
	Bonobo_Listener_event (listener, "result", &any, &ev);
	CORBA_exception_free (&ev);

	bonobo_object_release_unref (listener, NULL);
}

static void
discover_shared_folder (EvolutionStorage *storage, Bonobo_Listener listener,
			const char *user, const char *folder_name)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	listener = bonobo_object_dup_ref (listener, NULL);
	exchange_account_async_discover_shared_folder (estorage->priv->account,
						       user, folder_name,
						       shared_folder_listener_notify,
						       listener);
}

static void
cancel_discover_shared_folder (EvolutionStorage *storage, const char *user,
			       const char *folder_name)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	exchange_account_cancel_discover_shared_folder (estorage->priv->account,
							user, folder_name);
}

static void
remove_shared_folder (EvolutionStorage *storage, Bonobo_Listener listener,
		      const char *path)
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);

	listener = bonobo_object_dup_ref (listener, NULL);
	exchange_account_async_remove_shared_folder (estorage->priv->account, path,
						     shared_folder_listener_notify,
						     listener);
}

static void
ok_clicked (GtkDialog *dialog, int response, gpointer user_data)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
show_folder_properties (EvolutionStorage *storage, const char *path,
			guint item, gulong parent_window_id) 
{
	ExchangeStorage *estorage = EXCHANGE_STORAGE (storage);
	EFolder *folder;
	ExchangeHierarchy *hier;

	folder = exchange_account_get_folder (estorage->priv->account, path);
	g_return_if_fail (folder != NULL);

	hier = e_folder_exchange_get_hierarchy (folder);
	g_return_if_fail (hier != NULL);

	if (folder == hier->toplevel) {
		GtkWidget *dialog;

		dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 _("Can't edit permissions of \"%s\""),
						 e_folder_get_name (folder));
		e_dialog_set_transient_for_xid (GTK_WINDOW (dialog),
						parent_window_id);

		/* We don't gtk_dialog_run it, because the shell is
		 * blocking on us. We just let it get destroyed by the
		 * main loop.
		 */
		gtk_widget_show (dialog);
		g_signal_connect (dialog, "response",
				  G_CALLBACK (ok_clicked), NULL);
		return;
	}

	exchange_permissions_dialog_new (estorage->priv->account, folder,
					 parent_window_id);
}

static gboolean
idle_fill_storage (gpointer user_data)
{
	ExchangeStorage *estorage = user_data;
	EvolutionStorage *storage = user_data;
	ExchangeAccount *account = estorage->priv->account;
	GPtrArray *folders;
	int i;

	estorage->priv->idle_id = 0;

	if (!exchange_account_get_connection (account)) {
		evolution_storage_has_subfolders (storage, "/",
						  _("Connecting..."));
	} else {
		folders = exchange_account_get_folders (account);
		if (folders) {
			for (i = 0; i < folders->len; i++) {
				account_new_folder (account, folders->pdata[i],
						    storage);
			}
			g_ptr_array_free (folders, TRUE);
		}
	}

	g_signal_connect (account, "new_folder",
			  G_CALLBACK (account_new_folder), estorage);
	g_signal_connect (account, "removed_folder",
			  G_CALLBACK (account_removed_folder), estorage);
	g_signal_connect (account, "updated_folder",
			  G_CALLBACK (account_updated_folder), estorage);

	g_object_unref (storage);

	return FALSE;
}

/**
 * exchange_storage_new:
 * @account: the account the storage will represent
 *
 * This creates a storage for @account.
 **/
EvolutionStorage *
exchange_storage_new (ExchangeAccount *account)
{
	ExchangeStorage *estorage;
	EvolutionStorage *storage;

	estorage = g_object_new (EXCHANGE_TYPE_STORAGE, NULL);
	storage = EVOLUTION_STORAGE (estorage);
	evolution_storage_construct (storage, account->account_name, TRUE);
	evolution_storage_add_property_item (storage, _("Permissions..."),
					     _("Change permissions for this folder"),
					     NULL);

	estorage->priv->account = account;

	g_object_ref (estorage);
	estorage->priv->idle_id = g_idle_add (idle_fill_storage, estorage);

	return storage;
}
