/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
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

/* WebDAV test program / utility */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <e-util/e-passwords.h>
#include <libgnome/gnome-util.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>

#include "e2k-connection.h"
#include "e2k-restriction.h"
#include "e2k-security-descriptor.h"
#include "e2k-xml-utils.h"

#include "e2k-propnames.h"
#include "e2k-propnames.c"

extern char e2k_des_key[8];
gboolean hotmail = FALSE;
GMainLoop *loop;

static gboolean
http_error (SoupMessage *msg)
{
	if (SOUP_MESSAGE_IS_ERROR (msg)) {
		printf ("%d %s\n", msg->errorcode, msg->errorphrase);
		g_main_loop_quit (loop);
		return TRUE;
	}

	return FALSE;
}

static E2kConnection *
get_conn (const char *uri)
{
	E2kConnection *conn;
	SoupUri *suri;
	char *key;

	conn = e2k_connection_new (uri);
	if (!conn) {
		printf ("Could not create connection\n");
		exit (1);
	}

	suri = soup_uri_new (uri);
	if (suri->user && suri->passwd) {
		soup_uri_free (suri);
		return conn;
	}

	if (!suri->user)
		suri->user = g_strdup (g_get_user_name ());
	key = g_strdup_printf ("exchange://%s@%s", suri->user, suri->host);
	suri->passwd = e_passwords_get_password ("Exchange", key);
	if (!suri->passwd) {
		printf ("No password info available for %s\n", key);
		exit (1);
	}
	g_free (key);

	e2k_connection_set_auth (conn, suri->user, NULL, NULL, suri->passwd);
	soup_uri_free (suri);
	return conn;
}


static void
done (E2kConnection *conn, SoupMessage *msg, gpointer data)
{
	http_error (msg);
	g_main_loop_quit (loop);
}

static void
got_folder_tree (E2kConnection *conn, SoupMessage *msg, E2kResult *results,
		 int nresults, gpointer user_data)
{
	char *name, *class;
	int i;

	if (http_error (msg))
		return;

	for (i = 0; i < nresults; i++) {
		name = e2k_properties_get_prop (results[i].props,
						E2K_PR_DAV_DISPLAY_NAME);
		class = e2k_properties_get_prop (results[i].props,
						 E2K_PR_EXCHANGE_FOLDER_CLASS);

		printf ("%s:\n    %s, %s\n", results[i].href,
			name, class ? class : "(No Outlook folder class)");
	}
	g_main_loop_quit (loop);
}

static const char *folder_tree_props[] = {
	E2K_PR_DAV_DISPLAY_NAME,
	E2K_PR_EXCHANGE_FOLDER_CLASS
};
static const int n_folder_tree_props = sizeof (folder_tree_props) / sizeof (folder_tree_props[0]);

static void
display_folder_tree (char *top)
{
	E2kConnection *conn;

	conn = get_conn (top);
	e2k_connection_search (conn, top,
			       folder_tree_props, n_folder_tree_props,
			       TRUE, NULL, NULL,
			       got_folder_tree, NULL);
}

static int
listing_contents (E2kConnection *conn, SoupMessage *msg,
		  E2kResult *results, int nresults,
		  int first, int total, gpointer user_data)
{
	int i;

	for (i = 0; i < nresults; i++) {
		printf ("%3d %s (%s)\n", first + i, results[i].href,
			(char *)e2k_properties_get_prop (results[i].props,
							 E2K_PR_DAV_DISPLAY_NAME));
	}

	return nresults;
}

static void
list_contents (char *top)
{
	E2kConnection *conn;
	E2kRestriction *rn;
	const char *prop;

	conn = get_conn (top);

	prop = E2K_PR_DAV_DISPLAY_NAME;
	rn = e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					E2K_RELOP_EQ, FALSE);
	e2k_connection_search_with_progress (conn, top,
					     &prop, 1, rn, NULL,
					     10, TRUE,
					     listing_contents, done, NULL);
	e2k_restriction_unref (rn);
}

static int
mp_compar (const void *k, const void *m)
{
	const char *key = k;
	struct mapi_proptag *mp = (void *)m;

	return strncmp (key, mp->proptag, 5);
}

static void
print_propname (const char *propname)
{
	struct mapi_proptag *mp;

	printf ("  %s", propname);

	if (!strncmp (propname, E2K_NS_MAPI_PROPTAG, sizeof (E2K_NS_MAPI_PROPTAG) - 1)) {
		mp = bsearch (propname + 42, mapi_proptags, nmapi_proptags,
			      sizeof (struct mapi_proptag), mp_compar);
		if (mp)
			printf (" (%s)", mp->name);
	}

	printf (":\n");
}

