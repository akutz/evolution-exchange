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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gtk/gtkmain.h>
#include <libgnomeui/gnome-app.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnome/libgnome.h>

#include <gdk/gdk.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-exception.h>

#include "exchange-types.h"
#include "exchange-migrate.h"

int
main (int argc, char **argv)
{
	gboolean ret=FALSE;
	CORBA_short major=1;
	CORBA_short minor=4;
	CORBA_short revision=0;
	gchar *source = "~/evolution";
	gchar *dest= "/tmp/.evolution-test";
	gchar *user = NULL, *server = NULL, *base_dir, *uid = NULL;
	int opt;
	char optstr[] = "M:m:r:u:h:s:d:";

	gnome_program_init("migr-test", VERSION, LIBGNOMEUI_MODULE, argc, argv, NULL);
	gdk_init(&argc, &argv);

	if (argc == 1) {
		printf("Usage: %s [-M major][ -m minor][ -r revision] <-u user> <-h server> [-s source][ -d destination] \n", argv[0]);
		return 1;
	}

	if (argc < 5) {
		printf("Error.. User name and Server name not provided \n");
		return 1;
	}

	if (argc < 15) {
		printf("Warning.. All the arguments not provided, Proceeding with the default values \n");
	}
	
	while ((opt = getopt (argc, argv, optstr)) != EOF) {
		switch (opt) {
			case 'M':
				major = atoi(optarg);
				break;
			case 'm':
				minor = atoi(optarg);
				break;
			case 'r':
				revision = atoi(optarg);
				break;
			case 'u':
				user = optarg;
				break;
			case 'h':
				server = optarg;
				break;
			case 's':
				source = optarg;
				break;
			case 'd':
				dest = optarg;
				break;
			default:
				break;
		}	
	}
	
	uid = g_strdup_printf ("%s@%s", user, server);

	/* destination path */
	base_dir = g_build_filename (dest, uid, NULL);
	printf("base dir is %s; uid = %s; dest = %s ; source=%s \n", base_dir, uid, dest, source);
		
	ret = exchange_migrate (major, minor, revision, base_dir, (char *) uid);
	if (ret)
		printf ("Migrated\n");
	else
		printf ("Failed to migrate\n");

	g_free (base_dir);
	g_free (uid);

	return 0;
}
