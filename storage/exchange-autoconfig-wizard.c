/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

/* exchange-autoconfig-wizard: Automatic account configuration wizard */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e2k-autoconfig.h"
#include "e2k-utils.h"
#include "exchange-autoconfig-wizard.h"

#include <e-util/e-account.h>
#include <e-util/e-account-list.h>
#include <e-util/e-dialog-utils.h>
#include <libedataserverui/e-passwords.h>

#include <gconf/gconf-client.h>
#include <glade/glade-xml.h>
#include <gtk/gtkentry.h>
#include <libgnomeui/gnome-druid.h>
#include <libgnomeui/gnome-druid-page-standard.h>
#include <libgnomeui/gnome-href.h>

typedef struct {
	GnomeDruid *druid;

	GladeXML *xml;
	E2kAutoconfig *ac;
	E2kOperation op;
	GPtrArray *pages;

	GtkWindow *window;

	/* OWA Page */
	GtkEntry *owa_uri_entry;
	GtkEntry *username_entry;
	GtkEntry *password_entry;
	GtkToggleButton *remember_password_check;

	/* Global Catalog Page */
	GtkEntry *gc_server_entry;

	/* Failure Page */
	GtkBox *failure_vbox;
	GtkLabel *failure_label;
	GtkWidget *failure_href;

	/* Verify Page */
	GtkEntry *name_entry;
	GtkEntry *email_entry;
	GtkToggleButton *default_account_check;
} ExchangeAutoconfigGUI;

enum {
	EXCHANGE_AUTOCONFIG_PAGE_OWA,
	EXCHANGE_AUTOCONFIG_PAGE_GC,
	EXCHANGE_AUTOCONFIG_PAGE_FAILURE,
	EXCHANGE_AUTOCONFIG_PAGE_VERIFY
};

static void owa_page_changed (GtkEntry *entry, ExchangeAutoconfigGUI *gui);
static void gc_page_changed (GtkEntry *entry, ExchangeAutoconfigGUI *gui);
static void verify_page_changed (GtkEntry *entry, ExchangeAutoconfigGUI *gui);


static void
autoconfig_gui_set_page (ExchangeAutoconfigGUI *gui, int page)
{
	gnome_druid_set_page (gui->druid, gui->pages->pdata[page]);
}
	
static void
autoconfig_gui_set_next_sensitive (ExchangeAutoconfigGUI *gui,
				   gboolean next_sensitive)
{
	gnome_druid_set_buttons_sensitive (gui->druid, TRUE,
					   next_sensitive,
					   TRUE, FALSE);
}


#define SETUP_ENTRY(name, changed)				\
	gui->name = (GtkEntry *)				\
		glade_xml_get_widget (gui->xml, #name);		\
	g_signal_connect (gui->name, "changed",			\
			  G_CALLBACK (changed), gui);

static ExchangeAutoconfigGUI *
autoconfig_gui_new (void)
{
	ExchangeAutoconfigGUI *gui;

	gui = g_new0 (ExchangeAutoconfigGUI, 1);

	gui->xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-autoconfig-wizard.glade", NULL, NULL);
	if (!gui->xml) {
		g_warning ("Could not find exchange-autoconfig-wizard.glade");
		g_free (gui);
		return NULL;
	}

	gui->ac = e2k_autoconfig_new (NULL, NULL, NULL,
				      E2K_AUTOCONFIG_USE_EITHER);

	SETUP_ENTRY (owa_uri_entry, owa_page_changed);
	SETUP_ENTRY (username_entry, owa_page_changed);
	SETUP_ENTRY (password_entry, owa_page_changed);
	gui->remember_password_check = (GtkToggleButton *)
		glade_xml_get_widget (gui->xml, "remember_password_check");

	SETUP_ENTRY (gc_server_entry, gc_page_changed);

	gui->failure_label = (GtkLabel *)
		glade_xml_get_widget (gui->xml, "failure_label");
	gui->failure_vbox = (GtkBox *)
		glade_xml_get_widget (gui->xml, "failure_vbox");

	SETUP_ENTRY (name_entry, verify_page_changed);
	SETUP_ENTRY (email_entry, verify_page_changed);
	gui->default_account_check = (GtkToggleButton *)
		glade_xml_get_widget (gui->xml, "default_account_check");

	return gui;
}


static inline gboolean
check_field (GtkEntry *entry)
{
	return (*gtk_entry_get_text (entry) != '\0');
}


static void
owa_page_prepare (ExchangeAutoconfigGUI *gui)
{
	if (gui->ac->username)
		gtk_entry_set_text (gui->username_entry, gui->ac->username);

	if (gui->ac->owa_uri)
		gtk_entry_set_text (gui->owa_uri_entry, gui->ac->owa_uri);

	if (!gui->window)
		gui->window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (gui->owa_uri_entry)));

	owa_page_changed (NULL, gui);
}