static void
print_binary (GByteArray *data)
{
	unsigned char *start, *end, *p;

	end = data->data + data->len;
	for (start = data->data; start < end; start += 16) {
		printf ("    ");
		for (p = start; p < end && p < start + 16; p++)
			printf ("%02x ", *p);
		while (p++ < start + 16)
			printf ("   ");
		printf ("   ");
		for (p = start; p < end && p < start + 16; p++)
			printf ("%c", isprint (*p) ? *p : '.');
		printf ("\n");
	}
}

static void
print_prop (const char *propname, E2kPropType type, gpointer data,
	    gpointer user_data)
{
	print_propname (propname);

	switch (type) {
	case E2K_PROP_TYPE_BINARY:
		print_binary (data);
		break;

	case E2K_PROP_TYPE_STRING_ARRAY:
	case E2K_PROP_TYPE_INT_ARRAY:
	{
		GPtrArray *array = data;
		int i;

		for (i = 0; i < array->len; i++)
			printf ("    %s\n", (char *)array->pdata[i]);
		break;
	}

	case E2K_PROP_TYPE_BINARY_ARRAY:
	{
		GPtrArray *array = data;
		int i;

		for (i = 0; i < array->len; i++) {
			print_binary (array->pdata[i]);
			printf ("\n");
		}
		break;
	}

	case E2K_PROP_TYPE_XML:
		printf ("    (xml)\n");
		break;

	case E2K_PROP_TYPE_STRING:
	default:
		printf ("    %s\n", (char *)data);
		break;
	}
}

static void
got_properties (E2kConnection *conn, SoupMessage *msg, E2kResult *results, 
		int nresults, gpointer user_data)
{
	int i;

	if (http_error (msg))
		return;

	for (i = 0; i < nresults; i++) {
		printf ("%s\n", results[i].href);
		e2k_properties_foreach (results[i].props, print_prop, NULL);
	}
	g_main_loop_quit (loop);
}

static void
got_all_properties (SoupMessage *msg, gpointer conn)
{
	E2kResult *results;
	int nresults;

	if (http_error (msg))
		return;

	e2k_results_from_multistatus (msg, &results, &nresults);
	got_properties (conn, msg, results, nresults, NULL);
	e2k_results_free (results, nresults);
}


#define ALL_PROPS \
"<?xml version=\"1.0\" encoding=\"utf-8\" ?>" \
"<propfind xmlns=\"DAV:\" xmlns:e=\"http://schemas.microsoft.com/exchange/\">" \
"  <allprop>" \
"    <e:allprop/>" \
"  </allprop>" \
"</propfind>"

static void
get_all_properties (char *uri)
{
	E2kConnection *conn;
	SoupMessage *msg;

	conn = get_conn (uri);
	msg = e2k_soup_message_new_full (conn, uri, "PROPFIND",
					 "text/xml", SOUP_BUFFER_USER_OWNED,
					 ALL_PROPS, strlen (ALL_PROPS));
	soup_message_add_header (msg->request_headers, "Brief", "t");
	soup_message_add_header (msg->request_headers, "Depth", "0");

	e2k_soup_message_queue (msg, got_all_properties, conn);
}

static void
get_property (char *uri, char *prop)
{
	E2kConnection *conn;
	int i;

	if (!strncmp (prop, "PR_", 3)) {
		for (i = 0; i < nmapi_proptags; i++)
			if (!strcmp (mapi_proptags[i].name, prop)) {
				prop = g_strconcat (E2K_NS_MAPI_PROPTAG,
						    mapi_proptags[i].proptag,
						    NULL);
				break;
			}
	}

	conn = get_conn (uri);
	e2k_connection_propfind (conn, uri, "0",
				 (const char **)&prop, 1,
				 got_properties, NULL);
}

