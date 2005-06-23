/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage-set-view.c
 *
 * Copyright (C) 2000, 2001, 2002 Ximian, Inc.
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
 *
 * Author: Ettore Perazzoli
 * Etree-ification: Chris Toshok
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-storage-set-view.h"

#include "e-folder-dnd-bridge.h"
#include "e-shell-marshal.h"

#include <e-util/e-icon-factory.h>
#include <e-util/e-util.h>
#include <misc/e-gui-utils.h>
#include <table/e-tree-memory-callbacks.h>
#include <table/e-cell-text.h>
#include <table/e-cell-toggle.h>
#include <table/e-cell-tree.h>

#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-popup-menu.h>

#include <string.h>

#include "check-empty.xpm"
#include "check-filled.xpm"
#include "check-missing.xpm"


static GdkPixbuf *checks [3];

/*#define DEBUG_XML*/

#define ROOT_NODE_NAME "/RootNode"

/* This is used on the source side to define the two basic types that we always
   export.  */
typedef enum {
	E_FOLDER_DND_PATH_TARGET_TYPE_IDX = 0,
} DndTargetTypeIdx;

#define PARENT_TYPE E_TREE_TYPE
static ETreeClass *parent_class = NULL;

struct EStorageSetViewPrivate {
	EStorageSet *storage_set;

	ETreeModel *etree_model;
	ETreePath root_node;

	GHashTable *path_to_etree_node;

	/* Path of the row selected by the latest "cursor_activated" signal.  */
	char *selected_row_path;

	/* Path of the row selected by a right click.  */
	char *right_click_row_path;

	unsigned int show_folders : 1;
	unsigned int show_checkboxes : 1;
	unsigned int allow_dnd : 1;
	unsigned int search_enabled : 1;

	/* The folder we're dragging.  */
	EFolder *drag_folder;

	GHashTable *checkboxes;

	/* Callback to determine whether the row should have a checkbox or
	   not, when show_checkboxes is TRUE.  */
	EStorageSetViewHasCheckBoxFunc has_checkbox_func;
	void *has_checkbox_func_data;
};

enum {
	FOLDER_SELECTED,
	FOLDER_OPENED,
	DND_ACTION,
	FOLDER_CONTEXT_MENU,
	CHECKBOXES_CHANGED,
	LAST_SIGNAL
};

static unsigned int signals[LAST_SIGNAL] = { 0 };

/* Forward declarations.  */

static void setup_folder_changed_callbacks (EStorageSetView *storage_set_view,
					    EFolder *folder,
					    const char *path);

/* DND stuff.  */

typedef enum {
	DND_TARGET_TYPE_URI_LIST,
} DndTargetType;

#define URI_LIST_TYPE   "text/uri-list"

/* Sorting callbacks.  */

static int
storage_sort_callback (ETreeMemory *etmm,
		       ETreePath node1,
		       ETreePath node2,
		       void *closure)
{
	return g_utf8_collate (e_tree_model_value_at (E_TREE_MODEL (etmm), node1, 0),
	                       e_tree_model_value_at (E_TREE_MODEL (etmm), node2, 0));
}

static int
folder_sort_callback (ETreeMemory *etmm,
		      ETreePath node1,
		      ETreePath node2,
		      void *closure)
{
	EStorageSetViewPrivate *priv;
	EFolder *folder_1, *folder_2;
	const char *folder_path_1, *folder_path_2;
	int priority_1, priority_2;

	priv = E_STORAGE_SET_VIEW (closure)->priv;

	folder_path_1 = e_tree_memory_node_get_data(etmm, node1);
	folder_path_2 = e_tree_memory_node_get_data(etmm, node2);

	folder_1 = e_storage_set_get_folder (priv->storage_set, folder_path_1);
	folder_2 = e_storage_set_get_folder (priv->storage_set, folder_path_2);

	priority_1 = e_folder_get_sorting_priority (folder_1);
	priority_2 = e_folder_get_sorting_priority (folder_2);

	if (priority_1 == priority_2)
		return g_utf8_collate (e_tree_model_value_at (E_TREE_MODEL (etmm), node1, 0),
				       e_tree_model_value_at (E_TREE_MODEL (etmm), node2, 0));
	else if (priority_1 < priority_2)
		return -1;
	else			/* priority_1 > priority_2 */
		return +1;
}

/* Helper functions.  */

static gboolean
add_node_to_hash (EStorageSetView *storage_set_view,
		  const char *path,
		  ETreePath node)
{
	EStorageSetViewPrivate *priv;
	char *hash_path;

	g_return_val_if_fail (g_path_is_absolute (path), FALSE);

	priv = storage_set_view->priv;

	if (g_hash_table_lookup (priv->path_to_etree_node, path) != NULL) {
		g_warning ("EStorageSetView: Node already existing while adding -- %s", path);
		return FALSE;
	}

	hash_path = g_strdup (path);

	g_hash_table_insert (priv->path_to_etree_node, hash_path, node);

	return TRUE;
}

static ETreePath
lookup_node_in_hash (EStorageSetView *storage_set_view,
		     const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;

	priv = storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, path);
	if (node == NULL)
		g_warning ("EStorageSetView: Node not found while updating -- %s", path);

	return node;
}

static ETreePath
remove_node_from_hash (EStorageSetView *storage_set_view,
		       const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;

	priv = storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, path);
	if (node == NULL) {
		g_warning ("EStorageSetView: Node not found while removing -- %s", path);
		return NULL;
	}

	g_hash_table_remove (priv->path_to_etree_node, path);

	return node;
}