static gboolean
owa_page_next (ExchangeAutoconfigGUI *gui)
{	
	E2kAutoconfigResult result;
	const char *old, *new;

	e2k_autoconfig_set_owa_uri (gui->ac, gtk_entry_get_text (gui->owa_uri_entry));
	e2k_autoconfig_set_username (gui->ac, gtk_entry_get_text (gui->username_entry));
	e2k_autoconfig_set_password (gui->ac, gtk_entry_get_text (gui->password_entry));

	gtk_widget_set_sensitive (GTK_WIDGET (gui->window), FALSE);
	e2k_operation_init (&gui->op);
	result = e2k_autoconfig_check_exchange (gui->ac, &gui->op);

	if (result == E2K_AUTOCONFIG_OK) {
		result = e2k_autoconfig_check_global_catalog (gui->ac, &gui->op);
		e2k_operation_free (&gui->op);
		gtk_widget_set_sensitive (GTK_WIDGET (gui->window), TRUE);

		if (result == E2K_AUTOCONFIG_OK)
			autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_VERIFY);
		else
			autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_GC);
		return TRUE;
	}

	/* Update the entries with anything we autodetected */
	owa_page_prepare (gui);
	e2k_operation_free (&gui->op);
	gtk_widget_set_sensitive (GTK_WIDGET (gui->window), TRUE);

	switch (result) {
	case E2K_AUTOCONFIG_CANT_CONNECT:
		if (!strncmp (gui->ac->owa_uri, "http:", 5)) {
			old = "http";
			new = "https";
		} else {
			old = "https";
			new = "http";
		}

		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not connect to the Exchange "
			    "server.\nMake sure the URL is correct "
			    "(try \"%s\" instead of \"%s\"?) "
			    "and try again."), new, old);
		return TRUE;

	case E2K_AUTOCONFIG_CANT_RESOLVE:
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not locate Exchange server.\n"
			    "Make sure the server name is spelled correctly "
			    "and try again."));
		return TRUE;

	case E2K_AUTOCONFIG_AUTH_ERROR:
	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM:
	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC:
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not authenticate to the Exchange "
			    "server.\nMake sure the username and "
			    "password are correct and try again."));
		return TRUE;

	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN:
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not authenticate to the Exchange "
			    "server.\nMake sure the username and "
			    "password are correct and try again.\n\n"
			    "You may need to specify the Windows "
			    "domain name as part of your username "
			    "(eg, \"MY-DOMAIN\\%s\")."),
			  gui->ac->username);
		return TRUE;

	case E2K_AUTOCONFIG_NO_OWA:
	case E2K_AUTOCONFIG_NOT_EXCHANGE:
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not find OWA data at the indicated URL.\n"
			    "Make sure the URL is correct and try again."));
		return TRUE;

	case E2K_AUTOCONFIG_CANT_BPROPFIND:
		gtk_label_set_text (
			gui->failure_label,
			_("Ximian Connector requires access to certain "
			  "functionality on the Exchange Server that appears "
			  "to be disabled or blocked.  (This is usually "
			  "unintentional.)  Your Exchange Administrator will "
			  "need to enable this functionality in order for "
			  "you to be able to use Ximian Connector.\n\n"
			  "For information to provide to your Exchange "
			  "administrator, please follow the link below:"));

		if (gui->failure_href)
			gtk_widget_destroy (gui->failure_href);
		gui->failure_href = gnome_href_new ("http://www.ximian.com/products/connector/iis-lockdown.html", "http://www.ximian.com/products/connector/iis-lockdown.html");
		gtk_box_pack_start (gui->failure_vbox, gui->failure_href, FALSE, FALSE, 0);
		gtk_widget_show (gui->failure_href);

		autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_FAILURE);
		return TRUE;

	case E2K_AUTOCONFIG_EXCHANGE_5_5:
		gtk_label_set_text (
			gui->failure_label,
			_("The Exchange server URL you provided is for an "
			  "Exchange 5.5 Server. Ximian Connector supports "
			  "Microsoft Exchange 2000 and 2003 only."));

		if (gui->failure_href) {
			gtk_widget_destroy (gui->failure_href);
			gui->failure_href = NULL;
		}
		autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_FAILURE);
		return TRUE;

	default:
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not configure Exchange account because "
			    "an unknown error occurred. Check the URL, "
			    "username, and password, and try again."));
		return TRUE;
	}
}