static void
got_sd (E2kConnection *conn, SoupMessage *msg, E2kResult *results, 
	int nresults, gpointer user_data)
{
	xmlNodePtr xml_form;
	GByteArray *binary_form;
	E2kSecurityDescriptor *sd;
	E2kPermissionsRole role;
	guint32 perms;
	GList *sids, *s;
	E2kSid *sid;
	char *start, *end;

	if (http_error (msg))
		return;

	if (nresults == 0)
		goto done;

	xml_form = e2k_properties_get_prop (results[0].props,
					    E2K_PR_EXCHANGE_SD_XML);
	binary_form = e2k_properties_get_prop (results[0].props,
					       E2K_PR_EXCHANGE_SD_BINARY);
	if (!xml_form || !binary_form)
		goto done;

	start = strstr (msg->response.body, "security_descriptor");
	end = strstr (start + 1, "security_descriptor>");
	while (*start != '<')
		start--;
	while (*end != '>')
		end++;
	printf ("%.*s\n\n", end - start + 1, start);

	print_binary (binary_form);
	printf ("\n");

	sd = e2k_security_descriptor_new (xml_form, binary_form);
	if (!sd) {
		printf ("(Could not parse)\n");
		goto done;
	}

	sids = e2k_security_descriptor_get_sids (sd);
	for (s = sids; s; s = s->next) {
		sid = s->data;
		perms = e2k_security_descriptor_get_permissions (sd, sid);
		role = e2k_permissions_role_find (perms);
		printf ("%s: %s (0x%lx)\n",
			e2k_sid_get_display_name (sid),
			e2k_permissions_role_get_name (role),
			(unsigned long)perms);
	}
	g_list_free (sids);

	if (!e2k_security_descriptor_to_binary (sd))
		printf ("\nSD is malformed.\n");
	g_object_unref (sd);

 done:
	g_main_loop_quit (loop);
}

static void
get_sd (char *uri)
{
	E2kConnection *conn;
	const char *props[] = {
		E2K_PR_EXCHANGE_SD_BINARY,
		E2K_PR_EXCHANGE_SD_XML,
	};

	conn = get_conn (uri);
	e2k_connection_propfind (conn, uri, "0", props, 2, got_sd, NULL);
}

static void
got_body (E2kConnection *conn, SoupMessage *msg, gpointer data)
{
	if (msg->errorclass == SOUP_ERROR_CLASS_TRANSPORT) {
		printf ("Soup error: %d\n", msg->errorcode);
		g_main_loop_quit (loop);
		return;
	}

	if (SOUP_MESSAGE_IS_ERROR (msg))
		printf ("%d %s\n", msg->errorcode, msg->errorphrase);
	else
		fwrite (msg->response.body, 1, msg->response.length, stdout);

	g_main_loop_quit (loop);
}

static void
get_body (char *uri)
{
	E2kConnection *conn;

	conn = get_conn (uri);
	e2k_connection_get (conn, uri, got_body, NULL);
}

static void
delete (char *uri)
{
	E2kConnection *conn;

	conn = get_conn (uri);
	e2k_connection_delete (conn, uri, done, NULL);
}

static void
notify (E2kConnection *conn, const char *uri,
	E2kConnectionChangeType type, gpointer user_data)
{
	switch (type) {
	case E2K_CONNECTION_OBJECT_CHANGED:
		printf ("Changed\n");
		break;
	case E2K_CONNECTION_OBJECT_ADDED:
		printf ("Added\n");
		break;
	case E2K_CONNECTION_OBJECT_REMOVED:
		printf ("Removed\n");
		break;
	case E2K_CONNECTION_OBJECT_MOVED:
		printf ("Moved\n");
		break;
	}
}

static void
subscribe (char *uri)
{
	E2kConnection *conn;

	conn = get_conn (uri);

	e2k_connection_subscribe (conn, uri,
				  E2K_CONNECTION_OBJECT_CHANGED, 0,
				  notify, NULL);
	e2k_connection_subscribe (conn, uri,
				  E2K_CONNECTION_OBJECT_ADDED, 0,
				  notify, NULL);
	e2k_connection_subscribe (conn, uri,
				  E2K_CONNECTION_OBJECT_REMOVED, 0,
				  notify, NULL);
	e2k_connection_subscribe (conn, uri,
				  E2K_CONNECTION_OBJECT_MOVED, 0,
				  notify, NULL);
}

static void
moved (E2kConnection *conn, SoupMessage *msg,
       E2kResult *results, int nresults, gpointer data)
{
	if (http_error (msg))
		return;
	if (!nresults)
		printf ("?\n");
	else if (!SOUP_ERROR_IS_SUCCESSFUL (results[0].status))
		printf ("Failed: %d\n", results[0].status);
	else {
		printf ("moved to %s\n",
			(char *)e2k_properties_get_prop (results[0].props,
							 E2K_PR_DAV_LOCATION));
	}
	g_main_loop_quit (loop);
}

