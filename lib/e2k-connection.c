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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "e2k-connection.h"
#include "e2k-encoding-utils.h"
#include "e2k-marshal.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"

#include <libsoup/soup-headers.h>
#include <libsoup/soup-misc.h>
#include <libsoup/soup-socket.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class;

enum {
	REDIRECT,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

struct _E2kConnectionPrivate {
	char *owa_uri;
	SoupUri *suri;
	time_t last_timestamp;

	/* Notification listener */
	SoupSocketConnectId *get_local_address_id;
	GIOChannel *listener_channel;
	int listener_watch_id;

	char *notification_uri;
	GHashTable *subscriptions_by_id, *subscriptions_by_uri;

	/* Forms-based authentication */
	char *cookie;
	gboolean cookie_verified;
};

/* For operations with progress */
#define E2K_CONNECTION_MIN_BATCH_SIZE 25
#define E2K_CONNECTION_MAX_BATCH_SIZE 100

#ifdef E2K_DEBUG
char *e2k_debug, *e2k_debug_types, e2k_debug_hint;
int e2k_debug_level;
#define E2K_DEBUG_ALL_TYPES "ACFMNST"
#endif

static gboolean renew_subscription (gpointer user_data);
static void unsubscribe_internal (E2kConnection *conn, const char *uri, GList *sub_list);
static gboolean do_notification (GIOChannel *source, GIOCondition condition, gpointer data);

static void
init (GObject *object)
{
	E2kConnection *conn = E2K_CONNECTION (object);

	conn->priv = g_new0 (E2kConnectionPrivate, 1);
	conn->priv->subscriptions_by_id =
		g_hash_table_new (g_str_hash, g_str_equal);
	conn->priv->subscriptions_by_uri =
		g_hash_table_new (g_str_hash, g_str_equal);
}

static void
destroy_sub_list (gpointer uri, gpointer sub_list, gpointer conn)
{
	unsubscribe_internal (conn, uri, sub_list);
	g_list_free (sub_list);
}

static void
dispose (GObject *object)
{
	E2kConnection *conn = E2K_CONNECTION (object);

	if (conn->priv) {
		g_free (conn->priv->owa_uri);
		if (conn->priv->suri)
			soup_uri_free (conn->priv->suri);
		if (conn->priv->get_local_address_id)
			soup_socket_connect_cancel (conn->priv->get_local_address_id);

		g_hash_table_foreach (conn->priv->subscriptions_by_uri,
				      destroy_sub_list, conn);
		g_hash_table_destroy (conn->priv->subscriptions_by_uri);

		g_hash_table_destroy (conn->priv->subscriptions_by_id);

		if (conn->priv->listener_watch_id)
			g_source_remove (conn->priv->listener_watch_id);
		if (conn->priv->listener_channel) {
			g_io_channel_shutdown (conn->priv->listener_channel,
					       FALSE, NULL);
			g_io_channel_unref (conn->priv->listener_channel);
		}

		g_free (conn->priv->cookie);

		g_free (conn->priv);
		conn->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;

	signals[REDIRECT] =
		g_signal_new ("redirect",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (E2kConnectionClass, redirect),
			      NULL, NULL,
			      e2k_marshal_NONE__INT_STRING_STRING,
			      G_TYPE_NONE, 3,
			      G_TYPE_INT,
			      G_TYPE_STRING,
			      G_TYPE_STRING);

#ifdef E2K_DEBUG
	e2k_debug = getenv ("E2K_DEBUG");
	if (e2k_debug) {
		e2k_debug_level = atoi (e2k_debug);
		if (strpbrk (e2k_debug, E2K_DEBUG_ALL_TYPES))
			e2k_debug_types = e2k_debug;
		else
			e2k_debug_types = E2K_DEBUG_ALL_TYPES;
	}
#endif
}

E2K_MAKE_TYPE (e2k_connection, E2kConnection, class_init, init, PARENT_TYPE)


static void
renew_sub_list (gpointer key, gpointer value, gpointer data)
{
	GList *sub_list;

	for (sub_list = value; sub_list; sub_list = sub_list->next)
		renew_subscription (sub_list->data);
}

static void
got_connection (SoupSocket *sock, SoupSocketConnectStatus status,
		gpointer user_data)
{
	E2kConnection *conn = user_data;
	struct sockaddr_in sin;
	socklen_t len;
	GIOChannel *chan;
	int s, ret;
	char local_ipaddr[16];
	unsigned short port;

	conn->priv->get_local_address_id = NULL;

	if (status != SOUP_SOCKET_CONNECT_ERROR_NONE) {
		g_warning ("Could not connect to server.");
		goto done;
	}

	chan = soup_socket_get_iochannel (sock);
	s = g_io_channel_unix_get_fd (chan);

	len = sizeof (sin);
	if (getsockname (s, (struct sockaddr *)&sin, &len) == -1 ||
	    sin.sin_family != AF_INET) {
		g_warning ("Could not get local address.");
		goto done;
	}

#ifdef HAVE_INET_NTOP
	inet_ntop (AF_INET, &sin.sin_addr, local_ipaddr,
		   sizeof (local_ipaddr));
#else
	strncpy (local_ipaddr, inet_ntoa (sin.sin_addr),
		 sizeof (local_ipaddr));
#endif

	s = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == -1) {
		g_warning ("Could not create listening socket");
		goto done;
	}

	memset (&sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;

	port = (short)getpid ();
	do {
		port++;
		if (port < 1024)
			port += 1024;
		sin.sin_port = htons (port);
		ret = bind (s, (struct sockaddr *)&sin, sizeof (sin));
	} while (ret == -1 && errno == EADDRINUSE);

	if (ret == -1) {
		close (s);
		g_warning ("Could not bind listening socket");
		goto done;
	}

	conn->priv->listener_channel = g_io_channel_unix_new (s);
	g_io_channel_set_encoding (conn->priv->listener_channel, NULL, NULL);
	g_io_channel_set_buffered (conn->priv->listener_channel, FALSE);

	conn->priv->listener_watch_id =
		g_io_add_watch (conn->priv->listener_channel,
				G_IO_IN, do_notification, conn);

	conn->priv->notification_uri = g_strdup_printf ("httpu://%s:%u/",
							local_ipaddr,
							port);

	g_hash_table_foreach (conn->priv->subscriptions_by_uri,
			      renew_sub_list, conn);

 done:
	if (sock)
		soup_socket_unref (sock);
	g_object_unref (conn);
}

gboolean
e2k_connection_construct (E2kConnection *conn, const char *uri)
{
	conn->priv->owa_uri = g_strdup (uri);
	conn->priv->suri = soup_uri_new (uri);
	if (!conn->priv->suri)
		return FALSE;

	g_object_ref (conn);
	conn->priv->get_local_address_id = soup_socket_connect (
		conn->priv->suri->host, conn->priv->suri->port,
		got_connection, conn);

	return TRUE;
}

/**
 * e2k_connection_new:
 * @uri: OWA uri to connect to
 *
 * Return value: a new #E2kConnection based at @uri
 **/
E2kConnection *
e2k_connection_new (const char *uri)
{
	E2kConnection *conn;

	conn = g_object_new (E2K_TYPE_CONNECTION, NULL);
	if (!e2k_connection_construct (conn, uri)) {
		g_object_unref (conn);
		return NULL;
	}

	return conn;
}

/**
 * e2k_connection_set_auth:
 * @conn: the connection
 * @username: the Windows username (not including domain) of the user
 * @domain: the NT domain, or %NULL to use the default (if using NTLM)
 * @authmech: the HTTP Authorization type to use; either "Basic" or "NTLM"
 * @password: the user's password
 *
 * Sets the authentication information on @conn.
 *
 * Note that as of 1.4, this will cancel *all* pending soup connections,
 * because that is the only way to ensure that all old SoupAuths are
 * destroyed.
 **/
void
e2k_connection_set_auth (E2kConnection *conn, const char *username,
			 const char *domain, const char *authmech,
			 const char *password)
{
	if (!authmech || !g_ascii_strcasecmp (authmech, "ntlm")) {
		if (username) {
			g_free (conn->priv->suri->user);
			conn->priv->suri->user = g_strdup (username);
		}
		if (domain) {
			g_free (conn->priv->suri->authmech);
			conn->priv->suri->authmech =
				g_strdup_printf ("NTLM;domain=%s", domain);
		}
	} else {
		g_free (conn->priv->suri->authmech);
		conn->priv->suri->authmech = g_strdup (authmech);

		if (username) {
			g_free (conn->priv->suri->user);
			if (domain) {
				conn->priv->suri->user =
					g_strdup_printf ("%s\\%s", domain,
							 username);
			} else
				conn->priv->suri->user = g_strdup (username);
		}
	}

	if (password) {
		g_free (conn->priv->suri->passwd);
		conn->priv->suri->passwd = g_strdup (password);
	}

	/* GAAH! Unfortunately, we have no other choice here. Otherwise
	 * soup will keep sending messages across old pre-authenticated
	 * NTLM connections.
	 */
	soup_shutdown ();
}

/**
 * e2k_connection_get_last_timestamp:
 * @conn: the connection
 *
 * Return value: a %time_t corresponding to the last "Date" header
 * received from the server.
 **/
time_t
e2k_connection_get_last_timestamp (E2kConnection *conn)
{
	return conn->priv->last_timestamp;
}

static SoupContext *
e2k_soup_context_get (E2kConnection *conn, const char *uri, const char *method)
{
	SoupContext *ctx;
	SoupUri *suri;

	if (method[0] == 'B') {
		char *slash_uri = e2k_strdup_with_trailing_slash (uri);
		suri = soup_uri_new (slash_uri);
		g_free (slash_uri);
	} else
		suri = soup_uri_new (uri);
	if (!suri)
		return NULL;

	suri->protocol = conn->priv->suri->protocol;
	suri->port = conn->priv->suri->port;
	if (suri->user)
		g_free (suri->user);
	suri->user = g_strdup (conn->priv->suri->user);
	if (suri->passwd)
		g_free (suri->passwd);
	suri->passwd = g_strdup (conn->priv->suri->passwd);
	if (suri->authmech)
		g_free (suri->authmech);
	suri->authmech = g_strdup (conn->priv->suri->authmech);

	ctx = soup_context_from_uri (suri);
	soup_uri_free (suri);
	return ctx;
}

#ifdef E2K_DEBUG
void E2K_DEBUG_HINT (char hint)
{
	e2k_debug_hint = hint;
}

/* Debug levels:
 * 0 - None
 * 1 - Basic request and response
 * 2 - 1 plus all headers
 * 3 - 2 plus all bodies
 * 4 - 3 plus Global Catalog debug too
 */

static void
print_header (gpointer name, gpointer value, gpointer data)
{
	printf ("%s: %s\n", (char *)name, (char *)value);
}

static void
e2k_debug_handler (SoupMessage *msg, gpointer user_data)
{
	printf ("%d %s\nE2k-Debug: %p @ %lu\n",
		msg->errorcode, msg->errorphrase,
		msg, time (0));
	if (e2k_debug_level > 1) {
		soup_message_foreach_header (msg->response_headers,
					     print_header, NULL);
	}
	if (e2k_debug_level > 2 && msg->response.length &&
	    SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		const char *content_type =
			soup_message_get_header (msg->response_headers,
						 "Content-Type");
		if (!content_type || e2k_debug_level > 4 ||
		    g_ascii_strcasecmp (content_type, "text/html")) {
			fputc ('\n', stdout);
			fwrite (msg->response.body, 1, msg->response.length, stdout);
			fputc ('\n', stdout);
		}
	}
	printf ("\n");
}

static void
e2k_debug_setup (SoupMessage *msg)
{
	const SoupUri *uri;

	if (!e2k_debug_level || !strchr (e2k_debug_types, e2k_debug_hint))
		return;

	uri = soup_context_get_uri (msg->context);
	printf ("%s %s%s%s HTTP/1.1\nE2k-Debug: %p @ %lu\n",
		msg->method, uri->path,
		uri->querystring ? "?" : "",
		uri->querystring ? uri->querystring : "",
		msg, (unsigned long)time (0));
	if (e2k_debug_level > 1) {
		print_header ("Host", uri->host, NULL);
		soup_message_foreach_header (msg->request_headers,
					     print_header, NULL);
	}
	if (e2k_debug_level > 2 && msg->request.length &&
	    strcmp (msg->method, "POST")) {
		fputc ('\n', stdout);
		fwrite (msg->request.body, 1, msg->request.length, stdout);
	}
	printf ("\n");

	soup_message_add_handler (msg, SOUP_HANDLER_POST_BODY,
				  e2k_debug_handler, NULL);
}
#endif

#define E2K_FBA_FLAG_FORCE_DOWNLEVEL 1
#define E2K_FBA_FLAG_TRUSTED         4
#define E2K_SOUP_ERROR_TIMEOUT       440

gboolean
e2k_connection_fba (E2kConnection *conn, SoupMessage *failed_msg)
{
	static gboolean in_fba_auth = FALSE;
	SoupContext *tmp_ctx;
	int status, len;
	char *body;
	char *action, *method, *name, *value;
	xmlDoc *doc = NULL;
	xmlNode *node;
	SoupMessage *post_msg;
	GString *form_body, *cookie_str;
	const GSList *cookies, *c;

	if (in_fba_auth)
		return FALSE;

	if (conn->priv->cookie) {
		g_free (conn->priv->cookie);
		conn->priv->cookie = NULL;
		if (!conn->priv->cookie_verified) {
			/* New cookie failed on the first try. Must
			 * be a bad password.
			 */
			return FALSE;
		}
		/* Otherwise, it's just expired. */
	}

	if (!conn->priv->suri->user || !conn->priv->suri->passwd)
		return FALSE;

	in_fba_auth = TRUE;

	/* This is a hack. We need to force @failed_msg to give up its
	 * connection, and this is the easiest way to do that.
	 */
	tmp_ctx = failed_msg->context;
	soup_context_ref (tmp_ctx);
	soup_message_set_context (failed_msg, NULL);
	soup_message_set_context (failed_msg, tmp_ctx);
	soup_context_unref (tmp_ctx);

	status = e2k_connection_get_owa_sync (conn, conn->priv->owa_uri,
					      FALSE, &body, &len);
	if (!SOUP_ERROR_IS_SUCCESSFUL (status) || len == 0)
		goto failed;

	body[len] = '\0';
	doc = e2k_parse_html (body);
	g_free (body);

	node = e2k_xml_find (doc->children, "form");
	if (!node)
		goto failed;

	method = xmlGetProp (node, "method");
	if (!method || g_ascii_strcasecmp (method, "post") != 0) {
		if (method)
			xmlFree (method);
		goto failed;
	}
	xmlFree (method);

	value = xmlGetProp (node, "action");
	if (!value)
		goto failed;
	if (*value == '/') {
		SoupUri *suri;

		suri = soup_uri_new (conn->priv->owa_uri);
		g_free (suri->path);
		suri->path = g_strdup (value);
		action = soup_uri_to_string (suri, TRUE);
		soup_uri_free (suri);
	} else
		action = g_strdup (value);
	xmlFree (value);

	form_body = g_string_new (NULL);
	while ((node = e2k_xml_find (node, "input"))) {
		name = xmlGetProp (node, "name");
		if (!name)
			continue;
		value = xmlGetProp (node, "value");

		if (form_body->len > 0)
			g_string_append_c (form_body, '&');

		if (!g_ascii_strcasecmp (name, "destination") && value) {
			g_string_append (form_body, name);
			g_string_append_c (form_body, '=');
			e2k_uri_append_encoded (form_body, value, NULL);
		} else if (!g_ascii_strcasecmp (name, "flags")) {
			g_string_append_printf (form_body, "flags=%d",
						E2K_FBA_FLAG_TRUSTED);
		} else if (!g_ascii_strcasecmp (name, "username")) {
			g_string_append (form_body, "username=");
			e2k_uri_append_encoded (form_body, conn->priv->suri->user, NULL);
		} else if (!g_ascii_strcasecmp (name, "password")) {
			g_string_append (form_body, "password=");
			e2k_uri_append_encoded (form_body, conn->priv->suri->passwd, NULL);
		} else if (!g_ascii_strcasecmp (name, "trusted")) {
			g_string_append_printf (form_body, "trusted=%d",
						E2K_FBA_FLAG_TRUSTED);
		}

		if (value)
			xmlFree (value);
		xmlFree (name);
	}
	xmlFreeDoc (doc);
	doc = NULL;

	post_msg = e2k_soup_message_new_full (conn, action, "POST",
					      "application/x-www-form-urlencoded",
					      SOUP_BUFFER_SYSTEM_OWNED,
					      form_body->str, form_body->len);
	soup_message_set_flags (post_msg, SOUP_MESSAGE_NO_REDIRECT);
	soup_message_set_http_version (post_msg, SOUP_HTTP_1_0);
	e2k_soup_message_send (post_msg);
	g_string_free (form_body, FALSE);
	g_free (action);

	if (!SOUP_ERROR_IS_SUCCESSFUL (post_msg->errorcode) &&
	    !SOUP_ERROR_IS_REDIRECTION (post_msg->errorcode)) {
		soup_message_free (post_msg);
		goto failed;
	}

	/* Extract the cookies */
	cookies = soup_message_get_header_list (post_msg->response_headers,
						"Set-Cookie");
	cookie_str = g_string_new (NULL);

	for (c = cookies; c; c = c->next) {
		value = c->data;
		len = strcspn (value, ";");

		if (cookie_str->len)
			g_string_append (cookie_str, "; ");
		g_string_append_len (cookie_str, value, len);
	}
	conn->priv->cookie = cookie_str->str;
	conn->priv->cookie_verified = FALSE;
	g_string_free (cookie_str, FALSE);
	soup_message_free (post_msg);

	in_fba_auth = FALSE;

	/* Set up the failed message to be requeued */
	soup_message_remove_header (failed_msg->request_headers, "Cookie");
	soup_message_add_header (failed_msg->request_headers,
				 "Cookie", conn->priv->cookie);
	return TRUE;

 failed:
	in_fba_auth = FALSE;
	if (doc)
		xmlFreeDoc (doc);
	return FALSE;
}

static void
fba_timeout_handler (SoupMessage *msg, gpointer conn)
{
#ifdef E2K_DEBUG
	if (e2k_debug_level)
		e2k_debug_handler (msg, NULL);
#endif

	if (e2k_connection_fba (conn, msg))
		soup_message_requeue (msg);
	else
		soup_message_set_error (msg, SOUP_ERROR_CANT_AUTHENTICATE);
}

static void
timestamp_handler (SoupMessage *msg, gpointer user_data)
{
	E2kConnection *conn = user_data;
	const char *date;

	date = soup_message_get_header (msg->response_headers, "Date");
	if (date)
		conn->priv->last_timestamp = e2k_parse_http_date (date);
}

static void
redirect_handler (SoupMessage *msg, gpointer user_data)
{
	E2kConnection *conn = user_data;
	const char *new_uri;
	SoupUri *soup_uri;
	char *old_uri;

	if (soup_message_get_flags (msg) & SOUP_MESSAGE_NO_REDIRECT)
		return;

#ifdef E2K_DEBUG
	if (e2k_debug_level)
		e2k_debug_handler (msg, NULL);
#endif

	new_uri = soup_message_get_header (msg->response_headers, "Location");
	if (new_uri) {
		soup_uri = soup_uri_copy (soup_context_get_uri (msg->context));
		soup_uri_set_auth (soup_uri, NULL, NULL, NULL);
		old_uri = soup_uri_to_string (soup_uri, FALSE);

		g_signal_emit (conn, signals[REDIRECT], 0,
			       msg->status, old_uri, new_uri);
		soup_uri_free (soup_uri);
		g_free (old_uri);
	}
}

static void
setup_message (E2kConnection *conn, SoupMessage *msg)
{
	soup_message_add_handler (msg, SOUP_HANDLER_PRE_BODY,
				  timestamp_handler, conn);
	soup_message_add_error_class_handler (msg, SOUP_ERROR_CLASS_REDIRECT,
					      SOUP_HANDLER_PRE_BODY,
					      redirect_handler, conn);
	soup_message_add_error_code_handler (msg, E2K_SOUP_ERROR_TIMEOUT,
					     SOUP_HANDLER_PRE_BODY,
					     fba_timeout_handler, conn);
	soup_message_add_header (msg->request_headers, "User-Agent",
				 "Evolution/" EVOLUTION_VERSION);
	if (conn->priv->cookie) {
		soup_message_add_header (msg->request_headers,
					 "Cookie", conn->priv->cookie);
	}
}

/**
 * e2k_soup_message_new:
 * @conn: the connection
 * @uri: URI, as with soup_context_get()
 * @method: method, as with soup_message_new()
 *
 * Use this instead of soup_message_new().
 *
 * Return value: a new %SoupMessage, set up for connector use
 **/
SoupMessage *
e2k_soup_message_new (E2kConnection *conn, const char *uri, const char *method)
{
	SoupContext *ctx;
	SoupMessage *msg;

	ctx = e2k_soup_context_get (conn, uri, method);
	msg = soup_message_new (ctx, method);
	setup_message (conn, msg);
	soup_context_unref (ctx);

	return msg;
}

/**
 * e2k_soup_message_new_full:
 * @conn: the connection
 * @uri: URI, as with soup_context_get()
 * @method: method, as with soup_message_new_full()
 * @content_type: MIME Content-Type of @body
 * @owner: ownership of @body
 * @body: request body
 * @length: length of @body
 *
 * Use this instead of soup_message_new_full().
 *
 * Return value: a new %SoupMessage with a request body, set up for
 * connector use
 **/
SoupMessage *
e2k_soup_message_new_full (E2kConnection *conn, const char *uri,
			   const char *method, const char *content_type,
			   SoupOwnership owner, const char *body,
			   gulong length)
{
	SoupContext *ctx;
	SoupMessage *msg;

	ctx = e2k_soup_context_get (conn, uri, method);
	msg = soup_message_new_full (ctx, method, owner, (char *)body, length);
	soup_message_add_header (msg->request_headers, "Content-Type",
				 content_type);
	setup_message (conn, msg);
	soup_context_unref (ctx);

	return msg;
}

/**
 * e2k_soup_message_queue:
 * @msg: the message to queue
 * @callback: callback to invoke when @msg is done
 * @user_data: data for @callback
 *
 * Queues @msg. Use this instead of soup_message_queue().
 **/
void
e2k_soup_message_queue (SoupMessage *msg, SoupCallbackFn callback,
			gpointer user_data)
{
#ifdef E2K_DEBUG
	e2k_debug_setup (msg);
#endif
	soup_message_queue (msg, callback, user_data);
}

/**
 * e2k_soup_message_send:
 * @msg: the message to send
 *
 * Synchronously sends @msg. Use this instead of soup_message_send().
 **/
SoupErrorClass
e2k_soup_message_send (SoupMessage *msg)
{
#ifdef E2K_DEBUG
	e2k_debug_setup (msg);
#endif
	return soup_message_send (msg);
}


typedef struct {
	E2kSimpleCallback callback;
	gpointer user_data;
	E2kConnection *conn;
} E2kCallbackData;

static void
e2k_callback (SoupMessage *msg, gpointer user_data)
{
	E2kCallbackData *data = user_data;

	data->callback (data->conn, msg, data->user_data);

	g_object_unref (data->conn);
	g_free (data);
}

static void
e2k_results_callback (SoupMessage *msg, gpointer user_data)
{
	E2kCallbackData *data = user_data;
	E2kResultsCallback callback = (E2kResultsCallback)data->callback;
	E2kResult *results = NULL;
	int nresults = 0;

	if (msg->errorcode == SOUP_ERROR_DAV_MULTISTATUS)
		e2k_results_from_multistatus (msg, &results, &nresults);

	callback (data->conn, msg, results, nresults, data->user_data);
	e2k_results_free (results, nresults);

	g_object_unref (data->conn);
	g_free (data);
}

static void
e2k_connection_queue_message (E2kConnection *conn, SoupMessage *msg,
			      SoupCallbackFn soup_callback,
			      gpointer callback, gpointer user_data)
{
	E2kCallbackData *callback_data;

	callback_data = g_new (E2kCallbackData, 1);
	callback_data->conn = conn;
	g_object_ref (conn);
	callback_data->callback = callback;
	callback_data->user_data = user_data;
	e2k_soup_message_queue (msg, soup_callback, callback_data);
}

static int
e2k_connection_send_message (E2kConnection *conn, SoupMessage *msg)
{
	e2k_soup_message_send (msg);
	return msg->errorcode;
}

typedef struct {
	E2kProgressCallback progress_callback;
	E2kSimpleCallback done_callback;
	gpointer user_data;

	E2kConnection *conn;
	gpointer caller_data;
	char *uri;
	SoupCallbackFn soup_callback;
} E2kProgressiveCallbackData;

static void
e2k_connection_queue_progressive_message (E2kConnection *conn,
					  SoupMessage *msg, const char *uri,
					  SoupCallbackFn soup_callback,
					  gpointer caller_data,
					  E2kProgressCallback progress_callback,
					  E2kSimpleCallback done_callback,
					  gpointer user_data)
{
	E2kProgressiveCallbackData *callback_data;

	callback_data = g_new (E2kProgressiveCallbackData, 1);
	callback_data->conn = conn;
	g_object_ref (conn);

	callback_data->soup_callback = soup_callback;
	callback_data->uri = g_strdup (uri);
	callback_data->caller_data = caller_data;

	callback_data->progress_callback = progress_callback;
	callback_data->done_callback = done_callback;
	callback_data->user_data = user_data;

#ifdef E2K_DEBUG
	e2k_debug_setup (msg);
#endif
	soup_message_queue (msg, soup_callback, callback_data);
}

static void
e2k_connection_requeue_progressive_message (E2kProgressiveCallbackData *callback_data,
					    SoupMessage *msg)
{
#ifdef E2K_DEBUG
	e2k_debug_setup (msg);
#endif
	soup_message_queue (msg, callback_data->soup_callback, callback_data);
}

static void
e2k_connection_free_progressive_data (E2kProgressiveCallbackData *callback_data)
{
	g_object_unref (callback_data->conn);
	g_free (callback_data->uri);
	g_free (callback_data);
}

typedef struct {
	E2kResultsCallback callback;
	gpointer user_data;

	GArray *results_array;

	E2kConnection *conn;
	gpointer caller_data;
	char *uri;
	SoupCallbackFn soup_callback;
} E2kCumulativeCallbackData;

static void
e2k_connection_queue_cumulative_message (E2kConnection *conn,
					 SoupMessage *msg, const char *uri,
					 SoupCallbackFn soup_callback,
					 gpointer caller_data,
					 E2kResultsCallback callback,
					 gpointer user_data)
{
	E2kCumulativeCallbackData *callback_data;

	callback_data = g_new (E2kCumulativeCallbackData, 1);
	callback_data->conn = conn;
	g_object_ref (conn);

	callback_data->soup_callback = soup_callback;
	callback_data->uri = g_strdup (uri);
	callback_data->caller_data = caller_data;

	callback_data->callback = callback;
	callback_data->user_data = user_data;

	callback_data->results_array = e2k_results_array_new ();

#ifdef E2K_DEBUG
	e2k_debug_setup (msg);
#endif
	soup_message_queue (msg, soup_callback, callback_data);
}

static void
e2k_connection_requeue_cumulative_message (E2kCumulativeCallbackData *callback_data,
					   SoupMessage *msg)
{
#ifdef E2K_DEBUG
	e2k_debug_setup (msg);
#endif
	soup_message_queue (msg, callback_data->soup_callback, callback_data);
}

static void
e2k_connection_free_cumulative_data (E2kCumulativeCallbackData *callback_data)
{
	g_object_unref (callback_data->conn);
	g_free (callback_data->uri);
	e2k_results_array_free (callback_data->results_array, TRUE);
	g_free (callback_data);
}


/* g_return_if_fail for async functions */
static SoupMessage error_message;
#define e2k_callback_return_if_fail(condition, invocation)		      \
	if (!(condition)) {						      \
		soup_message_set_error (&error_message, SOUP_ERROR_MALFORMED);\
		invocation;						      \
		g_log (G_LOG_DOMAIN,					      \
		       G_LOG_LEVEL_CRITICAL,				      \
		       "file %s: line %d (%s): assertion `%s' failed",	      \
		       __FILE__,					      \
		       __LINE__,					      \
		       G_GNUC_PRETTY_FUNCTION,				      \
		       #condition);					      \
		return;							      \
	}


/* GET */

static SoupMessage *
get_msg (E2kConnection *conn, const char *uri, gboolean owa, gboolean claim_ie)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new (conn, uri, "GET");
	if (!owa)
		soup_message_add_header (msg->request_headers, "Translate", "F");
	if (claim_ie) {
		soup_message_remove_header (msg->request_headers, "User-Agent");
		soup_message_add_header (msg->request_headers, "User-Agent",
					 "MSIE 6.0b (Windows NT 5.0; compatible; "
					 "Evolution/" EVOLUTION_VERSION ")");
	}

	return msg;
}

/**
 * e2k_connection_get:
 * @conn: the connection
 * @uri: URI of the object to GET
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a GET operation on @conn for @uri. @callback will be
 * asynchronously invoked with the result. (The message body can be
 * found in the %response field of the #SoupMessage passed to
 * @callback).
 **/
void
e2k_connection_get (E2kConnection *conn, const char *uri,
		    E2kSimpleCallback callback, gpointer user_data)
{
	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));