static GdkPixbuf *
get_pixbuf_for_folder (EStorageSetView *storage_set_view,
		       EFolder *folder)
{
	EStorageSetViewPrivate *priv;
	const char *icon_name;

	priv = storage_set_view->priv;

	icon_name = e_folder_get_custom_icon_name (folder);
	if (!icon_name) {
		const char *type_name;
		EFolderTypeRegistry *folder_type_registry;

		type_name = e_folder_get_type_string (folder);
		folder_type_registry = e_storage_set_get_folder_type_registry (priv->storage_set);

		icon_name = e_folder_type_registry_get_icon_name_for_type (folder_type_registry,
									   type_name);
	}

	if (!icon_name)
		return NULL;

	return e_icon_factory_get_icon (icon_name, E_ICON_SIZE_LIST);
}

/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (object);
	priv = storage_set_view->priv;

	if (priv->etree_model != NULL) {
		/* Destroy the tree.  */
		e_tree_memory_node_remove (E_TREE_MEMORY(priv->etree_model), priv->root_node);
		g_object_unref (priv->etree_model);
		priv->etree_model = NULL;

		/* (The data in the hash table was all freed by freeing the tree.)  */
		g_hash_table_destroy (priv->path_to_etree_node);
		priv->path_to_etree_node = NULL;
	}

	if (priv->storage_set != NULL) {
		g_object_unref (priv->storage_set);
		priv->storage_set = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (object);
	priv = storage_set_view->priv;

	if (priv->checkboxes != NULL) {
		g_hash_table_foreach (priv->checkboxes, (GHFunc) g_free, NULL);
		g_hash_table_destroy (priv->checkboxes);
	}

	g_free (priv->selected_row_path);
	g_free (priv->right_click_row_path);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* ETree methods.  */

/* -- Source-side DnD.  */

static gint
impl_tree_start_drag (ETree *tree,
		      int row,
		      ETreePath path,
		      int col,
		      GdkEvent *event)
{
	GtkTargetEntry target_entry = { E_FOLDER_DND_PATH_TARGET_TYPE, 0, 0 };
	GdkDragContext *context;
	GtkTargetList *target_list;
	GdkDragAction actions;
	EStorageSetView *storage_set_view;

	storage_set_view = E_STORAGE_SET_VIEW (tree);

	if (! storage_set_view->priv->allow_dnd)
		return FALSE;

	target_list = gtk_target_list_new (&target_entry, 1);
	actions = GDK_ACTION_MOVE | GDK_ACTION_COPY;

	context = e_tree_drag_begin (tree, row, col,
				     target_list,
				     actions,
				     1, event);

	gtk_drag_set_icon_default (context);

	gtk_target_list_unref (target_list);

	return TRUE;
}

static EFolder *
get_folder_at_node (EStorageSetView *storage_set_view,
		    ETreePath path)
{
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	priv = storage_set_view->priv;

	if (path == NULL)
		return NULL;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path);
	g_assert (folder_path != NULL);

	return e_storage_set_get_folder (priv->storage_set, folder_path);
}

static void
impl_tree_drag_begin (ETree *etree,
		      int row,
		      ETreePath path,
		      int col,
		      GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	g_free (priv->selected_row_path);
	priv->selected_row_path = g_strdup (e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path));

	priv->drag_folder = get_folder_at_node (storage_set_view, path);
}

static void
impl_tree_drag_end (ETree *tree,
		    int row,
		    ETreePath path,
		    int col,
		    GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (tree);
	priv = storage_set_view->priv;

	priv->drag_folder = NULL;
}

static void
impl_tree_drag_data_get (ETree *etree,
			 int drag_row,
			 ETreePath drag_path,
			 int drag_col,
			 GdkDragContext *context,
			 GtkSelectionData *selection_data,
			 unsigned int info,
			 guint32 time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	if (priv->drag_folder == NULL)
		return;

	/* FIXME */
}

static void
impl_tree_drag_data_delete (ETree *tree,
			    int row,
			    ETreePath path,
			    int col,
			    GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (tree);
	priv = storage_set_view->priv;

	if (priv->drag_folder == NULL)
		return;

	/* FIXME */
}

/* -- Destination-side DnD.  */

static gboolean
impl_tree_drag_motion (ETree *tree,
		       int row,
		       ETreePath path,
		       int col,
		       GdkDragContext *context,
		       int x,
		       int y,
		       unsigned int time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	const char *folder_path;
	EFolder *folder;

	storage_set_view = E_STORAGE_SET_VIEW (tree);
	priv = storage_set_view->priv;

	if (! priv->allow_dnd)
		return FALSE;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model),
						   e_tree_node_at_row (E_TREE (storage_set_view), row));
	if (folder_path == NULL)
		return FALSE;
	folder = e_storage_set_get_folder (priv->storage_set, folder_path);
	if (!folder)
		return FALSE;

	if (priv->drag_folder) {
		char *storage_name;
		EStorage *storage;

		storage_name = g_strndup (folder_path + 1, strcspn (folder_path + 1, "/"));
		storage = e_storage_set_get_storage (priv->storage_set, storage_name);
		g_free (storage_name);

		if (storage && !e_storage_will_accept_folder (storage, folder, priv->drag_folder))
			return FALSE;
	} else {
		if (!e_folder_dnd_bridge_motion (GTK_WIDGET (storage_set_view), context, time,
						 priv->storage_set, folder_path))
			return FALSE;
	}

	e_tree_drag_highlight (E_TREE (storage_set_view), row, -1);
	gdk_drag_status (context, context->suggested_action, time);
	return TRUE;
}

