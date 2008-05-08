/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2006 Novell, Inc.
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
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


#include <glib.h>

#ifndef G_OS_WIN32
#define CLOSESOCKET(s) close (s)
#else
#define CLOSESOCKET(s) closesocket (s)
#include <winsock2.h>
/* Note: pthread.h from pthreads-win32 also defines ETIMEDOUT (!), as
 * this same value.
 */
#define ETIMEDOUT WSAETIMEDOUT
#endif

#include <camel/camel-file-utils.h>

#include "camel-stub-marshal.h"

#if 1
#define CAMEL_MARSHAL_DEBUG
static gboolean debug = 0;
#define DEBUGGING debug
#else
#define DEBUGGING 0
#endif

/**
 * camel_stub_marshal_new:
 * @fd: a socket file descriptor
 *
 * Creates a new #CamelStubMarshal, which handles sending and
 * receiving data between #CamelExchangeStore and #MailStubExchange.
 *
 * Return value: the new #CamelStubMarshal.
 **/
CamelStubMarshal *
camel_stub_marshal_new (int fd)
{
	CamelStubMarshal *marshal = g_new0 (CamelStubMarshal, 1);

#ifdef CAMEL_MARSHAL_DEBUG
	char *e2k_debug = getenv ("E2K_DEBUG");

	if (e2k_debug && strchr (e2k_debug, 'm'))
		debug = TRUE;
#endif

	marshal->fd = fd;
	marshal->out = g_byte_array_new ();
	g_byte_array_set_size (marshal->out, 4);
	marshal->in = g_byte_array_new ();
	marshal->inptr = (char *)marshal->in->data;
	return marshal;
}

/**
 * camel_stub_marshal_free:
 * @marshal: the #CamelStubMarshal
 *
 * Frees @marshal
 **/
void
camel_stub_marshal_free (CamelStubMarshal *marshal)
{
	CLOSESOCKET (marshal->fd);
	g_byte_array_free (marshal->out, TRUE);
	g_byte_array_free (marshal->in, TRUE);
	g_free (marshal);
}

static gboolean
do_read (CamelStubMarshal *marshal, char *buf, size_t len)
{
	size_t nread = 0;
	ssize_t n;

	do {
		if ((n = camel_read_socket (marshal->fd, buf + nread, len - nread)) <= 0) {
			if (errno != ETIMEDOUT)
				break;
			else
				n = 0;
		}
		nread += n;
	} while (nread < len);

	if (nread < len) {
		CLOSESOCKET (marshal->fd);
		marshal->fd = -1;
		return FALSE;
	}

	return TRUE;
}

static int
marshal_read (CamelStubMarshal *marshal, char *buf, int len)
{
	int avail = marshal->in->len - (marshal->inptr - (char *)marshal->in->data);
	int nread;

	if (avail == 0) {
		g_byte_array_set_size (marshal->in, 4);
		marshal->inptr = (char *)marshal->in->data + 4;
		if (!do_read (marshal, (char *)marshal->in->data, 4))
			return -1;
		avail =  (int)marshal->in->data[0]        +
			((int)marshal->in->data[1] <<  8) +
			((int)marshal->in->data[2] << 16) +
			((int)marshal->in->data[3] << 24) - 4;
		g_byte_array_set_size (marshal->in, avail + 4);
		marshal->inptr = (char *)marshal->in->data + 4;
		if (!do_read (marshal, ((char *)marshal->in->data) + 4, avail)) {
			g_byte_array_set_size (marshal->in, 4);
			marshal->inptr = (char *)marshal->in->data + 4;
			return -1;
		}
	}

	if (len <= avail)
		nread = len;
	else
		nread = avail;
	memcpy (buf, marshal->inptr, nread);
	marshal->inptr += nread;

	if (DEBUGGING) {
		if (nread < len)
			printf ("<<< short read: %d of %d\n", nread, len);
	}

	return nread;
}

static int
marshal_getc (CamelStubMarshal *marshal)
{
	char buf;

	if (marshal_read (marshal, &buf, 1) == 1)
		return (unsigned char)buf;
	return -1;
}