	e2k_connection_queue_message (conn, get_msg (conn, uri, FALSE, FALSE),
				      e2k_callback, callback, user_data);
}

/**
 * e2k_connection_get_sync:
 * @conn: the connection
 * @uri: URI of the object to GET
 * @body: on return, the response body
 * @length: on return, the response body length
 *
 * Synchronously performs a GET operation on @conn for @uri. If
 * successful, the body and length will be returned in @body and
 * @length. The body is not terminated by a '\0'. If not successful,
 * @body and @len will be untouched, even if there was a response
 * body.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_get_sync (E2kConnection *conn, const char *uri,
			 char **body, int *len)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);

	msg = get_msg (conn, uri, FALSE, FALSE);
	status = e2k_connection_send_message (conn, msg);

	if (SOUP_ERROR_IS_SUCCESSFUL (status)) {
		*body = msg->response.body;
		*len = msg->response.length;
		msg->response.owner = SOUP_BUFFER_USER_OWNED;
	}

	soup_message_free (msg);
	return status;
}

/**
 * e2k_connection_get_owa:
 * @conn: the connection
 * @uri: URI of the object to GET
 * @claim_ie: whether or not to claim to be IE
 * @callback: callback to return body to
 * @user_data: data for @callback
 *
 * As with e2k_connection_get(), but used when you need the data that
 * would be returned to OWA rather than the raw object data.
 **/