static void
impl_tree_drag_leave (ETree *etree,
		      int row,
		      ETreePath path,
		      int col,
		      GdkDragContext *context,
		      unsigned int time)
{
	e_tree_drag_unhighlight (etree);
}

static gboolean
impl_tree_drag_drop (ETree *etree,
		     int row,
		     ETreePath path,
		     int col,
		     GdkDragContext *context,
		     int x,
		     int y,
		     unsigned int time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	e_tree_drag_unhighlight (etree);

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model),
						   e_tree_node_at_row (E_TREE (storage_set_view), row));
	if (folder_path == NULL)
		return FALSE;

	return e_folder_dnd_bridge_drop (GTK_WIDGET (etree), context, time,
					 priv->storage_set, folder_path);
}

static void
impl_tree_drag_data_received (ETree *etree,
			      int row,
			      ETreePath path,
			      int col,
			      GdkDragContext *context,
			      int x,
			      int y,
			      GtkSelectionData *selection_data,
			      unsigned int info,
			      unsigned int time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model),
						   e_tree_node_at_row (E_TREE (storage_set_view), row));
	if (path == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	e_folder_dnd_bridge_data_received  (GTK_WIDGET (etree),
					    context,
					    selection_data,
					    time,
					    priv->storage_set,
					    folder_path);
}


static gboolean
impl_right_click (ETree *etree,
		  int row,
		  ETreePath path,
		  int col,
		  GdkEvent *event)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	EFolder *folder;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	/* Avoid recursion which would lock up the event loop (#48388).  */
	if (priv->right_click_row_path != NULL)
		return TRUE;

	priv->right_click_row_path = g_strdup (e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path));

	folder = e_storage_set_get_folder (priv->storage_set,
					   priv->right_click_row_path);

	g_object_ref (folder);
	g_signal_emit (storage_set_view, signals[FOLDER_CONTEXT_MENU], 0,
		       event, folder);
	g_object_unref (folder);

	e_tree_right_click_up (E_TREE (storage_set_view));

	g_free (priv->right_click_row_path);
	priv->right_click_row_path = NULL;

	return TRUE;
}

static void
impl_cursor_activated (ETree *tree,
		       int row,
		       ETreePath path)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (tree);

	priv = storage_set_view->priv;

	g_free (priv->selected_row_path);
	if (path) {
		priv->selected_row_path = g_strdup (e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model), path));

		g_signal_emit (storage_set_view, signals[FOLDER_SELECTED], 0,
			       priv->selected_row_path);
	}
	else
		priv->selected_row_path = NULL;
}

/* ETreeModel Methods */

static gboolean
path_is_storage (ETreeModel *etree,
		 ETreePath tree_path)
{
	return e_tree_model_node_depth (etree, tree_path) == 1;
}

static GdkPixbuf*
etree_icon_at (ETreeModel *etree,
	       ETreePath tree_path,
	       void *model_data)
{
	EStorageSetView *storage_set_view;
	EStorageSet *storage_set;
	EFolder *folder;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	storage_set = storage_set_view->priv->storage_set;

	path = (char*) e_tree_memory_node_get_data (E_TREE_MEMORY(etree), tree_path);

	folder = e_storage_set_get_folder (storage_set, path);
	if (folder == NULL)
		return NULL;
		
	/* No icon for a storage with children (or with no real root folder) */
	if (path_is_storage (etree, tree_path)) {
		EStorage *storage;
		GList *subfolder_paths;

		if (! strcmp (e_folder_get_type_string (folder), "noselect"))
			return NULL;

		storage = e_storage_set_get_storage (storage_set, path + 1);
		subfolder_paths = e_storage_get_subfolder_paths (storage, "/");
		if (subfolder_paths != NULL) {
			e_free_string_list (subfolder_paths);
			return NULL;
		}
	}

	return get_pixbuf_for_folder (storage_set_view, folder);
}

/* This function returns the number of columns in our ETreeModel. */
static int
etree_column_count (ETreeModel *etc,
		    void *data)
{
	return 3;
}

static gboolean
etree_has_save_id (ETreeModel *etm,
		   void *data)
{
	return TRUE;
}

static gchar *
etree_get_save_id (ETreeModel *etm,
		   ETreePath node,
		   void *model_data)
{
	return g_strdup(e_tree_memory_node_get_data (E_TREE_MEMORY(etm), node));
}

static gboolean
etree_has_get_node_by_id (ETreeModel *etm,
			  void *data)
{
	return TRUE;
}

static ETreePath
etree_get_node_by_id (ETreeModel *etm,
		      const char *save_id,
		      void *model_data)
{
	EStorageSetView *storage_set_view;
	storage_set_view = E_STORAGE_SET_VIEW (model_data);

	return g_hash_table_lookup (storage_set_view->priv->path_to_etree_node, save_id);
}

