/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-misc-dialogs.c
 *
 * Copyright (C) 2000-2004 Novell, Inc.
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

#include "e-folder-misc-dialogs.h"
#include "Evolution-Addressbook-SelectNames.h"

#include <string.h>

#include <bonobo-activation/bonobo-activation-activate.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-widget.h>
#include <glade/glade-xml.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkentry.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtkstock.h>
#include <e-util/e-dialog-utils.h>
#include <e-util/e-request.h>
#include <libgnome/gnome-i18n.h>

#undef GTK_DISABLE_DEPRECATED
#include <gtk/gtkcombo.h>
#include <gtk/gtkoptionmenu.h>

typedef struct {
	EStorageSetView *storage_set_view;
	char *folder_path;
} EFolderDeleteData;

static void
delete_cb (EStorageSet *storage_set,
	   EStorageResult result,
	   gpointer user_data)
{
	EFolderDeleteData *dd = user_data;

	if (result != E_STORAGE_OK) {
		e_notice (dd->storage_set_view, GTK_MESSAGE_ERROR,
			  _("Cannot delete folder:\n%s"),
			  e_storage_result_to_string (result));
	}

	g_object_unref (dd->storage_set_view);
	g_free (dd->folder_path);
	g_free (dd);
}

static void
delete_dialog_response (GtkDialog *dialog, int response, gpointer data)
{
	EFolderDeleteData *dd = data;

	if (response == GTK_RESPONSE_OK) {
		EStorageSet *storage_set =
			e_storage_set_view_get_storage_set (dd->storage_set_view);
		e_storage_set_async_remove_folder (storage_set,
						   dd->folder_path,
						   delete_cb, dd);
	} else {
		/* Silently free dd */
		delete_cb (NULL, E_STORAGE_OK, dd);
	}

	if (response != GTK_RESPONSE_DELETE_EVENT)
		gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
e_folder_delete_dialog (EStorageSetView *storage_set_view,
			EFolder     	*folder,
			const char  	*folder_path)
{
	GtkWidget *dialog;
	EFolderDeleteData *dd;
	const char *folder_name;
	char *title;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (folder_path != NULL);

	dd = g_new0 (EFolderDeleteData, 1);
	dd->storage_set_view = g_object_ref (storage_set_view);
	dd->folder_path = g_strdup (folder_path);

	folder_name = e_folder_get_name (folder);

	dialog = gtk_message_dialog_new (NULL,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 _("Really delete folder \"%s\"?"),
					 folder_name);
	e_dialog_set_transient_for (GTK_WINDOW (dialog),
				    GTK_WIDGET (storage_set_view));

	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_DELETE, GTK_RESPONSE_OK);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6); 
	
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 6);

	title = g_strdup_printf (_("Delete \"%s\""), folder_name);
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_free (title);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (delete_dialog_response),
			  dd);
	gtk_widget_show (dialog);
}


typedef struct {
	EStorageSetView *storage_set_view;
	EStorageSet *storage_set;
	char *new_name, *new_path;
} EFolderRenameData;

static void
rename_cb (EStorageSet *storage_set, EStorageResult result, void *data)
{
	EFolderRenameData *frd = data;
	EFolder *folder;

	if (result != E_STORAGE_OK) {
		e_notice (frd->storage_set_view, GTK_MESSAGE_ERROR,
			  _("Cannot rename folder:\n%s"),
			  e_storage_result_to_string (result));
	} else {
		folder = e_storage_set_get_folder (frd->storage_set,
						   frd->new_path);
		if (folder)
			e_folder_set_name (folder, frd->new_name);
	}

	g_object_unref (frd->storage_set_view);
	g_free (frd->new_path);
	g_free (frd->new_name);
	g_free (frd);
}

