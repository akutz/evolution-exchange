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

#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glade/glade.h>
#include <gtk/gtkmain.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo/bonobo-exception.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <gconf/gconf-client.h>
#include <libsoup/soup-misc.h>

#include <e-util/e-dialog-utils.h>
#include <e-util/e-passwords.h>
#include <pas/pas-book-factory.h>
#include <pcs/cal-factory.h>
#include <shell/evolution-shell-component.h>

#include "e2k-connection.h"
#include "e2k-license.h"
#include "e2k-utils.h"
#include "mail-stub-listener.h"
#include "mail-stub-exchange.h"
#include "cal-backend-exchange.h"
#include "addressbook/pas-backend-exchange.h"
#include "addressbook/pas-backend-ad.h"

#include "exchange-account.h"
#include "exchange-autoconfig-wizard.h"
#include "exchange-component.h"
#include "exchange-config-listener.h"
#include "exchange-delegates-control.h"
#include "exchange-offline-handler.h"
#include "exchange-oof.h"
#include "exchange-storage.h"
#include "e-folder-exchange.h"

#define EXCHANGE_STORAGE_COMPONENT_ID	"OAFIID:GNOME_Evolution_ExchangeStorage_ShellComponent"
#define EXCHANGE_CALENDAR_FACTORY_ID	"OAFIID:GNOME_Evolution_ExchangeStorage_CalendarFactory"
#define EXCHANGE_ADDRESSBOOK_FACTORY_ID	"OAFIID:GNOME_Evolution_ExchangeStorage_AddressbookFactory"
#define EXCHANGE_CONFIG_FACTORY_ID	"OAFIID:GNOME_Evolution_ExchangeStorage_ConfigFactory"

#define EXCHANGE_DELEGATES_CONTROL_ID	"OAFIID:GNOME_Evolution_ExchangeStorage_DelegatesConfigControl"
#define EXCHANGE_OOF_CONTROL_ID		"OAFIID:GNOME_Evolution_ExchangeStorage_OOFConfigControl"
#define EXCHANGE_AUTOCONFIG_WIZARD_ID	"OAFIID:GNOME_Evolution_ExchangeStorage_Startup_Wizard"

static CalFactory *cal_factory = NULL;
static PASBookFactory *pas_book_factory = NULL;
static BonoboGenericFactory *config_factory = NULL;

GConfClient *exchange_configdb;
ExchangeConfigListener *config_listener;
ExchangeAccount *global_account;
gboolean exchange_component_interactive = FALSE;
char *evolution_dir;

ExchangeAccount *
exchange_component_get_account_for_uri (const char *uri)
{
	/* FIXME */
	if (global_account)
		g_object_ref (global_account);
	return global_account;
}


struct mse_connect_data {
	int cmd_fd, status_fd;
	ExchangeAccount *account;
};

static void
mse_connect_cb (ExchangeAccount *account, gpointer user_data)
{
	struct mse_connect_data *mcd = user_data;
	MailStub *mse;

	if (exchange_account_get_connection (account)) {
		mse = mail_stub_exchange_new (mcd->account, mcd->cmd_fd,
					      mcd->status_fd);
	} else {
		close (mcd->cmd_fd);
		close (mcd->status_fd);
	}
	g_object_unref (mcd->account);
	g_free (mcd);
}

static void
new_connection (MailStubListener *listener, int cmd_fd, int status_fd,
		ExchangeAccount *account)
{
	struct mse_connect_data *mcd;

	mcd = g_new (struct mse_connect_data, 1);
	mcd->account = account;
	g_object_ref (mcd->account);
	mcd->cmd_fd = cmd_fd;
	mcd->status_fd = status_fd;

	exchange_account_async_connect (account, mse_connect_cb, mcd);
}

static void
destroy_listener (gpointer listener, GObject *where_account_was)
{
	g_object_unref (listener);
}

static void
destroy_storage (gpointer storage, GObject *where_account_was)
{
	GNOME_Evolution_Shell shell = g_object_get_data (storage, "shell");

	evolution_storage_deregister_on_shell (storage, shell);
}