void
e2k_connection_get_owa (E2kConnection *conn, const char *uri,
			gboolean claim_ie,
			E2kSimpleCallback callback, gpointer user_data)
{
	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));

	e2k_connection_queue_message (conn, get_msg (conn, uri, TRUE, claim_ie),
				      e2k_callback, callback, user_data);
}

/**
 * e2k_connection_get_owa_sync:
 * @conn: the connection
 * @uri: URI of the object to GET
 * @claim_ie: whether or not to claim to be IE
 * @body: on return, the response body
 * @length: on return, the response body length
 *
 * As with e2k_connection_get_sync(), but used when you need the data
 * that would be returned to OWA rather than the raw object data.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_get_owa_sync (E2kConnection *conn, const char *uri,
			     gboolean claim_ie, char **body, int *len)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);

	msg = get_msg (conn, uri, TRUE, claim_ie);
	status = e2k_connection_send_message (conn, msg);

	if (SOUP_ERROR_IS_SUCCESSFUL (status)) {
		*body = msg->response.body;
		*len = msg->response.length;
		msg->response.owner = SOUP_BUFFER_USER_OWNED;
	}

	soup_message_free (msg);
	return status;
}

/* PUT */

static SoupMessage *
put_msg (E2kConnection *conn, const char *uri, const char *content_type,
	 SoupOwnership buffer_type, const char *body, int length)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (conn, uri, "PUT", content_type,
					 buffer_type, body, length);
	soup_message_add_header (msg->request_headers, "Translate", "f");

	/* In some cases (particularly involving combinations of
	 * slow/hosed workstation, fast/nearby server, and small
	 * message bodies), the "100 Continue" and final responses
	 * will be received by soup in the same packet, which it can't
	 * deal with. So we kludge by forcing the message down to
	 * HTTP/1.0 so Exchange won't send the "100 Continue".
	 * FIXME when soup doesn't suck any more.
	 */
	soup_message_set_http_version (msg, SOUP_HTTP_1_0);

	return msg;
}

/**
 * e2k_connection_put:
 * @conn: the connection
 * @uri: the URI to PUT to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a PUT operation on @conn for @uri. @callback will be
 * asynchronously invoked with the result.
 **/
