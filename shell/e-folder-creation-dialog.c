/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage_set.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>

#include <gtk/gtkentry.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#undef GTK_DISABLE_DEPRECATED
#include <gtk/gtkoptionmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkscrolledwindow.h>

#include <glade/glade-xml.h>

#include <gal/util/e-util.h>
#include <shell/e-shell-utils.h>
#include <e-util/e-icon-factory.h>

#include "e-storage-set.h"
#include "e-storage-set-view.h"

#include "e-util/e-dialog-utils.h"

#include "e-folder-creation-dialog.h"

#define GLADE_FILE_NAME  CONNECTOR_GLADEDIR "/e-folder-creation-dialog.glade"

/* Forward declarations for the weak references.  */
static void dialog_destroy_notify (void *data, GObject *where_the_dialog_was);
static void storage_set_destroy_notify (void *data, GObject *where_the_was);

/* Data for the callbacks.  */
typedef struct {
	GtkWidget *dialog;
	EStorageSet *storage_set;

	GtkWidget *folder_name_entry;
	GtkWidget *storage_set_view;
	GtkWidget *folder_type_option_menu;

	GList *folder_types;

	char *folder_path;

	EFolderCreationDialogCallback result_callback;
	void *result_callback_data;

	gboolean creation_in_progress;
} DialogData;

static void
dialog_data_destroy (DialogData *dialog_data)
{
	e_free_string_list (dialog_data->folder_types);
	g_free (dialog_data->folder_path);

	if (dialog_data->dialog != NULL)
		g_object_weak_unref (G_OBJECT (dialog_data->dialog), dialog_destroy_notify, dialog_data);

	if (dialog_data->storage_set != NULL)
		g_object_weak_unref (G_OBJECT (dialog_data->storage_set), storage_set_destroy_notify, dialog_data);

	g_free (dialog_data);
}

/* Callback for the asynchronous folder creation function.  */

static void
async_create_cb (EStorageSet *storage_set,
		 EStorageResult result,
		 void *data)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;

	dialog_data->creation_in_progress = FALSE;

	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog_data->dialog), GTK_RESPONSE_OK, TRUE);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog_data->dialog), GTK_RESPONSE_CANCEL, TRUE);

	if (result == E_STORAGE_OK) {
		/* Success! Tell the callback of this, then return */
		if (dialog_data->result_callback != NULL)
			(* dialog_data->result_callback) (dialog_data->storage_set,
							  E_FOLDER_CREATION_DIALOG_RESULT_SUCCESS,
							  dialog_data->folder_path,
							  dialog_data->result_callback_data);
		if (dialog_data->dialog != NULL) {
			gtk_widget_destroy (dialog_data->dialog);
		} else {
			/* If dialog_data->dialog is NULL, it means that the
			   dialog has been destroyed before we were done, so we
			   have to free the dialog_data ourselves.  */
			dialog_data_destroy (dialog_data);
		}
		return;
	}

	/* Tell the callback something failed, then popup a dialog
           explaining how it failed */
	if (dialog_data->result_callback != NULL)
		(* dialog_data->result_callback) (dialog_data->storage_set,
						  E_FOLDER_CREATION_DIALOG_RESULT_FAIL,
						  dialog_data->folder_path,
						  dialog_data->result_callback_data);

	e_notice (dialog_data->dialog, GTK_MESSAGE_ERROR,
		  _("Cannot create the specified folder:\n%s"),
		  e_storage_result_to_string (result));

	/* If dialog_data->dialog is NULL, it means that the dialog has been
	   destroyed before we were done, so we have to free the dialog_data
	   ourselves.  */
	if (dialog_data->dialog == NULL)
		dialog_data_destroy (dialog_data);
}

/* Dialog signal callbacks.  */

