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

/* camel-stub.c: class for a stub to talk to the backend */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>

#include "camel-stub.h"

#include <camel/camel-exception.h>
#include <camel/camel-operation.h>

#include <errno.h>
#include <string.h>

#ifndef G_OS_WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#else
#include <winsock2.h>
#endif

CamelStub *das_global_camel_stub;

static void
class_init (CamelObjectClass *stub_class)
{
	camel_object_class_add_event (stub_class, "notification", NULL);
}

static void
init (CamelStub *stub)
{
	stub->read_lock = g_mutex_new ();
	stub->write_lock = g_mutex_new ();
	stub->have_status_thread = FALSE;
}

static void
finalize (CamelStub *stub)
{
	if (stub->cmd)
		camel_stub_marshal_free (stub->cmd);

	if (stub->have_status_thread) {
		void *unused;

		/* When we close the command channel, the storage will
		 * close the status channel, which will in turn cause
		 * the status loop to exit.
		 *
		 * Hah... as if. More than likely it'll just get caught in a read() call.
		 */
		if (stub->op)
			camel_operation_cancel (stub->op);

		pthread_join (stub->status_thread, &unused);
		camel_stub_marshal_free (stub->status);

		if (stub->op) {
			camel_operation_unref (stub->op);
			stub->op = NULL;
		}
	}

	if (stub->backend_name)
		g_free (stub->backend_name);

	g_mutex_free (stub->read_lock);
	g_mutex_free (stub->write_lock);

	if (das_global_camel_stub == stub)
		das_global_camel_stub = NULL;
}

CamelType
camel_stub_get_type (void)
{
	static CamelType camel_stub_type = CAMEL_INVALID_TYPE;

	if (!camel_stub_type) {
		camel_stub_type = camel_type_register (
			CAMEL_OBJECT_TYPE, "CamelStub",
			sizeof (CamelStub),
			sizeof (CamelStubClass),
			(CamelObjectClassInitFunc) class_init,
			NULL,
			(CamelObjectInitFunc) init,
			(CamelObjectFinalizeFunc) finalize);
	}

	return camel_stub_type;
}


static void *
status_main (void *data)
{
	CamelObject *stub_object = data;
	CamelStub *stub = data;
	CamelStubMarshal *status_channel = stub->status;
	guint32 retval;

	stub->have_status_thread = TRUE;

	stub->op = camel_operation_new (NULL, NULL);
	camel_operation_register (stub->op);

	while (1) {
		if (camel_operation_cancel_check (stub->op))
			break;

		if (camel_stub_marshal_decode_uint32 (status_channel, &retval) == -1)
			break;

		/* FIXME. If you don't have exactly one thing listening
		 * to this, it will get out of sync. But I don't want
		 * CamelStub to have to know the details of the message
		 * structures. We probably need to make the data more
		 * self-describing.
		 */
		camel_object_trigger_event (stub_object, "notification",
					    GUINT_TO_POINTER (retval));
	}

	camel_operation_unregister (stub->op);

	stub->have_status_thread = FALSE;

	return NULL;
}

#ifndef G_OS_WIN32

static int
connect_to_storage (CamelStub *stub, struct sockaddr_un *sa_un,
		    CamelException *ex)
{
	int fd;

	fd = socket (AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not create socket: %s"),
				      g_strerror (errno));
		return -1;
	}
	if (connect (fd, (struct sockaddr *)sa_un, sizeof (*sa_un)) == -1) {
		close (fd);
		if (errno == ECONNREFUSED) {
			/* The user has an account configured but the
			 * backend isn't listening, which probably means that
			 * he doesn't have a license.
			 */
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     "Cancelled");
		} else if (errno == ENOENT) {
			/* socket_path has changed, mostly because user name has been changed.
			 * need to restart evolution for changes to take effect.
			 */
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not connect to %s: Please restart Evolution"),
					      stub->backend_name);
		}
		else {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not connect to %s: %s"),
					      stub->backend_name,
					      g_strerror (errno));
		}
		return -1;
	}
	return fd;
}