void
e2k_connection_put (E2kConnection *conn, const char *uri,
		    const char *content_type, const char *body, int length,
		    E2kSimpleCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (content_type != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (body != NULL,
				     callback (conn, &error_message, user_data));

	msg = put_msg (conn, uri, content_type,
		       SOUP_BUFFER_SYSTEM_OWNED,
		       g_memdup (body, length), length);
	e2k_connection_queue_message (conn, msg, e2k_callback, callback, user_data);
}

/**
 * e2k_connection_put:
 * @conn: the connection
 * @uri: the URI to PUT to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 *
 * Synchronously performs a PUT operation on @conn for @uri.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_put_sync (E2kConnection *conn, const char *uri,
			 const char *content_type,
			 const char *body, int length)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (content_type != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (body != NULL, SOUP_ERROR_MALFORMED);

	msg = put_msg (conn, uri, content_type,
		       SOUP_BUFFER_USER_OWNED,
		       body, length);
	status = e2k_connection_send_message (conn, msg);

	soup_message_free (msg);
	return status;
}

/* APPEND - FIXME: remove this */

static SoupMessage *
append_msg (E2kConnection *conn, const char *folder_uri,
	    const char *object_name, int count, const char *content_type,
	    SoupOwnership buffer_type, const char *body, int length)
{
	SoupMessage *msg;
	char *uri;

	if (count) {
		uri = g_strdup_printf ("%s%s-%d.EML", folder_uri,
				       object_name, count);
	} else
		uri = g_strdup_printf ("%s%s.EML", folder_uri, object_name);

	msg = e2k_soup_message_new_full (conn, uri, "PUT", content_type,
					 buffer_type, body, length);
	g_free (uri);
	soup_message_add_header (msg->request_headers, "If-None-Match", "*");

	/* See put_msg() */
	soup_message_set_http_version (msg, SOUP_HTTP_1_0);

	return msg;
}

typedef struct {
	char *object_name, *content_type, *body;
	int length, count;
} E2kAppendData;

static void
tried_appending (SoupMessage *msg, gpointer user_data)
{
	E2kProgressiveCallbackData *callback_data = user_data;
	E2kAppendData *append_data = callback_data->caller_data;

	if (msg->errorcode == SOUP_ERROR_PRECONDITION_FAILED) {
		append_data->count++;
		msg = append_msg (callback_data->conn, callback_data->uri,
				  append_data->object_name, append_data->count,
				  append_data->content_type,
				  SOUP_BUFFER_USER_OWNED, append_data->body,
				  append_data->length);
		e2k_connection_requeue_progressive_message (callback_data, msg);
		return;
	}

	callback_data->done_callback (callback_data->conn, msg,
				      callback_data->user_data);
	g_free (append_data->object_name);
	g_free (append_data->content_type);
	g_free (append_data->body);
	g_free (append_data);
	e2k_connection_free_progressive_data (callback_data);
}

/**
 * e2k_connection_append:
 * @conn: the connection
 * @folder_uri: the URI of the folder to PUT into
 * @object_name: base name of the new object
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @callback: async callback
 * @user_data: data for @callback
 *
 * PUTs data into @folder_uri on @conn with a new name based on
 * @object_name. DEPRECATED; code that currently uses this should be
 * taking advantage of the fact that it already has a summary listing
 * of the folder, and using that to generate the correct name on the
 * first try rather than causing a series of "412 Precondition Failed"
 * as it tries various names.
 **/
void
e2k_connection_append (E2kConnection *conn, const char *folder_uri,
		       const char *object_name, const char *content_type,
		       const char *body, int length,
		       E2kSimpleCallback callback, gpointer user_data)
{
	E2kAppendData *append_data;
	char *slash_folder_uri;
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (folder_uri != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (object_name != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (content_type != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (body != NULL,
				     callback (conn, &error_message, user_data));

	append_data = g_new (E2kAppendData, 1);
	append_data->object_name = e2k_uri_encode (object_name, NULL);
	append_data->count = 0;

	append_data->body = g_memdup (body, length);
	append_data->length = length;
	append_data->content_type = g_strdup (content_type);

	slash_folder_uri = e2k_strdup_with_trailing_slash (folder_uri);
	msg = append_msg (conn, slash_folder_uri, append_data->object_name,
			  append_data->count, append_data->content_type,
			  SOUP_BUFFER_USER_OWNED, append_data->body,
			  append_data->length);
	e2k_connection_queue_progressive_message (conn, msg, slash_folder_uri,
						  tried_appending, append_data,
						  NULL, callback, user_data);
}

/* POST */

static SoupMessage *
post_msg (E2kConnection *conn, const char *uri, const char *content_type,
	  SoupOwnership buffer_type, const char *body, int length)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (conn, uri, "POST", content_type,
					 buffer_type, body, length);
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	/* See put_msg() */
	soup_message_set_http_version (msg, SOUP_HTTP_1_0);

	return msg;
}

/**
 * e2k_connection_post:
 * @conn: the connection
 * @uri: the URI to POST to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a POST operation on @conn for @uri. @callback will be
 * asynchronously invoked with the result.
 *
 * Note that POSTed objects will be irrevocably(?) marked as "unsent",
 * If you open a POSTed message in Outlook, it will open in the composer
 * rather than in the message viewer.
 **/
void
e2k_connection_post (E2kConnection *conn, const char *uri,
		     const char *content_type, const char *body, int length,
		     E2kSimpleCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (content_type != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (body != NULL,
				     callback (conn, &error_message, user_data));

	msg = post_msg (conn, uri, content_type,
			SOUP_BUFFER_SYSTEM_OWNED,
			g_memdup (body, length), length);
	e2k_connection_queue_message (conn, msg, e2k_callback, callback, user_data);
}

/**
 * e2k_connection_post_sync:
 * @conn: the connection
 * @uri: the URI to POST to
 * @content_type: MIME Content-Type of the data
 * @body: data to PUT
 * @length: length of @body
 *
 * As with e2k_connection_post(), but synchronous.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_post_sync (E2kConnection *conn, const char *uri,
			  const char *content_type,
			  const char *body, int length)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (content_type != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (body != NULL, SOUP_ERROR_MALFORMED);

	msg = post_msg (conn, uri, content_type,
			SOUP_BUFFER_USER_OWNED,
			body, length);

	status = e2k_connection_send_message (conn, msg);

	soup_message_free (msg);
	return status;
}

/* PROPPATCH */

static void
add_namespaces (const char *namespace, char abbrev, gpointer user_data)
{
	GString *propxml = user_data;

	g_string_append_printf (propxml, " xmlns:%c=\"%s\"", abbrev, namespace);
}

static void
write_prop (GString *xml, const char *propertyname,
	    E2kPropType type, gpointer value, gboolean set)
{
	const char *namespace, *name, *typestr;
	char *encoded, abbrev;
	gboolean b64enc, need_type;
	GByteArray *data;
	GPtrArray *array;
	int i;

	namespace = e2k_prop_namespace_name (propertyname);
	abbrev = e2k_prop_namespace_abbrev (propertyname);
	name = e2k_prop_property_name (propertyname);

	need_type = (strstr (namespace, "/mapi/id/") != NULL);

	g_string_append_printf (xml, "<%c:%s", abbrev, name);
	if (!set) {
		g_string_append (xml, "/>");
		return;
	} else if (!need_type)
		g_string_append_c (xml, '>');

	switch (type) {
	case E2K_PROP_TYPE_BINARY:
		if (need_type)
			g_string_append (xml, " T:dt=\"bin.base64\">");
		data = value;
		encoded = e2k_base64_encode (data->data, data->len);
		g_string_append (xml, encoded);
		g_free (encoded);
		break;

	case E2K_PROP_TYPE_STRING_ARRAY:
		typestr = " T:dt=\"mv.string\">";
		b64enc = FALSE;
		goto array_common;

	case E2K_PROP_TYPE_INT_ARRAY:
		typestr = " T:dt=\"mv.int\">";
		b64enc = FALSE;
		goto array_common;

	case E2K_PROP_TYPE_BINARY_ARRAY:
		typestr = " T:dt=\"mv.bin.base64\">";
		b64enc = TRUE;

	array_common:
		if (need_type)
			g_string_append (xml, typestr);
		array = value;
		for (i = 0; i < array->len; i++) {
			g_string_append (xml, "<X:v>");

			if (b64enc) {
				data = array->pdata[i];
				encoded = e2k_base64_encode (data->data,
							     data->len);
				g_string_append (xml, encoded);
				g_free (encoded);
			} else
				e2k_g_string_append_xml_escaped (xml, array->pdata[i]);

			g_string_append (xml, "</X:v>");
		}
		break;

	case E2K_PROP_TYPE_XML:
		g_assert_not_reached ();
		break;

	case E2K_PROP_TYPE_STRING:
	default:
		if (need_type) {
			switch (type) {
			case E2K_PROP_TYPE_INT:
				typestr = " T:dt=\"int\">";
				break;
			case E2K_PROP_TYPE_BOOL:
				typestr = " T:dt=\"boolean\">";
				break;
			case E2K_PROP_TYPE_FLOAT:
				typestr = " T:dt=\"float\">";
				break;
			case E2K_PROP_TYPE_DATE:
				typestr = " T:dt=\"dateTime.tz\">";
				break;
			default:
				typestr = ">";
				break;
			}
			g_string_append (xml, typestr);
		}
		e2k_g_string_append_xml_escaped (xml, value);
		break;

	}

	g_string_append_printf (xml, "</%c:%s>", abbrev, name);
}

static void
add_set_props (const char *propertyname, E2kPropType type,
	       gpointer value, gpointer user_data)
{
	GString **props = user_data;

	if (!*props)
		*props = g_string_new (NULL);

	write_prop (*props, propertyname, type, value, TRUE);
}

static void
add_remove_props (const char *propertyname, E2kPropType type,
		  gpointer value, gpointer user_data)
{
	GString **props = user_data;

	if (!*props)
		*props = g_string_new (NULL);

	write_prop (*props, propertyname, type, value, FALSE);
}

static SoupMessage *
patch_msg (E2kConnection *conn, const char *uri, const char *method,
	   const char **hrefs, int nhrefs, E2kProperties *props,
	   gboolean create)
{
	SoupMessage *msg;
	GString *propxml, *subxml;
	int i;

	propxml = g_string_new (E2K_XML_HEADER);
	g_string_append (propxml, "<D:propertyupdate xmlns:D=\"DAV:\"");

	/* Iterate over the properties, noting each namespace once,
	 * then add them all to the header.
	 */
	e2k_properties_foreach_namespace (props, add_namespaces, propxml);
	g_string_append (propxml, ">\r\n");

	/* If this is a BPROPPATCH, add the <target> section. */
	if (hrefs) {
		g_string_append (propxml, "<D:target>\r\n");
		for (i = 0; i < nhrefs; i++) {
			g_string_append_printf (propxml, "<D:href>%s</D:href>",
						hrefs[i]);
		}
		g_string_append (propxml, "\r\n</D:target>\r\n");
	}

	/* Add <set> properties. */
	subxml = NULL;
	e2k_properties_foreach (props, add_set_props, &subxml);
	if (subxml) {
		g_string_append (propxml, "<D:set><D:prop>\r\n");
		g_string_append (propxml, subxml->str);
		g_string_append (propxml, "\r\n</D:prop></D:set>");
		g_string_free (subxml, TRUE);
	}

	/* Add <remove> properties. */
	subxml = NULL;
	e2k_properties_foreach_removed (props, add_remove_props, &subxml);
	if (subxml) {
		g_string_append (propxml, "<D:remove><D:prop>\r\n");
		g_string_append (propxml, subxml->str);
		g_string_append (propxml, "\r\n</D:prop></D:remove>");
		g_string_free (subxml, TRUE);
	}

	/* Finish it up */
	g_string_append (propxml, "\r\n</D:propertyupdate>");

	/* And build the message. */
	msg = e2k_soup_message_new_full (conn, uri, method,
					 "text/xml", SOUP_BUFFER_SYSTEM_OWNED,
					 propxml->str, propxml->len);
	g_string_free (propxml, FALSE);
	soup_message_add_header (msg->request_headers, "Brief", "t");
	if (!create)
		soup_message_add_header (msg->request_headers, "If-Match", "*");

	return msg;
}

/**
 * e2k_connection_proppatch:
 * @conn: the connection
 * @uri: the URI to PROPPATCH
 * @props: the properties to set/remove
 * @create: whether or not to create @uri if it does not exist
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a PROPPATCH operation on @conn for @uri. @callback will be
 * asynchronously invoked with the result.
 *
 * If @create is %FALSE and @uri does not already exist, the response
 * code will be %SOUP_ERROR_PRECONDITION_FAILED.
 **/
void
e2k_connection_proppatch (E2kConnection *conn, const char *uri,
			  E2kProperties *props, gboolean create,
			  E2kSimpleCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (props != NULL,
				     callback (conn, &error_message, user_data));

	msg = patch_msg (conn, uri, "PROPPATCH", NULL, 0, props, create);
	e2k_connection_queue_message (conn, msg, e2k_callback,
				      callback, user_data);
}

/**
 * e2k_connection_proppatch_sync:
 * @conn: the connection
 * @uri: the URI to PROPPATCH
 * @props: the properties to set/remove
 * @create: whether or not to create @uri if it does not exist
 *
 * Synchronously PROPPATCHes @uri.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_proppatch_sync (E2kConnection *conn, const char *uri,
			       E2kProperties *props, gboolean create)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (props != NULL, SOUP_ERROR_MALFORMED);

	msg = patch_msg (conn, uri, "PROPPATCH", NULL, 0, props, create);
	status = e2k_connection_send_message (conn, msg);

	soup_message_free (msg);
	return status;
}

/**
 * e2k_connection_bproppatch:
 * @conn: the connection
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri
 * @nhrefs: length of @hrefs
 * @props: the properties to set/remove
 * @create: whether or not to create @uri if it does not exist
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a BPROPPATCH (bulk PROPPATCH) operation on @conn for @hrefs.
 * This is like e2k_conntion_proppatch except that it will attempt to
 * patch every URI in @hrefs. This is also used in some cases where
 * Exchange mysteriously does not always allow a PROPPATCH (#29726).
 *
 * The results are returned to @callback per-href, in an array of
 * #E2kResult
 **/
void
e2k_connection_bproppatch (E2kConnection *conn, const char *uri,
			   const char **hrefs, int nhrefs,
			   E2kProperties *props, gboolean create,
			   E2kResultsCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (props != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));

	msg = patch_msg (conn, uri, "BPROPPATCH", hrefs, nhrefs, props, create);
	e2k_connection_queue_message (conn, msg, e2k_results_callback,
				      callback, user_data);
}

/**
 * e2k_connection_bproppatch_sync:
 * @conn: the connection
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri
 * @nhrefs: length of @hrefs
 * @props: the properties to set/remove
 * @create: whether or not to create @uri if it does not exist
 * @results: on return, the results of patching the various hrefs
 * @nresults: length of @results
 *
 * Synchronously BPROPPATCHes @uri. If successful, the results are
 * returned in @results, which you must free with e2k_results_free().
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_bproppatch_sync (E2kConnection *conn, const char *uri,
				const char **hrefs, int nhrefs,
				E2kProperties *props, gboolean create,
				E2kResult **results, int *nresults)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (props != NULL, SOUP_ERROR_MALFORMED);

	msg = patch_msg (conn, uri, "BPROPPATCH", hrefs, nhrefs, props, create);
	status = e2k_connection_send_message (conn, msg);

	if (status == SOUP_ERROR_DAV_MULTISTATUS)
		e2k_results_from_multistatus (msg, results, nresults);
	soup_message_free (msg);
	return status;
}

/* PROPFIND */

static SoupMessage *
propfind_msg (E2kConnection *conn, const char *base_uri, const char *depth, 
	      const char **props, int nprops, const char **hrefs, int nhrefs)
{
	SoupMessage *msg;
	GString *propxml;
	GData *set_namespaces;
	const char *name;
	char abbrev;
	int i;

	propxml = g_string_new (E2K_XML_HEADER);
	g_string_append (propxml, "<D:propfind xmlns:D=\"DAV:\"");

	set_namespaces = NULL;
	for (i = 0; i < nprops; i++) {
		name = e2k_prop_namespace_name (props[i]);
		abbrev = e2k_prop_namespace_abbrev (props[i]);

		if (!g_datalist_get_data (&set_namespaces, name)) {
			g_datalist_set_data (&set_namespaces, name,
					     GINT_TO_POINTER (1));
			g_string_append_printf (propxml, " xmlns:%c=\"%s\"",
						abbrev, name);
		}
	}
	g_datalist_clear (&set_namespaces);
	g_string_append (propxml, ">\r\n");

	if (hrefs) {
		g_string_append (propxml, "<D:target>\r\n");
		for (i = 0; i < nhrefs; i++) {
			g_string_append_printf (propxml, "<D:href>%s</D:href>",
						hrefs[i]);
		}
		g_string_append (propxml, "\r\n</D:target>\r\n");
	}

	g_string_append (propxml, "<D:prop>\r\n");
	for (i = 0; i < nprops; i++) {
		abbrev = e2k_prop_namespace_abbrev (props[i]);
		name = e2k_prop_property_name (props[i]);
		g_string_append_printf (propxml, "<%c:%s/>", abbrev, name);
	}
	g_string_append (propxml, "\r\n</D:prop>\r\n</D:propfind>");

	msg = e2k_soup_message_new_full (conn, base_uri, 
					 hrefs ? "BPROPFIND" : "PROPFIND",
					 "text/xml", SOUP_BUFFER_SYSTEM_OWNED,
					 propxml->str, propxml->len);
	g_string_free (propxml, FALSE);
	soup_message_add_header (msg->request_headers, "Brief", "t");
	soup_message_add_header (msg->request_headers, "Depth", depth);

	return msg;
}

/**
 * e2k_connection_propfind:
 * @conn: the connection
 * @uri: the URI to PROPFIND on
 * @depth: PROPFIND Depth, see below
 * @props: array of properties to find
 * @nprops: length of @props
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a PROPFIND operation on @conn for @uri. @depth is normally
 * "0", meaning to find properties on only @uri, but can be any of the
 * values allowed for the "Depth" header.
 *
 * The results are returned to @callback in an array of #E2kResult for
 * historical reasons, but the array will always have 0 or 1 members.
 **/
void
e2k_connection_propfind (E2kConnection *conn, const char *uri,
			 const char *depth, const char **props, int nprops,
			 E2kResultsCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (depth != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (props != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));

	msg = propfind_msg (conn, uri, depth, props, nprops, NULL, 0);
	e2k_connection_queue_message (conn, msg, e2k_results_callback,
				      callback, user_data);
}

/**
 * e2k_connection_propfind_sync:
 * @conn: the connection
 * @uri: the URI to PROPFIND on
 * @depth: PROPFIND Depth
 * @props: array of properties to find
 * @nprops: length of @props
 * @results: on return, the results
 * @nresults: length of @results
 *
 * Synchronously performs a PROPFIND operation on @conn for @uri. If
 * successful, the results are returned as an array of #E2kResult
 * (which you must free with e2k_results_free()), but the array will
 * always have either 0 or 1 members.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_propfind_sync (E2kConnection *conn, const char *uri,
			      const char *depth,
			      const char **props, int nprops,
			      E2kResult **results, int *nresults)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (depth != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (props != NULL, SOUP_ERROR_MALFORMED);

	msg = propfind_msg (conn, uri, depth, props, nprops, NULL, 0);
	status = e2k_connection_send_message (conn, msg);

	if (msg->errorcode == SOUP_ERROR_DAV_MULTISTATUS)
		e2k_results_from_multistatus (msg, results, nresults);
	soup_message_free (msg);
	return status;
}

static void
free_msg (gpointer msg, gpointer data)
{
	soup_message_free (msg);
}

static void
e2k_bpropfinding (SoupMessage *msg, gpointer user_data)
{
	E2kCumulativeCallbackData *data = user_data;
	GSList *msgs = data->caller_data;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		data->callback (data->conn, msg, NULL, 0, data->user_data);
		goto done;
	}

	e2k_results_array_add_from_multistatus (data->results_array, msg);

	if (msgs) {
		msg = msgs->data;
		data->caller_data = g_slist_delete_link (msgs, msgs);

		e2k_connection_requeue_cumulative_message (data, msg);
		return;
	}

	data->callback (data->conn, msg, 
			(E2kResult *)data->results_array->data,
			data->results_array->len,
			data->user_data);

 done:
	g_slist_foreach (msgs, free_msg, NULL);
	g_slist_free (msgs);
	e2k_connection_free_cumulative_data (data);
}

/**
 * e2k_connection_bpropfind:
 * @conn: the connection
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri
 * @nhrefs: length of @hrefs
 * @depth: PROPFIND Depth
 * @props: array of properties to find
 * @nprops: length of @props
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a BPROPFIND (bulk PROPFIND) operation on @conn for @hrefs.
 * The results are returned to @callback per-href, in an array of
 * #E2kResult.
 **/
void
e2k_connection_bpropfind (E2kConnection *conn, const char *uri, 
			  const char **hrefs, int nhrefs, const char *depth,
			  const char **props, int nprops,
			  E2kResultsCallback callback, gpointer user_data)
{
	SoupMessage *msg;
	GSList *msgs;
	int i;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (depth != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (props != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (hrefs != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));

	msgs = NULL;
	for (i = 0; i < nhrefs; i += E2K_CONNECTION_MAX_BATCH_SIZE) {
		msg = propfind_msg (conn, uri, depth, props, nprops,
				    hrefs + i, MIN (E2K_CONNECTION_MAX_BATCH_SIZE, nhrefs - i));
		msgs = g_slist_prepend (msgs, msg);
	}

	msgs = g_slist_reverse (msgs);
	msg = msgs->data;
	msgs = g_slist_delete_link (msgs, msgs);

	e2k_connection_queue_cumulative_message (conn, msg, uri,
						 e2k_bpropfinding, msgs,
						 callback, user_data);
}

/**
 * e2k_connection_bpropfind_sync:
 * @conn: the connection
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri
 * @nhrefs: length of @hrefs
 * @depth: PROPFIND Depth
 * @props: array of properties to find
 * @nprops: length of @props
 * @results: on return, the results
 * @nresults: length of @results
 *
 * Synchronously performs a BPROPFIND (bulk PROPFIND) operation on
 * @conn for @hrefs. If successful, the results are returned as an
 * array of #E2kResult, which you must free with e2k_results_free().
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_bpropfind_sync (E2kConnection *conn, const char *uri,
			       const char **hrefs, int nhrefs,
			       const char *depth,
			       const char **props, int nprops,
			       E2kResult **results, int *nresults)
{
	GArray *results_array;
	SoupMessage *msg;
	int i, status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (depth != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (props != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (hrefs != NULL, SOUP_ERROR_MALFORMED);

	results_array = e2k_results_array_new ();
	for (i = 0; i < nhrefs; i += E2K_CONNECTION_MAX_BATCH_SIZE) {
		msg = propfind_msg (conn, uri, depth, props, nprops,
				    hrefs + i, MIN (E2K_CONNECTION_MAX_BATCH_SIZE, nhrefs - i));
		status = e2k_connection_send_message (conn, msg);
		if (!SOUP_ERROR_IS_SUCCESSFUL (status))
			break;

		e2k_results_array_add_from_multistatus (results_array, msg);
		soup_message_free (msg);
	}

	if (SOUP_ERROR_IS_SUCCESSFUL (status)) {
		*results = (E2kResult *)results_array->data;
		*nresults = results_array->len;
		e2k_results_array_free (results_array, FALSE);
	} else {	
		*results = NULL;
		*nresults = 0;
		e2k_results_array_free (results_array, TRUE);
	}

	return status;
}

/* SEARCH */

static SoupMessage *
search_msg (E2kConnection *conn, const char *uri,
	    SoupOwnership buffer_type, const char *searchxml,
	    int size, gboolean ascending, int offset)
{
	SoupMessage *msg;

	msg = e2k_soup_message_new_full (conn, uri, "SEARCH", "text/xml",
					 buffer_type, searchxml,
					 strlen (searchxml));
	soup_message_add_header (msg->request_headers, "Brief", "t");

	if (size) {
		char *range;

		if (offset == INT_MAX) {
			range = g_strdup_printf ("rows=-%u", size);
		} else {
			range = g_strdup_printf ("rows=%u-%u",
						 offset, offset + size - 1);
		}
		soup_message_add_header (msg->request_headers, "Range", range);
		g_free (range);
	}

	return msg;
}

static char *
search_xml (const char **props, int nprops,
	    gboolean folders_only, E2kRestriction *rn,
	    const char *orderby)
{
	GString *xml;
	char *ret, *where;
	int i;

	xml = g_string_new (E2K_XML_HEADER);
	g_string_append (xml, "<searchrequest xmlns=\"DAV:\"><sql>\r\n");
	g_string_append (xml, "SELECT ");

	for (i = 0; i < nprops; i++) {
		if (i > 0)
			g_string_append (xml, ", ");
		g_string_append_c (xml, '"');
		g_string_append   (xml, props[i]);
		g_string_append_c (xml, '"');
	}

	if (folders_only)
		g_string_append_printf (xml, "\r\nFROM SCOPE('hierarchical traversal of \"\"')\r\n");
	else
		g_string_append (xml, "\r\nFROM \"\"\r\n");

	if (rn) {
		where = e2k_restriction_to_sql (rn);
		if (where) {
			e2k_g_string_append_xml_escaped (xml, where);
			g_string_append (xml, "\r\n");
			g_free (where);
		}
	}

	if (orderby)
		g_string_append_printf (xml, "ORDER BY \"%s\"\r\n", orderby);

	g_string_append (xml, "</sql></searchrequest>");

	ret = xml->str;
	g_string_free (xml, FALSE);

	return ret;
}

static gboolean
search_result_get_range (SoupMessage *msg, int *first, int *total)
{
	const char *range, *p;

	range = soup_message_get_header (msg->response_headers,
					 "Content-Range");
	if (!range)
		return FALSE;
	p = strstr (range, "rows ");
	if (!p)
		return FALSE;

	if (first)
		*first = atoi (p + 5);

	if (total) {
		p = strstr (range, "total=");
		if (p)
			*total = atoi (p + 6);
		else
			*total = -1;
	}

	return TRUE;
}

typedef struct {
	char *xml;
	guint search_size;
	gboolean ascending;
} E2kSearchProgressData;

static void
e2k_progress_searching (SoupMessage *msg, gpointer user_data)
{
	E2kProgressiveCallbackData *callback_data = user_data;
	E2kSearchProgressData *search_data = callback_data->caller_data;
	E2kResult *results = NULL;
	int nresults = 0, first = 0, total = 0, next;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		if (msg->errorcode == SOUP_ERROR_INVALID_RANGE)
			soup_message_set_error (msg, SOUP_ERROR_DAV_MULTISTATUS);
		goto cleanup;
	}

	search_result_get_range (msg, &first, &total);
	if (total == 0)
		goto cleanup;

	e2k_results_from_multistatus (msg, &results, &nresults);
	if (total == -1)
		total = first + nresults;

	search_data->search_size =
		callback_data->progress_callback (callback_data->conn, msg,
						  results, nresults,
						  first, total,
						  callback_data->user_data);
	e2k_results_free (results, nresults);

	if (search_data->search_size) {
		if (search_data->ascending && first + nresults < total)
			next = first + nresults;
		else if (!search_data->ascending && first > 0) {
			if (first >= search_data->search_size)
				next = first - search_data->search_size;
			else {
				search_data->search_size = first;
				next = 0;
			}
		} else
			goto cleanup;

		msg = search_msg (callback_data->conn,
				  callback_data->uri,
				  SOUP_BUFFER_USER_OWNED,
				  search_data->xml,
				  search_data->search_size,
				  search_data->ascending,
				  next);
		e2k_connection_requeue_progressive_message (callback_data, msg);
		return;
	}

 cleanup:
	callback_data->done_callback (callback_data->conn, msg,
				      callback_data->user_data);

	g_free (search_data->xml);
	g_free (search_data);
	e2k_connection_free_progressive_data (callback_data);
}

/**
 * e2k_connection_search_with_progress:
 * @conn: the connection
 * @uri: the folder to search
 * @props: the properties to search for
 * @nprops: size of @props array
 * @rn: the search restriction
 * @orderby: if non-%NULL, the field to sort the search results by
 * @increment_size: the maximum number of results to return at a time
 * @ascending: %TRUE for an ascending search, %FALSE for descending.
 * @progress_callback: callback to invoke with received data
 * @done_callback: callback to invoke when the search is done
 * @user_data: data to be passed to callbacks.
 *
 * Queues a SEARCH operation on @conn for @uri, like
 * e2k_connection_search(), but returns the results in increments of
 * @increment_size (subject to a certain minimum increment size). This
 * lets the caller provide feedback to the UI as data is received.
 *
 * Specifying %FALSE for @ascending will make it start from the end of
 * the folder (the results array passed to the callback will still be
 * in ascending order, but the first invocation of the callback will
 * be with the last @increment_size results, etc). This is preferable
 * to using "ORDER BY ... DESC" in the SEARCH query because it won't
 * end up skipping or duplicating messages if messages are added to or
 * deleted from the folder during the course of the search.
 *
 * @progress_callback will be invoked one or more times as data is
 * received. The @first argument to @callback will tell the index of
 * the first returned result, relative to the complete set of results.
 * The @total argument will tell the total number of results. (Note
 * that this could potentially increase or decrease during between
 * rounds.) After all the data has been returned (or an error occurs),
 * @done_callback will be invoked.
 **/
void
e2k_connection_search_with_progress (E2kConnection *conn, const char *uri, 
				     const char **props, int nprops,
				     E2kRestriction *rn, const char *orderby,
				     int increment_size, gboolean ascending,
				     E2kProgressCallback progress_callback,
				     E2kSimpleCallback done_callback,
				     gpointer user_data)
{
	E2kSearchProgressData *search_data;
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     done_callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     done_callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (props != NULL,
				     done_callback (conn, &error_message, user_data));

	if (increment_size < E2K_CONNECTION_MIN_BATCH_SIZE)
		increment_size = E2K_CONNECTION_MIN_BATCH_SIZE;

	search_data = g_new (E2kSearchProgressData, 1);
	search_data->xml = search_xml (props, nprops, FALSE, rn, orderby);
	search_data->search_size = increment_size;
	search_data->ascending = ascending;

	msg = search_msg (conn, uri, SOUP_BUFFER_USER_OWNED,
			  search_data->xml, increment_size,
			  ascending, ascending ? 0 : INT_MAX);
	e2k_connection_queue_progressive_message (conn, msg, uri,
						  e2k_progress_searching,
						  search_data,
						  progress_callback,
						  done_callback, user_data);
}

static void
e2k_searching (SoupMessage *msg, gpointer user_data)
{
	E2kCumulativeCallbackData *data = user_data;
	int total;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		data->callback (data->conn, msg, NULL, 0, data->user_data);
		goto done;
	}

	e2k_results_array_add_from_multistatus (data->results_array, msg);
	search_result_get_range (msg, NULL, &total);

	if (data->results_array->len < total) {
		msg = search_msg (data->conn, data->uri,
				  SOUP_BUFFER_USER_OWNED, data->caller_data,
				  E2K_CONNECTION_MAX_BATCH_SIZE, TRUE,
				  data->results_array->len);
		e2k_connection_requeue_cumulative_message (data, msg);
		return;
	}

	data->callback (data->conn, msg, 
			(E2kResult *)data->results_array->data,
			data->results_array->len,
			data->user_data);

 done:
	g_free (data->caller_data);
	e2k_connection_free_cumulative_data (data);
}

/**
 * e2k_connection_search:
 * @conn: the connection
 * @uri: the folder to search
 * @props: the properties to search for
 * @nprops: size of @props array
 * @folders_only: if %TRUE, only folders are returned
 * @rn: the search restriction
 * @orderby: if non-%NULL, the field to sort the search results by
 * @callback: callback to invoke when the search is done
 * @user_data: data to be passed to callbacks.
 *
 * Queues a SEARCH operation on @conn for @uri and returns the results
 * all at once.
 *
 * If you only want folders in the result, specify %TRUE for
 * @folders_only. If you only want non-folders, add an appropriate
 * restriction to @rn. (FIXME: the callers shouldn't need to know
 * about this optimization. We should set @folders_only automatically
 * if @rn restrict to just folders.)
 **/
void
e2k_connection_search (E2kConnection *conn, const char *uri, 
		       const char **props, int nprops,
		       gboolean folders_only, E2kRestriction *rn,
		       const char *orderby,
		       E2kResultsCallback callback, gpointer user_data)
{
	SoupMessage *msg;
	char *xml;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (props != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));

	xml = search_xml (props, nprops, folders_only, rn, orderby);
	msg = search_msg (conn, uri, SOUP_BUFFER_USER_OWNED, xml,
			  E2K_CONNECTION_MAX_BATCH_SIZE, TRUE, 0);

	e2k_connection_queue_cumulative_message (conn, msg, uri,
						 e2k_searching, xml,
						 callback, user_data);
}

/**
 * e2k_connection_search_sync:
 * @conn: the connection
 * @uri: the folder to search
 * @props: the properties to search for
 * @nprops: size of @props array
 * @folders_only: if %TRUE, only folders are returned
 * @rn: the search restriction
 * @orderby: if non-%NULL, the field to sort the search results by
 * @results: on return, the results
 * @nresults: length of @results
 *
 * Synchronously performs a SEARCH on @conn for @uri. If successful,
 * the results are returned as an array of #E2kResult, which you must
 * free with e2k_results_free().
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_search_sync (E2kConnection *conn, const char *uri, 
			    const char **props, int nprops,
			    gboolean folders_only, E2kRestriction *rn,
			    const char *orderby,
			    E2kResult **results, int *nresults)
{
	SoupMessage *msg;
	int status, total = INT_MAX;
	GArray *results_array;
	char *xml;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (props != NULL, SOUP_ERROR_MALFORMED);

	results_array = e2k_results_array_new ();
	xml = search_xml (props, nprops, folders_only, rn, orderby);

	while (results_array->len < total) {
		msg = search_msg (conn, uri, SOUP_BUFFER_USER_OWNED, xml,
				  MIN (E2K_CONNECTION_MAX_BATCH_SIZE, total - results_array->len),
				  TRUE, results_array->len);
		status = e2k_connection_send_message (conn, msg);
		if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS)
			break;

		search_result_get_range (msg, NULL, &total);
		e2k_results_array_add_from_multistatus (results_array, msg);
		soup_message_free (msg);
	}

	g_free (xml);
	if (SOUP_ERROR_IS_SUCCESSFUL (status)) {
		*results = (E2kResult *)results_array->data;
		*nresults = results_array->len;
		e2k_results_array_free (results_array, FALSE);
	} else {
		*results = NULL;
		*nresults = 0;
		e2k_results_array_free (results_array, TRUE);
	}

	return status;
}

/* DELETE */

static SoupMessage *
delete_msg (E2kConnection *conn, const char *uri)
{
	return e2k_soup_message_new (conn, uri, "DELETE");
}

/**
 * e2k_connection_delete:
 * @conn: the connection
 * @uri: URI to DELETE
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a DELETE operation on @conn for @uri, which may be a folder
 * or an object.
 **/
void
e2k_connection_delete (E2kConnection *conn, const char *uri,
		       E2kSimpleCallback callback, gpointer user_data)
{
	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));

	e2k_connection_queue_message (conn, delete_msg (conn, uri),
				      e2k_callback, callback, user_data);
}