static void
config_listener_account_created (ExchangeConfigListener *config_listener,
				 ExchangeAccount *account,
				 gpointer shell_client)
{
	MailStubListener *listener;
	EvolutionStorage *storage;
	GNOME_Evolution_Shell shell;
	char *path;

	global_account = account;

	path = g_strdup_printf ("/tmp/.exchange-%s/%s",
				g_get_user_name (),
				account->account_filename);
	listener = mail_stub_listener_new (path);
	g_signal_connect (listener, "new_connection",
			  G_CALLBACK (new_connection), account);
	g_free (path);
	g_object_weak_ref (G_OBJECT (account), destroy_listener, listener);

	storage = exchange_storage_new (account);
	shell = evolution_shell_client_corba_objref (shell_client);
	evolution_storage_register_on_shell (storage, shell);
	g_object_set_data (G_OBJECT (storage), "shell", shell);
	g_object_weak_ref (G_OBJECT (account), destroy_storage, storage);

	if (exchange_component_interactive)
		exchange_oof_init (account, 0);
}

static void
config_listener_account_removed (ExchangeConfigListener *config_listener,
				 ExchangeAccount *account,
				 gpointer shell_client)
{
	global_account = NULL;
}

static void
owner_set_cb (EvolutionShellComponent *shell_component,
	      EvolutionShellClient *shell_client,
	      const char *evo_dir, gpointer user_data)
{
	char *path;

	evolution_dir = g_strdup (evo_dir);
	exchange_configdb = gconf_client_get_default ();

	path = g_strdup_printf ("/tmp/.exchange-%s", g_get_user_name ());
	if (mkdir (path, 0700) == -1) {
		if (errno == EEXIST) {
			struct stat st;

			if (stat (path, &st) == -1) {
				g_warning ("Could not stat %s", path);
				return;
			}
			if (st.st_uid != getuid () ||
			    (st.st_mode & 07777) != 0700) {
				g_warning ("Bad socket dir %s", path);
				return;
			}
		} else {
			g_warning ("Can't create %s", path);
			return;
		}
	}
	g_free (path);

	config_listener = exchange_config_listener_new (exchange_configdb);
	g_signal_connect (config_listener, "exchange_account_created",
			  G_CALLBACK (config_listener_account_created),
			  shell_client);
	g_signal_connect (config_listener, "exchange_account_removed",
			  G_CALLBACK (config_listener_account_removed),
			  shell_client);
}

/* This returns %TRUE all the time, so if set as an idle callback it
   effectively causes each and every nested glib mainloop to be quit.  */
static gboolean
quit_cb (gpointer closure)
{
	bonobo_main_quit ();
	return TRUE;
}

static void
owner_unset_cb (EvolutionShellComponent *shell_component,
		gpointer user_data)
{
	g_object_unref (exchange_configdb);
	exchange_configdb = NULL;

	soup_shutdown ();
	g_timeout_add (500, quit_cb, NULL);
}

static gboolean
idle_do_interactive (gpointer user_data)
{
	GdkNativeWindow new_view_xid = (GdkNativeWindow)user_data;

	if (global_account)
		exchange_oof_init (global_account, new_view_xid);

	return FALSE;
}

static void
interactive_cb (EvolutionShellComponent *shell_component,
		gboolean on, gulong new_view_xid,
		gpointer user_data)
{
	exchange_component_interactive = on;

	if (exchange_component_interactive)
		g_idle_add (idle_do_interactive, (gpointer)new_view_xid);
}

static void
last_calendar_gone_cb (CalFactory *factory, gpointer data)
{
	/* FIXME: what to do? */
}

/* Creates the calendar factory object and registers it */
static gboolean
setup_pcs (int argc, char **argv)
{
	cal_factory = cal_factory_new ();
	if (!cal_factory) {
		g_message ("setup_pcs(): Could not create the calendar factory");
		return FALSE;
	}

	cal_factory_register_method (cal_factory, "exchange", CAL_BACKEND_EXCHANGE_TYPE);

	/* register the factory in OAF */
	if (!cal_factory_oaf_register (cal_factory, EXCHANGE_CALENDAR_FACTORY_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (cal_factory));
		cal_factory = NULL;
		return FALSE;
	}

	g_signal_connect (cal_factory, "last_calendar_gone",
			  G_CALLBACK (last_calendar_gone_cb), NULL);

	return TRUE;
}

static void
last_book_gone_cb (PASBookFactory *factory, gpointer data)
{
        /* FIXME: what to do? */
}