static gboolean
owa_page_check (ExchangeAutoconfigGUI *gui)
{
	return (check_field (gui->owa_uri_entry) &&
		check_field (gui->username_entry) &&
		check_field (gui->password_entry));
}

static void
owa_page_changed (GtkEntry *entry, ExchangeAutoconfigGUI *gui)
{
	autoconfig_gui_set_next_sensitive (gui, owa_page_check (gui));
}

static void
gc_page_prepare (ExchangeAutoconfigGUI *gui)
{
	gc_page_changed (NULL, gui);
}

static gboolean
gc_page_next (ExchangeAutoconfigGUI *gui)
{	
	E2kAutoconfigResult result;

	e2k_autoconfig_set_gc_server (gui->ac, gtk_entry_get_text (gui->gc_server_entry), -1);

	gtk_widget_set_sensitive (GTK_WIDGET (gui->window), FALSE);
	e2k_operation_init (&gui->op);
	result = e2k_autoconfig_check_global_catalog (gui->ac, &gui->op);
	e2k_operation_free (&gui->op);
	gtk_widget_set_sensitive (GTK_WIDGET (gui->window), TRUE);

	if (result == E2K_AUTOCONFIG_OK)
		autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_VERIFY);
	else if (result == E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN) {
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not authenticate to the Global Catalog "
			    "server. You may need to go back and specify "
			    "the Windows domain name as part of your "
			    "username (eg, \"MY-DOMAIN\\%s\")."),
			  gui->ac->username);
	} else {
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Could not connect to specified server.\n"
			    "Please check the server name and try again."));
	}
	return TRUE;
}

static gboolean
gc_page_check (ExchangeAutoconfigGUI *gui)
{
	return check_field (gui->gc_server_entry);
}

static void
gc_page_changed (GtkEntry *entry, ExchangeAutoconfigGUI *gui)
{
	autoconfig_gui_set_next_sensitive (gui, gc_page_check (gui));
}

static void
failure_page_prepare (ExchangeAutoconfigGUI *gui)
{
	autoconfig_gui_set_next_sensitive (gui, FALSE);
}

static gboolean
failure_page_back (ExchangeAutoconfigGUI *gui)
{	
	autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_OWA);
	return TRUE;
}

static void
verify_page_prepare (ExchangeAutoconfigGUI *gui)
{
	if (gui->ac->display_name)
		gtk_entry_set_text (gui->name_entry, gui->ac->display_name);
	else
		gtk_entry_set_text (gui->name_entry, _("Unknown"));
	if (gui->ac->email)
		gtk_entry_set_text (gui->email_entry, gui->ac->email);
	else
		gtk_entry_set_text (gui->email_entry, _("Unknown"));

	verify_page_changed (NULL, gui);
}

static gboolean
verify_page_back (ExchangeAutoconfigGUI *gui)
{	
	autoconfig_gui_set_page (gui, EXCHANGE_AUTOCONFIG_PAGE_OWA);
	return TRUE;
}