/**
 * e2k_connection_delete_sync:
 * @conn: the connection
 * @uri: URI to DELETE
 *
 * Synchronously performs a DELETE operation on @conn for @uri.
 *
 * Return value: the HTTP status
 **/
int
e2k_connection_delete_sync (E2kConnection *conn, const char *uri)
{
	SoupMessage *msg;
	int status;

	g_return_val_if_fail (E2K_IS_CONNECTION (conn), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (uri != NULL, SOUP_ERROR_MALFORMED);

	msg = delete_msg (conn, uri);
	status = e2k_connection_send_message (conn, msg);

	soup_message_free (msg);
	return status;
}

/* BDELETE */

typedef struct {
	GPtrArray *batches;
	int sofar, total;
} E2kBDeleteData;

static SoupMessage *
bdelete_msg (E2kConnection *conn, const char *uri, GPtrArray *batches)
{
	SoupMessage *msg;
	char *xml;

	xml = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
			       "<delete xmlns=\"DAV:\"><target>%s"
			       "</target></delete>",
			       (char *)batches->pdata[0]);
	g_free (batches->pdata[0]);
	g_ptr_array_remove_index (batches, 0);

	msg = e2k_soup_message_new_full (conn, uri, "BDELETE", "text/xml",
					 SOUP_BUFFER_SYSTEM_OWNED,
					 xml, strlen (xml));

	return msg;
}

