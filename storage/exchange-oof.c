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

/* exchange-oof: Out of Office code */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-oof.h"
#include "exchange-component.h"
#include "exchange-account.h"
#include "e2k-utils.h"
#include "e2k-uri.h"

#include <string.h>
#include <e-util/e-dialog-utils.h>
#include <gal/util/e-util.h>
#include <glade/glade-xml.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktextbuffer.h>
#include <gtk/gtktextview.h>

#define PARENT_TYPE EVOLUTION_TYPE_CONFIG_CONTROL
static EvolutionConfigControlClass *parent_class = NULL;

struct _ExchangeOOFControlPrivate {
	ExchangeAccount *account;
	GladeXML *xml;

	gboolean state;
	char *message;
};

static void dispose (GObject *);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
}

static void
init (GObject *object)
{
	ExchangeOOFControl *control =
		EXCHANGE_OOF_CONTROL (object);

	control->priv = g_new0 (ExchangeOOFControlPrivate, 1);
}

static void
dispose (GObject *object)
{
	ExchangeOOFControl *control =
		EXCHANGE_OOF_CONTROL (object);

	if (control->priv->xml) {
		g_object_unref (control->priv->xml);
		control->priv->xml = NULL;
	}

	if (control->priv->account) {
		g_object_unref (control->priv->account);
		control->priv->account = NULL;
	}

	if (control->priv->message) {
		g_free (control->priv->message);
		control->priv->message = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}


E2K_MAKE_TYPE (exchange_oof_control, ExchangeOOFControl, class_init, init, PARENT_TYPE)

static void
control_apply_cb (EvolutionConfigControl *config_control, gpointer user_data)
{
	ExchangeOOFControl *control =
		EXCHANGE_OOF_CONTROL (config_control);
	GtkWidget *textview;
	GtkTextBuffer *buffer;
	char *message;

	textview = glade_xml_get_widget (control->priv->xml, "oof_message");
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	if (gtk_text_buffer_get_modified (buffer)) {
		GtkTextIter start, end;

		g_free (control->priv->message);
		gtk_text_buffer_get_bounds (buffer, &start, &end);
		control->priv->message = message =
			gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
		gtk_text_buffer_set_modified (buffer, FALSE);
	} else
		message = NULL;

	exchange_oof_set (control->priv->account, control->priv->state, message);
}

static void
text_changed (GtkTextBuffer *buffer, gpointer control)
{
	evolution_config_control_changed (control);
}

static void
state_toggled (GtkToggleButton *button, gpointer user_data)
{
	ExchangeOOFControl *control = user_data;
	gboolean state = gtk_toggle_button_get_active (button);
	GtkWidget *textview;

	g_return_if_fail (state != control->priv->state);

	control->priv->state = state;
	evolution_config_control_changed (EVOLUTION_CONFIG_CONTROL (control));

	textview = glade_xml_get_widget (control->priv->xml, "oof_message");
	gtk_widget_set_sensitive (textview, state);
}

static BonoboObject *
dummy_control (const char *message)
{
	GtkWidget *label;

	label = gtk_label_new (message);
	gtk_widget_show (label);

	return BONOBO_OBJECT (evolution_config_control_new (label));
}

BonoboObject *
exchange_oof_control_new (void)
{
	ExchangeOOFControl *control;
	ExchangeAccount *account;
	GtkWidget *notebook, *radio, *textview;
	GtkTextBuffer *buffer;

	account = exchange_component_get_account_for_uri (NULL);
	if (!account)
		return dummy_control (_("No Exchange accounts configured."));

	control = g_object_new (EXCHANGE_TYPE_OOF_CONTROL, NULL);
	control->priv->account = account;
	g_object_ref (account);

	if (!exchange_oof_get (account, &control->priv->state, &control->priv->message)) {
		bonobo_object_unref (control);
		return dummy_control (_("Could not read out-of-office state."));
	}

	/* The gui */
	control->priv->xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-oof.glade",
					    "oof_vbox", NULL);
	if (!control->priv->xml) {
		bonobo_object_unref (control);
		return dummy_control (_("Unable to load out-of-office UI."));
	}

	/* Put the (already parentless) glade widgets into the control */
	notebook = glade_xml_get_widget (control->priv->xml, "oof_vbox");
	evolution_config_control_construct (EVOLUTION_CONFIG_CONTROL (control),
					    notebook);

	/* Set up data */
	radio = glade_xml_get_widget (control->priv->xml,
				      control->priv->state ? "oof_yes_radio" : "oof_no_radio");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);

	radio = glade_xml_get_widget (control->priv->xml, "oof_yes_radio");
	g_signal_connect (radio, "toggled", G_CALLBACK (state_toggled), control);

	textview = glade_xml_get_widget (control->priv->xml, "oof_message");
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_set_text (buffer, control->priv->message,
				  strlen (control->priv->message));
	gtk_text_buffer_set_modified (buffer, FALSE);
	g_signal_connect (buffer, "changed", G_CALLBACK (text_changed), control);

	/* Set up config control signals */
	g_signal_connect (control, "apply", G_CALLBACK (control_apply_cb), NULL);

	return BONOBO_OBJECT (control);
}