#else

static int
connect_to_storage (CamelStub *stub, const char *socket_path,
		    CamelException *ex)
{
	SOCKET fd;
	struct sockaddr_in *sa_in;
	gsize contents_length;
	GError *error = NULL;
	int rc;

	if (!g_file_get_contents (socket_path, (gchar **) &sa_in,
				  &contents_length, &error)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Count not read file '%s': %s"),
				      socket_path, error->message);
		g_error_free (error);
		return -1;
	}

	if (contents_length != sizeof (*sa_in)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Wrong size file '%s'"),
				      socket_path);
		g_free (sa_in);
		return -1;
	}

	fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd == INVALID_SOCKET) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Could not create socket: %s"),
				      g_win32_error_message (WSAGetLastError ()));
		return -1;
	}
	rc = connect (fd, (struct sockaddr *)sa_in, sizeof (*sa_in));
	g_free (sa_in);

	if (rc == SOCKET_ERROR) {
		closesocket (fd);
		if (WSAGetLastError () == WSAECONNREFUSED) {
			/* The user has an account configured but the
			 * backend isn't listening, which probably means that
			 * he doesn't have a license.
			 */
			camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
					     "Cancelled");
		} else {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
					      _("Could not connect to %s: %s"),
					      stub->backend_name,
					      g_win32_error_message (WSAGetLastError ()));
		}
		return -1;
	}
	return fd;
}

#endif


/**
 * camel_stub_new:
 * @socket_path: path to the server's UNIX domain socket
 * @backend_name: human-readably name of the service we are connecting to
 * @ex: a #CamelException
 *
 * Tries to connect to the backend server listening at @socket_path.
 *
 * Return value: on success, a new #CamelStub. On failure, %NULL, in
 * which case @ex will be set.
 **/
CamelStub *
camel_stub_new (const char *socket_path, const char *backend_name,
		CamelException *ex)
{
	CamelStub *stub;
#ifndef G_OS_WIN32
	struct sockaddr_un sa_un;
#endif
	int fd;

#ifndef G_OS_WIN32
	if (strlen (socket_path) > sizeof (sa_un.sun_path) - 1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      _("Path too long: %s"), socket_path);
		return NULL;
	}
	sa_un.sun_family = AF_UNIX;
	strcpy (sa_un.sun_path, socket_path);
#endif

	stub = (CamelStub *)camel_object_new (CAMEL_STUB_TYPE);
	stub->backend_name = g_strdup (backend_name);

#ifndef G_OS_WIN32
	fd = connect_to_storage (stub, &sa_un, ex);
#else
	fd = connect_to_storage (stub, socket_path, ex);
#endif
	if (fd == -1) {
		camel_object_unref (CAMEL_OBJECT (stub));
		return NULL;
	}
	stub->cmd = camel_stub_marshal_new (fd);

#ifndef G_OS_WIN32
	fd = connect_to_storage (stub, &sa_un, ex);
#else
	fd = connect_to_storage (stub, socket_path, ex);
#endif
	if (fd == -1) {
		camel_object_unref (CAMEL_OBJECT (stub));
		return NULL;
	}
	stub->status = camel_stub_marshal_new (fd);

	/* Spawn status thread */
	if (pthread_create (&stub->status_thread, NULL, status_main, stub) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not start status thread: %s"),
				      g_strerror (errno));
		camel_object_unref (CAMEL_OBJECT (stub));
		return NULL;
	}

	das_global_camel_stub = stub;
	return stub;
}