static gboolean
has_checkbox (EStorageSetView *storage_set_view, ETreePath tree_path)
{
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	priv = storage_set_view->priv;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY(storage_set_view->priv->etree_model),
						   tree_path);
	g_assert (folder_path != NULL);

	if (strchr (folder_path + 1, '/') == NULL) {
		/* If it's a toplevel, never allow checking it.  */
		return FALSE;
	}

	if (priv->has_checkbox_func)
		return (* priv->has_checkbox_func) (priv->storage_set,
						    folder_path,
						    priv->has_checkbox_func_data);

	return TRUE;
}

static void *
etree_value_at (ETreeModel *etree,
		ETreePath tree_path,
		int col,
		void *model_data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	EStorageSet *storage_set;
	EFolder *folder;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	priv = storage_set_view->priv;
	storage_set = priv->storage_set;

	/* Storages are always highlighted. */
	if (col == 1 && path_is_storage (etree, tree_path))
		return (void *) TRUE;

	path = (char *) e_tree_memory_node_get_data (E_TREE_MEMORY(etree), tree_path);

	folder = e_storage_set_get_folder (storage_set, path);

	switch (col) {
	case 0: /* Title */
		if (folder == NULL)
			return (void *) "?";
		return (void *) e_folder_get_name (folder);
	case 1: /* bold */
		return GINT_TO_POINTER (FALSE);
	case 2: /* checkbox */
		if (!has_checkbox (storage_set_view, tree_path))
			return GINT_TO_POINTER (2);
		if (priv->checkboxes == NULL)
			return GINT_TO_POINTER (0);
		return GINT_TO_POINTER(g_hash_table_lookup (priv->checkboxes,
							    path) ? 1 : 0);
	default:
		return NULL;
	}

}

static void
etree_fill_in_children (ETreeModel *etree,
			ETreePath tree_path,
			void *model_data)
{
	EStorageSetView *storage_set_view;
	EStorageSet *storage_set;
	ETreePath *parent;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	storage_set = storage_set_view->priv->storage_set;

	parent = e_tree_model_node_get_parent (etree, tree_path);
	path = (char *) e_tree_memory_node_get_data (E_TREE_MEMORY(etree), parent);
	if (tree_path == e_tree_model_node_get_first_child (etree, parent)) {
		g_signal_emit (storage_set_view, signals[FOLDER_OPENED], 0, path);
	}
}

static void
etree_set_value_at (ETreeModel *etree,
		    ETreePath tree_path,
		    int col,
		    const void *val,
		    void *model_data)
{
	gboolean value;
	char *path;
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	char *old_path;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	priv = storage_set_view->priv;

	switch (col) {
	case 2: /* checkbox */
		if (!has_checkbox (storage_set_view, tree_path))
			return;

		e_tree_model_pre_change (etree);

		value = GPOINTER_TO_INT (val);
		path = (char *) e_tree_memory_node_get_data (E_TREE_MEMORY(etree), tree_path);
		if (!priv->checkboxes) {
			priv->checkboxes = g_hash_table_new (g_str_hash, g_str_equal);
		}

		old_path = g_hash_table_lookup (priv->checkboxes, path);

		if (old_path) {
			g_hash_table_remove (priv->checkboxes, path);
			g_free (old_path);
		} else {
			path = g_strdup (path);
			g_hash_table_insert (priv->checkboxes, path, path);
		}

		e_tree_model_node_col_changed (etree, tree_path, col);
		g_signal_emit (storage_set_view, signals[CHECKBOXES_CHANGED], 0);
		break;
	}
}

static gboolean
etree_is_editable (ETreeModel *etree,
		   ETreePath path,
		   int col,
		   void *model_data)
{
	if (col == 2)
		return TRUE;
	else
		return FALSE;
}


/* This function duplicates the value passed to it. */
static void *
etree_duplicate_value (ETreeModel *etc,
		       int col,
		       const void *value,
		       void *data)
{
	if (col == 0)
		return (void *)g_strdup (value);
	else
		return (void *)value;
}

/* This function frees the value passed to it. */
static void
etree_free_value (ETreeModel *etc,
		  int col,
		  void *value,
		  void *data)
{
	if (col == 0)
		g_free (value);
}

/* This function creates an empty value. */
static void *
etree_initialize_value (ETreeModel *etc,
			int col,
			void *data)
{
	if (col == 0)
		return g_strdup ("");
	else
		return NULL;
}

/* This function reports if a value is empty. */
static gboolean
etree_value_is_empty (ETreeModel *etc,
		      int col,
		      const void *value,
		      void *data)
{
	if (col == 0)
		return !(value && *(char *)value);
	else
		return !value;
}

/* This function reports if a value is empty. */
static char *
etree_value_to_string (ETreeModel *etc,
		       int col,
		       const void *value,
		       void *data)
{
	if (col == 0)
		return g_strdup(value);
	else
		return g_strdup(value ? "Yes" : "No");
}

static void
etree_node_destroy_func (void *data,
			 void *user_data)
{
	EStorageSetView *storage_set_view;
	char *path;

	path = (char *) data;
	storage_set_view = E_STORAGE_SET_VIEW (user_data);

	if (strcmp (path, ROOT_NODE_NAME))
		remove_node_from_hash (storage_set_view, path);
	g_free (path);
}

/* StorageSet signal handling.  */

