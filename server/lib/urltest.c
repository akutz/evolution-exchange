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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/*
 * This program takes account URL as input and dups E2KUri structure
 * values
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "e2k-uri.h"
#include "test-utils.h"

const gchar *test_program_name = "urltest";

static void
dump_uri (E2kUri *euri)
{
	const gchar *temp;

	printf ("URI contents \n");
	printf ("==================== \n");

	if (euri->protocol)
		printf("Protocol : %s \n", euri->protocol);
	else
		printf ("Protocol : NULL \n");
	if (euri->user)
		printf("User : %s \n", euri->user);
	else
		printf ("User : NULL \n");
	if (euri->domain)
		printf("Domain : %s \n", euri->domain);
	else
		printf ("Domain : NULL \n");
	if (euri->authmech)
		printf("Authmech : %s \n", euri->authmech);
	else
		printf ("Authmech : NULL \n");
	if (euri->passwd)
		printf("Password : %s \n", euri->passwd);
	else
		printf ("Password : NULL \n");
	if (euri->host)
		printf("Host : %s \n", euri->host);
	else
		printf ("Host : NULL \n");
	if (euri->port)
		printf("Port : %d \n", euri->port);
	else
		printf ("Port : NULL \n");
	if (euri->path)
		printf("Path : %s \n", euri->path);
	else
		printf ("Path : NULL \n");
	if (euri->query)
		printf("Query : %s \n", euri->query);
	else
		printf ("Query : NULL \n");
	if (euri->fragment)
		printf("Fragment : %s \n", euri->fragment);
	else
		printf ("Fragment : NULL \n");
}

void
test_main (gint argc,
           gchar **argv)
{
	const gchar *url;
	E2kUri *euri;

	if (argc != 2) {
		fprintf (stderr, "Usage: %s url \n", argv[0]);
		exit (1);
	}

	url = argv[1];
	euri = e2k_uri_new (url);
	dump_uri (euri);
	test_quit ();
}
