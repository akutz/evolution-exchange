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

/* mail-stub-listener.c: stub connection listener */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>

#ifndef G_OS_WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#else
#include <winsock2.h>
#endif

#include "mail-stub-listener.h"
#include <e2k-marshal.h>
#include <e2k-types.h>

enum {
	NEW_CONNECTION,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

static void finalize (GObject *);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	/* signals */
	signals[NEW_CONNECTION] =
		g_signal_new ("new_connection",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (MailStubListenerClass, new_connection),
			      NULL, NULL,
			      e2k_marshal_NONE__INT_INT,
			      G_TYPE_NONE, 2,
			      G_TYPE_INT, G_TYPE_INT);
}

static void
finalize (GObject *object)
{
	MailStubListener *listener = MAIL_STUB_LISTENER (object);

	if (listener->channel)
		g_io_channel_unref (listener->channel);
	if (listener->socket_path) {
		g_unlink (listener->socket_path);
		g_free (listener->socket_path);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (mail_stub_listener, MailStubListener, class_init, NULL, PARENT_TYPE)

static gboolean
new_connection (GIOChannel *source, GIOCondition condition, gpointer data)
{
	MailStubListener *listener = data;
	gint fd;

	fd = accept (g_io_channel_unix_get_fd (source), NULL, NULL);
	if (fd == -1)
		return TRUE;

	if (listener->cmd_fd == -1) {
		listener->cmd_fd = fd;
		return TRUE;
	}

	g_signal_emit (listener, signals[NEW_CONNECTION], 0,
		       listener->cmd_fd, fd);
	listener->cmd_fd = -1;

	return TRUE;
}

gboolean
mail_stub_listener_construct (MailStubListener *listener, const gchar *socket_path)
{
#ifndef G_OS_WIN32
	struct sockaddr_un sa_un;
#else
	struct sockaddr_in sa_in;
	gint addr_len;
	GError *error = NULL;
#endif
	gint fd;

#ifndef G_OS_WIN32
	g_return_val_if_fail (strlen (socket_path) < sizeof (sa_un.sun_path), FALSE);

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1)
		return FALSE;

	unlink (socket_path);
	listener->socket_path = g_strdup (socket_path);
	sa_un.sun_family = AF_UNIX;
	strcpy (sa_un.sun_path, socket_path);

	if (bind (fd, (struct sockaddr *)&sa_un, sizeof (sa_un)) == -1 || listen (fd, 5) == -1) {
		close (fd);
		return FALSE;
	}

	listener->channel = g_io_channel_unix_new (fd);
#else
	fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd == SOCKET_ERROR)
		return FALSE;
	memset (&sa_in, 0, sizeof (sa_in));
	sa_in.sin_family = AF_INET;
	sa_in.sin_port = 0;
	sa_in.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (bind (fd, (struct sockaddr *) &sa_in, sizeof (sa_in)) == SOCKET_ERROR) {
		g_warning ("bind() failed: %s",
			   g_win32_error_message (WSAGetLastError ()));
		closesocket (fd);
		return FALSE;
	}

	if (listen (fd, 1) == SOCKET_ERROR) {
		g_warning ("listen() failed: %s",
		g_win32_error_message (WSAGetLastError ()));
		closesocket (fd);
		return FALSE;
	}

	addr_len = sizeof (sa_in);
	if (getsockname (fd, (struct sockaddr *) &sa_in, &addr_len) == SOCKET_ERROR) {
		g_warning ("getsockname() failed: %s",
			   g_win32_error_message (WSAGetLastError ()));
		closesocket (fd);
		return FALSE;
	}

	g_unlink (socket_path);
	listener->socket_path = g_strdup (socket_path);

	printf ("listening on port %d\n", ntohs(sa_in.sin_port));

	if (!g_file_set_contents (socket_path, (gchar *) &sa_in, addr_len, &error)) {
		g_warning ("Could not save socket address in '%s': %s",
			   socket_path, error->message);
		g_error_free (error);
		closesocket (fd);
		return FALSE;
	}

	listener->channel = g_io_channel_win32_new_socket (fd);
#endif
	g_io_add_watch (listener->channel, G_IO_IN, new_connection, listener);

	listener->cmd_fd = -1;

	return TRUE;
}

/**
 * mail_stub_listener_new:
 * @socket_path: path to create a listening socket on
 *
 * Creates a new #MailStubListener listening for connections at
 * @socket_path.
 *
 * Return value: the new #MailStubListener.
 **/
MailStubListener *
mail_stub_listener_new (const gchar *socket_path)
{
	MailStubListener *listener;

	listener = g_object_new (MAIL_TYPE_STUB_LISTENER, NULL);
	if (!mail_stub_listener_construct (listener, socket_path)) {
		g_object_unref (listener);
		return NULL;
	}
	return listener;
}