static void
new_storage_cb (EStorageSet *storage_set,
		EStorage *storage,
		void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreePath node;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;

	path = g_strconcat ("/", e_storage_get_name (storage), NULL);

	node = e_tree_memory_node_insert (E_TREE_MEMORY(priv->etree_model), priv->root_node, -1, path);
	e_tree_memory_sort_node (E_TREE_MEMORY(priv->etree_model), priv->root_node,
				 storage_sort_callback, storage_set_view);

	if (! add_node_to_hash (storage_set_view, path, node)) {
		e_tree_memory_node_remove (E_TREE_MEMORY(priv->etree_model), node);
		return;
	}
}

static void
removed_storage_cb (EStorageSet *storage_set,
		    EStorage *storage,
		    void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	path = g_strconcat ("/", e_storage_get_name (storage), NULL);
	node = lookup_node_in_hash (storage_set_view, path);
	g_free (path);

	e_tree_memory_node_remove (E_TREE_MEMORY(etree), node);
}

static void
new_folder_cb (EStorageSet *storage_set,
	       const char *path,
	       void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath parent_node;
	ETreePath new_node;
	const char *last_separator;
	char *parent_path;
	char *copy_of_path;

	g_return_if_fail (g_path_is_absolute (path));

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	last_separator = strrchr (path, '/');

	parent_path = g_strndup (path, last_separator - path);
	parent_node = g_hash_table_lookup (priv->path_to_etree_node, parent_path);
	if (parent_node == NULL) {
		g_warning ("EStorageSetView: EStorageSet reported new subfolder for non-existing folder -- %s",
			   parent_path);
		g_free (parent_path);
		return;
	}

	g_free (parent_path);

	copy_of_path = g_strdup (path);
	new_node = e_tree_memory_node_insert (E_TREE_MEMORY(etree), parent_node, -1, copy_of_path);
	e_tree_memory_sort_node (E_TREE_MEMORY(etree), parent_node, folder_sort_callback, storage_set_view);

	if (! add_node_to_hash (storage_set_view, path, new_node)) {
		e_tree_memory_node_remove (E_TREE_MEMORY(etree), new_node);
		return;
	}

	setup_folder_changed_callbacks (storage_set_view,
					e_storage_set_get_folder (storage_set, path),
					path);
}

static void
updated_folder_cb (EStorageSet *storage_set,
		   const char *path,
		   void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	node = lookup_node_in_hash (storage_set_view, path);
	e_tree_model_pre_change (etree);
	e_tree_model_node_data_changed (etree, node);
}

static void
removed_folder_cb (EStorageSet *storage_set,
		   const char *path,
		   void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	node = lookup_node_in_hash (storage_set_view, path);
	e_tree_memory_node_remove (E_TREE_MEMORY(etree), node);
}

static void
close_folder_cb (EStorageSet *storage_set,
		 const char *path,
		 void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	node = lookup_node_in_hash (storage_set_view, path);
	e_tree_model_node_request_collapse (priv->etree_model, node);
}

static void
class_init (EStorageSetViewClass *klass)
{
	GObjectClass *object_class;
	ETreeClass *etree_class;

	parent_class = g_type_class_ref(PARENT_TYPE);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	etree_class = E_TREE_CLASS (klass);
	etree_class->right_click             = impl_right_click;
	etree_class->cursor_activated        = impl_cursor_activated;
	etree_class->start_drag              = impl_tree_start_drag;
	etree_class->tree_drag_begin         = impl_tree_drag_begin;
	etree_class->tree_drag_end           = impl_tree_drag_end;
	etree_class->tree_drag_data_get      = impl_tree_drag_data_get;
	etree_class->tree_drag_data_delete   = impl_tree_drag_data_delete;
	etree_class->tree_drag_motion        = impl_tree_drag_motion;
	etree_class->tree_drag_drop          = impl_tree_drag_drop;
	etree_class->tree_drag_leave         = impl_tree_drag_leave;
	etree_class->tree_drag_data_received = impl_tree_drag_data_received;

	signals[FOLDER_SELECTED]
		= g_signal_new ("folder_selected",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EStorageSetViewClass, folder_selected),
				NULL, NULL,
				e_shell_marshal_NONE__STRING,
				G_TYPE_NONE, 1,
				G_TYPE_STRING);

	signals[FOLDER_OPENED]
		= g_signal_new ("folder_opened",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EStorageSetViewClass, folder_opened),
				NULL, NULL,
				e_shell_marshal_NONE__STRING,
				G_TYPE_NONE, 1,
				G_TYPE_STRING);

	signals[DND_ACTION]
		= g_signal_new ("dnd_action",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EStorageSetViewClass, dnd_action),
				NULL, NULL,
				e_shell_marshal_NONE__POINTER_POINTER_POINTER_POINTER,
				G_TYPE_NONE, 4,
				G_TYPE_POINTER,
				G_TYPE_POINTER,
				G_TYPE_POINTER,
				G_TYPE_POINTER);

	signals[FOLDER_CONTEXT_MENU]
		= g_signal_new ("folder_context_menu",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EStorageSetViewClass, folder_context_menu),
				NULL, NULL,
				e_shell_marshal_NONE__BOXED_OBJECT,
				G_TYPE_NONE, 2,
				GDK_TYPE_EVENT,
				E_TYPE_FOLDER);

	signals[CHECKBOXES_CHANGED]
		= g_signal_new ("checkboxes_changed",
				G_OBJECT_CLASS_TYPE (object_class),
				G_SIGNAL_RUN_FIRST,
				G_STRUCT_OFFSET (EStorageSetViewClass, checkboxes_changed),
				NULL, NULL,
				e_shell_marshal_NONE__NONE,
				G_TYPE_NONE, 0);

	checks [0] = gdk_pixbuf_new_from_xpm_data (check_empty_xpm);
	checks [1] = gdk_pixbuf_new_from_xpm_data (check_filled_xpm);
	checks [2] = gdk_pixbuf_new_from_xpm_data (check_missing_xpm);
}

