/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 *
 * This  program is free  software; you  can redistribute  it and/or
 * modify it under the terms of version 2  of the GNU General Public
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

/* Free/Busy test program. Note though that this uses the code in
 * e2k-freebusy.c, which is not currently used by Connector itself.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "e2k-freebusy.h"
#include "e2k-connection.h"
#include "e2k-global-catalog.h"
#include "e2k-uri.h"

#include <e-util/e-passwords.h>
#include <e-util/e-time-utils.h>
#include <libgnome/gnome-util.h>

char **global_argv, *server, *password;
const char *user;
GMainLoop *loop;

static void
got_dn (E2kGlobalCatalog *gc, E2kGlobalCatalogStatus status,
	E2kGlobalCatalogEntry *entry, gpointer user_data)
{
	E2kConnection *conn;
	E2kFreebusy *fb;
	E2kFreebusyEvent event;
	int ti, bi, oi;
	char *public_uri;
	struct tm tm;
	time_t t;

	if (status != E2K_GLOBAL_CATALOG_OK) {
		fprintf (stderr, "Lookup failed: %d\n", status);
		g_main_loop_quit (loop);
		return;
	}

	public_uri = g_strdup_printf ("http://%s/public", server);
	conn = e2k_connection_new (public_uri);
	e2k_connection_set_auth (conn, user, NULL, NULL, password);

	fb = e2k_freebusy_new (conn, public_uri, entry->legacy_exchange_dn);
	g_free (public_uri);
	g_object_unref (conn);

	if (!fb) {
		fprintf (stderr, "Could not get fb props\n");
		g_main_loop_quit (loop);
		return;
	}

	if (!fb->events[E2K_BUSYSTATUS_ALL]->len) {
		printf ("No data\n");
		g_main_loop_quit (loop);
		return;
	}

	printf ("                         6am      9am      noon     3pm      6pm\n");

	ti = bi = oi = 0;
	for (t = fb->start; t < fb->end; t += 30 * 60) {
		if ((t - fb->start) % (24 * 60 * 60) == 0) {
			tm = *localtime (&t);
			printf ("\n%02d-%02d: ", tm.tm_mon + 1, tm.tm_mday);
		}

		for (; oi < fb->events[E2K_BUSYSTATUS_OOF]->len; oi++) {
			event = g_array_index (fb->events[E2K_BUSYSTATUS_OOF],
					       E2kFreebusyEvent, oi);
			if (event.end <= t)
				continue;
			if (event.start < t + (30 * 60)) {
				printf ("O");
				goto next;
			}
			if (event.start > t)
				break;
		}
		for (; bi < fb->events[E2K_BUSYSTATUS_BUSY]->len; bi++) {
			event = g_array_index (fb->events[E2K_BUSYSTATUS_BUSY],
					       E2kFreebusyEvent, bi);
			if (event.end <= t)
				continue;
			if (event.start < t + (30 * 60)) {
				printf ("X");
				goto next;
			}
			if (event.start > t)
				break;
		}
		for (; ti < fb->events[E2K_BUSYSTATUS_TENTATIVE]->len; ti++) {
			event = g_array_index (fb->events[E2K_BUSYSTATUS_TENTATIVE],
					       E2kFreebusyEvent, ti);
			if (event.end <= t)
				continue;
			if (event.start < t + (30 * 60)) {
				printf ("t");
				goto next;
			}
			if (event.start > t)
				break;
		}
		printf (".");

	next:
		if ((t - fb->start) % (60 * 60))
			printf (" ");
	}
	printf ("\n");

	g_main_loop_quit (loop);
}
		
static gboolean
lookup_user (gpointer email)
{
	E2kGlobalCatalog *gc;
	char *key;

	server = global_argv[1];
	user = g_get_user_name ();

	key = g_strdup_printf ("exchange://%s@%s", user, server);
	password = e_passwords_get_password ("Exchange", key);
	g_free (key);
	if (!password) {
		fprintf (stderr, "No password available for %s@%s\n",
			 user, server);
		g_main_loop_quit (loop);
		return FALSE;
	}

	gc = e2k_global_catalog_new (server, -1, user, NULL, password);
	if (!gc) {
		fprintf (stderr, "Could not create GC\n");
		g_main_loop_quit (loop);
		return FALSE;
	}

	e2k_global_catalog_async_lookup (gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
					 global_argv[2], E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN,
					 got_dn, NULL);

	return FALSE;
}

int
main (int argc, char **argv)
{
	if (argc != 3) {
		fprintf (stderr, "Usage: %s server email-addr\n", argv[0]);
		exit (1);
	}

	gnome_program_init ("fbtest", VERSION, LIBGNOME_MODULE,
			    argc, argv, NULL);

	global_argv = argv;
	g_idle_add (lookup_user, NULL);

	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

	return 0;
}
