/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
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

/* exchange-change-password: Change Password code */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-change-password.h"
#include "exchange-account.h"
#include "e2k-utils.h"

#define FILENAME CONNECTOR_GLADEDIR "/exchange-change-password.glade"
#define ROOTNODE "pass_dialog"
#define STARTNODE "pass_vbox"
#define IS_EMPTY(x) (!(x) || !strcmp ((x), ""))

static void
check_response (GtkWidget *w, gpointer data)
{
	gtk_dialog_response (GTK_DIALOG(data), GTK_RESPONSE_DELETE_EVENT);
	return;
}

static void
check_pass_cb (GtkWidget *w, gpointer data)
{
        GladeXML *main_app;
        GtkEntry *cur_entry;
        GtkEntry *new_entry;
        GtkEntry *confirm_entry;
        const char *cur_pass, *new_pass, *confirm_pass;
	struct password_data *pdata;
	char *existing_password;
	GtkDialog *dialog;
	E2kAutoconfig *ac;
	GtkLabel *top_label;

	pdata = (struct password_data *) data;

	existing_password = (char *) pdata->existing_password;
	main_app = (GladeXML *) pdata->xml;
	dialog = (GtkDialog *) pdata->dialog;
	ac = (E2kAutoconfig *) pdata->ac;

        cur_entry = GTK_ENTRY (glade_xml_get_widget (main_app, "confirm_pass_entry"));
        new_entry = GTK_ENTRY (glade_xml_get_widget (main_app, "new_pass_entry"));
        confirm_entry = GTK_ENTRY (glade_xml_get_widget (main_app, "confirm_pass_entry"));
	top_label = GTK_LABEL (glade_xml_get_widget (main_app, "pass_label"));

	cur_pass = gtk_entry_get_text (cur_entry);
        new_pass = gtk_entry_get_text (new_entry);
        confirm_pass = gtk_entry_get_text (confirm_entry);

	if (IS_EMPTY (cur_pass) || IS_EMPTY (new_pass) 
		|| IS_EMPTY (confirm_pass)) {
		gtk_dialog_response (dialog, GTK_RESPONSE_REJECT);
		return;
	}

	if (existing_password) {
		if (strcmp (cur_pass, existing_password) != 0) {
			/* User entered a wrong existing password. Prompt him again. */
			gtk_label_set_text (top_label, "The current password does not match the existing password for your account. Please enter the correct password");
			gtk_dialog_response (dialog, GTK_RESPONSE_REJECT);
			return;
		}

		if (strcmp (new_pass, confirm_pass) == 0) {
			g_message ("Password confirmed\n");
			/* e2k_autoconfig_set_password crashes if existing_password is NULL */
			e2k_autoconfig_set_password (ac, new_pass);
			gtk_dialog_response (dialog, GTK_RESPONSE_DELETE_EVENT);
			return;
		}
		else {
			/* The password does not confirm to the new password */
			gtk_label_set_text (top_label, "The two passwords do not match. Please re-enter the passwords.");
			gtk_dialog_response (dialog, GTK_RESPONSE_CANCEL);
			return;
		}
	}

}

/* @voluntary : 1 , the user wants to change his password by clicking
 * 		the menu option
 *		0 , connector has found that the password has expired
 */
void 
exchange_change_password (char *password, E2kAutoconfig *ac, int voluntary)
{
	GtkWidget *top_widget;
	GtkEntry *password_entry;
	GtkButton *ok_button;
	GtkButton *cancel_button;
	GtkResponseType response;
	GladeXML *xml;
	struct password_data pdata;
	GtkLabel *top_label;
	
	xml = glade_xml_new (FILENAME, ROOTNODE, NULL);
	top_widget = glade_xml_get_widget (xml, ROOTNODE);

	password_entry = GTK_ENTRY (glade_xml_get_widget (xml, "current_pass_entry"));
	gtk_entry_set_visibility (password_entry, FALSE);

	password_entry = GTK_ENTRY (glade_xml_get_widget (xml, "new_pass_entry"));
	gtk_entry_set_visibility (password_entry, FALSE);

	password_entry = GTK_ENTRY (glade_xml_get_widget (xml, "confirm_pass_entry"));
	gtk_entry_set_visibility (password_entry, FALSE);

	ok_button = GTK_BUTTON (glade_xml_get_widget (xml, "okbutton1")); 
	cancel_button = GTK_BUTTON (glade_xml_get_widget (xml, "cancelbutton1")); 

	top_label = GTK_LABEL (glade_xml_get_widget (xml, "pass_label"));

	if (voluntary)
		gtk_label_set_text (top_label, "");

	pdata.xml = xml;
	pdata.existing_password = (char *) password;
	pdata.dialog = GTK_DIALOG(top_widget);

	g_signal_connect (ok_button, "clicked", G_CALLBACK (check_pass_cb), &pdata );
	g_signal_connect (cancel_button, "clicked", G_CALLBACK (check_response), GTK_WIDGET(top_widget));

run_dialog_again:	
	response = gtk_dialog_run (GTK_DIALOG(top_widget));
	if (response == GTK_RESPONSE_REJECT) {	
		/* Popup wrong existing password warning */
		goto run_dialog_again;
	}
	if (response == GTK_RESPONSE_CANCEL) {
		/* Popup that both passwords do not match */
		goto run_dialog_again;
	}
	if (response == GTK_RESPONSE_DELETE_EVENT)
		gtk_widget_destroy (top_widget);
	g_object_unref (xml);

}