static void
init (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;

	priv = g_new0 (EStorageSetViewPrivate, 1);
	priv->path_to_etree_node          = g_hash_table_new (g_str_hash, g_str_equal);
	priv->show_folders                = TRUE;
	priv->show_checkboxes             = FALSE;
	priv->allow_dnd                   = FALSE;  /*FIXME: Enable dnd - See defect #62442 */
	priv->search_enabled              = FALSE;

	storage_set_view->priv = priv;
}

/* Handling of the "changed" signal in EFolders displayed in the EStorageSetView.  */

typedef struct {
	EStorageSetView *storage_set_view;
	char *path;
	EFolder *folder;
	guint name_sig, changed_sig;
} FolderChangedCallbackData;

static void
folder_changed_callback_data_destroy_notify (void *data,
					     GObject *ex_storage_set_view)
{
	FolderChangedCallbackData *fcd;

	fcd = (FolderChangedCallbackData *) data;

	if (fcd->folder) {
		g_signal_handler_disconnect (fcd->folder, fcd->name_sig);
		g_signal_handler_disconnect (fcd->folder, fcd->changed_sig);
		g_object_unref (fcd->folder);
	}
	g_free (fcd->path);
	g_free (fcd);
}

static void
folder_changed_cb (EFolder *folder,
		   void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	FolderChangedCallbackData *callback_data;
	ETreePath node;

	callback_data = (FolderChangedCallbackData *) data;

	storage_set_view = callback_data->storage_set_view;
	priv = callback_data->storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, callback_data->path);
	if (node == NULL) {
		g_warning ("EStorageSetView -- EFolder::changed emitted for a folder whose path I don't know.");
		return;
	}

	e_tree_model_pre_change (priv->etree_model);
	e_tree_model_node_data_changed (priv->etree_model, node);
}

static void
folder_name_changed_cb (EFolder *folder,
			void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	FolderChangedCallbackData *callback_data;
	ETreePath parent_node;
	const char *last_separator;
	char *parent_path;

	callback_data = (FolderChangedCallbackData *) data;

	storage_set_view = callback_data->storage_set_view;
	priv = storage_set_view->priv;

	last_separator = strrchr (callback_data->path, '/');

	parent_path = g_strndup (callback_data->path, last_separator - callback_data->path);
	parent_node = g_hash_table_lookup (priv->path_to_etree_node, parent_path);
	g_free (parent_path);

	if (parent_node == NULL) {
		g_warning ("EStorageSetView -- EFolder::name_changed emitted for a folder whose path I don't know.");
		return;
	}

	e_tree_memory_sort_node (E_TREE_MEMORY (priv->etree_model), parent_node,
				 folder_sort_callback, storage_set_view);
}

static void
setup_folder_changed_callbacks (EStorageSetView *storage_set_view,
				EFolder *folder,
				const char *path)
{
	FolderChangedCallbackData *fcd;

	fcd = g_new0 (FolderChangedCallbackData, 1);
	fcd->storage_set_view = storage_set_view;
	fcd->path = g_strdup (path);
	fcd->folder = g_object_ref (folder);

	fcd->name_sig = g_signal_connect (folder, "name_changed",
					  G_CALLBACK (folder_name_changed_cb),
					  fcd);

	fcd->changed_sig = g_signal_connect (folder, "changed",
					     G_CALLBACK (folder_changed_cb),
					     fcd);

	g_object_weak_ref (G_OBJECT (storage_set_view),
			   folder_changed_callback_data_destroy_notify,
			   fcd);
	g_object_add_weak_pointer (G_OBJECT (folder),
				   (gpointer *)&fcd->folder);
}

static void
insert_folders (EStorageSetView *storage_set_view,
		ETreePath parent,
		EStorage *storage,
		const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;
	GList *folder_path_list;
	GList *p;
	const char *storage_name;

	priv = storage_set_view->priv;
	etree = priv->etree_model;

	storage_name = e_storage_get_name (storage);

	folder_path_list = e_storage_get_subfolder_paths (storage, path);
	if (folder_path_list == NULL)
		return;

	for (p = folder_path_list; p != NULL; p = p->next) {
		EFolder *folder;
		const char *folder_name;
		const char *folder_path;
		char *full_path;

		folder_path = (const char *) p->data;
		folder = e_storage_get_folder (storage, folder_path);
		folder_name = e_folder_get_name (folder);

		full_path = g_strconcat ("/", storage_name, folder_path, NULL);

		setup_folder_changed_callbacks (storage_set_view, folder, full_path);

		node = e_tree_memory_node_insert (E_TREE_MEMORY(etree), parent, -1, (void *) full_path);
		e_tree_memory_sort_node(E_TREE_MEMORY(etree), parent, folder_sort_callback, storage_set_view);
		add_node_to_hash (storage_set_view, full_path, node);

		insert_folders (storage_set_view, node, storage, folder_path);
	}

	e_free_string_list (folder_path_list);
}