static void
account_connected (ExchangeAccount *account, gpointer shell_view_xid)
{
	exchange_oof_init (account, (GdkNativeWindow)shell_view_xid);
}

/**
 * exchange_oof_init:
 * @account: an #ExchangeAccount
 * @shell_view_xid: window to use as parent if a dialog is created
 *
 * Checks @account's Out-of-Office state (if it is connected), or
 * waits for it to become connected and then checks its state. If
 * OOF is enabled, asks the user whether or not he would like to
 * turn it off.
 **/
void
exchange_oof_init (ExchangeAccount *account,
		   GdkNativeWindow shell_view_xid)
{
	GladeXML *xml;
	GtkWidget *dialog;
	GtkResponseType response;
	gboolean oof;

	if (!exchange_oof_get (account, &oof, NULL)) {
		g_signal_connect (account, "connected",
				  G_CALLBACK (account_connected),
				  (gpointer)shell_view_xid);
		return;
	}
	if (!oof)
		return;

	xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-oof.glade",
			     "oof_dialog", NULL);
	g_return_if_fail (xml != NULL);

	dialog = glade_xml_get_widget (xml, "oof_dialog");
	g_return_if_fail (dialog != NULL);
	e_dialog_set_transient_for_xid (GTK_WINDOW (dialog), shell_view_xid);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_object_unref (xml);

	if (response == GTK_RESPONSE_YES) {
		if (!exchange_oof_set (account, FALSE, NULL)) {
			e_notice_with_xid (shell_view_xid, GTK_MESSAGE_ERROR,
					   _("Could not update out-of-office state"));
		}
	}
}

/**
 * exchange_oof_get:
 * @account: an #ExchangeAccount
 * @oof: pointer to variable to pass back OOF state in
 * @message: pointer to variable to pass back OOF message in
 *
 * Checks if Out-of-Office is enabled for @account and returns the
 * state in *@oof and the message in *@message (which the caller
 * must free).
 *
 * Return value: %TRUE if the OOF state was read, %FALSE if an error
 * occurred.
 **/
gboolean
exchange_oof_get (ExchangeAccount *account, gboolean *oof, char **message)
{
	E2kConnection *conn;
	char *url, *body, *p, *checked, *ta_start, *ta_end;
	int len, status;

	conn = exchange_account_get_connection (account);
	if (!conn)
		return FALSE;

	url = e2k_uri_concat (account->home_uri, "?Cmd=options");
	status = e2k_connection_get_owa_sync (conn, url, FALSE, &body, &len);
	g_free (url);
	if (!SOUP_ERROR_IS_SUCCESSFUL (status))
		return FALSE;

	p = e_strstrcase (body, "<!--End OOF Assist-->");
	if (p)
		*p = '\0';
	else
		body[len - 1] = '\0';

	p = e_strstrcase (body, "name=\"OofState\"");
	if (p)
		p = e_strstrcase (body, "value=\"1\"");
	if (!p) {
		g_warning ("Could not find OofState in options page");
		g_free (body);
		return FALSE;
	}

	checked = e_strstrcase (p, "checked");
	*oof = (checked && checked < strchr (p, '>'));

	if (message) {
		ta_end = e_strstrcase (p, "</textarea>");
		if (!ta_end) {
			g_warning ("Could not find OOF text in options page");
			g_free (body);
			*message = g_strdup ("");
			return TRUE;
		}
		for (ta_start = ta_end - 1; ta_start > p; ta_start--) {
			if (*ta_start == '>')
				break;
		}
		if (*ta_start++ != '>') {
			g_warning ("Could not find OOF text in options page");
			g_free (body);
			*message = g_strdup ("");
			return TRUE;
		}

		*message = g_strndup (ta_start, ta_end - ta_start);
		/* FIXME: HTML decode */
	}

	return TRUE;
}

/**
 * exchange_oof_set:
 * @account: an #ExchangeAccount
 * @oof: new OOF state
 * @message: new OOF message, or %NULL
 *
 * Sets the OOF state for @account to @oof, and optionally updates
 * the OOF message.
 *
 * Return value: %TRUE if the OOF state was updated, %FALSE if an
 * error occurred.
 **/
gboolean
exchange_oof_set (ExchangeAccount *account, gboolean oof, const char *message)
{
	E2kConnection *conn;
	char *body;
	int status;

	conn = exchange_account_get_connection (account);
	if (!conn)
		return FALSE;

	if (message) {
		char *message_enc = e2k_uri_encode (message, FALSE, NULL);
		body = g_strdup_printf ("Cmd=options&OofState=%d&"
					"OofReply=%s",
					oof ? 1 : 0, message_enc);
		g_free (message_enc);
	} else {
		body = g_strdup_printf ("Cmd=options&OofState=%d",
					oof ? 1 : 0);
	}

	status = e2k_connection_post_sync (conn, account->home_uri,
					   "application/x-www-form-urlencoded",
					   body, strlen (body));
	g_free (body);
	return SOUP_ERROR_IS_SUCCESSFUL (status) ||
		SOUP_ERROR_IS_REDIRECTION (status);
}
