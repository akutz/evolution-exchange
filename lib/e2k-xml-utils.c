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

#include "e2k-xml-utils.h"
#include <stdlib.h>
#include <libxml/xmlmemory.h>
#include <libxml/HTMLparser.h>
#include <libsoup/soup-headers.h>

static void
my_xml_parser_error_handler (void *ctx, const char *msg, ...)
{
	;
}

/**
 * e2k_parse_xml:
 * @buf: the data to parse
 * @len: the length of the buffer, or -1 if it is '\0'-terminated
 *
 * Parses the XML document in @buf.
 *
 * Return value: a pointer to an #xmlDoc
 **/
xmlDoc *
e2k_parse_xml (const char *buf, int len)
{
	static gboolean inited = FALSE;

	if (!inited) {
		xmlDefaultSAXHandlerInit();
		xmlDefaultSAXHandler.warning = my_xml_parser_error_handler;
		xmlDefaultSAXHandler.error = my_xml_parser_error_handler;
		inited = TRUE;
	}

	if (len == -1)
		len = strlen (buf);

	/* We use xmlRecoverMemory because Exchange will let you
	 * put control-characters into data, which will make the
	 * XML be not well-formed.
	 */
	return xmlRecoverMemory (buf, len);
}

/**
 * e2k_parse_html:
 * @buf: the NUL-terminated data to parse
 *
 * Parses the HTML document in @buf.
 *
 * Return value: a pointer to an #xmlDoc
 **/
xmlDoc *
e2k_parse_html (const char *buf)
{
	static gboolean inited = FALSE;

	if (!inited) {
		htmlDefaultSAXHandlerInit();
		htmlDefaultSAXHandler.warning = my_xml_parser_error_handler;
		htmlDefaultSAXHandler.error = my_xml_parser_error_handler;
		inited = TRUE;
	}

	return htmlParseDoc ((char *)buf, NULL);
}

void
e2k_g_string_append_xml_escaped (GString *string, const char *value)
{
	while (*value) {
		switch (*value) {
		case '<':
			g_string_append (string, "&lt;");
			break;
		case '>':
			g_string_append (string, "&gt;");
			break;
		case '&':
			g_string_append (string, "&amp;");
			break;
		case '"':
			g_string_append (string, "&quot;");
			break;

		default:
			g_string_append_c (string, *value);
			break;
		}
		value++;
	}
}

char *
e2k_xml_escape (const char *value)
{
	char *retval;
	GString *escaped;

	escaped = g_string_new (NULL);
	e2k_g_string_append_xml_escaped (escaped, value);
	retval = escaped->str;
	g_string_free (escaped, FALSE);
	return retval;
}

char *
e2k_xml_unescape (const char *value)
{
	int i;
	char *retval;
	GString *unescaped = g_string_new ("");

	for (i = 0; value[i]; i++) {
		if (!strncmp (&value[i], "&lt;", 4)) {
			unescaped = g_string_append_c (unescaped, '<');
			i += 3;
		}
		else if (!strncmp (&value[i], "&gt;", 4)) {
			unescaped = g_string_append_c (unescaped, '>');
			i += 3;
		}
		else if (!strncmp (&value[i], "&amp;", 5)) {
			unescaped = g_string_append_c (unescaped, '&');
			i += 4;
		}
		else if (!strncmp (&value[i], "&quot;", 6)) {
			unescaped = g_string_append_c (unescaped, '"');
			i += 5;
		}
		else
			unescaped = g_string_append_c (unescaped, value[i]);
	}

	retval = unescaped->str;
	g_string_free (unescaped, FALSE);

	return retval;
}

/**
 * e2k_xml_find:
 * @node: a node of an xml document
 * @name: the name of the element to find
 *
 * Starts or continues a pre-order depth-first search of an xml
 * document for an element named @name. @node is used as the starting
 * point of the search, but is not examined itself.
 *
 * To search the complete document, pass the root node of the document
 * as @node on the first call, and then on each successive call,
 * pass the previous match as @node.
 *
 * Return value: the first matching element after @node, or %NULL when
 * there are no more matches.
 **/
xmlNode *
e2k_xml_find (xmlNode *node, const char *name)
{
	g_return_val_if_fail (name != NULL, NULL);

	while (node) {
		/* If the current node has children, then the first
		 * child is the next node to examine. If it doesn't
		 * have children but does have a younger sibling, then
		 * that sibling is next up. Otherwise, climb back up
		 * the tree until we find a node that does have a
		 * younger sibling.
		 */
		if (node->children)
			node = node->children;
		else {
			while (node && !node->next)
				node = node->parent;
			if (!node)
				return NULL;
			node = node->next;
		}

		if (node->name && !strcmp (node->name, name))
			return node;
	}

	return NULL;
}