static gboolean
stub_send_internal (CamelStub *stub, CamelException *ex, gboolean oneway,
		    CamelStubCommand command, va_list ap)
{
	CamelStubArgType argtype;
	int status = 0;
	guint32 retval;

	g_return_val_if_fail (stub, FALSE);

	camel_object_ref (CAMEL_OBJECT (stub));
	if (!oneway)
		g_mutex_lock (stub->read_lock);

	/* Send command */
	g_mutex_lock (stub->write_lock);
	camel_stub_marshal_encode_uint32 (stub->cmd, command);
	while (1) {
		argtype = va_arg (ap, int);
		switch (argtype) {
		case CAMEL_STUB_ARG_RETURN:
		case CAMEL_STUB_ARG_END:
			goto done_input;

		case CAMEL_STUB_ARG_UINT32:
		{
			guint32 val = va_arg (ap, guint32);

			camel_stub_marshal_encode_uint32 (stub->cmd, val);
			break;
		}

		case CAMEL_STUB_ARG_STRING:
		{
			char *string = va_arg (ap, char *);

			camel_stub_marshal_encode_string (stub->cmd, string);
			break;
		}

		case CAMEL_STUB_ARG_FOLDER:
		{
			char *name = va_arg (ap, char *);

			camel_stub_marshal_encode_folder (stub->cmd, name);
			break;
		}

		case CAMEL_STUB_ARG_BYTEARRAY:
		{
			GByteArray *ba = va_arg (ap, GByteArray *);

			camel_stub_marshal_encode_bytes (stub->cmd, ba);
			break;
		}

		case CAMEL_STUB_ARG_STRINGARRAY:
		{
			GPtrArray *arr = va_arg (ap, GPtrArray *);
			int i;

			camel_stub_marshal_encode_uint32 (stub->cmd, arr->len);
			for (i = 0; i < arr->len; i++)
				camel_stub_marshal_encode_string (stub->cmd, arr->pdata[i]);
			break;
		}

		case CAMEL_STUB_ARG_UINT32ARRAY:
		{
			GArray *arr = va_arg (ap, GArray *);
			int i;

			camel_stub_marshal_encode_uint32 (stub->cmd, arr->len);
			for (i = 0; i < arr->len; i++)
				camel_stub_marshal_encode_uint32 (stub->cmd, g_array_index (arr, guint32, i));
			break;
		}

		default:
			g_critical ("%s: Uncaught case (%d)", G_STRLOC, argtype);
			break;
		}
	}
 done_input:

	status = camel_stub_marshal_flush (stub->cmd);
	g_mutex_unlock (stub->write_lock);

	if (status == -1)
		goto comm_fail;
	else if (oneway)
		goto done;

	/* Read response */
	do {
		if (camel_stub_marshal_decode_uint32 (stub->cmd, &retval) == -1)
			goto comm_fail;

		switch (retval) {
		case CAMEL_STUB_RETVAL_OK:
			break;

		case CAMEL_STUB_RETVAL_EXCEPTION:
		{
			char *desc;

			/* FIXME: exception id? */

			if (camel_stub_marshal_decode_string (stub->cmd, &desc) == -1)
				goto comm_fail;
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, desc);
			g_free (desc);

			goto err_out;
		}

		case CAMEL_STUB_RETVAL_PROGRESS:
		{
			guint32 percent;

			if (camel_stub_marshal_decode_uint32 (stub->cmd, &percent) == -1)
				goto comm_fail;
			camel_operation_progress (NULL, percent);
			break;
		}

		case CAMEL_STUB_RETVAL_RESPONSE:
		{
			if (argtype != CAMEL_STUB_ARG_RETURN)
				goto comm_fail;

			do {
				argtype = va_arg (ap, int);
				switch (argtype) {
				case CAMEL_STUB_ARG_END:
					goto done_output;

				case CAMEL_STUB_ARG_UINT32:
				{
					guint32 *val = va_arg (ap, guint32 *);
					guint32 val32;

					status = camel_stub_marshal_decode_uint32 (stub->cmd, &val32);
					*val = val32;
					break;
				}

				case CAMEL_STUB_ARG_STRING:
				{
					char **string = va_arg (ap, char **);

					status = camel_stub_marshal_decode_string (stub->cmd, string);
					break;
				}

				case CAMEL_STUB_ARG_FOLDER:
				{
					char **name = va_arg (ap, char **);

					status = camel_stub_marshal_decode_folder (stub->cmd, name);
					break;
				}

				case CAMEL_STUB_ARG_BYTEARRAY:
				{
					GByteArray **ba = va_arg (ap, GByteArray **);

					status = camel_stub_marshal_decode_bytes (stub->cmd, ba);
					break;
				}

				case CAMEL_STUB_ARG_STRINGARRAY:
				{
					GPtrArray **arr = va_arg (ap, GPtrArray **);
					guint32 len;
					char *string;
					int i;

					status = camel_stub_marshal_decode_uint32 (stub->cmd, &len);
					if (status == -1)
						break;
					*arr = g_ptr_array_new ();
					for (i = 0; i < len && status != -1; i++) {
						status = camel_stub_marshal_decode_string (stub->cmd, &string);
						if (status != -1)
							g_ptr_array_add (*arr, string);
					}

					if (status == -1) {
						while ((*arr)->len)
							g_free ((*arr)->pdata[(*arr)->len]);
						g_ptr_array_free (*arr, TRUE);
					}

					break;
				}

				case CAMEL_STUB_ARG_UINT32ARRAY:
				{
					GArray **arr = va_arg (ap, GArray **);
					guint32 i, len, unread_count;
					status = camel_stub_marshal_decode_uint32 (stub->cmd, &len);
					if (status == -1)
						break;
					*arr = g_array_new (FALSE, FALSE, sizeof (guint32));
					for (i = 0; i< len && status != -1; i++) {
						status = camel_stub_marshal_decode_uint32 (stub->cmd, &unread_count);
						if (status != -1)
							g_array_append_val (*arr, unread_count);
					}
					if (status == -1)
						g_array_free (*arr, TRUE);

					break;
				}

				default:
					g_critical ("%s: Uncaught case (%d)", G_STRLOC, argtype);
					status = -1;
					break;
				}
			} while (status != -1);
		done_output:
			if (status == -1)
				goto comm_fail;

			break;
		}

		default:
			g_critical ("%s: Uncaught case (%d)", G_STRLOC, retval);
			break;
		}
	} while (retval != CAMEL_STUB_RETVAL_OK);

 done:
	if (!oneway)
		g_mutex_unlock (stub->read_lock);
	camel_object_unref (CAMEL_OBJECT (stub));
	return TRUE;

 comm_fail:
	if (camel_stub_marshal_eof (stub->cmd)) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Lost connection to %s"),
				      stub->backend_name);
	} else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Error communicating with %s: %s"),
				      stub->backend_name, g_strerror (errno));
	}

 err_out:
	if (!oneway)
		g_mutex_unlock (stub->read_lock);
	camel_object_unref (CAMEL_OBJECT (stub));
	return FALSE;
}