static void
dialog_response_cb (GtkDialog *dialog,
		    int response_id,
		    void *data)
{
	DialogData *dialog_data;
	GtkWidget *folder_type_menu_item;
	const char *folder_type;
	const char *parent_path;
	const char *reason;
	const char *folder_name;
	char *path;

	dialog_data = (DialogData *) data;

	if (response_id != GTK_RESPONSE_OK) {
		if (dialog_data->result_callback != NULL)
			(* dialog_data->result_callback) (dialog_data->storage_set,
							  E_FOLDER_CREATION_DIALOG_RESULT_CANCEL,
							  NULL,
							  dialog_data->result_callback_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	folder_name = gtk_entry_get_text(GTK_ENTRY (dialog_data->folder_name_entry));

	if (! e_shell_folder_name_is_valid (folder_name, &reason)) {
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("The specified folder name is not valid: %s"), reason);
		return;
	}

	parent_path = e_storage_set_view_get_current_folder
		(E_STORAGE_SET_VIEW (dialog_data->storage_set_view));
	if (parent_path == NULL) {
		if (dialog_data->result_callback != NULL)
			(* dialog_data->result_callback) (dialog_data->storage_set,
							  E_FOLDER_CREATION_DIALOG_RESULT_CANCEL,
							  NULL,
							  dialog_data->result_callback_data);
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	path = g_build_filename (parent_path, folder_name, NULL);

	folder_type_menu_item = GTK_OPTION_MENU (dialog_data->folder_type_option_menu)->menu_item;
	folder_type = g_object_get_data (G_OBJECT (folder_type_menu_item), "folder_type");

	if (folder_type == NULL) {
		g_warning ("Cannot get folder type for selected GtkOptionMenu item.");
		return;
	}

	g_free (dialog_data->folder_path);
	dialog_data->folder_path = path;

	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, FALSE);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL, FALSE);

	dialog_data->creation_in_progress = TRUE;

	e_storage_set_async_create_folder (dialog_data->storage_set,
					   path,
					   folder_type,
					   async_create_cb, dialog_data);
}

static void
dialog_destroy_notify (void *data,
		       GObject *where_the_dialog_was)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;
	dialog_data->dialog = NULL;

	if (dialog_data->creation_in_progress) {
		/* If the dialog has been closed before we are done creating
		   the folder, the dialog_data will be freed after the creation
		   is completed.  */
		dialog_data->folder_name_entry = NULL;
		dialog_data->storage_set_view = NULL;
		dialog_data->folder_type_option_menu = NULL;
		return;
	}

	dialog_data_destroy (dialog_data);
}

static void
folder_name_entry_activate_cb (GtkEntry *entry,
			       void *data)
{
	DialogData *dialog_data;
	const char *parent_path;
	
	dialog_data = (DialogData *) data;

	parent_path = e_storage_set_view_get_current_folder (E_STORAGE_SET_VIEW (dialog_data->storage_set_view));

	if (parent_path != NULL
	    && GTK_ENTRY (dialog_data->folder_name_entry)->text_length > 0)
		gtk_dialog_response (GTK_DIALOG (dialog_data->dialog), GTK_RESPONSE_OK);	
}

static void
folder_name_entry_changed_cb (GtkEditable *editable,
			      void *data)
{
	DialogData *dialog_data;
	const char *parent_path;

	dialog_data = (DialogData *) data;

	parent_path = e_storage_set_view_get_current_folder (E_STORAGE_SET_VIEW (dialog_data->storage_set_view));

	if (parent_path != NULL
	    && GTK_ENTRY (dialog_data->folder_name_entry)->text_length > 0)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog_data->dialog), GTK_RESPONSE_OK, TRUE);
	else
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog_data->dialog), GTK_RESPONSE_OK, FALSE);
}

static void
storage_set_view_folder_selected_cb (EStorageSetView *storage_set_view,
				     const char *path,
				     void *data)
{
	DialogData *dialog_data;

	dialog_data = (DialogData *) data;

	if (GTK_ENTRY (dialog_data->folder_name_entry)->text_length > 0)
		gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog_data->dialog), GTK_RESPONSE_OK, TRUE);
}

/* EStorageSet signal callbacks.  */

static void
storage_set_destroy_notify (void *data, GObject *ex_storage_set)
{
	DialogData *dialog_data = (DialogData *) data;

	dialog_data->storage_set = NULL;
	gtk_widget_destroy (GTK_WIDGET (dialog_data->dialog));
}

/* Dialog setup.  */

static void
setup_dialog (GtkWidget *dialog,
	      GladeXML *gui,
	      GtkWidget *parent)
{
	if (parent != NULL)
		e_dialog_set_transient_for (GTK_WINDOW (dialog), parent);

	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gtk_window_set_title (GTK_WINDOW (dialog), _("Create New Folder"));

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK, FALSE);

	gtk_widget_show (dialog);
}