static void
insert_storages (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;
	EStorageSet *storage_set;
	GList *storage_list;
	GList *p;

	priv = storage_set_view->priv;

	storage_set = priv->storage_set;

	storage_list = e_storage_set_get_storage_list (storage_set);

	for (p = storage_list; p != NULL; p = p->next) {
		EStorage *storage = E_STORAGE (p->data);
		const char *name;
		char *path;
		ETreePath parent;

		name = e_storage_get_name (storage);
		path = g_strconcat ("/", name, NULL);

		parent = e_tree_memory_node_insert (E_TREE_MEMORY(priv->etree_model), priv->root_node, -1, path);
		e_tree_memory_sort_node (E_TREE_MEMORY(priv->etree_model),
					 priv->root_node,
					 storage_sort_callback, storage_set_view);

		g_hash_table_insert (priv->path_to_etree_node, path, parent);

		if (priv->show_folders)
			insert_folders (storage_set_view, parent, storage, "/");
	}

	e_free_object_list (storage_list);
}

void
e_storage_set_view_construct (EStorageSetView   *storage_set_view,
			      EStorageSet       *storage_set)
{
	EStorageSetViewPrivate *priv;
	ETableExtras *extras;
	ECell *cell;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (E_IS_STORAGE_SET (storage_set));

	priv = storage_set_view->priv;

	priv->etree_model = e_tree_memory_callbacks_new (etree_icon_at,

							 etree_column_count,

							 etree_has_save_id,
							 etree_get_save_id,
							 etree_has_get_node_by_id,
							 etree_get_node_by_id,

							 etree_value_at,
							 etree_set_value_at,
							 etree_is_editable,

							 etree_duplicate_value,
							 etree_free_value,
							 etree_initialize_value,
							 etree_value_is_empty,
							 etree_value_to_string,

							 storage_set_view);

	e_tree_memory_set_node_destroy_func (E_TREE_MEMORY (priv->etree_model),
					     etree_node_destroy_func, storage_set_view);
	e_tree_memory_set_expanded_default (E_TREE_MEMORY (priv->etree_model), FALSE);

	priv->root_node = e_tree_memory_node_insert (E_TREE_MEMORY(priv->etree_model), NULL, -1,
						     g_strdup (ROOT_NODE_NAME));
	add_node_to_hash (storage_set_view, ROOT_NODE_NAME, priv->root_node);

	extras = e_table_extras_new ();
	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	g_object_set((cell), "bold_column", 1, NULL);
	e_table_extras_add_cell (extras, "render_tree",
				 e_cell_tree_new (NULL, NULL, TRUE, cell));

	e_table_extras_add_cell (extras, "optional_checkbox",
				 e_cell_toggle_new (2, 3, checks));

	e_tree_construct_from_spec_file (E_TREE (storage_set_view), priv->etree_model, extras,
					 CONNECTOR_ETSPECDIR "/e-storage-set-view.etspec", NULL);

	e_tree_root_node_set_visible (E_TREE(storage_set_view), FALSE);

	g_object_unref (extras);

	g_object_ref (storage_set);
	priv->storage_set = storage_set;

	e_tree_drag_dest_set (E_TREE (storage_set_view), 0, NULL, 0, GDK_ACTION_MOVE | GDK_ACTION_COPY);

	g_signal_connect_object (storage_set, "new_storage", G_CALLBACK (new_storage_cb), storage_set_view, 0);
	g_signal_connect_object (storage_set, "removed_storage", G_CALLBACK (removed_storage_cb), storage_set_view, 0);
	g_signal_connect_object (storage_set, "new_folder", G_CALLBACK (new_folder_cb), storage_set_view, 0);
	g_signal_connect_object (storage_set, "updated_folder", G_CALLBACK (updated_folder_cb), storage_set_view, 0);
	g_signal_connect_object (storage_set, "removed_folder", G_CALLBACK (removed_folder_cb), storage_set_view, 0);
	g_signal_connect_object (storage_set, "close_folder", G_CALLBACK (close_folder_cb), storage_set_view, 0);

	g_signal_connect_object (priv->etree_model, "fill_in_children", G_CALLBACK (etree_fill_in_children), storage_set_view, 0);

	insert_storages (storage_set_view);
}

/* DON'T USE THIS. Use e_storage_set_new_view() instead. */
GtkWidget *
e_storage_set_view_new (EStorageSet *storage_set)
{
	GtkWidget *new;

	g_return_val_if_fail (E_IS_STORAGE_SET (storage_set), NULL);

	new = g_object_new (e_storage_set_view_get_type (), NULL);

	e_storage_set_view_construct (E_STORAGE_SET_VIEW (new), storage_set);

	return new;
}


EStorageSet *
e_storage_set_view_get_storage_set (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;

	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);

	priv = storage_set_view->priv;
	return priv->storage_set;
}

void
e_storage_set_view_set_current_folder (EStorageSetView *storage_set_view,
				       const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (path != NULL && g_path_is_absolute (path));

	priv = storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, path);
	if (node == NULL)
		return;

	e_tree_show_node (E_TREE (storage_set_view), node);
	e_tree_set_cursor (E_TREE (storage_set_view), node);

	g_free (priv->selected_row_path);
	priv->selected_row_path = g_strdup (path);

	g_signal_emit (storage_set_view, signals[FOLDER_SELECTED], 0, path);
}