static gboolean
verify_page_next (ExchangeAutoconfigGUI *gui)
{	
	g_free (gui->ac->display_name);
	gui->ac->display_name = g_strdup (gtk_entry_get_text (gui->name_entry));
	g_free (gui->ac->email);
	gui->ac->email = g_strdup (gtk_entry_get_text (gui->email_entry));

	return FALSE;
}

static gboolean
verify_page_check (ExchangeAutoconfigGUI *gui)
{
	const char *email;

	email = gtk_entry_get_text (gui->email_entry);

	return (check_field (gui->name_entry) && strchr (email, '@'));
}

static void
verify_page_changed (GtkEntry *entry, ExchangeAutoconfigGUI *gui)
{
	autoconfig_gui_set_next_sensitive (gui, verify_page_check (gui));
}

static gboolean 
is_active_exchange_account (EAccount *account) 
{ 
	if (!account->enabled) 
		return FALSE; 
	if (!account->source || !account->source->url) 
		return FALSE; 
	return (strncmp (account->source->url, "exchange://", 11) == 0); 
}

static void
autoconfig_gui_apply (ExchangeAutoconfigGUI *gui)
{
	EAccountList *list;
	EAccount *account;
	EIterator *iter;
	GConfClient *gconf;
	char *pw_key;
	gboolean found = FALSE;

	/* Create the account. */
	gconf = gconf_client_get_default ();
	list = e_account_list_new (gconf);
	g_object_unref (gconf);
	if (!list) {
		e_notice (gui->window, GTK_MESSAGE_ERROR,
			  _("Configuration system error.\n"
			    "Unable to create new account."));
		return;
	}

	/* Check if any account is already configured, and throw an error if so */
	/* NOTE: This condition check needs to be removed when we start 
	   supporting multiple accounts */
	for (iter = e_list_get_iterator ((EList *)list); 
	     e_iterator_is_valid (iter); 
	     e_iterator_next (iter)) {
		account = (EAccount *)e_iterator_get (iter);
		if (account && (found = is_active_exchange_account (account))) {
			e_notice (gui->window, GTK_MESSAGE_ERROR,
			_("You may only configure a single Exchange account"));
			break;
		}
		account = NULL;
	}
	g_object_unref(iter);
	if (found)
		return;

	account = e_account_new ();

	account->name = g_strdup (gui->ac->email);
	account->enabled = TRUE;
	account->pgp_no_imip_sign = TRUE;

	account->id->name = g_strdup (gui->ac->display_name);
	account->id->address = g_strdup (gui->ac->email);

	account->source->url = g_strdup (gui->ac->account_uri);
	account->transport->url = g_strdup (gui->ac->account_uri);

	/* Remember the password at least for this session */
	pw_key = g_strdup_printf ("exchange://%s@%s",
				  gui->ac->username,
				  gui->ac->exchange_server);
	e_passwords_add_password (pw_key, gui->ac->password);

	/* Maybe longer */
	if (gtk_toggle_button_get_active (gui->remember_password_check)) {
		account->source->save_passwd = TRUE;
		e_passwords_remember_password ("Exchange", pw_key);
	}
	g_free (pw_key);

	e_account_list_add (list, account);
	if (gtk_toggle_button_get_active (gui->default_account_check))
		e_account_list_set_default (list, account);
	g_object_unref (account);
	e_account_list_save (list);
	g_object_unref (list);
}

static void
autoconfig_gui_free (ExchangeAutoconfigGUI *gui)
{
	g_object_unref (gui->xml);
	e2k_autoconfig_free (gui->ac);
	if (gui->pages)
		g_ptr_array_free (gui->pages, TRUE);
	g_free (gui);
}

static struct {
	const char *page_name, *body_name;
	void (*prepare_func) (ExchangeAutoconfigGUI *gui);
	gboolean (*back_func) (ExchangeAutoconfigGUI *gui);
	gboolean (*next_func) (ExchangeAutoconfigGUI *gui);
} autoconfig_pages[] = {
	{ "owa_page", "owa_page_vbox", owa_page_prepare, 
	  NULL, owa_page_next },
	{ "gc_page", "gc_page_vbox", gc_page_prepare, 
	  NULL, gc_page_next },
	{ "failure_page", "failure_page_vbox", failure_page_prepare, 
	  failure_page_back, NULL },
	{ "verify_page", "verify_page_vbox", verify_page_prepare, 
	  verify_page_back, verify_page_next }
};
static const int num_autoconfig_pages = sizeof (autoconfig_pages) / sizeof (autoconfig_pages[0]);