static void
encode_uint32 (CamelStubMarshal *marshal, guint32 value)
{
	unsigned char c;
	int i;

	for (i = 28; i > 0; i -= 7) {
		if (value >= (1 << i)) {
			c = (value >> i) & 0x7f;
			g_byte_array_append (marshal->out, &c, 1);
		}
	}
	c = value | 0x80;
	g_byte_array_append (marshal->out, &c, 1);
}

static int
decode_uint32 (CamelStubMarshal *marshal, guint32 *dest)
{
        guint32 value = 0;
	int v;

        /* until we get the last byte, keep decoding 7 bits at a time */
        while ( ((v = marshal_getc (marshal)) & 0x80) == 0 && v!=-1) {
                value |= v;
                value <<= 7;
        }
	if (v == -1) {
		*dest = value >> 7;
		return -1;
	}
	*dest = value | (v & 0x7f);

        return 0;
}

static void
encode_string (CamelStubMarshal *marshal, const char *str)
{
	int len;

	if (!str || !*str) {
		encode_uint32 (marshal, 1);
		return;
	}

	len = strlen (str);
	encode_uint32 (marshal, len + 1);
	g_byte_array_append (marshal->out, (guint8 *) str, len);
}

static int
decode_string (CamelStubMarshal *marshal, char **str)
{
	guint32 len;
	char *ret;

	if (decode_uint32 (marshal, &len) == -1) {
		*str = NULL;
		return -1;
	}

	if (len == 1) {
		*str = NULL;
		return 0;
	}

	ret = g_malloc (len--);
	if (marshal_read (marshal, ret, len) != len) {
		g_free (ret);
		*str = NULL;
		return -1;
	}

	ret[len] = 0;
	*str = ret;
	return 0;
}


/**
 * camel_stub_marshal_encode_uint32:
 * @marshal: the #CamelStubMarshal
 * @value: value to send
 *
 * Sends @value across @marshall.
 **/
void
camel_stub_marshal_encode_uint32 (CamelStubMarshal *marshal, guint32 value)
{
	if (DEBUGGING)
		printf (">>> %lu\n", (unsigned long)value);

	encode_uint32 (marshal, value);
}

/**
 * camel_stub_marshal_decode_uint32:
 * @marshal: the #CamelStubMarshal
 * @dest: on successful return, will contain the received value.
 *
 * Receives a uint32 value across @marshal.
 *
 * Return value: 0 on success, -1 on failure.
 **/
int
camel_stub_marshal_decode_uint32 (CamelStubMarshal *marshal, guint32 *dest)
{
	if (decode_uint32 (marshal, dest) == -1)
		return -1;

	if (DEBUGGING)
		printf ("<<< %lu\n", (unsigned long)*dest);
	return 0;
}

/**
 * camel_stub_marshal_encode_string:
 * @marshal: the #CamelStubMarshal
 * @str: string to send
 *
 * Sends @str across @marshall.
 **/
void
camel_stub_marshal_encode_string (CamelStubMarshal *marshal, const char *str)
{
	if (DEBUGGING)
		printf (">>> \"%s\"\n", str ? str : "");

	encode_string (marshal, str);
}

/**
 * camel_stub_marshal_decode_string:
 * @marshal: the #CamelStubMarshal
 * @str: on successful return, will contain the received string.
 *
 * Receives a string across @marshal.
 *
 * Return value: 0 on success, -1 on failure.
 **/
int
camel_stub_marshal_decode_string (CamelStubMarshal *marshal, char **str)
{
	if (decode_string (marshal, str) == -1)
		return -1;
	if (!*str)
		*str = g_malloc0 (1);

	if (DEBUGGING)
		printf ("<<< \"%s\"\n", *str);
	return 0;
}

/**
 * camel_stub_marshal_encode_folder:
 * @marshal: the #CamelStubMarshal
 * @name: folder name to send
 *
 * Sends @name across @marshall. This is an optimization over
 * camel_stub_marshal_encode_string(), because if the same folder name
 * is used in successive calls to camel_stub_marshal_encode_folder()
 * (as will normally be the case when doing many things in the same folder),
 * the folder name only needs to be sent once.
 **/
