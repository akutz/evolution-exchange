/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

/* Change password test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "e2k-kerberos.h"
#include "test-utils.h"

const char *test_program_name = "cptest";

void
test_main (int argc, char **argv)
{

	int res;
	
	char user[20] ;	
	char old_pwd[256] ;
	char new_pwd[256] ;
	char domain[256] = "nicel.com";
	char kdc[256] = "164.99.155.182";

	#if 0
	if (argc != 3) {
		fprintf (stderr, "Usage: %s old_passwd new_passwd\n", argv[0]);
		exit (1);
	}
	#endif

	strcpy (user, argv[1]);
	strcpy (old_pwd, argv[2]);
	strcpy (new_pwd, argv[3]);
	
	#if 0
	res = e2k_check_expire (user, old_pwd);

	if (res)
		fprintf (stderr, "Error in expire check passwd...\n");

	fprintf (stderr, "Expire check Succeeded\n");
	
	#endif

	res = e2k_create_krb_config_file (domain, kdc);
	if (res)
		fprintf (stderr, "Error in create krb\n");
	else
		fprintf (stderr, "Succeeded creating krb\n");

	
	res = e2k_change_passwd(user, old_pwd, new_pwd);

	if (res)
		fprintf (stderr, "Error in change passwd\n");
	else
		fprintf (stderr, "Succeeded\n");
	
	
	test_quit ();

	printf ("\n");

	
}