void
e_folder_rename_dialog (EStorageSetView *storage_set_view,
			EFolder         *folder)
{
	EFolderRenameData *frd;
	EStorageSet *storage_set;
	const char *old_name;
	char *prompt, *new_name;
	char *old_path, *old_base_path, *new_path;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (E_IS_FOLDER (folder));

	storage_set = e_storage_set_view_get_storage_set (storage_set_view);

	old_name = e_folder_get_name (folder);
	old_path = e_storage_set_get_path_for_physical_uri (storage_set, e_folder_get_physical_uri (folder));
	old_base_path = g_path_get_dirname (old_path);

	prompt = g_strdup_printf (_("Rename the \"%s\" folder to:"), old_name);

	while (1) {
		new_name = e_request_string (NULL, _("Rename Folder"),
					     prompt, old_name);
		if (new_name == NULL)
			break;
		if (strcmp (old_name, new_name) == 0) {
			g_free (new_name);
			new_name = NULL;
			break;
		}

		new_path = g_build_filename (old_base_path, new_name, NULL);
		if (e_storage_set_get_folder (storage_set, new_path) != NULL) {
			g_free (new_path);
			e_notice (storage_set_view, GTK_MESSAGE_ERROR,
				  _("A folder named \"%s\" already exists. Please use a different name."),
				  new_name);
			continue;
		}

		break;
	}

	g_free (prompt);
	g_free (old_base_path);

	if (new_name) {
		frd = g_new0 (EFolderRenameData, 1);
		frd->storage_set_view = g_object_ref (storage_set_view);
		frd->storage_set = storage_set;
		frd->new_name = new_name;
		frd->new_path = new_path;

		e_storage_set_async_xfer_folder (storage_set,
						 old_path, new_path, TRUE,
						 rename_cb, frd);
	}

	g_free (old_path);
}


/* Timeout for showing the progress dialog (msecs).  */

#define PROGRESS_DIALOG_DELAY 500


/* Dialog creation and handling.  */

static void
setup_folder_name_combo (GladeXML *glade_xml)
{
	GtkWidget *combo;
	GList *string_list;
	char *strings[] = {
		"Calendar",
		"Inbox",
		"Contacts",
		NULL
		/* FIXME: Should these be translated?  */
	};
	int i;

	combo = glade_xml_get_widget (glade_xml, "folder-name-combo");
	g_assert (GTK_IS_COMBO (combo));

	string_list = NULL;
	for (i = 0; strings[i] != NULL; i ++)
		string_list = g_list_append (string_list, strings[i]);
	gtk_combo_set_popdown_strings (GTK_COMBO (combo), string_list);
	g_list_free (string_list);

	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (combo)->entry), "Calendar");
}

static void
user_clicked (GtkWidget *button, GNOME_Evolution_Addressbook_SelectNames corba_iface)
{
	CORBA_Environment ev;
	
	CORBA_exception_init (&ev);
	GNOME_Evolution_Addressbook_SelectNames_activateDialog (corba_iface, "User", &ev);

	if (BONOBO_EX (&ev))
		g_warning ("Cannot activate SelectNames dialog -- %s", BONOBO_EX_REPOID (&ev));

	CORBA_exception_free (&ev);
}

static GtkWidget *
setup_name_selector (GladeXML *glade_xml,
		     GNOME_Evolution_Addressbook_SelectNames *iface_ret)
{
	GNOME_Evolution_Addressbook_SelectNames corba_iface;
	Bonobo_Control control;
	CORBA_Environment ev;
	GtkWidget *placeholder;
	GtkWidget *control_widget;
	GtkWidget *button;

	placeholder = glade_xml_get_widget (glade_xml, "user-picker-placeholder");
	g_assert (GTK_IS_CONTAINER (placeholder));

	CORBA_exception_init (&ev);

	corba_iface = bonobo_activation_activate_from_id ("OAFIID:GNOME_Evolution_Addressbook_SelectNames:" BASE_VERSION,
							  0, NULL, &ev);
	if (corba_iface == CORBA_OBJECT_NIL || BONOBO_EX (&ev)) {
		g_warning ("Cannot activate SelectNames -- %s", BONOBO_EX_REPOID (&ev));
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}

	GNOME_Evolution_Addressbook_SelectNames_addSectionWithLimit (corba_iface, "User", "User", 1, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Cannot add SelectNames section -- %s", BONOBO_EX_REPOID (&ev));
		goto err;
	}

	control = GNOME_Evolution_Addressbook_SelectNames_getEntryBySection (corba_iface, "User", &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Cannot get SelectNames section -- %s", BONOBO_EX_REPOID (&ev));
		goto err;
	}

	control_widget = bonobo_widget_new_control_from_objref (control, CORBA_OBJECT_NIL);
	gtk_container_add (GTK_CONTAINER (placeholder), control_widget);
	gtk_widget_show (control_widget);

	button = glade_xml_get_widget (glade_xml, "button-user");
	g_signal_connect (button, "clicked", G_CALLBACK (user_clicked), corba_iface);

	CORBA_exception_free (&ev);
	*iface_ret = corba_iface;
	return control_widget;

 err:
	bonobo_object_release_unref (corba_iface, NULL);
	CORBA_exception_free (&ev);
	return NULL;
}

static void
server_option_menu_item_activate_callback (GtkMenuItem *menu_item,
					   void *data)
{
	char **storage_name_return;

	storage_name_return = (char **) data;
	if (*storage_name_return != NULL)
		g_free (*storage_name_return);

	*storage_name_return = g_strdup ((const char *) g_object_get_data (G_OBJECT (menu_item), "storage_name"));
}