/**
 * camel_stub_send:
 * @stub: a #CamelStub
 * @ex: a #CamelException
 * @command: the command to send
 * @...: arguments to @command
 *
 * Sends @command to the backend and waits for a response.
 *
 * Return value: %TRUE if command executed successfully, %FALSE
 * if it failed, in which case @ex will be set.
 **/
gboolean
camel_stub_send (CamelStub *stub, CamelException *ex,
		 CamelStubCommand command, ...)
{
	va_list ap;
	gboolean retval;

	va_start (ap, command);
	retval = stub_send_internal (stub, ex, FALSE, command, ap);
	va_end (ap);

	return retval;
}

/**
 * camel_stub_send_oneway:
 * @stub: a #CamelStub
 * @command: the command to send
 * @...: arguments to @command
 *
 * Sends @command to the backend without waiting for a response. (This
 * only works for commands such as %CAMEL_STUB_CMD_SET_MESSAGE_FLAGS
 * which the server does not send a response for.)
 *
 * Return value: %TRUE if command was sent successfull, %FALSE if
 * the command could not be sent.
 **/
gboolean
camel_stub_send_oneway (CamelStub *stub, CamelStubCommand command, ...)
{
	va_list ap;
	gboolean retval;

	va_start (ap, command);
	retval = stub_send_internal (stub, NULL, TRUE, command, ap);
	va_end (ap);

	return retval;
}
