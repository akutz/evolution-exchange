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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e2k-user-dialog.h"
#include "e2k-types.h"

#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-widget.h>
#include <e-util/e-gtk-utils.h>
#include <e-util/e-dialog-utils.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>
#include "Evolution-Addressbook-SelectNames.h"
#include "e2k-xml-utils.h"

struct _E2kUserDialogPrivate {
	char *section_name;
        GNOME_Evolution_Addressbook_SelectNames corba_select_names;
	GtkWidget *entry, *parent_window;
};

#define SELECT_NAMES_OAFIID "OAFIID:GNOME_Evolution_Addressbook_SelectNames"

#define PARENT_TYPE GTK_TYPE_DIALOG
static GtkDialogClass *parent_class;

static void parent_window_destroyed (gpointer dialog, GObject *where_parent_window_was);

static void
finalize (GObject *object)
{
	E2kUserDialog *dialog = E2K_USER_DIALOG (object);

	g_free (dialog->priv->section_name);
	g_free (dialog->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
	E2kUserDialog *dialog = E2K_USER_DIALOG (object);

	if (dialog->priv->corba_select_names != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		bonobo_object_release_unref (dialog->priv->corba_select_names, &ev);
		CORBA_exception_free (&ev);

		dialog->priv->corba_select_names = CORBA_OBJECT_NIL;
	}

	if (dialog->priv->parent_window) {
		g_object_weak_unref (G_OBJECT (dialog->priv->parent_window),
				     parent_window_destroyed, dialog);
		dialog->priv->parent_window = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
class_init (E2kUserDialogClass *class)
{
	GObjectClass *object_class = (GObjectClass *) class;

	parent_class = g_type_class_ref (GTK_TYPE_DIALOG);

	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void
init (E2kUserDialog *dialog)
{
	dialog->priv = g_new0 (E2kUserDialogPrivate, 1);

	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 6);
}

E2K_MAKE_TYPE (e2k_user_dialog, E2kUserDialog, class_init, init, PARENT_TYPE)



static void
parent_window_destroyed (gpointer user_data, GObject *where_parent_window_was)
{
	E2kUserDialog *dialog = user_data;

	dialog->priv->parent_window = NULL;
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
}

static void
addressbook_clicked_cb (GtkWidget *widget, gpointer data)
{
	E2kUserDialog *dialog = data;
	E2kUserDialogPrivate *priv;
	CORBA_Environment ev;

	priv = dialog->priv;

	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_SelectNames_activateDialog (
		priv->corba_select_names, priv->section_name, &ev);

	CORBA_exception_free (&ev);
}

static gboolean
e2k_user_dialog_construct (E2kUserDialog *dialog,
			   GtkWidget *parent_window,
			   const char *label_text,
			   const char *section_name)
{
	E2kUserDialogPrivate *priv;
	Bonobo_Control corba_control;
	CORBA_Environment ev;
	GtkWidget *hbox, *vbox, *label, *button;

	gtk_window_set_title (GTK_WINDOW (dialog), _("Select User"));
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK,
				NULL);

	e_dialog_set_transient_for (GTK_WINDOW (dialog), parent_window);

	priv = dialog->priv;
	priv->section_name = g_strdup (section_name);

	priv->parent_window = parent_window;
	g_object_weak_ref (G_OBJECT (parent_window),
			   parent_window_destroyed, dialog);

	/* Set up the actual select names bits */
	CORBA_exception_init (&ev);

	priv->corba_select_names =
		bonobo_activation_activate_from_id (SELECT_NAMES_OAFIID, 0, NULL, &ev);
	GNOME_Evolution_Addressbook_SelectNames_addSectionWithLimit (
		priv->corba_select_names, section_name,
		section_name, 1, &ev);

	if (BONOBO_EX (&ev)) {
		g_message ("e2k_user_dialog_construct(): Unable to add section!");
		return FALSE;
	}

	corba_control = GNOME_Evolution_Addressbook_SelectNames_getEntryBySection (
		priv->corba_select_names, section_name, &ev);

	if (BONOBO_EX (&ev)) {
		g_message ("e2k_user_dialog_construct(): Unable to get addressbook entry!");
		return FALSE;
	}

	CORBA_exception_free (&ev);

	hbox = gtk_hbox_new (FALSE, 6);

	label = gtk_label_new (label_text);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 6);

	/* The vbox is a workaround for bug 43315 */
	vbox = gtk_vbox_new (FALSE, 0);
	priv->entry = bonobo_widget_new_control_from_objref (corba_control, CORBA_OBJECT_NIL);
	gtk_box_pack_start (GTK_BOX (vbox), priv->entry, TRUE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 6);

	button = gtk_button_new_with_label (_("Addressbook..."));
	g_signal_connect (button, "clicked",
			  G_CALLBACK (addressbook_clicked_cb),
			  dialog);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 6);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox,
			    TRUE, TRUE, 6);
	gtk_widget_show_all (hbox);

	return TRUE;
}

/**
 * e2k_user_dialog_new:
 * @parent_window: The window invoking the dialog.
 * @label_text: Text to label the entry in the initial dialog with
 * @section_name: The section name for the select-names dialog
 *
 * Creates a new user selection dialog.
 *
 * Return value: A newly-created user selection dialog, or %NULL if
 * the dialog could not be created.
 **/
GtkWidget *
e2k_user_dialog_new (GtkWidget *parent_window,
		     const char *label_text, const char *section_name)
{
	E2kUserDialog *dialog;

	g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);
	g_return_val_if_fail (label_text != NULL, NULL);
	g_return_val_if_fail (section_name != NULL, NULL);

	dialog = g_object_new (E2K_TYPE_USER_DIALOG, NULL);
	if (!e2k_user_dialog_construct (dialog, parent_window,
					label_text, section_name)) {
		g_object_unref (dialog);
		return NULL;
	}

	return GTK_WIDGET (dialog);
}

/**
 * e2k_user_dialog_get_user:
 * @dialog: the dialog
 *
 * Gets the email address of the selected user from the dialog.
 *
 * Return value: the email address, which must be freed with g_free().
 **/
char *
e2k_user_dialog_get_user (E2kUserDialog *dialog)
{
	E2kUserDialogPrivate *priv;
	char *dests, *addr;
	xmlDoc *doc;
	xmlNode *node;

	g_return_val_if_fail (E2K_IS_USER_DIALOG (dialog), NULL);

	priv = dialog->priv;

	dests = NULL;
	bonobo_widget_get_property (BONOBO_WIDGET (priv->entry),
				    "destinations", TC_CORBA_string, &dests,
				    NULL);
	if (!dests)
		return NULL;
	doc = e2k_parse_xml (dests, -1);
	g_free (dests);
	if (!doc)
		return NULL;
	if (!doc->xmlRootNode) {
		xmlFreeDoc (doc);
		return NULL;
	}

	node = doc->xmlRootNode;
	while (node->xmlChildrenNode && strcmp (node->name, "destination") != 0)
		node = node->xmlChildrenNode;
	if (!node->xmlChildrenNode) {
		xmlFreeDoc (doc);
		return NULL;
	}
	node = node->xmlChildrenNode;
	while (node && strcmp (node->name, "email") != 0)
		node = node->next;
	if (!node || !node->xmlChildrenNode || !node->xmlChildrenNode->content) {
		xmlFreeDoc (doc);
		return NULL;
	}

	addr = g_strdup (node->xmlChildrenNode->content);
	xmlFreeDoc (doc);
	return addr;
}