static void
folder_name_entry_changed_callback (GtkEditable *editable,
				    void *data)
{
	GtkDialog *dialog = GTK_DIALOG (data);
	const char *folder_name_text = gtk_entry_get_text (GTK_ENTRY (editable));

	if (*folder_name_text == '\0')
		gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, FALSE);
	else
		gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, TRUE);
}

static void
setup_server_option_menu (EStorageSetView *storage_set_view,
			  GladeXML *glade_xml,
			  char **storage_name_return)
{
	GList *storages;
	GList *p;
	GtkWidget *widget;
	GtkWidget *menu;

	widget = glade_xml_get_widget (glade_xml, "server-option-menu");
	g_return_if_fail (GTK_IS_OPTION_MENU (widget));

	menu = gtk_menu_new ();
	gtk_widget_show (menu);

	*storage_name_return = NULL;
	storages = e_storage_set_get_storage_list (e_storage_set_view_get_storage_set (storage_set_view));
	for (p = storages; p != NULL; p = p->next) {
		GtkWidget *menu_item;
		const char *storage_name;

		storage_name = e_storage_get_name (E_STORAGE (p->data));

		menu_item = gtk_menu_item_new_with_label (storage_name);
		g_signal_connect (menu_item, "activate",
				  G_CALLBACK (server_option_menu_item_activate_callback),
				  storage_name_return);
		g_object_set_data_full (G_OBJECT (menu_item), "storage_name", g_strdup (storage_name), g_free);

		gtk_widget_show (menu_item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

		if (*storage_name_return == NULL)
			*storage_name_return = g_strdup (storage_name);
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (widget), menu);

	/* FIXME: Default to the current storage in the shell view.  */
}

static gboolean
do_foreign_folder_dialog (EStorageSetView *storage_set_view,
			  char **user_email_address_return,
			  char **storage_name_return,
			  char **folder_name_return)
{
	GNOME_Evolution_Addressbook_SelectNames corba_iface;
	GladeXML *glade_xml;
	GtkWidget *dialog;
	GtkWidget *name_selector_widget;
	GtkWidget *folder_name_entry;
	char *user_email_address;
	int response;

	glade_xml = glade_xml_new (CONNECTOR_GLADEDIR "/e-foreign-folder-dialog.glade",
				   NULL, NULL);
	g_return_val_if_fail (glade_xml != NULL, FALSE);

	dialog = glade_xml_get_widget (glade_xml, "dialog");
	g_return_val_if_fail (dialog != NULL, FALSE);

	name_selector_widget = setup_name_selector (glade_xml, &corba_iface);
	if (name_selector_widget == NULL) {
		g_object_unref (glade_xml);
		return FALSE;
	}

	setup_server_option_menu (storage_set_view, glade_xml, storage_name_return);
	setup_folder_name_combo (glade_xml);

	folder_name_entry = glade_xml_get_widget (glade_xml, "folder-name-entry");

	/* Connect the callback to set the OK button insensitive when there is
	   no text in the folder_name_entry.  Notice that we put a value there
	   by default so the OK button is sensitive by default.  */
	g_signal_connect (folder_name_entry, "changed",
			  G_CALLBACK (folder_name_entry_changed_callback), dialog);

	while (TRUE) {
		response = gtk_dialog_run (GTK_DIALOG (dialog));
		if (response == GTK_RESPONSE_CANCEL) {
			g_free (*storage_name_return);
			*storage_name_return = NULL;
			gtk_widget_destroy (dialog);
			bonobo_object_release_unref (corba_iface, NULL);
			return FALSE;
		}

		bonobo_widget_get_property (BONOBO_WIDGET (name_selector_widget),
					    "addresses", TC_CORBA_string, &user_email_address,
					    NULL);

		if (user_email_address != NULL && *user_email_address != '\0')
			break;

		g_free (user_email_address);

		/* It would be nice to insensitivize the OK button appropriately
		   instead of doing this, but unfortunately we can't do this for the
		   Bonobo control.  */
		e_notice (dialog, GTK_MESSAGE_ERROR, _("Please select a user."));
	}

	*user_email_address_return = user_email_address;
	*folder_name_return = g_strdup (gtk_entry_get_text (GTK_ENTRY (folder_name_entry)));

	gtk_widget_destroy (dialog);
	bonobo_object_release_unref (corba_iface, NULL);
	return TRUE;
}


/* Discovery process.  */

static void storage_set_view_weak_notify (void *data, GObject *where_the_object_was);
static void storage_weak_notify (void *data, GObject *where_the_object_was);

struct _DiscoveryData {
	EStorageSetView *storage_set_view;
	GtkWidget *dialog;
	char *user_email_address;
	char *folder_name;
	EStorage *storage;
};
typedef struct _DiscoveryData DiscoveryData;

static void
cleanup_discovery (DiscoveryData *discovery_data)
{
	if (discovery_data->dialog != NULL)
		gtk_widget_destroy (discovery_data->dialog);

	if (discovery_data->storage_set_view != NULL)
		g_object_weak_unref (G_OBJECT (discovery_data->storage_set_view), storage_set_view_weak_notify, discovery_data);

	if (discovery_data->storage != NULL)
		g_object_weak_unref (G_OBJECT (discovery_data->storage), storage_weak_notify, discovery_data);

	g_free (discovery_data->user_email_address);
	g_free (discovery_data->folder_name);
	g_object_unref (discovery_data->storage);
	g_free (discovery_data);
}

static int
progress_bar_timeout_callback (void *data)
{
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (data));

	return TRUE;
}

