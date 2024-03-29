/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003-2004 Novell, Inc.
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

/* Server-side rule test program */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>

#include "e2k-context.h"
#include "e2k-propnames.h"
#include "e2k-rule.h"
#include "e2k-rule-xml.h"
#include "test-utils.h"

const gchar *test_program_name = "ruletest";

static const gchar *rules_props[] = {
	PR_RULES_DATA,
};

void
test_main (gint argc,
           gchar **argv)
{
	const gchar *url;
	E2kContext *ctx;
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults;
	GByteArray *ba;
	E2kRules *rules;
	xmlDoc *doc;

	if (argc != 2) {
		fprintf (stderr, "Usage: %s URL\n", argv[0]);
		exit (1);
	}
	url = argv[1];

	ctx = test_get_context (url);
	status = e2k_context_propfind (ctx, NULL, url,
				       rules_props,
				       G_N_ELEMENTS (rules_props),
				       &results, &nresults);
	test_abort_if_http_error (status);

	ba = e2k_properties_get_prop (results[0].props, PR_RULES_DATA);
	if (!ba) {
		printf ("No rules\n");
		goto done;
	}

	rules = e2k_rules_from_binary (ba);
	if (!rules) {
		printf ("Could not parse rules\n");
		goto done;
	}

	doc = e2k_rules_to_xml (rules);
	if (doc) {
		xmlDocFormatDump (stdout, doc, TRUE);
		xmlFreeDoc (doc);
	} else
		printf ("Could not convert normal rules to XML\n");

	e2k_rules_free (rules);
	e2k_results_free (results, nresults);

 done:
	test_quit ();
}