const char *
e_storage_set_view_get_current_folder (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;
	ETreePath etree_node;
	const char *path;

	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);

	priv = storage_set_view->priv;

	if (!priv->show_folders)
		return NULL; /* Mmh! */

	etree_node = e_tree_get_cursor (E_TREE (storage_set_view));

	if (etree_node == NULL)
		return NULL; /* Mmh? */

	path = (char*)e_tree_memory_node_get_data(E_TREE_MEMORY(priv->etree_model), etree_node);

	return path;
}

void
e_storage_set_view_set_show_folders (EStorageSetView *storage_set_view,
				     gboolean show)
{
	EStorageSetViewPrivate *priv;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	priv = storage_set_view->priv;

	if (show == priv->show_folders)
		return;

	/* tear down existing tree and hash table mappings */
	e_tree_memory_node_remove (E_TREE_MEMORY(priv->etree_model), priv->root_node);

	/* now re-add the root node */
	priv->root_node = e_tree_memory_node_insert (E_TREE_MEMORY(priv->etree_model), NULL, -1,
						     g_strdup (ROOT_NODE_NAME));
	add_node_to_hash (storage_set_view, ROOT_NODE_NAME, priv->root_node);

	/* then reinsert the storages after setting the "show_folders"
	   flag.  insert_storages will call insert_folders if
	   show_folders is TRUE */

	priv->show_folders = show;
	insert_storages (storage_set_view);
}

gboolean
e_storage_set_view_get_show_folders (EStorageSetView *storage_set_view)
{
	return storage_set_view->priv->show_folders;
}


void
e_storage_set_view_set_show_checkboxes (EStorageSetView *storage_set_view,
					gboolean show,
					EStorageSetViewHasCheckBoxFunc has_checkbox_func,
					void *func_data)
{
	EStorageSetViewPrivate *priv;
	ETableState *state;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	priv = storage_set_view->priv;

	show = !! show;

	if (show == priv->show_checkboxes)
		return;

	priv->show_checkboxes = show;

	state = e_tree_get_state_object (E_TREE (storage_set_view));
	state->col_count = show ? 2 : 1;
	state->columns = g_renew (int, state->columns, state->col_count);
	state->columns [state->col_count - 1] = 0;
	if (show)
		state->columns [0] = 1;

	state->expansions = g_renew (double, state->expansions, state->col_count);
	state->expansions [0] = 1.0;
	if (show)
		state->expansions [1] = 1.0;

	e_tree_set_state_object (E_TREE (storage_set_view), state);

	priv->has_checkbox_func = has_checkbox_func;
	priv->has_checkbox_func_data = func_data;
}

gboolean
e_storage_set_view_get_show_checkboxes (EStorageSetView *storage_set_view)
{
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), FALSE);

	return storage_set_view->priv->show_checkboxes;
}

void
e_storage_set_view_enable_search (EStorageSetView *storage_set_view,
				  gboolean enable)
{
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	enable = !! enable;

	if (enable == storage_set_view->priv->search_enabled)
		return;
	
	storage_set_view->priv->search_enabled = enable;
	e_tree_set_search_column (E_TREE (storage_set_view), enable ? 0 : -1);
}

void
e_storage_set_view_set_checkboxes_list (EStorageSetView *storage_set_view,
					GSList          *checkboxes)
{
	gboolean changed = FALSE;
	EStorageSetViewPrivate *priv = storage_set_view->priv;

	e_tree_model_pre_change (priv->etree_model);
	if (priv->checkboxes) {
		g_hash_table_foreach (priv->checkboxes, (GHFunc) g_free, NULL);
		g_hash_table_destroy (priv->checkboxes);
		changed = TRUE;
	}

	if (checkboxes) {
		priv->checkboxes = g_hash_table_new (g_str_hash, g_str_equal);
		for (; checkboxes; checkboxes = g_slist_next (checkboxes)) {
			char *path = checkboxes->data;

			if (g_hash_table_lookup (priv->checkboxes, path))
				continue;
			path = g_strdup (path);
			g_hash_table_insert (priv->checkboxes, path, path);
		}
		changed = TRUE;
	}

	if (changed)
		e_tree_model_node_changed (priv->etree_model,
					   e_tree_model_get_root (priv->etree_model));
	else
		e_tree_model_no_change (priv->etree_model);
}

static void
essv_add_to_list (gpointer	key,
		  gpointer	value,
		  gpointer	user_data)
{
	GSList **list = user_data;

	*list = g_slist_prepend (*list, g_strdup (key));
}

GSList *
e_storage_set_view_get_checkboxes_list (EStorageSetView *storage_set_view)
{
	GSList *list = NULL;

	if (storage_set_view->priv->checkboxes) {
		g_hash_table_foreach (storage_set_view->priv->checkboxes, essv_add_to_list, &list);

		list = g_slist_reverse (list);
	}

	return list;
}

void
e_storage_set_view_set_allow_dnd (EStorageSetView *storage_set_view,
				  gboolean allow_dnd)
{
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	storage_set_view->priv->allow_dnd = !! allow_dnd;
}

gboolean
e_storage_set_view_get_allow_dnd (EStorageSetView *storage_set_view)
{
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), FALSE);

	return storage_set_view->priv->allow_dnd;
}

const char *
e_storage_set_view_get_right_click_path (EStorageSetView *storage_set_view)
{
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);

	return storage_set_view->priv->right_click_row_path;
}

E_MAKE_TYPE (e_storage_set_view, "EStorageSetView", EStorageSetView, class_init, init, PARENT_TYPE)
