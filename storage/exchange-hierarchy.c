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

/* ExchangeHierarchy: abstract class for a hierarchy of folders
 * in an Exchange storage. Subclasses of ExchangeHierarchy implement
 * normal WebDAV hierarchies, the GAL hierarchy, and hierarchies
 * of individually-selected other users' folders.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy.h"
#include "e-folder-exchange.h"
#include "e2k-marshal.h"

enum {
	NEW_FOLDER,
	UPDATED_FOLDER,
	REMOVED_FOLDER,
	SCANNED_FOLDER,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

#define HIER_CLASS(hier) (EXCHANGE_HIERARCHY_CLASS (G_OBJECT_GET_CLASS (hier)))

static void dispose (GObject *object);
static void finalize (GObject *object);
static gboolean is_empty        (ExchangeHierarchy *hier);
static void add_to_storage      (ExchangeHierarchy *hier);
static void async_scan_subtree  (ExchangeHierarchy *hier,
				 EFolder *folder,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void rescan              (ExchangeHierarchy *hier);
static void async_create_folder (ExchangeHierarchy *hier,
				 EFolder *parent, const char *name,
				 const char *type,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void async_remove_folder (ExchangeHierarchy *hier,
				 EFolder *folder,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void async_xfer_folder   (ExchangeHierarchy *hier,
				 EFolder *source,
				 EFolder *dest_parent,
				 const char *dest_name,
				 gboolean remove_source,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *exchange_hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	exchange_hierarchy_class->is_empty = is_empty;
	exchange_hierarchy_class->add_to_storage = add_to_storage;
	exchange_hierarchy_class->rescan = rescan;
	exchange_hierarchy_class->async_scan_subtree = async_scan_subtree;
	exchange_hierarchy_class->async_create_folder = async_create_folder;
	exchange_hierarchy_class->async_remove_folder = async_remove_folder;
	exchange_hierarchy_class->async_xfer_folder = async_xfer_folder;

	/* signals */
	signals[NEW_FOLDER] =
		g_signal_new ("new_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchyClass, new_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[UPDATED_FOLDER] =
		g_signal_new ("updated_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchyClass, updated_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[REMOVED_FOLDER] =
		g_signal_new ("removed_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchyClass, removed_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[SCANNED_FOLDER] =
		g_signal_new ("scanned_folder",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchyClass, scanned_folder),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
dispose (GObject *object)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (object);

	if (hier->toplevel) {
		g_object_unref (hier->toplevel);
		hier->toplevel = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (object);

	g_free (hier->owner_name);
	g_free (hier->owner_email);
	g_free (hier->source_uri);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy, ExchangeHierarchy, class_init, NULL, PARENT_TYPE)


/**
 * exchange_hierarchy_new_folder:
 * @hier: the hierarchy
 * @folder: the new folder
 *
 * Emits a %new_folder signal.
 **/
void
exchange_hierarchy_new_folder (ExchangeHierarchy *hier,
			       EFolder *folder)
{
	g_signal_emit (hier, signals[NEW_FOLDER], 0, folder);
}

/**
 * exchange_hierarchy_updated_folder:
 * @hier: the hierarchy
 * @folder: the updated folder
 *
 * Emits an %updated_folder signal.
 **/
void
exchange_hierarchy_updated_folder (ExchangeHierarchy *hier,
				   EFolder *folder)
{
	g_signal_emit (hier, signals[UPDATED_FOLDER], 0, folder);
}

/**
 * exchange_hierarchy_removed_folder:
 * @hier: the hierarchy
 * @folder: the (about-to-be-)removed folder
 *
 * Emits a %removed_folder signal.
 **/
void
exchange_hierarchy_removed_folder (ExchangeHierarchy *hier,
				   EFolder *folder)
{
	g_signal_emit (hier, signals[REMOVED_FOLDER], 0, folder);
}

/**
 * exchange_hierarchy_scanned_folder:
 * @hier: the hierarchy
 * @folder: the just-scanned folder
 *
 * Emits a %scanned_folder signal.
 **/
void
exchange_hierarchy_scanned_folder (ExchangeHierarchy *hier,
				  EFolder *folder)
{
	g_signal_emit (hier, signals[SCANNED_FOLDER], 0, folder);
}


static gboolean
is_empty (ExchangeHierarchy *hier)
{
	return FALSE;
}

gboolean
exchange_hierarchy_is_empty (ExchangeHierarchy *hier)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), TRUE);

	return HIER_CLASS (hier)->is_empty (hier);
}


static void
async_create_folder (ExchangeHierarchy *hier, EFolder *parent,
		     const char *name, const char *type,
		     ExchangeAccountFolderCallback callback,
		     gpointer user_data)
{
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED,
		  NULL, user_data);
}

static void
async_remove_folder (ExchangeHierarchy *hier, EFolder *folder,
		     ExchangeAccountFolderCallback callback,
		     gpointer user_data)
{
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED,
		  NULL, user_data);
}

static void
async_xfer_folder (ExchangeHierarchy *hier, EFolder *source,
		   EFolder *dest_parent, const char *dest_name,
		   gboolean remove_source,
		   ExchangeAccountFolderCallback callback,
		   gpointer user_data)
{
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED,
		  NULL, user_data);
}