static void
e2k_bdeleting (SoupMessage *msg, gpointer user_data)
{
	E2kProgressiveCallbackData *callback_data = user_data;
	E2kBDeleteData *bdelete_data = callback_data->caller_data;
	E2kResult *results = NULL;
	int nresults = 0, i;

	if (msg->errorcode == SOUP_ERROR_DAV_MULTISTATUS)
		e2k_results_from_multistatus (msg, &results, &nresults);

	if (nresults) {
		callback_data->progress_callback (callback_data->conn, msg,
						  results, nresults,
						  bdelete_data->sofar,
						  bdelete_data->total,
						  callback_data->user_data);
		e2k_results_free (results, nresults);
		bdelete_data->sofar += nresults;
	}

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode) && bdelete_data->batches->len) {
		msg = bdelete_msg (callback_data->conn, callback_data->uri,
				   bdelete_data->batches);
		e2k_connection_requeue_progressive_message (callback_data, msg);
		return;
	}

	callback_data->done_callback (callback_data->conn, msg,
				      callback_data->user_data);

	for (i = 0; i < bdelete_data->batches->len; i++)
		g_free (bdelete_data->batches->pdata[i]);
	g_ptr_array_free (bdelete_data->batches, TRUE);
	g_free (bdelete_data);
	e2k_connection_free_progressive_data (callback_data);
}