static void
setup_folder_name_entry (GtkWidget *dialog,
			 GladeXML *gui)
{
	GtkWidget *folder_name_entry;

	folder_name_entry = glade_xml_get_widget (gui, "folder_name_entry");
}

static GtkWidget *
add_storage_set_view (GtkWidget *dialog,
		      GladeXML *gui,
		      EStorageSet *storage_set,
		      EFolder *default_parent)
{
	GtkWidget *storage_set_view;
	GtkWidget *scrolled_window;
	GtkWidget *vbox;

	storage_set_view = e_storage_set_create_new_view (storage_set);

	e_storage_set_view_set_allow_dnd (E_STORAGE_SET_VIEW (storage_set_view), FALSE);

	GTK_WIDGET_SET_FLAGS (storage_set_view, GTK_CAN_FOCUS);

	if (default_parent != NULL) {
		const char *physical_uri;
		char *path;

		physical_uri = e_folder_get_physical_uri (default_parent);
		path = e_storage_set_get_path_for_physical_uri (storage_set, physical_uri);
		if (path) {
			e_storage_set_view_set_current_folder (E_STORAGE_SET_VIEW (storage_set_view),
							       path);
			g_free (path);
		}
	}

	vbox = glade_xml_get_widget (gui, "main_vbox");

	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled_window, TRUE, TRUE, 6);

	gtk_container_add (GTK_CONTAINER (scrolled_window), storage_set_view);

	gtk_widget_show (scrolled_window);
	gtk_widget_show (storage_set_view);

	return storage_set_view;
}

typedef struct {
	const char *type;
	const char *display_name;
	GdkPixbuf  *icon;
} EFolderCreationType;

static int
type_compare_func (const void *a, const void *b)
{
	const EFolderCreationType *val_a, *val_b;
	char *a_display_name_casefolded;
	char *b_display_name_casefolded;
	int retval;

	val_a = (const EFolderCreationType *) a;
	val_b = (const EFolderCreationType *) b;

	a_display_name_casefolded = g_utf8_casefold (val_a->display_name, -1);
	b_display_name_casefolded = g_utf8_casefold (val_b->display_name, -1);

	retval = g_utf8_collate (a_display_name_casefolded, b_display_name_casefolded);

	g_free (a_display_name_casefolded);
	g_free (b_display_name_casefolded);

	return retval;
}

static GList *
add_folder_types (GtkWidget *dialog,
		  GladeXML *gui,
		  EStorageSet *storage_set,
		  const char *default_type)
{
	EFolderTypeRegistry *folder_type_registry;
	GtkWidget *folder_type_option_menu;
	GtkWidget *menu;
	GList *folder_types;
	GList *types;
	GList *p;
	int default_item;
	int i, len;

	folder_type_option_menu = glade_xml_get_widget (gui, "folder_type_option_menu");

	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (folder_type_option_menu));

	folder_type_registry = e_storage_set_get_folder_type_registry (storage_set);
	g_assert (folder_type_registry != NULL);

	folder_types = e_folder_type_registry_get_type_names (folder_type_registry);
	if (folder_types == NULL)
		return NULL;		/* Uh? */

	types = NULL;
	for (p = folder_types; p != NULL; p = p->next) {
		EFolderCreationType *new;
		const char *typename = p->data;
		const char *icon_name;

		if (! e_folder_type_registry_type_is_user_creatable (folder_type_registry, typename))
			continue;

		new = g_new0 (EFolderCreationType, 1);
		new->type = typename;
		new->display_name = e_folder_type_registry_get_display_name_for_type (folder_type_registry, typename);
		icon_name = e_folder_type_registry_get_icon_name_for_type (folder_type_registry, typename);
		new->icon = e_icon_factory_get_icon (icon_name, E_ICON_SIZE_MENU);

		types = g_list_prepend (types, new);
	}

	types = g_list_sort (types, type_compare_func);

	default_item = -1;
	for (p = types, i = 0; p != NULL; p = p->next, i++) {
		EFolderCreationType *type;
		GtkWidget *menu_item, *box, *label, *icon;

		type = p->data;

		label = gtk_label_new (type->display_name);
		gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
		icon = gtk_image_new_from_pixbuf (type->icon);
		box = gtk_hbox_new (FALSE, 6);
		gtk_box_pack_start (GTK_BOX (box), icon, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

		menu_item = gtk_menu_item_new ();
		gtk_container_add (GTK_CONTAINER (menu_item), box);
		gtk_widget_show_all (menu_item);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

		g_object_set_data_full (G_OBJECT (menu_item), "folder_type", g_strdup (type->type), g_free);

		if (strcmp (type->type, default_type) == 0)
			default_item = i;
		else if (default_item == -1) {
			len = strlen (type->type);
			if (strncmp (type->type, default_type, len) == 0 &&
			    default_type[len] == '/')
				default_item = i;
		}
	}
	if (default_item == -1)
		default_item = 0;

	for (p = types; p != NULL; p = p->next)
		g_free (p->data);
	g_list_free (types);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (folder_type_option_menu), menu);
	gtk_widget_show (menu);

	gtk_option_menu_set_history (GTK_OPTION_MENU (folder_type_option_menu), default_item);
	gtk_widget_queue_resize (folder_type_option_menu);

	return folder_types;
}