static void
move (char *from, char *to, gboolean delete)
{
	E2kConnection *conn;

	conn = get_conn (from);

	e2k_connection_transfer (conn, from, to, "<href></href>",
				 delete, moved, NULL);
}

static void
named (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	xmlDoc *doc;
	xmlNode *node;
	char *data;

	if (http_error (msg))
		return;

	doc = e2k_parse_xml (msg->response.body, msg->response.length);
	if (!doc || !doc->xmlRootNode || !doc->xmlRootNode->xmlChildrenNode ||
	    !doc->xmlRootNode->xmlChildrenNode->xmlChildrenNode) {
		printf ("Unknown\n");
		g_main_loop_quit (loop);
		return;
	}
	node = doc->xmlRootNode->xmlChildrenNode->xmlChildrenNode;
	if (!strcmp (node->name, "error")) {
		printf ("Error: %s\n", xmlNodeGetContent (node));
		g_main_loop_quit (loop);
		return;
	}

	for (node = node->xmlChildrenNode; node; node = node->next) {
		data = xmlNodeGetContent (node);
		if (data && *data)
			printf ("%s: %s\n", node->name, data);
		xmlFree (data);
	}
	xmlFreeDoc (doc);
	g_main_loop_quit (loop);
}

static void
name (char *alias, char *uri_prefix)
{
	E2kConnection *conn;
	char *uri;

	uri = g_strdup_printf ("%s?Cmd=galfind&AN=%s", uri_prefix, alias);
	conn = get_conn (uri);
	e2k_connection_get_owa (conn, uri, TRUE, named, NULL);
}

static void
put (const char *file, const char *uri)
{
	E2kConnection *conn;
	struct stat st;
	char *buf;
	int fd;

	fd = open (file, O_RDONLY);
	if (fd == -1 || fstat (fd, &st) == -1) {
		fprintf (stderr, "%s\n", strerror (errno));
		exit (1);
	}
	buf = g_malloc (st.st_size);
	read (fd, buf, st.st_size);

	conn = get_conn (uri);
	e2k_connection_put (conn, uri, "message/rfc822", buf, st.st_size,
			    done, NULL);
}

static void
usage (void)
{
	printf ("usage: ebrowse -t URI                       (shallow folder tree)\n");
	printf ("       ebrowse -l URI                       (contents listing)\n");
	printf ("       ebrowse [ -p | -P prop ] URI         (look up all/one prop)\n");
	printf ("       ebrowse -S URI                       (look up security descriptor)\n");
	printf ("       ebrowse -b URI                       (fetch body)\n");
	printf ("       ebrowse -q FILE URI                  (put body)\n");
	printf ("       ebrowse -d URI                       (delete)\n");
	printf ("       ebrowse -s URI                       (subscribe and listen)\n");
	printf ("       ebrowse [ -m | -c ] SRC DEST         (move/copy)\n");
	printf ("       ebrowse -n ALIAS URI                 (lookup name)\n");
	exit (1);
}

char **global_argv;
int global_argc;

static gboolean
idle_parse_argv (gpointer data)
{
	char *uri;

	uri = global_argv[global_argc - 1];

	if (global_argv[1][1] == 'h') {
		global_argv++;
		hotmail = TRUE;
	}

	switch (global_argv[1][1]) {
	case 't':
		display_folder_tree (uri);
		break;

	case 'l':
		list_contents (uri);
		break;

	case 'b':
		get_body (uri);
		break;

	case 'd':
		delete (uri);
		break;

	case 'p':
		get_all_properties (uri);
		break;

	case 'P':
		get_property (uri, global_argv[2]);
		break;

	case 'S':
		get_sd (uri);
		break;

	case 's':
		subscribe (uri);
		break;

	case 'm':
	case 'c':
		move (global_argv[2], uri, global_argv[1][1] == 'm');
		break;

	case 'n':
		name (global_argv[2], uri);
		break;

	case 'q':
		put (global_argv[2], uri);
		break;

	default:
		usage ();
	}

	return FALSE;
}

int
main (int argc, char **argv)
{
	gnome_program_init ("ebrowse", VERSION, LIBGNOME_MODULE,
			    argc, argv, NULL);

	if (argc == 1 || argv[1][0] != '-')
		usage ();

	global_argc = argc;
	global_argv = argv;
	g_idle_add (idle_parse_argv, NULL);

	loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (loop);

	return 0;
}