static gboolean
setup_pas (int argc, char **argv)
{
        pas_book_factory = pas_book_factory_new ();

        if (!pas_book_factory)
                return FALSE;

        pas_book_factory_register_backend (
                pas_book_factory, "exchange", pas_backend_exchange_new);
        pas_book_factory_register_backend (
                pas_book_factory, "activedirectory", pas_backend_ad_new);

        g_signal_connect (pas_book_factory, "last_book_gone",
			  G_CALLBACK (last_book_gone_cb), NULL);

        if (!pas_book_factory_activate (pas_book_factory, EXCHANGE_ADDRESSBOOK_FACTORY_ID)) {
                bonobo_object_unref (BONOBO_OBJECT (pas_book_factory));
                pas_book_factory = NULL;
                return FALSE;
        }

        return TRUE;
}

static BonoboObject *
config_factory_func (BonoboGenericFactory *factory,
		     const char *component_id,
		     gpointer data)
{
	if (!strcmp (component_id, EXCHANGE_DELEGATES_CONTROL_ID))
		return exchange_delegates_control_new ();
	else if (!strcmp (component_id, EXCHANGE_OOF_CONTROL_ID))
		return exchange_oof_control_new ();
	else
		return NULL;
}


static gboolean
setup_config (int argc, char **argv)
{
	config_factory = bonobo_generic_factory_new (
		EXCHANGE_CONFIG_FACTORY_ID, config_factory_func, NULL);
	return config_factory != CORBA_OBJECT_NIL;
}

int
main (int argc, char **argv)
{
	ExchangeOfflineHandler *offline_handler;
	int ret;

	EvolutionShellComponent *shell_component;
	EvolutionShellComponentFolderType types[] = {
		{ "exchange-server", "none", "exchange-server", "Exchange server", FALSE, NULL, NULL },
		{ NULL, NULL, NULL, NULL, FALSE, NULL, NULL }
	};

	bindtextdomain (PACKAGE, CONNECTOR_LOCALEDIR);
	textdomain (PACKAGE);

	gnome_program_init (PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    GNOME_PARAM_HUMAN_READABLE_NAME, _("Ximian Connector for Microsoft Exchange"),
			    NULL);

	e2k_license_validate ();
	gnome_vfs_init ();
	glade_init ();
	
	shell_component = evolution_shell_component_new (
		types, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL);
	offline_handler = exchange_offline_handler_new ();
	bonobo_object_add_interface (BONOBO_OBJECT (shell_component),
				     BONOBO_OBJECT (offline_handler));

	g_signal_connect (shell_component, "owner_set",
			  G_CALLBACK (owner_set_cb), NULL);
	g_signal_connect (shell_component, "owner_unset",
			  G_CALLBACK (owner_unset_cb), NULL);
	g_signal_connect (shell_component, "interactive",
			  G_CALLBACK (interactive_cb), NULL);

	/* register factories */
	ret = bonobo_activation_active_server_register (
		EXCHANGE_STORAGE_COMPONENT_ID,
		bonobo_object_corba_objref (BONOBO_OBJECT (shell_component))); 
	if (ret != Bonobo_ACTIVATION_REG_SUCCESS)
		goto failed;

	if (!setup_pcs (argc, argv))
		goto failed;

        if (!setup_pas (argc, argv))
		goto failed;

        if (!setup_config (argc, argv))
		goto failed;

	fprintf (stderr, "Evolution Exchange Storage up and running\n");
#ifdef E2K_DEBUG
	if (getenv ("E2K_DEBUG")) {
		/* Redirect stderr to stdout and make it line-buffered
		 * rather than block-buffered, for ease of debug
		 * redirection.
		 */
		dup2 (STDOUT_FILENO, STDERR_FILENO);
		setvbuf (stdout, NULL, _IOLBF, 0);
		setvbuf (stderr, NULL, _IOLBF, 0);
		printf ("E2K_DEBUG=%s\n", getenv ("E2K_DEBUG"));
	}
#endif

	bonobo_main ();

	e_passwords_shutdown ();
	return 0;

 failed:
	printf ("\nCould not register Evolution Exchange backend services.\n"
		"This probably means another copy of evolution-exchange-storage\n"
		"is already running.\n\n");
	return 1;
}