static void
progress_bar_weak_notify (void *data,
			  GObject *where_the_object_was)
{
	int timeout_id;

	timeout_id = GPOINTER_TO_INT (data);
	g_source_remove (timeout_id);
}

/* This is invoked if the "Cancel" button is clicked.  */
static void
progress_dialog_response_callback (GtkDialog *dialog,
				   int response,
				   void *data)
{
	DiscoveryData *discovery_data;

	discovery_data = (DiscoveryData *) data;

	e_storage_cancel_discover_shared_folder (discovery_data->storage,
						 discovery_data->user_email_address,
						 discovery_data->folder_name);

	cleanup_discovery (discovery_data);
}

static int
progress_dialog_show_timeout_callback (void *data)
{
	GtkWidget *dialog;

	dialog = GTK_WIDGET (data);
	gtk_widget_show_all (dialog);
	return FALSE;
}

static GtkWidget *
create_progress_dialog (EStorage *storage,
			const char *user_email_address,
			const char *folder_name)
{
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *progress_bar;
	int timeout_id;
	char *text;

	dialog = gtk_dialog_new_with_buttons (_("Opening Folder"), NULL, 0,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      NULL);
	gtk_widget_set_size_request (dialog, 300, -1);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	text = g_strdup_printf (_("Opening Folder \"%s\""), folder_name);
	label = gtk_label_new (text);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, TRUE, 0);
	g_free (text);

	text = g_strdup_printf (_("in \"%s\" ..."), e_storage_get_name (storage));
	label = gtk_label_new (text);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, TRUE, 0);
	g_free (text);

	progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), progress_bar, FALSE, TRUE, 0);

	timeout_id = g_timeout_add (50, progress_bar_timeout_callback, progress_bar);
	g_object_weak_ref (G_OBJECT (progress_bar), progress_bar_weak_notify, GINT_TO_POINTER (timeout_id));

	timeout_id = g_timeout_add (PROGRESS_DIALOG_DELAY, progress_dialog_show_timeout_callback, dialog);
	g_object_weak_ref (G_OBJECT (progress_bar), progress_bar_weak_notify, GINT_TO_POINTER (timeout_id));

	return dialog;
}

static void
storage_set_view_weak_notify (void *data,
			      GObject *where_the_object_was)
{
	DiscoveryData *discovery_data;

	discovery_data = (DiscoveryData *) data;
	discovery_data->storage_set_view = NULL;

	cleanup_discovery (discovery_data);
}

static void
storage_weak_notify (void *data,
		     GObject *where_the_object_was)
{
	DiscoveryData *discovery_data;

	discovery_data = (DiscoveryData *) data;
	discovery_data->storage = NULL;

	cleanup_discovery (discovery_data);

	/* FIXME: Should we signal the user when this happens?  I.e. when the
	   storage dies for some reason before the folder is discovered.  */
}

static void
foreign_folder_discovery_callback (EStorage *storage,
				   EStorageResult result,
				   const char *path,
				   void *data)
{
	DiscoveryData *discovery_data;
	EStorageSetView *storage_set_view;

	discovery_data = (DiscoveryData *) data;
	storage_set_view = discovery_data->storage_set_view;

	/* Make sure the progress dialog doesn't show up now. */
	cleanup_discovery (discovery_data);

	if (result == E_STORAGE_OK && storage_set_view) {
		char *uri;

		uri = g_strconcat ("/", e_storage_get_name (storage), path, NULL);
		e_storage_set_view_set_current_folder (storage_set_view, uri);
		g_free (uri);
	} else {
		e_notice (storage_set_view, GTK_MESSAGE_ERROR,
			  _("Could not open other user's folder: %s."),
			  e_storage_result_to_string (result));
	}
}

