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

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-exception.h>
#include <libgnomeui/gnome-ui-init.h>

#include <camel/camel.h>
#include <e-util/e-icon-factory.h>
#include <e-util/e-passwords.h>
#include <libedata-book/e-data-book-factory.h>
#include <libedata-cal/e-data-cal-factory.h>

#include "e2k-utils.h"
#include "addressbook/e-book-backend-exchange.h"
#include "addressbook/e-book-backend-gal.h"
#include "calendar/e-cal-backend-exchange-calendar.h"
#include "calendar/e-cal-backend-exchange-tasks.h"

#include "exchange-autoconfig-wizard.h"
#include "exchange-component.h"
#include "exchange-oof.h"

static BonoboGenericFactory *component_factory = NULL;
static EDataCalFactory *cal_factory = NULL;
static EDataBookFactory *book_factory = NULL;

ExchangeComponent *global_exchange_component;

static BonoboObject *
exchange_component_factory (BonoboGenericFactory *factory,
			    const char *component_id, void *component)
{
	g_return_val_if_fail (strcmp (component_id, EXCHANGE_COMPONENT_IID) == 0, NULL);

	return component;
}

static gboolean
setup_component_factory (void)
{
	global_exchange_component = exchange_component_new ();

	component_factory =
		bonobo_generic_factory_new (EXCHANGE_COMPONENT_FACTORY_IID,
					    exchange_component_factory,
					    global_exchange_component);
	return TRUE;
}

static void
last_calendar_gone_cb (EDataCalFactory *factory, gpointer data)
{
	/* FIXME: what to do? */
}

/* Creates the calendar factory object and registers it */
static gboolean
setup_calendar_factory (void)
{
	cal_factory = e_data_cal_factory_new ();
	if (!cal_factory) {
		g_message ("setup_calendar_factory(): Could not create the calendar factory");
		return FALSE;
	}

	e_data_cal_factory_register_method (
		cal_factory, "exchange", ICAL_VEVENT_COMPONENT,
		E_TYPE_CAL_BACKEND_EXCHANGE_CALENDAR);
	e_data_cal_factory_register_method (
		cal_factory, "exchange", ICAL_VTODO_COMPONENT,
		E_TYPE_CAL_BACKEND_EXCHANGE_TASKS);

	/* register the factory with bonobo */
	if (!e_data_cal_factory_register_storage (cal_factory, EXCHANGE_CALENDAR_FACTORY_ID)) {
		bonobo_object_unref (BONOBO_OBJECT (cal_factory));
		cal_factory = NULL;
		return FALSE;
	}

	g_signal_connect (cal_factory, "last_calendar_gone",
			  G_CALLBACK (last_calendar_gone_cb), NULL);
	return TRUE;
}

static void
last_book_gone_cb (EDataBookFactory *factory, gpointer data)
{
        /* FIXME: what to do? */
}

static gboolean
setup_addressbook_factory (void)
{
        book_factory = e_data_book_factory_new ();

        if (!book_factory)
                return FALSE;

        e_data_book_factory_register_backend (
                book_factory, "exchange", e_book_backend_exchange_new);
	e_data_book_factory_register_backend (
                book_factory, "gal", e_book_backend_gal_new);

        g_signal_connect (book_factory, "last_book_gone",
			  G_CALLBACK (last_book_gone_cb), NULL);

        if (!e_data_book_factory_activate (book_factory, EXCHANGE_ADDRESSBOOK_FACTORY_ID)) {
                bonobo_object_unref (BONOBO_OBJECT (book_factory));
                book_factory = NULL;
                return FALSE;
        }

        return TRUE;
}

int
main (int argc, char **argv)
{
	char *path;
	char *config_directory;

	bindtextdomain (PACKAGE, CONNECTOR_LOCALEDIR);
	bind_textdomain_codeset (PACKAGE, "UTF-8");
	textdomain (PACKAGE);

	gnome_program_init (PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    GNOME_PARAM_HUMAN_READABLE_NAME, _("Ximian Connector for Microsoft Exchange"),
			    NULL);
	e_icon_factory_init ();

	config_directory = g_build_filename (g_get_home_dir(), ".evolution", NULL);
	camel_init (config_directory, FALSE);
	g_free(config_directory);

	path = g_strdup_printf ("/tmp/.exchange-%s", g_get_user_name ());
	if (mkdir (path, 0700) == -1) {
		if (errno == EEXIST) {
			struct stat st;

			if (stat (path, &st) == -1) {
				g_warning ("Could not stat %s", path);
				return 1;
			}
			if (st.st_uid != getuid () ||
			    (st.st_mode & 07777) != 0700) {
				g_warning ("Bad socket dir %s", path);
				return 1;
			}
		} else {
			g_warning ("Can't create %s", path);
			return 1;
		}
	}
	g_free (path);

	/* register factories */
	if (!setup_component_factory ())
		goto failed;
	if (!setup_calendar_factory ())
		goto failed;
        if (!setup_addressbook_factory ())
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