/**
 * e2k_connection_bdelete:
 * @conn: the connection
 * @uri: the base URI
 * @hrefs: array of URIs, possibly relative to @uri, to delete
 * @nhrefs: length of @hrefs
 * @progress_callback: callback to call as objects are deleted
 * @don_callback: callback to call when the operation is complete
 * @user_data: data for @callback
 *
 * Queues a BDELETE (bulk DELETE) operation on @conn for @hrefs.
 * @progress_callback will be called with the URIs of deleted objects
 * as the operation progresses (at least once if any object is
 * deleted, even if all of the objects are deleted at once).
 * @done_callback will be called when the operation has finished
 * (successfully or not).
 **/
void
e2k_connection_bdelete (E2kConnection *conn, const char *uri,
			const char **hrefs, int nhrefs,
			E2kProgressCallback progress_callback,
			E2kSimpleCallback done_callback, gpointer user_data)
{
	E2kBDeleteData *bdelete_data;
	GString *href_string;
	int href, i, batchsize;
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     done_callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     done_callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (hrefs != NULL,
				     done_callback (conn, &error_message, user_data));

	batchsize = (nhrefs + 9) / 10;
	if (batchsize < E2K_CONNECTION_MIN_BATCH_SIZE)
		batchsize = E2K_CONNECTION_MIN_BATCH_SIZE;

	bdelete_data = g_new0 (E2kBDeleteData, 1);
	bdelete_data->batches = g_ptr_array_new ();
	for (href = 0; href < nhrefs; ) {
		href_string = g_string_new (NULL);
		for (i = 0; href < nhrefs && i < batchsize; href++, i++) {
			g_string_append_printf (href_string, "<href>%s</href>",
						hrefs[href]);
		}
		g_ptr_array_add (bdelete_data->batches, href_string->str);
		g_string_free (href_string, FALSE);
	}
	bdelete_data->total = nhrefs;

	msg = bdelete_msg (conn, uri, bdelete_data->batches);
	e2k_connection_queue_progressive_message (conn, msg, uri,
						  e2k_bdeleting, bdelete_data,
						  progress_callback,
						  done_callback, user_data);
}

/* MKCOL */

/**
 * e2k_connection_mkcol:
 * @conn: the connection
 * @uri: URI of the new folder
 * @props: properties to set on the new folder, or %NULL
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a MKCOL operation on @conn to create @uri, with optional
 * additional properties.
 **/
void
e2k_connection_mkcol (E2kConnection *conn, const char *uri,
		      E2kProperties *props,
		      E2kSimpleCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (uri != NULL,
				     callback (conn, &error_message, user_data));

	if (!props)
		msg = e2k_soup_message_new (conn, uri, "MKCOL");
	else
		msg = patch_msg (conn, uri, "MKCOL", NULL, 0, props, TRUE);

	e2k_connection_queue_message (conn, msg, e2k_callback,
				      callback, user_data);
}

/* BMOVE / BCOPY */

/**
 * e2k_connection_transfer:
 * @conn: the connection
 * @source_folder: URI of the source folder
 * @dest_folder: URI of the destination folder
 * @source_hrefs: an XML "<hrefs>...</hrefs>" string
 * @delete_originals: whether or not to delete the original objects
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a BMOVE or BCOPY (depending on @delete_originals) operation
 * on @conn for @source_folder. The objects in @source_folder
 * described by @source_hrefs will be moved or copied to @dest_folder.
 * @callback will be called with an array of #E2kResult describing the
 * success or failure of each move/copy. (The #E2K_PR_DAV_LOCATION
 * property for each result will show the new location of the object.)
 *
 * NB: may not work correctly if @source_hrefs contains folders
 *
 * FIXME: @source_hrefs is weird. Should use an array of hrefs like
 * other calls.
 **/
void
e2k_connection_transfer (E2kConnection *conn,
			 const char *source_folder, const char *dest_folder,
			 const char *source_hrefs, gboolean delete_originals,
			 E2kResultsCallback callback, gpointer user_data)
{
	SoupMessage *msg;
	char *dest_uri, *xml;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (source_folder != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (dest_folder != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));
	e2k_callback_return_if_fail (source_hrefs != NULL,
				     callback (conn, &error_message, NULL, 0, user_data));

	dest_uri = e2k_strdup_with_trailing_slash (dest_folder);

	xml = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
			       "<%s xmlns=\"DAV:\"><target>%s"
			       "</target></%s>",
			       delete_originals ? "move" : "copy",
			       source_hrefs,
			       delete_originals ? "move" : "copy");
	msg = e2k_soup_message_new_full (conn, source_folder,
					 delete_originals ? "BMOVE" : "BCOPY",
					 "text/xml",
					 SOUP_BUFFER_SYSTEM_OWNED,
					 xml, strlen (xml));
	soup_message_add_header (msg->request_headers, "Overwrite", "f");
	soup_message_add_header (msg->request_headers, "Allow-Rename", "t");
	soup_message_add_header (msg->request_headers, "Destination", dest_uri);
	g_free (dest_uri);

	e2k_connection_queue_message (conn, msg, e2k_results_callback,
				      callback, user_data);
}

/**
 * e2k_connection_transfer_dir:
 * @conn: the connection
 * @source_href: URI of the source folder
 * @dest_href: URI of the destination folder
 * @delete_original: whether or not to delete the original folder
 * @callback: async callback
 * @user_data: data for @callback
 *
 * Queues a MOVE or COPY (depending on @delete_original) operation on
 * @conn for @source_href. The folder itself will be moved, renamed,
 * or copied to @dest_href (which is the name of the new folder
 * itself, not its parent).
 **/
void
e2k_connection_transfer_dir (E2kConnection *conn, const char *source_href,
			     const char *dest_href, gboolean delete_original,
			     E2kSimpleCallback callback, gpointer user_data)
{
	SoupMessage *msg;

	e2k_callback_return_if_fail (E2K_IS_CONNECTION (conn),
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (source_href != NULL,
				     callback (conn, &error_message, user_data));
	e2k_callback_return_if_fail (dest_href != NULL,
				     callback (conn, &error_message, user_data));

	msg = e2k_soup_message_new (conn, source_href, delete_original ? "MOVE" : "COPY");
	soup_message_add_header (msg->request_headers, "Overwrite", "f");
	soup_message_add_header (msg->request_headers, "Destination", dest_href);
	e2k_connection_queue_message (conn, msg, e2k_callback,
				      callback, user_data);
}


/* Subscriptions */

typedef struct {
	E2kConnection *conn;
	char *uri, *id;
	E2kConnectionChangeType type;
	int lifetime, min_interval;
	time_t last_notification;

	E2kConnectionChangeCallback callback;
	gpointer user_data;

	guint renew_timeout;
	SoupMessage *renew_msg;
	guint poll_timeout;
	SoupMessage *poll_msg;
	guint notification_timeout;
} E2kSubscription;

static gboolean
belated_notification (gpointer user_data)
{
	E2kSubscription *sub = user_data;

	sub->notification_timeout = 0;
	sub->callback (sub->conn, sub->uri, sub->type, sub->user_data);
	return FALSE;
}

static void
maybe_notification (E2kSubscription *sub)
{
	time_t now = time (NULL);
	int delay = sub->last_notification + sub->min_interval - now;

	if (delay > 0) {
		if (sub->notification_timeout)
			g_source_remove (sub->notification_timeout);
		sub->notification_timeout = g_timeout_add (delay * 1000,
							   belated_notification,
							   sub);
		return;
	}
	sub->last_notification = now;

	sub->callback (sub->conn, sub->uri, sub->type, sub->user_data);
}

static void
polled (SoupMessage *msg, gpointer user_data)
{
	E2kSubscription *sub = user_data;
	E2kConnection *conn = sub->conn;
	E2kResult *results;
	int nresults, i;
	xmlNode *ids;
	char *id;

	sub->poll_msg = NULL;
	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("Unexpected error %d %s from POLL",
			   msg->errorcode, msg->errorphrase);
		return;
	}

	e2k_results_from_multistatus (msg, &results, &nresults);
	for (i = 0; i < nresults; i++) {
		if (results[i].status != SOUP_ERROR_OK)
			continue;

		ids = e2k_properties_get_prop (results[i].props, E2K_PR_SUBSCRIPTION_ID);
		if (!ids)
			continue;
		for (ids = ids->xmlChildrenNode; ids; ids = ids->next) {
			if (strcmp (ids->name, "li") != 0 ||
			    !ids->xmlChildrenNode ||
			    !ids->xmlChildrenNode->content)
				continue;
			id = ids->xmlChildrenNode->content;
			sub = g_hash_table_lookup (conn->priv->subscriptions_by_id, id);
			if (sub)
				maybe_notification (sub);
		}
	}
	e2k_results_free (results, nresults);
}