/**
 * exchange_hierarchy_async_create_folder:
 * @hier: the hierarchy
 * @parent: the parent folder of the new folder
 * @name: name of the new folder (UTF8)
 * @type: Evolution folder type of the new folder
 * @callback: function to call after the folder creation attempt
 * @user_data: data to pass to @callback.
 *
 * This asynchronously attempts to create a new folder and calls
 * @callback with the result.
 **/
void
exchange_hierarchy_async_create_folder (ExchangeHierarchy *hier,
					EFolder *parent,
					const char *name, const char *type,
					ExchangeAccountFolderCallback callback,
					gpointer user_data)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
	g_return_if_fail (parent != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (type != NULL);

	HIER_CLASS (hier)->async_create_folder (hier, parent, name, type,
						callback, user_data);
}

/**
 * exchange_hierarchy_async_remove_folder:
 * @hier: the hierarchy
 * @folder: the folder to remove
 * @callback: function to call after the folder creation attempt
 * @user_data: data to pass to @callback.
 *
 * This asynchronously attempts to remove a folder and calls @callback
 * with the result.
 **/
void
exchange_hierarchy_async_remove_folder (ExchangeHierarchy *hier,
					EFolder *folder,
					ExchangeAccountFolderCallback callback,
					gpointer user_data)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
	g_return_if_fail (folder != NULL);

	HIER_CLASS (hier)->async_remove_folder (hier, folder,
						callback, user_data);
}

/**
 * exchange_hierarchy_async_xfer_folder:
 * @hier: the hierarchy
 * @source: the source folder
 * @dest_parent: the parent of the destination folder
 * @dest_name: name of the destination (UTF8)
 * @remove_source: %TRUE if this is a move, %FALSE if it is a copy
 * @callback: function to call after the folder creation attempt
 * @user_data: data to pass to @callback.
 *
 * This asynchronously attempts to move or copy a folder and calls
 * @callback with the result.
 **/
void
exchange_hierarchy_async_xfer_folder (ExchangeHierarchy *hier,
				      EFolder *source,
				      EFolder *dest_parent,
				      const char *dest_name,
				      gboolean remove_source,
				      ExchangeAccountFolderCallback callback,
				      gpointer user_data)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
	g_return_if_fail (source != NULL);
	g_return_if_fail (dest_parent != NULL);
	g_return_if_fail (dest_name != NULL);

	HIER_CLASS (hier)->async_xfer_folder (hier, source,
					      dest_parent, dest_name,
					      remove_source,
					      callback, user_data);
}


static void
rescan (ExchangeHierarchy *hier)
{
	;
}

/**
 * exchange_hierarchy_rescan:
 * @hier: the hierarchy
 *
 * Tells the hierarchy to rescan its folder tree, emitting
 * %updated_folder signals as appropriate.
 **/
void
exchange_hierarchy_rescan (ExchangeHierarchy *hier)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));

	HIER_CLASS (hier)->rescan (hier);
}


static void
async_scan_subtree (ExchangeHierarchy *hier, EFolder *folder,
		    ExchangeAccountFolderCallback callback,
		    gpointer user_data)
{
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OK, folder, user_data);
}

/**
 * exchange_hierarchy_async_scan_subtree:
 * @hier: the hierarchy
 * @folder: the folder to scan under
 * @callback: callback to call after scanning
 * @user_data: data to pass to @callback
 *
 * Scans for folders in @hier underneath @folder, emitting %new_folder
 * signals for each one found. Depending on the kind of hierarchy,
 * this may initiate a recursive scan. If @callback is not %NULL,
 * it will be called after the scan is complete.
 **/
void
exchange_hierarchy_async_scan_subtree (ExchangeHierarchy *hier,
				       EFolder *folder,
				       ExchangeAccountFolderCallback callback,
				       gpointer user_data)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));
	g_return_if_fail (folder != NULL);

	HIER_CLASS (hier)->async_scan_subtree (hier, folder, callback, user_data);
}


static void
add_to_storage (ExchangeHierarchy *hier)
{
	exchange_hierarchy_new_folder (hier, hier->toplevel);
}

/**
 * exchange_hierarchy_add_to_storage:
 * @hier: the hierarchy
 *
 * Tells the hierarchy to fill in its folder tree, emitting %new_folder
 * signals as appropriate.
 **/
void
exchange_hierarchy_add_to_storage (ExchangeHierarchy *hier)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY (hier));

	HIER_CLASS (hier)->add_to_storage (hier);
}


/**
 * exchange_hierarchy_construct:
 * @hier: the hierarchy
 * @account: the hierarchy's account
 * @type: the type of hierarchy
 * @toplevel: the top-level folder of the hierarchy
 * @owner_name: the display name of the owner of this hierarchy
 * @owner_email: the email address of the owner of this hierarchy
 * @source_uri: the evolution-mail source URI of this hierarchy.
 *
 * Constructs the hierarchy. @owner_name, @owner_email, and @source_uri
 * can be %NULL if not relevant to this hierarchy.
 **/
void
exchange_hierarchy_construct (ExchangeHierarchy *hier,
			      ExchangeAccount *account,
			      ExchangeHierarchyType type,
			      EFolder *toplevel,
			      const char *owner_name,
			      const char *owner_email,
			      const char *source_uri)
{
	/* We don't ref the account since we'll be destroyed when
	 * the account is
	 */
	hier->account = account;

	hier->toplevel = toplevel;
	g_object_ref (toplevel);

	hier->type = type;
	hier->owner_name = g_strdup (owner_name);
	hier->owner_email = g_strdup (owner_email);
	hier->source_uri = g_strdup (source_uri);
}
