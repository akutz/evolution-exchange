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
#include "config.h"
#endif

#include "e2k-license.h"
#include "e2k-encoding-utils.h"
#include "e2k-utils.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *license_data;
GHashTable *license_options;

static char *
extract_field (char **start)
{
	char *p, *value;
	char *name;

	for (name = *start; isspace ((unsigned char)*name); name++)
		;

	p = strchr (name, ':');
	if (!p || !p[1])
		return NULL;
	*p = '\0';
	value = p + 2;
	p = strchr (value, '\n');
	if (!p)
		return NULL;
	*p = '\0';
	*start = p + 1;
	if (*(p - 1) == '\r')
		*(p - 1) = '\0';

	if (g_ascii_strcasecmp (value, "false") &&
	    g_ascii_strcasecmp (value, "no"))
		g_hash_table_insert (license_options, name, value);

	return value;
}

/**
 * e2k_license_validate:
 *
 * This used to verify the cryptographic signature in the license file,
 * but now all it does is parse it for any optional configuration data.
 **/
void
e2k_license_validate (void)
{
	struct stat st;
	char *path, *sig, *p, *user, *restrictions, *timestamp;
	int fd;

	path = g_strdup_printf ("%s/evolution/connector-key.txt",
				g_get_home_dir ());
	fd = open (path, O_RDONLY);
	g_free (path);
	if (fd == -1) {
		path = g_strdup_printf ("%s/evolution/connector-key.data",
					g_get_home_dir ());
		fd = open (path, O_RDONLY);
		g_free (path);
	}
	if (fd == -1)
		fd = open ("/etc/ximian/connector-key.txt", O_RDONLY);
	if (fd == -1)
		fd = open (CONNECTOR_PREFIX "/etc/connector-key.txt", O_RDONLY);

	if (fd == -1)
		return;
	if (fstat (fd, &st) == -1) {
		close (fd);
		return;
	}

	license_data = g_malloc (st.st_size + 1);
	if (read (fd, license_data, st.st_size) != st.st_size) {
		close (fd);
		g_free (license_data);
		license_data = NULL;
		return;
	}
	close (fd);
	license_data[st.st_size] = '\0';
	license_options = g_hash_table_new (e2k_ascii_strcase_hash,
					    e2k_ascii_strcase_equal);

	/* Extract the fields */
	p = (char *)license_data;
	user = extract_field (&p);
	restrictions = extract_field (&p);
	timestamp = extract_field (&p);

	if (!user || !restrictions || !timestamp)
		return;

	/* Skip sig */
	while (isspace ((unsigned char)*p))
		p++;
	sig = p;
	do {
		p = strchr (p, '\n');
		if (!p)
			break;
		p++;
	} while (!isspace ((unsigned char)*p));

	if (p) {
		/* Read additional data */
		while (extract_field (&p))
			;
	}
}

/**
 * e2k_license_lookup_option:
 * @option: option name to look up
 *
 * Looks up an option in the license file.
 *
 * Return value: the string value of the option, or NULL if it is unset.
 **/
const char *
e2k_license_lookup_option (const char *option)
{
	if (!license_options)
		return NULL;
	return g_hash_table_lookup (license_options, option);
}
