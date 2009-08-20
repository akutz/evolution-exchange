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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libedataserverui/e-passwords.h>

#include "exchange-autoconfig-wizard.h"

#ifdef G_OS_WIN32
const gchar *_exchange_storage_datadir;
const gchar *_exchange_storage_gladedir;
const gchar *_exchange_storage_imagesdir;
#endif

gint
main (gint argc, gchar **argv)
{
	GError *error = NULL;

#ifdef G_OS_WIN32
	{
		gchar *localedir;

		/* We assume evolution-exchange is installed in the
		 * same run-time prefix as evolution-data-server.
		 */
		_exchange_storage_datadir = e_util_replace_prefix (PREFIX, e_util_get_prefix (), DATADIR);
		_exchange_storage_gladedir = e_util_replace_prefix (PREFIX, e_util_get_prefix (), CONNECTOR_GLADEDIR);
		_exchange_storage_imagesdir = e_util_replace_prefix (PREFIX, e_util_get_prefix (), CONNECTOR_IMAGESDIR);

		localedir = e_util_replace_prefix (CONNECTOR_LOCALEDIR, e_util_get_cp_prefix (), CONNECTOR_LOCALEDIR);
		bindtextdomain (GETTEXT_PACKAGE, localedir);
	}

/* PREFIX and DATADIR are part of GNOME_PROGRAM_STANDARD_PROPERTIES */

#undef PREFIX
#define PREFIX e_util_get_prefix ()

#undef DATADIR
#define DATADIR _exchange_storage_datadir

#else
	bindtextdomain (GETTEXT_PACKAGE, CONNECTOR_LOCALEDIR);
#endif
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();
	g_thread_init (NULL);
	gtk_init_with_args (&argc, &argv, NULL, NULL, (char *)GETTEXT_PACKAGE, &error);

	if (error != NULL) {
		g_printerr ("Failed initialize application, %s\n", error->message);
		g_error_free (error);
		return (1);
	}

	e_passwords_init ();

	exchange_autoconfig_assistant_run ();

	e_passwords_shutdown ();

	return 0;
}
