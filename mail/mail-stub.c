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

/* mail-stub.c: the part that talks to the camel stub */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <unistd.h>

#include <e2k-uri.h>
#include <e2k-types.h>
#include "mail-stub.h"
#include "camel-stub-constants.h"

#define d(x)

#define PARENT_TYPE G_TYPE_OBJECT
static MailStubClass *parent_class = NULL;

#define MS_CLASS(stub) (MAIL_STUB_CLASS (G_OBJECT_GET_CLASS (stub)))

static void finalize (GObject *);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

static void
finalize (GObject *object)
{
	MailStub *stub = MAIL_STUB (object);

	if (stub->channel)
		g_io_channel_unref (stub->channel);
	if (stub->cmd)
		camel_stub_marshal_free (stub->cmd);
	if (stub->status)
		camel_stub_marshal_free (stub->status);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (mail_stub, MailStub, class_init, NULL, PARENT_TYPE)


static void
free_string_array (GPtrArray *strings)
{
	int i;

	for (i = 0; i < strings->len; i++)
		g_free (strings->pdata[i]);
	g_ptr_array_free (strings, TRUE);
}

static gboolean
connection_handler (GIOChannel *source, GIOCondition condition, gpointer data)
{
	MailStub *stub = data;
	guint32 command;

	if (condition == G_IO_ERR || condition == G_IO_HUP)
		return FALSE;

	if (camel_stub_marshal_decode_uint32 (stub->cmd, &command) == -1)
		return FALSE;

	switch (command) {
	case CAMEL_STUB_CMD_CONNECT:
	{
		char *pwd
		d(printf("CONNECT\n"));
		g_object_ref (stub);
		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_STRING, &pwd,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		MS_CLASS (stub)->connect (stub, pwd);
		g_free (pwd);
		break;
	}

	case CAMEL_STUB_CMD_GET_FOLDER:
	{
		char *folder_name;
		GPtrArray *uids;
		GByteArray *flags;
		GPtrArray *hrefs;
		guint32 high_article_num;
		guint32 create;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_UINT32, &create,
					  CAMEL_STUB_ARG_STRINGARRAY, &uids,
					  CAMEL_STUB_ARG_BYTEARRAY, &flags,
					  CAMEL_STUB_ARG_STRINGARRAY, &hrefs,
					  CAMEL_STUB_ARG_UINT32, &high_article_num,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("GET_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->get_folder (stub, folder_name, create,
					     uids, flags, hrefs, high_article_num);
		g_free (folder_name);
		free_string_array (uids);
		g_byte_array_free (flags, TRUE);
		break;
	}

	case CAMEL_STUB_CMD_GET_TRASH_NAME:
	{
		d(printf("GET_TRASH_NAME\n"));
		g_object_ref (stub);
		MS_CLASS (stub)->get_trash_name (stub);
		break;
	}

	case CAMEL_STUB_CMD_SYNC_FOLDER:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SYNC_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->sync_folder (stub, folder_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_REFRESH_FOLDER:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("REFRESH_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->refresh_folder (stub, folder_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_SYNC_COUNT:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SYNC_COUNT %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->sync_count (stub, folder_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_EXPUNGE_UIDS:
	{
		char *folder_name;
		GPtrArray *uids;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_STRINGARRAY, &uids,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("EXPUNGE_UIDS %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->expunge_uids (stub, folder_name, uids);
		g_free (folder_name);
		free_string_array (uids);
		break;
	}

	case CAMEL_STUB_CMD_APPEND_MESSAGE:
	{
		char *folder_name, *subject;
		guint32 flags;
		GByteArray *body;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_UINT32, &flags,
					  CAMEL_STUB_ARG_STRING, &subject,
					  CAMEL_STUB_ARG_BYTEARRAY, &body,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("APPEND_MESSAGE %s %lu %s\n", folder_name,
			 (gulong)flags, subject));
		g_object_ref (stub);
		MS_CLASS (stub)->append_message (stub, folder_name, flags, subject, (char *) body->data, body->len);
		g_free (folder_name);
		g_free (subject);
		g_byte_array_free (body, TRUE);
		break;
	}

	case CAMEL_STUB_CMD_SET_MESSAGE_FLAGS:
	{
		char *folder_name, *uid;
		guint32 flags, mask;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_STRING, &uid,
					  CAMEL_STUB_ARG_UINT32, &flags,
					  CAMEL_STUB_ARG_UINT32, &mask,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SET_MESSAGE_FLAGS %s %s %lu %lu\n",
			 folder_name, uid, (gulong)flags, (gulong)mask));
		/* Not async, so we don't ref stub */
		MS_CLASS (stub)->set_message_flags (stub, folder_name, uid, flags, mask);
		g_free (folder_name);
		g_free (uid);
		break;
	}

	case CAMEL_STUB_CMD_SET_MESSAGE_TAG:
	{
		char *folder_name, *uid, *name, *value;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_STRING, &uid,
					  CAMEL_STUB_ARG_STRING, &name,
					  CAMEL_STUB_ARG_STRING, &value,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SET_MESSAGE_TAG %s %s %s %s\n",
			 folder_name, uid, name, value));
		/* Not async, so we don't ref stub */
		MS_CLASS (stub)->set_message_tag (stub, folder_name, uid,
						  name, value);
		g_free (folder_name);
		g_free (uid);
		g_free (name);
		g_free (value);
		break;
	}

	case CAMEL_STUB_CMD_GET_MESSAGE:
	{
		char *folder_name, *uid;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_STRING, &uid,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("GET_MESSAGE %s %s\n", folder_name, uid));
		g_object_ref (stub);
		MS_CLASS (stub)->get_message (stub, folder_name, uid);
		g_free (folder_name);
		g_free (uid);
		break;
	}

	case CAMEL_STUB_CMD_SEARCH_FOLDER:
	{
		char *folder_name, *text;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_STRING, &text,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SEARCH_FOLDER %s %s\n", folder_name, text));
		g_object_ref (stub);
		MS_CLASS (stub)->search (stub, folder_name, text);
		g_free (folder_name);
		g_free (text);
		break;
	}

	case CAMEL_STUB_CMD_TRANSFER_MESSAGES:
	{
		char *source_name, *dest_name;
		guint32 delete_originals;
		GPtrArray *uids;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &source_name,
					  CAMEL_STUB_ARG_FOLDER, &dest_name,
					  CAMEL_STUB_ARG_STRINGARRAY, &uids,
					  CAMEL_STUB_ARG_UINT32, &delete_originals,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("TRANSFER_MESSAGES %s -> %s (%d)\n",
			 source_name, dest_name, uids->len));
		g_object_ref (stub);
		MS_CLASS (stub)->transfer_messages (stub, source_name,
						    dest_name, uids,
						    delete_originals);
		g_free (source_name);
		g_free (dest_name);
		free_string_array (uids);
		break;
	}

	case CAMEL_STUB_CMD_GET_FOLDER_INFO:
	{
		char *top;
		guint32 store_flags;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_STRING, &top,
					  CAMEL_STUB_ARG_UINT32, &store_flags,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("GET_FOLDER_INFO %s%s\n", top, store_flags ? " (recursive)" : ""));
		g_object_ref (stub);
		MS_CLASS (stub)->get_folder_info (stub, top, store_flags);
		g_free (top);
		break;
	}

	case CAMEL_STUB_CMD_SEND_MESSAGE:
	{
		char *from;
		GPtrArray *recips;
		GByteArray *body;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_STRING, &from,
					  CAMEL_STUB_ARG_STRINGARRAY, &recips,
					  CAMEL_STUB_ARG_BYTEARRAY, &body,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SEND_MESSAGE from %s to %d recipients\n",
			 from, recips->len));
		g_object_ref (stub);
		MS_CLASS (stub)->send_message (stub, from, recips,
					       (char *) body->data, body->len);
		g_free (from);
		free_string_array (recips);
		g_byte_array_free (body, TRUE);
		break;
	}

	case CAMEL_STUB_CMD_CREATE_FOLDER:
	{
		char *parent_name, *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &parent_name,
					  CAMEL_STUB_ARG_STRING, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("CREATE_FOLDER %s %s\n", parent_name, folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->create_folder (stub, parent_name,
						folder_name);
		g_free (parent_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_DELETE_FOLDER:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("DELETE_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->delete_folder (stub, folder_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_RENAME_FOLDER:
	{
		char *old_name, *new_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &old_name,
					  CAMEL_STUB_ARG_FOLDER, &new_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("RENAME_FOLDER %s %s\n", old_name, new_name));
		g_object_ref (stub);
		MS_CLASS (stub)->rename_folder (stub, old_name, new_name);
		g_free (old_name);
		g_free (new_name);
		break;
	}

	case CAMEL_STUB_CMD_SUBSCRIBE_FOLDER:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("SUBSCRIBE_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->subscribe_folder (stub, folder_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_UNSUBSCRIBE_FOLDER:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					  CAMEL_STUB_ARG_FOLDER, &folder_name,
					  CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("UNSUBSCRIBE_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->unsubscribe_folder (stub, folder_name);
		g_free (folder_name);
		break;
	}

	case CAMEL_STUB_CMD_IS_SUBSCRIBED_FOLDER:
	{
		char *folder_name;

		if (!mail_stub_read_args (stub,
					CAMEL_STUB_ARG_FOLDER, &folder_name,
					CAMEL_STUB_ARG_END))
			goto comm_fail;
		d(printf("IS_SUBSCRIBED_FOLDER %s\n", folder_name));
		g_object_ref (stub);
		MS_CLASS (stub)->is_subscribed_folder (stub, folder_name);
		g_free (folder_name);
		break;
	}

	default:
		g_critical ("%s: Uncaught case (%d)", G_STRLOC, command);
		goto comm_fail;
	}

	return TRUE;

 comm_fail:
	/* Destroy the stub */
	g_object_unref (stub);
	return FALSE;
}

/**
 * mail_stub_read_args:
 * @stub: a #MailStub
 * @...: description of arguments to read
 *
 * Reads arguments as described from @stub's command channel.
 * The varargs list consists of pairs of #CamelStubArgType values
 * followed by pointers to variables of the appropriate type. The
 * list is terminated by %CAMEL_STUB_ARG_END.
 *
 * Return value: success or failure.
 **/
gboolean
mail_stub_read_args (MailStub *stub, ...)
{
	va_list ap;
	CamelStubArgType argtype;
	int status;

	va_start (ap, stub);

	do {
		argtype = va_arg (ap, int);
		switch (argtype) {
		case CAMEL_STUB_ARG_END:
			return TRUE;

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
			char **buf = va_arg (ap, char **);

			status = camel_stub_marshal_decode_string (stub->cmd, buf);
			break;
		}

		case CAMEL_STUB_ARG_FOLDER:
		{
			char **buf = va_arg (ap, char **);

			status = camel_stub_marshal_decode_folder (stub->cmd, buf);
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

			if (status == -1)
				free_string_array (*arr);

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

	return FALSE;
}

/**
 * mail_stub_return_data:
 * @stub: the MailStub
 * @retval: the kind of return data
 * @...: the data
 *
 * Sends substantive data to the CamelStub. If @retval is
 * %CAMEL_STUB_RETVAL_RESPONSE, it is the response data to the
 * last command. Otherwise it is an unsolicited informational
 * message.
 *
 * The data is not actually sent by this call. Response data will be
 * flushed when you call mail_stub_return_ok() or
 * mail_stub_return_error(). For asynchronous notifications, you
 * should be sure to call mail_stub_push_changes().
 **/
void
mail_stub_return_data (MailStub *stub, CamelStubRetval retval, ...)
{
	va_list ap;
	CamelStubArgType argtype;
	CamelStubMarshal *marshal;

	if (retval == CAMEL_STUB_RETVAL_RESPONSE)
		marshal = stub->cmd;
	else
		marshal = stub->status;

	camel_stub_marshal_encode_uint32 (marshal, retval);
	va_start (ap, retval);

	while (1) {
		argtype = va_arg (ap, int);
		switch (argtype) {
		case CAMEL_STUB_ARG_END:
			return;

		case CAMEL_STUB_ARG_UINT32:
		{
			guint32 val = va_arg (ap, guint32);

			camel_stub_marshal_encode_uint32 (marshal, val);
			break;
		}

		case CAMEL_STUB_ARG_STRING:
		{
			char *string = va_arg (ap, char *);

			camel_stub_marshal_encode_string (marshal, string);
			break;
		}

		case CAMEL_STUB_ARG_FOLDER:
		{
			char *name = va_arg (ap, char *);

			camel_stub_marshal_encode_folder (marshal, name);
			break;
		}

		case CAMEL_STUB_ARG_BYTEARRAY:
		{
			char *data = va_arg (ap, char *);
			int len = va_arg (ap, int);
			GByteArray ba;

			ba.data = (guint8 *) data;
			ba.len = len;
			camel_stub_marshal_encode_bytes (marshal, &ba);
			break;
		}

		case CAMEL_STUB_ARG_STRINGARRAY:
		{
			GPtrArray *arr = va_arg (ap, GPtrArray *);
			int i;

			camel_stub_marshal_encode_uint32 (marshal, arr->len);
			for (i = 0; i < arr->len; i++)
				camel_stub_marshal_encode_string (marshal, arr->pdata[i]);
			break;
		}

		case CAMEL_STUB_ARG_UINT32ARRAY:
		{
			GArray *arr = va_arg (ap, GArray *);
			int i;

			camel_stub_marshal_encode_uint32 (marshal, arr->len);
			for (i = 0; i < arr->len; i++)
				camel_stub_marshal_encode_uint32 (marshal, g_array_index (arr, guint32, i));
			break;
		}


		default:
			g_critical ("%s: Uncaught case (%d)", G_STRLOC, argtype);
			return;
		}
	}
}

/**
 * mail_stub_return_progress:
 * @stub: the MailStub
 * @percent: the percent value to return
 *
 * Sends progress data on the current operation.
 **/
void
mail_stub_return_progress (MailStub *stub, int percent)
{
	d(printf("  %d%%", percent));
	camel_stub_marshal_encode_uint32 (stub->cmd, CAMEL_STUB_RETVAL_PROGRESS);
	camel_stub_marshal_encode_uint32 (stub->cmd, percent);
	camel_stub_marshal_flush (stub->cmd);
}

/**
 * mail_stub_return_ok:
 * @stub: the MailStub
 *
 * Sends a success response to the CamelStub. One of two possible
 * completions to any non-oneway command. This also calls
 * mail_stub_push_changes().
 *
 * Since this unrefs @stub (to balance the ref in connection_handler(),
 * callers should not assume it is still valid after the call.
 **/
void
mail_stub_return_ok (MailStub *stub)
{
	d(printf("  OK\n"));
	camel_stub_marshal_flush (stub->status);
	camel_stub_marshal_encode_uint32 (stub->cmd, CAMEL_STUB_RETVAL_OK);
	camel_stub_marshal_flush (stub->cmd);
	g_object_unref (stub);
}

/**
 * mail_stub_return_error:
 * @stub: the MailStub
 * @message: the error message
 *
 * Sends a failure response to the CamelStub. The other of two
 * possible completions to any non-oneway command. This also calls
 * mail_stub_push_changes ();
 *
 * Since this unrefs @stub (to balance the ref in connection_handler(),
 * callers should not assume it is still valid after the call.
 **/
void
mail_stub_return_error (MailStub *stub, const char *message)
{
	d(printf("  Error: %s\n", message));
	camel_stub_marshal_flush (stub->status);
	camel_stub_marshal_encode_uint32 (stub->cmd, CAMEL_STUB_RETVAL_EXCEPTION);
	camel_stub_marshal_encode_string (stub->cmd, message);
	camel_stub_marshal_flush (stub->cmd);
	g_object_unref (stub);
}

/**
 * mail_stub_push_changes:
 * @stub: the MailStub
 *
 * This flushes the status channel, alerting the CamelStub of
 * new or changed messages.
 **/
void
mail_stub_push_changes (MailStub *stub)
{
	camel_stub_marshal_flush (stub->status);
}


/**
 * mail_stub_construct:
 * @stub: the #MailStub
 * @cmd_fd: command socket file descriptor
 * @status_fd: status socket file descriptor
 *
 * Initializes @stub with @cmd_fd and @status_fd.
 **/
void
mail_stub_construct (MailStub *stub, int cmd_fd, int status_fd)
{
#ifndef G_OS_WIN32
	stub->channel = g_io_channel_unix_new (cmd_fd);
#else
	stub->channel = g_io_channel_win32_new_socket (cmd_fd);
#endif
	g_io_add_watch (stub->channel, G_IO_IN | G_IO_ERR | G_IO_HUP,
			connection_handler, stub);
	stub->cmd = camel_stub_marshal_new (cmd_fd);
	stub->status = camel_stub_marshal_new (status_fd);
}
