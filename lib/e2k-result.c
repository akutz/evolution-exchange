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

#include "e2k-result.h"
#include "e2k-encoding-utils.h"
#include "e2k-propnames.h"
#include "e2k-xml-utils.h"

#include <stdlib.h>
#include <string.h>

#include <libsoup/soup-headers.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

static void
prop_get_binary_array (E2kResult *result, const char *propname, xmlNode *node)
{
	GPtrArray *array;

	array = g_ptr_array_new ();
	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (node->xmlChildrenNode && node->xmlChildrenNode->content)
			g_ptr_array_add (array, e2k_base64_decode (node->xmlChildrenNode->content));
		else
			g_ptr_array_add (array, g_byte_array_new ());
	}

	e2k_properties_set_binary_array (result->props, propname, array);
}

static void
prop_get_string_array (E2kResult *result, const char *propname,
		       E2kPropType real_type, xmlNode *node)
{
	GPtrArray *array;

	array = g_ptr_array_new ();
	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (node->xmlChildrenNode && node->xmlChildrenNode->content)
			g_ptr_array_add (array, g_strdup (node->xmlChildrenNode->content));
		else
			g_ptr_array_add (array, g_strdup (""));
	}

	e2k_properties_set_type_as_string_array (result->props, propname,
						 real_type, array);
}

static void
prop_get_binary (E2kResult *result, const char *propname, xmlNode *node)
{
	GByteArray *data;

	if (node->xmlChildrenNode && node->xmlChildrenNode->content)
		data = e2k_base64_decode (node->xmlChildrenNode->content);
	else
		data = g_byte_array_new ();

	e2k_properties_set_binary (result->props, propname, data);
}

static void
prop_get_string (E2kResult *result, const char *propname,
		 E2kPropType real_type, xmlNode *node)
{
	char *content;

	if (node->xmlChildrenNode && node->xmlChildrenNode->content)
		content = g_strdup (node->xmlChildrenNode->content);
	else
		content = g_strdup ("");

	e2k_properties_set_type_as_string (result->props, propname,
					   real_type, content);
}

static void
prop_get_xml (E2kResult *result, const char *propname, xmlNode *node)
{
	e2k_properties_set_xml (result->props, propname,
				xmlCopyNode (node, TRUE));
}

static void
prop_parse (xmlNode *node, E2kResult *result)
{
	char *name, *type;

	g_return_if_fail (node->ns != NULL);

	if (!result->props)
		result->props = e2k_properties_new ();

	name = g_strdup_printf ("%s%s", node->ns->href, node->name);

	type = xmlGetNsProp (node, "dt", E2K_NS_TYPE);
	if (type && !strcmp (type, "mv.bin.base64"))
		prop_get_binary_array (result, name, node);
	else if (type && !strcmp (type, "mv.int"))
		prop_get_string_array (result, name, E2K_PROP_TYPE_INT_ARRAY, node);
	else if (type && !strncmp (type, "mv.", 3))
		prop_get_string_array (result, name, E2K_PROP_TYPE_STRING_ARRAY, node);
	else if (type && !strcmp (type, "bin.base64"))
		prop_get_binary (result, name, node);
	else if (type && !strcmp (type, "int"))
		prop_get_string (result, name, E2K_PROP_TYPE_INT, node);
	else if (type && !strcmp (type, "boolean"))
		prop_get_string (result, name, E2K_PROP_TYPE_BOOL, node);
	else if (type && !strcmp (type, "float"))
		prop_get_string (result, name, E2K_PROP_TYPE_FLOAT, node);
	else if (type && !strcmp (type, "dateTime.tz"))
		prop_get_string (result, name, E2K_PROP_TYPE_DATE, node);
	else if (!node->xmlChildrenNode ||
		 !node->xmlChildrenNode->xmlChildrenNode)
		prop_get_string (result, name, E2K_PROP_TYPE_STRING, node);
	else
		prop_get_xml (result, name, node);

	if (type)
		xmlFree (type);
	g_free (name);
}