static gboolean
timeout_notification (gpointer user_data)
{
	E2kSubscription *sub = user_data, *sub2;
	E2kConnection *conn = sub->conn;
	GList *sub_list;
	GString *subscription_ids;

	sub->poll_timeout = 0;
	subscription_ids = g_string_new (sub->id);

	/* Find all subscriptions at this URI that are awaiting a
	 * POLL so we can POLL them all at once.
	 */
	sub_list = g_hash_table_lookup (conn->priv->subscriptions_by_uri,
					sub->uri);
	for (; sub_list; sub_list = sub_list->next) {
		sub2 = sub_list->data;
		if (sub2 == sub)
			continue;
		if (!sub2->poll_timeout)
			continue;
		g_source_remove (sub2->poll_timeout);
		sub2->poll_timeout = 0;
		g_string_append_printf (subscription_ids, ",%s", sub2->id);
	}

	sub->poll_msg = e2k_soup_message_new (conn, sub->uri, "POLL");
	soup_message_add_header (sub->poll_msg->request_headers,
				 "Subscription-id", subscription_ids->str);
	e2k_soup_message_queue (sub->poll_msg, polled, sub);

	g_string_free (subscription_ids, TRUE);
	return FALSE;
}

static gboolean
do_notification (GIOChannel *source, GIOCondition condition, gpointer data)
{
	E2kConnection *conn = data;
	E2kSubscription *sub;
	char buffer[1024], *id, *lasts;
	gsize len;
	GIOError err;

	err = g_io_channel_read_chars (source, buffer, sizeof (buffer) - 1, &len, NULL);
	if (err != G_IO_ERROR_NONE && err != G_IO_ERROR_AGAIN) {
		g_warning ("do_notification I/O error: %d (%s)", err,
			   g_strerror (errno));
		return FALSE;
	}
	buffer[len] = '\0';

#ifdef E2K_DEBUG
	if (e2k_debug_level && strchr (e2k_debug_types, 'N')) {
		if (e2k_debug_level == 1) {
			fwrite (buffer, 1, strcspn (buffer, "\r\n"), stdout);
			fputs ("\n\n", stdout);
		} else
			fputs (buffer, stdout);
	}
#endif

	if (g_ascii_strncasecmp (buffer, "NOTIFY ", 7) != 0)
		return TRUE;

	id = buffer;
	while (1) {
		id = strchr (id, '\n');
		if (!id++)
			return TRUE;
		if (g_ascii_strncasecmp (id, "Subscription-id: ", 17) == 0)
			break;
	}
	id += 17;

	for (id = strtok_r (id, ",\r", &lasts); id; id = strtok_r (NULL, ",\r", &lasts)) {
		sub = g_hash_table_lookup (conn->priv->subscriptions_by_id, id);
		if (!sub)
			continue;

		/* We don't want to POLL right away in case there are
		 * several changes in a row. So we just bump up the
		 * timeout to be one second from now. (Using an idle
		 * handler here doesn't actually work to prevent
		 * multiple POLLs.)
		 */
		if (sub->poll_timeout)
			g_source_remove (sub->poll_timeout);
		sub->poll_timeout =
			g_timeout_add (1000, timeout_notification, sub);
	}

	return TRUE;
}

static void
renew_cb (SoupMessage *msg, gpointer user_data)
{
	E2kSubscription *sub = user_data;

	sub->renew_msg = NULL;
	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		g_warning ("renew_subscription: %d %s", msg->errorcode, msg->errorphrase);
		return;
	}

	if (sub->id) {
		g_hash_table_remove (sub->conn->priv->subscriptions_by_id, sub->id);
		g_free (sub->id);
	}
	sub->id = g_strdup (soup_message_get_header (msg->response_headers,
						     "Subscription-id"));
	g_return_if_fail (sub->id != NULL);
	g_hash_table_insert (sub->conn->priv->subscriptions_by_id,
			     sub->id, sub);
}

#define E2K_SUBSCRIPTION_INITIAL_LIFETIME  3600 /*  1 hour  */
#define E2K_SUBSCRIPTION_MAX_LIFETIME     57600 /* 16 hours */

/* This must be kept in sync with E2kSubscriptionType */
static char *subscription_type[] = {
	"update",		/* E2K_SUBSCRIPTION_OBJECT_CHANGED */
	"update/newmember",	/* E2K_SUBSCRIPTION_OBJECT_ADDED */
	"delete",		/* E2K_SUBSCRIPTION_OBJECT_REMOVED */
	"move"			/* E2K_SUBSCRIPTION_OBJECT_MOVED */
};

static gboolean
renew_subscription (gpointer user_data)
{
	E2kSubscription *sub = user_data;
	E2kConnection *conn = sub->conn;
	char ltbuf[80];

	if (!conn->priv->notification_uri)
		return FALSE;

	if (sub->lifetime < E2K_SUBSCRIPTION_MAX_LIFETIME)
		sub->lifetime *= 2;

	sub->renew_msg = e2k_soup_message_new (conn, sub->uri, "SUBSCRIBE");
	sprintf (ltbuf, "%d", sub->lifetime);
	soup_message_add_header (sub->renew_msg->request_headers,
				 "Subscription-lifetime", ltbuf);
	soup_message_add_header (sub->renew_msg->request_headers,
				 "Notification-type",
				 subscription_type[sub->type]);
	if (sub->min_interval > 1) {
		sprintf (ltbuf, "%d", sub->min_interval);
		soup_message_add_header (sub->renew_msg->request_headers,
					 "Notification-delay", ltbuf);
	}
	soup_message_add_header (sub->renew_msg->request_headers,
				 "Call-back", conn->priv->notification_uri);

	e2k_soup_message_queue (sub->renew_msg, renew_cb, sub);
	sub->renew_timeout = g_timeout_add ((sub->lifetime - 60) * 1000,
					    renew_subscription, sub);
	return FALSE;
}

/**
 * e2k_connection_subscribe:
 * @conn: the connection
 * @uri: the folder URI to subscribe to notifications on
 * @type: the type of notification to subscribe to
 * @min_interval: the minimum interval (in seconds) between
 * notifications.
 * @callback: the callback to call when a notification has been
 * received
 * @data: data to pass to @callback.
 *
 * This subscribes to change notifications of the given @type on @uri.
 * @callback will (eventually) be invoked any time the folder changes
 * in the given way: whenever an object is added to it for
 * %E2K_CONNECTION_OBJECT_ADDED, whenever an object is deleted (but
 * not moved) from it (or the folder itself is deleted) for
 * %E2K_CONNECTION_OBJECT_REMOVED, whenever an object is moved in or
 * out of the folder for %E2K_CONNECTION_OBJECT_MOVED, and whenever
 * any of the above happens, or the folder or one of its items is
 * modified, for %E2K_CONNECTION_OBJECT_CHANGED. (This means that if
 * you subscribe to both CHANGED and some other notification on the
 * same folder that multiple callbacks may be invoked every time an
 * object is added/removed/moved/etc.)
 *
 * Notifications can be used *only* to discover changes made by other
 * clients! The code cannot assume that it will receive a notification
 * for every change that it makes to the server, for two reasons:
 * 
 * First, if multiple notifications occur within @min_interval seconds
 * of each other, the later ones will be suppressed, to avoid
 * excessive traffic between the client and the server as the client
 * tries to sync. Second, if there is a firewall between the client
 * and the server, it is possible that all notifications will be lost.
 **/
void
e2k_connection_subscribe (E2kConnection *conn, const char *uri,
			  E2kConnectionChangeType type, int min_interval,
			  E2kConnectionChangeCallback callback,
			  gpointer user_data)
{
	E2kSubscription *sub;
	GList *sub_list;
	gpointer key, value;

	sub = g_new0 (E2kSubscription, 1);
	sub->conn = conn;
	sub->uri = g_strdup (uri);
	sub->type = type;
	sub->lifetime = E2K_SUBSCRIPTION_INITIAL_LIFETIME / 2;
	sub->min_interval = min_interval;
	sub->callback = callback;
	sub->user_data = user_data;

	if (g_hash_table_lookup_extended (conn->priv->subscriptions_by_uri,
					  uri, &key, &value)) {
		sub_list = value;
		sub_list = g_list_prepend (sub_list, sub);
		g_hash_table_insert (conn->priv->subscriptions_by_uri,
				     key, sub_list);
	} else {
		g_hash_table_insert (conn->priv->subscriptions_by_uri,
				     sub->uri, g_list_prepend (NULL, sub));
	}

	renew_subscription (sub);
}

static void
free_subscription (E2kSubscription *sub)
{
	if (sub->renew_timeout)
		g_source_remove (sub->renew_timeout);
	if (sub->renew_msg)
		soup_message_cancel (sub->renew_msg);
	if (sub->poll_timeout)
		g_source_remove (sub->poll_timeout);
	if (sub->notification_timeout)
		g_source_remove (sub->notification_timeout);
	if (sub->poll_msg)
		soup_message_cancel (sub->poll_msg);
	g_free (sub->uri);
	g_free (sub->id);
	g_free (sub);
}

static void
unsubscribed (SoupMessage *msg, gpointer user_data)
{
	;
}

static void
unsubscribe_internal (E2kConnection *conn, const char *uri, GList *sub_list)
{
	GList *l;
	E2kSubscription *sub;
	SoupMessage *msg;
	GString *subscription_ids = NULL;

	for (l = sub_list; l; l = l->next) {
		sub = l->data;
		if (sub->id) {
			if (!subscription_ids)
				subscription_ids = g_string_new (sub->id);
			else {
				g_string_append_printf (subscription_ids,
							",%s", sub->id);
			}
			g_hash_table_remove (conn->priv->subscriptions_by_id, sub->id);
		}
		free_subscription (sub);
	}

	if (subscription_ids) {
		msg = e2k_soup_message_new (conn, uri, "UNSUBSCRIBE");
		soup_message_add_header (msg->request_headers,
					 "Subscription-id",
					 subscription_ids->str);
		e2k_soup_message_queue (msg, unsubscribed, NULL);
		g_string_free (subscription_ids, TRUE);
	}
}

/**
 * e2k_connection_unsubscribe:
 * @conn: the connection
 * @uri: the URI to unsubscribe from
 *
 * Unsubscribes to all notifications on @conn for @uri.
 **/
void
e2k_connection_unsubscribe (E2kConnection *conn, const char *uri)
{
	GList *sub_list;

	sub_list = g_hash_table_lookup (conn->priv->subscriptions_by_uri, uri);
	g_hash_table_remove (conn->priv->subscriptions_by_uri, uri);
	unsubscribe_internal (conn, uri, sub_list);
	g_list_free (sub_list);
}