void
camel_stub_marshal_encode_folder (CamelStubMarshal *marshal, const char *name)
{
	if (marshal->last_folder) {
		if (!strcmp (name, marshal->last_folder)) {
			if (DEBUGGING)
				printf (">>> (%s)\n", name);
			encode_string (marshal, "");
			return;
		}

		g_free (marshal->last_folder);
	}

	if (DEBUGGING)
		printf (">>> %s\n", name);
	encode_string (marshal, name);
	marshal->last_folder = g_strdup (name);
}

/**
 * camel_stub_marshal_decode_folder:
 * @marshal: the #CamelStubMarshal
 * @name: on successful return, will contain the received folder name.
 *
 * Receives a folder name across @marshal.
 *
 * Return value: 0 on success, -1 on failure.
 **/
int
camel_stub_marshal_decode_folder (CamelStubMarshal *marshal, char **name)
{
	if (decode_string (marshal, name) == -1)
		return -1;
	if (!*name) {
		*name = g_strdup (marshal->last_folder);
		if (DEBUGGING)
			printf ("<<< (%s)\n", *name);
	} else {
		g_free (marshal->last_folder);
		marshal->last_folder = g_strdup (*name);
		if (DEBUGGING)
			printf ("<<< %s\n", *name);
	}

	return 0;
}

/**
 * camel_stub_marshal_encode_bytes:
 * @marshal: the #CamelStubMarshal
 * @ba: data to send
 *
 * Sends @ba across @marshall.
 **/
void
camel_stub_marshal_encode_bytes (CamelStubMarshal *marshal, GByteArray *ba)
{
	if (DEBUGGING)
		printf (">>> %d bytes\n", ba->len);
	encode_uint32 (marshal, ba->len);
	g_byte_array_append (marshal->out, ba->data, ba->len);
}

/**
 * camel_stub_marshal_decode_bytes:
 * @marshal: the #CamelStubMarshal
 * @ba: on successful return, will contain the received data
 *
 * Receives a byte array across @marshal.
 *
 * Return value: 0 on success, -1 on failure.
 **/
int
camel_stub_marshal_decode_bytes (CamelStubMarshal *marshal, GByteArray **ba)
{
	guint32 len;

	if (decode_uint32 (marshal, &len) == -1) {
		*ba = NULL;
		return -1;
	}

	*ba = g_byte_array_new ();
	g_byte_array_set_size (*ba, len);
	if (len > 0 && marshal_read (marshal, (char *) (*ba)->data, len) != len) {
		g_byte_array_free (*ba, TRUE);
		*ba = NULL;
		return -1;
	}

	if (DEBUGGING)
		printf ("<<< %d bytes\n", (*ba)->len);
	return 0;
}


/**
 * camel_stub_marshal_flush:
 * @marshal: a #CamelStubMarshal
 *
 * Flushes pending data on @marshal. (No data is actually sent by the
 * "encode" routines until camel_stub_marshal_flush() is called.)
 *
 * Return value: 0 on success, -1 on failure.
 **/
int
camel_stub_marshal_flush (CamelStubMarshal *marshal)
{
	int left;

	if (marshal->out->len == 4)
		return 0;

	if (marshal->fd == -1) {
		if (DEBUGGING)
			printf ("--- flush failed\n");
		return -1;
	}

	if (DEBUGGING)
		printf ("---\n");

	left = marshal->out->len;

	marshal->out->data[0] =  left        & 0xFF;
	marshal->out->data[1] = (left >>  8) & 0xFF;
	marshal->out->data[2] = (left >> 16) & 0xFF;
	marshal->out->data[3] = (left >> 24) & 0xFF;

	if (camel_write_socket (marshal->fd, (char *) marshal->out->data, marshal->out->len) == -1) {
		CLOSESOCKET (marshal->fd);
		marshal->fd = -1;
		return -1;
	}

	g_byte_array_set_size (marshal->out, 4);

	return 0;
}

/**
 * camel_stub_marshal_eof:
 * @marshal: a #CamelStubMarshal
 *
 * Tests if the other end of @marshal's connection has been closed.
 *
 * Return value: %TRUE or %FALSE.
 **/
gboolean
camel_stub_marshal_eof (CamelStubMarshal *marshal)
{
	return marshal->fd == -1;
}
