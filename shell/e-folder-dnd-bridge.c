/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-dnd-bridge.c - Utility functions for handling dnd to Evolution
 * folders using the ShellComponentDnd interface.
 *
 * Copyright (C) 2002 Ximian, Inc.
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-folder-dnd-bridge.h"

#include "e-storage-set-view.h"

#include <e-util/e-dialog-utils.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>

#include <string.h>

/* Callbacks for folder operations.  */

static void
folder_xfer_callback (EStorageSet *storage_set,
		      EStorageResult result,
		      void *data)
{
	GtkWindow *parent;

	if (result == E_STORAGE_OK)
		return;

	parent = GTK_WINDOW (data);
	e_notice (parent, GTK_MESSAGE_ERROR, _("Cannot transfer folder:\n%s"),
		  e_storage_result_to_string (result));
}

/* Utility functions.  */

/* This will look for the targets in @drag_context, choose one that matches
   with the allowed types at @path, and return its name. */
static const char *
find_matching_target_for_drag_context (EStorageSet *storage_set,
				       const char *path,
				       GdkDragContext *drag_context,
				       GdkAtom *atom_return)
{
	EFolderTypeRegistry *folder_type_registry;
	EFolder *folder;
	GList *accepted_types;
	GList *p, *q;

	folder_type_registry = e_storage_set_get_folder_type_registry (storage_set);

	folder = e_storage_set_get_folder (storage_set, path);
	if (folder == NULL)
		return NULL;

	accepted_types = e_folder_type_registry_get_accepted_dnd_types_for_type (folder_type_registry,
										 e_folder_get_type_string (folder));

	for (p = drag_context->targets; p != NULL; p = p->next) {
		char *possible_type;

		possible_type = gdk_atom_name (p->data);
		for (q = accepted_types; q != NULL; q = q->next) {
			const char *accepted_type;

			accepted_type = (const char *) q->data;
			if (strcmp (possible_type, accepted_type) == 0) {
				g_free (possible_type);

				if (atom_return != NULL)
					*atom_return = p->data;

				return accepted_type;
			}
		}

		g_free (possible_type);
	}

	if (atom_return != NULL)
		*atom_return = 0;

	return NULL;
}

/* Bridge for the DnD motion event.  */

gboolean
e_folder_dnd_bridge_motion  (GtkWidget *widget,
			     GdkDragContext *context,
			     unsigned int time,
			     EStorageSet *storage_set,
			     const char *path)
{
	const char *dnd_type;

	g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
	g_return_val_if_fail (context != NULL, FALSE);
	g_return_val_if_fail (E_IS_STORAGE_SET (storage_set), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	dnd_type = find_matching_target_for_drag_context (storage_set, path, context, NULL);
	return (dnd_type != NULL);
}

/* Bridge for the drop event.  */

gboolean
e_folder_dnd_bridge_drop (GtkWidget *widget,
			  GdkDragContext *context,
			  unsigned int time,
			  EStorageSet *storage_set,
			  const char *path)
{
	GdkAtom atom;

	g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
	g_return_val_if_fail (context != NULL, FALSE);
	g_return_val_if_fail (E_IS_STORAGE_SET (storage_set), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (context->targets == NULL)
		return FALSE;

	if (find_matching_target_for_drag_context (storage_set, path, context, &atom) == NULL)
		return FALSE;

	gtk_drag_get_data (widget, context, atom, time);

	return FALSE;
}

/* Bridge for the data_received event.  */

static gboolean
handle_data_received_path (GdkDragContext *context,
			   GtkSelectionData *selection_data,
			   EStorageSet *storage_set,
			   const char *path,
			   GtkWindow *toplevel_window)
{
	const char *source_path;
	char *destination_path;
	char *base_name;
	gboolean handled;

	source_path = (const char *) selection_data->data;

	/* (Basic sanity checks.)  */
	if (source_path == NULL || source_path[0] != '/' || source_path[1] == '\0')
		return FALSE;

	base_name = g_path_get_basename (source_path);
	destination_path = g_build_filename (path, base_name, NULL);
	g_free (base_name);

	switch (context->action) {
	case GDK_ACTION_MOVE:
		e_storage_set_async_xfer_folder (storage_set,
						 source_path,
						 destination_path,
						 TRUE,
						 folder_xfer_callback,
						 toplevel_window);
		handled = TRUE;
		break;
	case GDK_ACTION_COPY:
		e_storage_set_async_xfer_folder (storage_set,
						 source_path,
						 destination_path,
						 FALSE,
						 folder_xfer_callback,
						 toplevel_window);
		handled = TRUE;
		break;
	default:
		handled = FALSE;
		g_warning ("EStorageSetView: Unknown action %d", context->action);
	}

	g_free (destination_path);

	return handled;
}

static gboolean
handle_data_received_non_path (GdkDragContext *context,
			       GtkSelectionData *selection_data,
			       EStorageSet *storage_set,
			       const char *path,
			       const char *target_type)
{
	EFolder *folder;

	folder = e_storage_set_get_folder (storage_set, path);
	if (!folder)
		return FALSE;

	return e_folder_accept_drop (folder, context,
				     target_type, selection_data);
}

void
e_folder_dnd_bridge_data_received (GtkWidget *widget,
				   GdkDragContext *context,
				   GtkSelectionData *selection_data,
				   unsigned int time,
				   EStorageSet *storage_set,
				   const char *path)
{
	char *target_type;
	gboolean handled;

	g_return_if_fail (GTK_IS_WIDGET (widget));
	g_return_if_fail (context != NULL);
	g_return_if_fail (E_IS_STORAGE_SET (storage_set));
	g_return_if_fail (path != NULL);

	if (selection_data->data == NULL && selection_data->length == -1)
		return;

	target_type = gdk_atom_name (selection_data->target);

	if (strcmp (target_type, E_FOLDER_DND_PATH_TARGET_TYPE) != 0) {
		handled = handle_data_received_non_path (context, selection_data, storage_set,
							 path, target_type);
	} else {
		GtkWindow *toplevel_window;

		toplevel_window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (widget)));
		handled = handle_data_received_path (context, selection_data, storage_set, path,
						     toplevel_window);
	}

	g_free (target_type);
	gtk_drag_finish (context, handled, FALSE, time);
}