/* Autoconfig druid */

static int
find_page (ExchangeAutoconfigGUI *gui, gpointer page)
{
	int page_num;

	for (page_num = 0; page_num < gui->pages->len; page_num++) {
		if (gui->pages->pdata[page_num] == (gpointer)page)
			return page_num;
	}

	return -1;
}

static gboolean
druid_next_cb (GnomeDruidPage *page, GtkWidget *druid,
	       ExchangeAutoconfigGUI *gui)
{
	int page_num = find_page (gui, page);

	if (page_num == -1 || !autoconfig_pages[page_num].next_func)
		return FALSE;

	return autoconfig_pages[page_num].next_func (gui);
}

static void
druid_prepare_cb (GnomeDruidPage *page, GtkWidget *druid,
		  ExchangeAutoconfigGUI *gui)
{
	int page_num = find_page (gui, page);

	if (page_num == -1 || !autoconfig_pages[page_num].prepare_func)
		return;

	autoconfig_pages[page_num].prepare_func (gui);
}

static gboolean
druid_back_cb (GnomeDruidPage *page, GtkWidget *druid,
	       ExchangeAutoconfigGUI *gui)
{
	int page_num = find_page (gui, page);

	if (page_num == -1 || !autoconfig_pages[page_num].back_func)
		return FALSE;

	return autoconfig_pages[page_num].back_func (gui);
}

static void
druid_finish_cb (GnomeDruidPage *page, GtkWidget *druid,
		 ExchangeAutoconfigGUI *gui)
{
	autoconfig_gui_apply (gui);
	gtk_widget_destroy (GTK_WIDGET (gui->window));
}

static void
druid_cancel_cb (GnomeDruid *druid, ExchangeAutoconfigGUI *gui)
{
	gtk_widget_destroy (GTK_WIDGET (gui->window));
}

static void
druid_destroyed (gpointer gui, GObject *where_druid_was)
{
	gtk_main_quit ();
}

void
exchange_autoconfig_druid_run (void)
{
	ExchangeAutoconfigGUI *gui;
	GtkWidget *page;
	GdkPixbuf *icon;
	int i;

	gui = autoconfig_gui_new ();
	g_return_if_fail (gui);

	gui->druid = (GnomeDruid *)glade_xml_get_widget (gui->xml, "autoconfig_druid");
	g_signal_connect (gui->druid, "cancel", G_CALLBACK (druid_cancel_cb), gui);
	gui->window = (GtkWindow *)glade_xml_get_widget (gui->xml, "window");
	gui->pages = g_ptr_array_new ();

	icon = gdk_pixbuf_new_from_file (CONNECTOR_IMAGESDIR "/connector.png", NULL);
	for (i = 0; i < num_autoconfig_pages; i++) {
		page = glade_xml_get_widget (gui->xml, autoconfig_pages[i].page_name);
		g_ptr_array_add (gui->pages, page);
		gnome_druid_page_standard_set_logo (GNOME_DRUID_PAGE_STANDARD (page), icon);

		g_signal_connect (page, "next", G_CALLBACK (druid_next_cb), gui);
		g_signal_connect_after (page, "prepare", G_CALLBACK (druid_prepare_cb), gui);
		g_signal_connect (page, "back", G_CALLBACK (druid_back_cb), gui);
	}
	g_object_unref (icon);

	page = glade_xml_get_widget (gui->xml, "finish_page");
	g_signal_connect (page, "finish", G_CALLBACK (druid_finish_cb), gui);

	g_object_weak_ref (G_OBJECT (gui->druid), druid_destroyed, gui);

	gtk_widget_show_all (GTK_WIDGET (gui->window));
	gtk_main ();
	autoconfig_gui_free (gui);
}

