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
#include "exchange-account.h"
#include "e2k-propnames.h"
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
	E2kContext *ctx;
	E2kHTTPStatus status;
	char *url, *body, *p, *checked, *ta_start, *ta_end;
	int len;

	ctx = exchange_account_get_context (account);
	if (!ctx)
		return FALSE;

	if (!message) {
		/* Do this the easy way */

		const char *prop = E2K_PR_EXCHANGE_OOF_STATE;
		E2kResult *results;
		int nresults;

		url = e2k_uri_concat (account->home_uri, "NON_IPM_SUBTREE/");
		status = e2k_context_propfind (ctx, NULL, url, &prop, 1,
					       &results, &nresults);
		g_free (url);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status) || nresults == 0)
			return FALSE;

		prop = e2k_properties_get_prop (results[0].props, E2K_PR_EXCHANGE_OOF_STATE);
		*oof = prop && atoi (prop);

		e2k_results_free (results, nresults);
		return TRUE;
	}

	url = e2k_uri_concat (account->home_uri, "?Cmd=options");
	status = e2k_context_get_owa (ctx, NULL, url, FALSE, &body, &len);
	g_free (url);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
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
	E2kContext *ctx;
	E2kHTTPStatus status;

	ctx = exchange_account_get_context (account);
	if (!ctx)
		return FALSE;

	if (message) {
		char *body, *message_enc;

		message_enc = e2k_uri_encode (message, NULL);
		body = g_strdup_printf ("Cmd=options&OofState=%d&"
					"OofReply=%s",
					oof ? 1 : 0, message_enc);
		status = e2k_context_post (ctx, NULL, account->home_uri,
					   "application/x-www-form-urlencoded",
					   body, strlen (body), NULL, NULL);
		g_free (message_enc);
		g_free (body);
	} else {
		E2kProperties *props;
		char *url;

		props = e2k_properties_new ();
		e2k_properties_set_bool (props, E2K_PR_EXCHANGE_OOF_STATE, oof);
		url = e2k_uri_concat (account->home_uri, "NON_IPM_SUBTREE/");
		/* Need to pass TRUE for "create" here or it won't work */
		status = e2k_context_proppatch (ctx, NULL, url, props,
						TRUE, NULL);
		g_free (url);
		e2k_properties_free (props);
	}

	return E2K_HTTP_STATUS_IS_SUCCESSFUL (status) ||
		E2K_HTTP_STATUS_IS_REDIRECTION (status);
}

struct _exchangeOOF_Data {
	ExchangeAccount *account;
	GladeXML *xml;
	gboolean state;
	char *message;
};


static void
toggled_state (GtkToggleButton *button, gpointer user_data)
{
	struct _exchangeOOF_Data *data = (struct _exchangeOOF_Data *)user_data;
	gboolean state = gtk_toggle_button_get_active (button);
	GtkWidget *textview;

	g_return_if_fail (state != data->state);

	data->state = state;
	textview = glade_xml_get_widget (data->xml, "oof_message");
	gtk_widget_set_sensitive (textview, state);
}

static void
update_state (gpointer user_data)
{
	struct _exchangeOOF_Data *data = (struct _exchangeOOF_Data *) user_data;
	GtkWidget *textview;
	GtkTextBuffer *buffer;
	char *message;

	textview = glade_xml_get_widget (data->xml, "oof_message");
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	if (gtk_text_buffer_get_modified (buffer)) {
		GtkTextIter start, end;

		g_free (data->message);
		gtk_text_buffer_get_bounds (buffer, &start, &end);
		data->message = message =
			gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
		gtk_text_buffer_set_modified (buffer, FALSE);
	} else
		message = NULL;

	if (!exchange_oof_set (data->account, data->state, message)) {
		e_notice (textview, GTK_MESSAGE_ERROR,
			  _("Could not update out-of-office state"));
	}
}

void
exchange_oof (ExchangeAccount *account, GtkWidget *parent)
{
	GladeXML *xml;
	GtkWidget *dialog;
	GtkResponseType response;
	gboolean oof;
	GtkWidget *radio, *textview;
	char *message;
	GtkTextBuffer *buffer;
	struct _exchangeOOF_Data *oof_data;

	if (!exchange_oof_get (account, &oof, &message)) {
		e_notice (parent, GTK_MESSAGE_ERROR,
			  _("Could not read out-of-office state"));
		return;
	}

	xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-oof.glade",
			     "Out of Office Assistant", NULL);
	g_return_if_fail (xml != NULL);

	dialog = glade_xml_get_widget (xml, "Out of Office Assistant");
	g_return_if_fail (dialog != NULL);
	e_dialog_set_transient_for (GTK_WINDOW (dialog), parent);

	/* Set up data */
	radio = glade_xml_get_widget (xml,
				      oof ? "oof_yes_radio" : "oof_no_radio");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
	
	oof_data = g_new0 (struct _exchangeOOF_Data, 1);
	oof_data->account = account;
	oof_data->xml = xml;
	oof_data->state = oof;
	oof_data->message = message;

	radio = glade_xml_get_widget (xml, "oof_yes_radio");
	g_signal_connect (radio, "toggled", G_CALLBACK (toggled_state), oof_data);
	
	textview = glade_xml_get_widget (xml, "oof_message");

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_set_text (buffer, message, strlen (message));
	gtk_text_buffer_set_modified (buffer, FALSE);
	if (!oof_data->state)
		gtk_widget_set_sensitive (textview, FALSE);	
	
	response = gtk_dialog_run (GTK_DIALOG (dialog));

	if (response == GTK_RESPONSE_OK)
		update_state (oof_data);
	
	gtk_widget_destroy (dialog);
	g_object_unref (xml);
}
