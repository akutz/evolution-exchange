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

/* Global Catalog test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e2k-global-catalog.h"
#include "e2k-sid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <e-util/e-passwords.h>
#include <libgnome/gnome-util.h>

int global_argc;
char **global_argv;
GMainLoop *loop;

static void
do_lookup (E2kGlobalCatalog *gc, const char *user)
{
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogLookupType type;
	guint32 flags;
	int i;

	if (*user == '/')
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN;
	else if (strchr (user, '@'))
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL;
	else
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_DN;

	flags =	E2K_GLOBAL_CATALOG_LOOKUP_SID |
		E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX |
		E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN |
		E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES |
		E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS;

	status = e2k_global_catalog_lookup (gc, type, user, flags, &entry);
	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		break;
	case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
		printf ("No entry for %s\n", user);
		g_main_loop_quit (loop);
		return;
	case E2K_GLOBAL_CATALOG_NO_DATA:
		printf ("Entry for %s contains no data\n", user);
		g_main_loop_quit (loop);
		return;
	default:
		printf ("Error looking up user\n");
		g_main_loop_quit (loop);
		return;
	}

	printf ("%s (%s)\n", entry->display_name, entry->dn);
	if (entry->email)
		printf ("  email: %s\n", entry->email);
	if (entry->mailbox)
		printf ("  mailbox: %s on %s\n", entry->mailbox, entry->exchange_server);
	if (entry->legacy_exchange_dn)
		printf ("  Exchange 5.5 DN: %s\n", entry->legacy_exchange_dn);
	if (entry->sid)
		printf ("  sid: %s\n", e2k_sid_get_string_sid (entry->sid));
	if (entry->delegates) {
		printf ("  delegates:\n");
		for (i = 0; i < entry->delegates->len; i++)
			printf ("    %s\n", (char *)entry->delegates->pdata[i]);
	}
	if (entry->delegators) {
		printf ("  delegators:\n");
		for (i = 0; i < entry->delegators->len; i++)
			printf ("    %s\n", (char *)entry->delegators->pdata[i]);
	}

	e2k_global_catalog_entry_free (gc, entry);
	g_main_loop_quit (loop);
}

static char *
lookup_dn (E2kGlobalCatalog *gc, const char *id)
{
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogLookupType type;
	E2kGlobalCatalogStatus status;
	char *dn;

	if (id[0] == '/')
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN;
	else if (strchr (id, '@'))
		type = E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL;
	else
		return g_strdup (id);

	status = e2k_global_catalog_lookup (gc, type, id, 0, &entry);
	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		break;
	case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
		printf ("No entry for %s\n", id);
		exit (1);
		break;
	default:
		printf ("Error looking up user %s\n", id);
		exit (1);
		break;
	}

	dn = g_strdup (entry->dn);
	e2k_global_catalog_entry_free (gc, entry);

	return dn;
}

static void
do_modify (E2kGlobalCatalog *gc, const char *user, int op, const char *delegate)
{
	char *self_dn, *deleg_dn;
	E2kGlobalCatalogStatus status;

	self_dn = lookup_dn (gc, user);
	deleg_dn = lookup_dn (gc, delegate);

	if (op == '+')
		status = e2k_global_catalog_add_delegate (gc, self_dn, deleg_dn);
	else
		status = e2k_global_catalog_remove_delegate (gc, self_dn, deleg_dn);

	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		printf ("Done\n");
		break;
	case E2K_GLOBAL_CATALOG_BAD_DATA:
		printf ("Invalid delegate DN\n");
		break;
	case E2K_GLOBAL_CATALOG_NO_DATA:
		printf ("No such delegate to remove\n");
		break;
	case E2K_GLOBAL_CATALOG_EXISTS:
		printf ("That user is already a delegate\n");
		break;
	default:
		printf ("Failed\n");
		break;
	}

	g_main_loop_quit (loop);
}

static gboolean
idle_parse_argv (gpointer data)
{
	E2kGlobalCatalog *gc;
	const char *server, *user;
	char *password, *key;

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

	if (global_argc == 3)
		do_lookup (gc, global_argv[2]);
	else
		do_modify (gc, global_argv[2], global_argv[3][0], global_argv[3] + 1);

	g_object_unref (gc);
	return FALSE;
}

int
main (int argc, char **argv)
{
	gnome_program_init ("gctest", VERSION, LIBGNOME_MODULE,
			    argc, argv, NULL);

	if (argc < 3 || argc > 4 ||
	    (argc == 4 && argv[3][0] != '+' && argv[3][0] != '-')) {
		fprintf (stderr, "Usage: %s server email-or-dn [[+|-]delegate]\n", argv[0]);
		exit (1);
	}

	global_argc = argc;
	global_argv = argv;
	g_idle_add (idle_parse_argv, NULL);

	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

	return 0;
}