static void
discover_folder (EStorageSetView *storage_set_view,
		 const char *user_email_address,
		 const char *storage_name,
		 const char *folder_name)
{
	EStorageSet *storage_set;
	EStorage *storage;
	GtkWidget *dialog;
	DiscoveryData *discovery_data;

	storage_set = e_storage_set_view_get_storage_set (storage_set_view);
	if (storage_set == NULL)
		goto error;

	storage = e_storage_set_get_storage (storage_set, storage_name);
	if (storage == NULL)
		goto error;

	dialog = create_progress_dialog (storage, user_email_address, folder_name);

	discovery_data = g_new (DiscoveryData, 1);
	discovery_data->dialog             = dialog;
	discovery_data->storage_set_view   = storage_set_view;
	discovery_data->user_email_address = g_strdup (user_email_address);
	discovery_data->folder_name        = g_strdup (folder_name);
	discovery_data->storage            = storage;
	g_object_ref (storage);

	g_object_weak_ref (G_OBJECT (storage_set_view), storage_set_view_weak_notify, discovery_data);
	g_object_weak_ref (G_OBJECT (storage), storage_weak_notify, discovery_data);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (progress_dialog_response_callback), discovery_data);

	e_storage_async_discover_shared_folder (storage,
						user_email_address,
						folder_name,
						foreign_folder_discovery_callback,
						discovery_data);
	return;

 error:
	/* FIXME: Be more verbose?  */
	e_notice (storage_set_view, GTK_MESSAGE_ERROR,
		  _("Cannot find the specified other user's folder."));
}

void
e_folder_add_foreign_dialog (EStorageSetView *storage_set_view)
{
	char *user_email_address = NULL;
	char *storage_name = NULL;
	char *folder_name = NULL;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	if (!do_foreign_folder_dialog (storage_set_view, &user_email_address, &storage_name, &folder_name))
		return;

	discover_folder (storage_set_view, user_email_address, storage_name, folder_name);

	g_free (user_email_address);
	g_free (storage_name);
	g_free (folder_name);
}


static void
remove_foreign_cb (EStorageSet *storage_set,
		   EStorageResult result,
		   gpointer user_data)
{
	EFolderDeleteData *dd = user_data;

	if (result != E_STORAGE_OK) {
		e_notice (dd->storage_set_view, GTK_MESSAGE_ERROR,
			  _("Cannot unsubscribe from folder:\n%s"),
			  e_storage_result_to_string (result));
	}

	g_object_unref (dd->storage_set_view);
	g_free (dd->folder_path);
	g_free (dd);
}

static void
remove_foreign_dialog_response (GtkDialog *dialog, int response, gpointer data)
{
	EFolderDeleteData *dd = data;

	if (response == GTK_RESPONSE_OK) {
		EStorageSet *storage_set =
			e_storage_set_view_get_storage_set (dd->storage_set_view);
		e_storage_set_async_remove_shared_folder (storage_set,
							  dd->folder_path,
							  remove_foreign_cb, dd);
	} else {
		/* Silently free dd */
		remove_foreign_cb (NULL, E_STORAGE_OK, dd);
	}

	if (response != GTK_RESPONSE_DELETE_EVENT)
		gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
e_folder_remove_foreign_dialog (EStorageSetView *storage_set_view,
				EFolder     	*folder,
				const char  	*folder_path)
{
	GtkWidget *dialog;
	EFolderDeleteData *dd;
	const char *folder_name;
	char *title;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (E_IS_FOLDER (folder));
	g_return_if_fail (folder_path != NULL);

	dd = g_new0 (EFolderDeleteData, 1);
	dd->storage_set_view = g_object_ref (storage_set_view);
	dd->folder_path = g_strdup (folder_path);

	folder_name = e_folder_get_name (folder);

	dialog = gtk_message_dialog_new (NULL,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 _("Really unsubscribe from folder \"%s\"?"),
					 folder_name);
	e_dialog_set_transient_for (GTK_WINDOW (dialog),
				    GTK_WIDGET (storage_set_view));

	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_REMOVE, GTK_RESPONSE_OK);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
	
	gtk_container_set_border_width (GTK_CONTAINER (dialog), 6); 
	
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 6);

	title = g_strdup_printf (_("Unsubscribe from \"%s\""), folder_name);
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_free (title);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (remove_foreign_dialog_response),
			  dd);
	gtk_widget_show (dialog);
}