static void
propstat_parse (xmlNode *node, E2kResult *result)
{
	node = node->xmlChildrenNode;
	if (!E2K_IS_NODE (node, "DAV:", "status"))
		return;
	if (!soup_headers_parse_status_line (node->xmlChildrenNode->content,
					     NULL, &result->status, NULL) ||
	    result->status != SOUP_ERROR_OK)
		return;

	node = node->next;
	if (!E2K_IS_NODE (node, "DAV:", "prop"))
		return;

	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE)
			prop_parse (node, result);
	}
}

static void
e2k_result_clear (E2kResult *result)
{
	xmlFree (result->href);
	result->href = NULL;
	if (result->props) {
		e2k_properties_free (result->props);
		result->props = NULL;
	}
}

GArray *
e2k_results_array_new (void)
{
	return g_array_new (FALSE, FALSE, sizeof (E2kResult));
}

void
e2k_results_array_add_from_multistatus (GArray *results_array,
					SoupMessage *msg)
{
	xmlDoc *doc;
	xmlNode *node, *rnode;
	E2kResult result;

	g_return_if_fail (msg->errorcode == SOUP_ERROR_DAV_MULTISTATUS);

	doc = e2k_parse_xml (msg->response.body, msg->response.length);
	if (!doc)
		return;
	node = doc->xmlRootNode;
	if (!node || !E2K_IS_NODE (node, "DAV:", "multistatus")) {
		xmlFree (doc);
		return;
	}

	for (node = node->xmlChildrenNode; node; node = node->next) {
		if (!E2K_IS_NODE (node, "DAV:", "response") ||
		    !node->xmlChildrenNode)
			continue;

		memset (&result, 0, sizeof (result));
		result.status = SOUP_ERROR_OK; /* sometimes omitted if Brief */

		for (rnode = node->xmlChildrenNode; rnode; rnode = rnode->next) {
			if (rnode->type != XML_ELEMENT_NODE)
				continue;

			if (E2K_IS_NODE (rnode, "DAV:", "href"))
				result.href = xmlNodeGetContent (rnode);
			else if (E2K_IS_NODE (rnode, "DAV:", "status")) {
				soup_headers_parse_status_line (
					rnode->xmlChildrenNode->content, NULL,
					&result.status, NULL);
			} else if (E2K_IS_NODE (rnode, "DAV:", "propstat"))
				propstat_parse (rnode, &result);
			else
				prop_parse (rnode, &result);
		}

		if (result.href) {
			if (SOUP_ERROR_IS_SUCCESSFUL (result.status) &&
			    !result.props)
				result.props = e2k_properties_new ();
			g_array_append_val (results_array, result);
		} else
			e2k_result_clear (&result);
	}

	xmlFreeDoc (doc);
}

void
e2k_results_array_free (GArray *results_array, gboolean free_results)
{
	if (free_results) {
		e2k_results_free ((E2kResult *)results_array->data,
				  results_array->len);
	}
	g_array_free (results_array, FALSE);
}


void
e2k_results_from_multistatus (SoupMessage *msg,
			      E2kResult **results, int *nresults)
{
	GArray *results_array;

	results_array = e2k_results_array_new ();
	e2k_results_array_add_from_multistatus (results_array, msg);

	*results = (E2kResult *)results_array->data;
	*nresults = results_array->len;
	e2k_results_array_free (results_array, FALSE);
}

E2kResult *
e2k_results_copy (E2kResult *results, int nresults)
{
	GArray *results_array = NULL;
	E2kResult result, *new_results;
	int i;

	results_array = g_array_new (TRUE, FALSE, sizeof (E2kResult));
	for (i = 0; i < nresults; i++) {
		result.href   = xmlMemStrdup (results[i].href);
		result.status = results[i].status;
		result.props  = e2k_properties_copy (results[i].props);

		g_array_append_val (results_array, result);
	}

	new_results = (E2kResult *) (results_array->data);
	g_array_free (results_array, FALSE);
	return new_results;
}

void
e2k_results_free (E2kResult *results, int nresults)
{
	int i;

	for (i = 0; i < nresults; i++)
		e2k_result_clear (&results[i]);
	g_free (results);
}