static const char *
get_type_from_parent (EFolder *folder)
{
	const char *folder_type;

	folder_type = e_folder_get_type_string (folder);
	if (folder_type == NULL || strcmp (folder_type, "noselect") == 0)
		return "mail";
	else
		return folder_type;
}

/* FIXME: Currently this is modal.  I think it's OK, but if people think it is
   not, we should change it to non-modal and make sure only one of these is
   open at once.  Currently it relies on modality for this.  */
void
e_folder_creation_dialog (EStorageSetView *parent_storage_set_view,
			  EFolder *default_parent,
			  const char *default_type,
			  EFolderCreationDialogCallback result_callback,
			  gpointer result_callback_data)
{
	GladeXML *gui;
	GtkWidget *dialog;
	GtkWidget *storage_set_view;
	GList *folder_types;
	EStorageSet *storage_set;
	DialogData *dialog_data;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (parent_storage_set_view));

	gui = glade_xml_new (GLADE_FILE_NAME, NULL, NULL);
	if (gui == NULL) {
		g_warning ("Cannot load Glade description file for the folder creation dialog -- %s",
			   GLADE_FILE_NAME);
		return;
	}

	dialog = glade_xml_get_widget (gui, "create_folder_dialog");

	setup_dialog (dialog, gui, GTK_WIDGET (parent_storage_set_view));
	setup_folder_name_entry (dialog, gui);

	storage_set = e_storage_set_view_get_storage_set (parent_storage_set_view);
	storage_set_view = add_storage_set_view (dialog, gui, storage_set, default_parent);
	if (!default_type)
		default_type = get_type_from_parent (default_parent);
	folder_types = add_folder_types (dialog, gui, storage_set, default_type);

	dialog_data = g_new0 (DialogData, 1);
	dialog_data->dialog                  = dialog;
	dialog_data->storage_set             = storage_set;
	dialog_data->folder_name_entry       = glade_xml_get_widget (gui, "folder_name_entry");
	dialog_data->storage_set_view        = storage_set_view;
	dialog_data->folder_type_option_menu = glade_xml_get_widget (gui, "folder_type_option_menu");
	dialog_data->folder_types            = folder_types;
	dialog_data->folder_path             = NULL;
	dialog_data->result_callback         = result_callback;
	dialog_data->result_callback_data    = result_callback_data;
	dialog_data->creation_in_progress    = FALSE;

	g_signal_connect (dialog, "response",
			  G_CALLBACK (dialog_response_cb), dialog_data);
	g_object_weak_ref (G_OBJECT (dialog), dialog_destroy_notify, dialog_data);

	g_signal_connect (dialog_data->folder_name_entry, "changed",
			  G_CALLBACK (folder_name_entry_changed_cb), dialog_data);

	g_signal_connect (dialog_data->folder_name_entry, "activate",
			  G_CALLBACK (folder_name_entry_activate_cb), dialog_data);

	g_signal_connect (dialog_data->storage_set_view, "folder_selected",
			  G_CALLBACK (storage_set_view_folder_selected_cb), dialog_data);

	g_object_weak_ref (G_OBJECT (storage_set), storage_set_destroy_notify, dialog_data);

	g_object_unref (gui);
}
