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

/* camel-exchange-folder.c: class for an Exchange folder */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <camel/camel-i18n.h>

#include "camel-exchange-folder.h"
#include "camel-exchange-search.h"
#include "camel-exchange-store.h"
#include "camel-exchange-summary.h"

#include <camel/camel-data-wrapper.h>
#include <camel/camel-exception.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-mime-filter-crlf.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-multipart.h>
#include <camel/camel-session.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-stream-mem.h>

static CamelFolderClass *parent_class = NULL;

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static void refresh_info (CamelFolder *folder, CamelException *ex);
static void folder_sync (CamelFolder *folder, gboolean expunge,
			 CamelException *ex);
static void expunge             (CamelFolder *folder,
				 CamelException *ex);
static void append_message (CamelFolder *folder, CamelMimeMessage *message,
			    const CamelMessageInfo *info, char **appended_uid,
			    CamelException *ex);
static CamelMimeMessage *get_message         (CamelFolder *folder,
					      const gchar *uid,
					      CamelException *ex);
static GPtrArray      *search_by_expression  (CamelFolder *folder,
					      const char *exp,
					      CamelException *ex);
static GPtrArray      *search_by_uids        (CamelFolder *folder,
					      const char *expression,
					      GPtrArray *uids,
					      CamelException *ex);
static void            search_free           (CamelFolder *folder,
					      GPtrArray *uids);
static void            transfer_messages_to  (CamelFolder *source,
					      GPtrArray *uids,
					      CamelFolder *dest,
					      GPtrArray **transferred_uids,
					      gboolean delete_originals,
					      CamelException *ex);
static void   transfer_messages_the_hard_way (CamelFolder *source,
					      GPtrArray *uids,
					      CamelFolder *dest,
					      GPtrArray **transferred_uids,
					      gboolean delete_originals,
					      CamelException *ex);

static void
class_init (CamelFolderClass *camel_folder_class)
{
	parent_class = (CamelFolderClass *)camel_type_get_global_classfuncs (camel_folder_get_type ());

	/* virtual method definition */
	camel_folder_class->sync = folder_sync;
	camel_folder_class->refresh_info = refresh_info;
	camel_folder_class->expunge = expunge;
	camel_folder_class->append_message = append_message;
	camel_folder_class->get_message = get_message;
	camel_folder_class->search_by_expression = search_by_expression;
	camel_folder_class->search_by_uids = search_by_uids;
	camel_folder_class->search_free = search_free;
	camel_folder_class->transfer_messages_to = transfer_messages_to;
}

#define CAMEL_EXCHANGE_SERVER_FLAGS \
	(CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_ANSWERED_ALL | \
	 CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_SEEN)

static void
init (CamelFolder *folder)
{
	folder->folder_flags = CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY | CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;
	folder->permanent_flags =
		CAMEL_EXCHANGE_SERVER_FLAGS | CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_USER;
}

static void
free_index_and_mid (gpointer thread_index, gpointer message_id, gpointer d)
{
	g_free (thread_index);
	g_free (message_id);
}

static void
finalize (CamelExchangeFolder *exch)
{
	camel_object_unref (CAMEL_OBJECT (exch->cache));

	if (exch->thread_index_to_message_id) {
		g_hash_table_foreach (exch->thread_index_to_message_id,
				      free_index_and_mid, NULL);
		g_hash_table_destroy (exch->thread_index_to_message_id);
	}
	g_free (exch->source);
}

CamelType
camel_exchange_folder_get_type (void)
{
	static CamelType camel_exchange_folder_type = CAMEL_INVALID_TYPE;

	if (camel_exchange_folder_type == CAMEL_INVALID_TYPE) {
		camel_exchange_folder_type = camel_type_register (
			CAMEL_FOLDER_TYPE, "CamelExchangeFolder",
			sizeof (CamelExchangeFolder),
			sizeof (CamelExchangeFolderClass),
			(CamelObjectClassInitFunc) class_init,
			NULL,
			(CamelObjectInitFunc) init,
			(CamelObjectFinalizeFunc) finalize );
	}

	return camel_exchange_folder_type;
}


static void
folder_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	GPtrArray *summary, *uids;
	CamelMessageInfo *info;
	CamelFolder *trash;
	int i;

	/* Give the backend a chance to process queued changes. */
	camel_stub_send (exch->stub, NULL, CAMEL_STUB_CMD_SYNC_FOLDER,
			 CAMEL_STUB_ARG_FOLDER, folder->full_name,
			 CAMEL_STUB_ARG_END);

	/* If there are still deleted messages left, we need to delete
	 * them the hard way.
	 */
	summary = camel_folder_get_summary (folder);
	uids = g_ptr_array_new ();
	for (i = 0; i < summary->len; i++) {
		info = summary->pdata[i];
		if (!(camel_message_info_flags(info) & CAMEL_MESSAGE_DELETED))
			continue;
		g_ptr_array_add (uids, (char *)camel_message_info_uid (info));
	}
	if (uids->len) {
		trash = camel_store_get_trash (folder->parent_store, ex);
		if (trash) {
			transfer_messages_the_hard_way (folder, uids, trash,
							NULL, TRUE, ex);
		}
	}
	g_ptr_array_free (uids, TRUE);
	camel_folder_free_summary (folder, summary);

	camel_folder_summary_save (folder->summary);
}

static void
refresh_info (CamelFolder *folder, CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);

	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_REFRESH_FOLDER,
			 CAMEL_STUB_ARG_FOLDER, folder->full_name,
			 CAMEL_STUB_ARG_END);
}

static void
expunge (CamelFolder *folder, CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	CamelFolder *trash;
	GPtrArray *uids;

	trash = camel_store_get_trash (folder->parent_store, NULL);
	if (trash)
		camel_object_unref (CAMEL_OBJECT (trash));
	if (trash != folder) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Can only expunge in Deleted Items folder"));
		return;
	}

	uids = camel_folder_get_uids (folder);
	camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_EXPUNGE_UIDS,
			 CAMEL_STUB_ARG_FOLDER, folder->full_name,
			 CAMEL_STUB_ARG_STRINGARRAY, uids,
			 CAMEL_STUB_ARG_END);
	camel_folder_free_uids (folder, uids);
}

static void
append_message_data (CamelFolder *folder, GByteArray *message,
		     const char *subject, const CamelMessageInfo *info,
		     char **appended_uid, CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	CamelStream *stream_cache;
	char *new_uid;

	if (!subject)
		subject = camel_message_info_subject (info);;
	if (!subject)
		subject = _("No Subject");

	if (camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_APPEND_MESSAGE,
			     CAMEL_STUB_ARG_FOLDER, folder->full_name,
			     CAMEL_STUB_ARG_UINT32, camel_message_info_flags(info),
			     CAMEL_STUB_ARG_STRING, subject,
			     CAMEL_STUB_ARG_BYTEARRAY, message,
			     CAMEL_STUB_ARG_RETURN,
			     CAMEL_STUB_ARG_STRING, &new_uid,
			     CAMEL_STUB_ARG_END)) {
		stream_cache = camel_data_cache_add (exch->cache,
						     "cache", new_uid, NULL);
		if (stream_cache) {
			camel_stream_write (stream_cache, message->data,
					    message->len);
			camel_stream_flush (stream_cache);
			camel_object_unref (CAMEL_OBJECT (stream_cache));
		}
		if (appended_uid)
			*appended_uid = new_uid;
		else
			g_free (new_uid);
	} else if (appended_uid)
		*appended_uid = NULL;
}

static void
append_message (CamelFolder *folder, CamelMimeMessage *message,
		const CamelMessageInfo *info, char **appended_uid,
		CamelException *ex)
{
	CamelStream *stream_mem;

	stream_mem = camel_stream_mem_new ();
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message),
					    stream_mem);
	camel_stream_flush (stream_mem);

	append_message_data (folder, CAMEL_STREAM_MEM (stream_mem)->buffer,
			     camel_mime_message_get_subject (message),
			     info, appended_uid, ex);

	camel_object_unref (CAMEL_OBJECT (stream_mem));
}

static void
fix_broken_multipart_related (CamelMimePart *part)
{
	CamelContentType *content_type;
	CamelDataWrapper *content;
	CamelMultipart *multipart, *new;
	CamelMimePart *subpart;
	int i, count, broken_parts;
	
	content = camel_medium_get_content_object (CAMEL_MEDIUM (part));
	
	content_type = content->mime_type;
	if (camel_content_type_is (content_type, "message", "rfc822")) {
		fix_broken_multipart_related (CAMEL_MIME_PART (content));
		return;
	}

	if (!camel_content_type_is (content_type, "multipart", "*"))
		return;
	multipart = CAMEL_MULTIPART (content);
	count = camel_multipart_get_number (multipart);

	if (camel_content_type_is (content_type, "multipart", "related") &&
	    camel_medium_get_header (CAMEL_MEDIUM (part), "X-MimeOLE"))
		broken_parts = count - 1;
	else
		broken_parts = 0;

	for (i = 0; i < count; i++) {
		subpart = camel_multipart_get_part (multipart, i);
		fix_broken_multipart_related (subpart);
		if (broken_parts && camel_mime_part_get_content_id (subpart))
			broken_parts--;
	}

	if (broken_parts) {
		new = camel_multipart_new ();
		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (new),
						  "multipart/mixed");
		camel_multipart_set_boundary (new, camel_multipart_get_boundary (multipart));
		camel_multipart_set_preface (new, multipart->preface);
		camel_multipart_set_postface (new, multipart->postface);

		for (i = 0; i < count; i++) {
			subpart = camel_multipart_get_part (multipart, i);
			camel_multipart_add_part (new, subpart);
		}

		camel_medium_set_content_object (CAMEL_MEDIUM (part),
						 CAMEL_DATA_WRAPPER (new));
		camel_object_unref (CAMEL_OBJECT (new));
	}
}

static GByteArray *
get_message_data (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	CamelStream *stream, *stream_mem;
	GByteArray *ba;

	stream = camel_data_cache_get (exch->cache, "cache", uid, NULL);
	if (stream) {
		ba = g_byte_array_new ();
		stream_mem = camel_stream_mem_new ();
		camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream_mem), ba);
		camel_stream_reset (stream);
		camel_stream_write_to_stream (stream, stream_mem);
		camel_object_unref (CAMEL_OBJECT (stream_mem));
		camel_object_unref (CAMEL_OBJECT (stream));

		return ba;
	}

	if (!camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_MESSAGE,
			      CAMEL_STUB_ARG_FOLDER, folder->full_name,
			      CAMEL_STUB_ARG_STRING, uid,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_BYTEARRAY, &ba,
			      CAMEL_STUB_ARG_END))
		return NULL;

	stream = camel_data_cache_add (exch->cache, "cache", uid, ex);
	if (!stream) {
		g_byte_array_free (ba, TRUE);
		return NULL;
	}

	camel_stream_write (stream, ba->data, ba->len);
	camel_stream_flush (stream);
	camel_object_unref (CAMEL_OBJECT (stream));

	return ba;
}

static CamelMimeMessage *
get_message (CamelFolder *folder, const char *uid, CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	CamelMimeMessage *msg;
	CamelStream *stream;
	CamelStreamFilter *filtered_stream;
	CamelMimeFilter *crlffilter;
	GByteArray *ba;

	ba = get_message_data (folder, uid, ex);
	if (!ba)
		return NULL;
	stream = camel_stream_mem_new_with_byte_array (ba);

	crlffilter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_DECODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	filtered_stream = camel_stream_filter_new_with_stream (stream);
	camel_stream_filter_add (filtered_stream, crlffilter);
	camel_object_unref (CAMEL_OBJECT (crlffilter));
	camel_object_unref (CAMEL_OBJECT (stream));

	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),
						  CAMEL_STREAM (filtered_stream));
	camel_object_unref (CAMEL_OBJECT (filtered_stream));

	camel_mime_message_set_source (msg, exch->source);
	fix_broken_multipart_related (CAMEL_MIME_PART (msg));
	return msg;
}

static GPtrArray *
search_by_expression (CamelFolder *folder, const char *expression,
		      CamelException *ex)
{
	CamelFolderSearch *search;
	GPtrArray *matches, *summary, *response;
	int i;

	search = camel_exchange_search_new ();
	camel_folder_search_set_folder (search, folder);
	summary = camel_folder_get_summary (folder);
	camel_folder_search_set_summary (search, summary);
	matches = camel_folder_search_execute_expression (search, expression, ex);
	camel_folder_free_summary (folder, summary);

	if (matches) {
		response = g_ptr_array_new ();
		for (i = 0; i < matches->len; i++)
			g_ptr_array_add (response, g_strdup (matches->pdata[i]));
		camel_folder_search_free_result (search, matches);
	} else
		response = NULL;

	camel_object_unref (CAMEL_OBJECT (search));

	return response;
}

static GPtrArray *
search_by_uids (CamelFolder *folder, const char *expression,
		GPtrArray *uids, CamelException *ex)
{
	CamelFolderSearch *search;
	GPtrArray *matches, *summary, *response;
	int i;

	summary = g_ptr_array_new();
	for (i = 0; i < uids->len; i++) {
		CamelMessageInfo *info;

		info = camel_folder_get_message_info (folder, uids->pdata[i]);
		if (info)
			g_ptr_array_add (summary, info);
	}

	if (summary->len == 0)
		return summary;

	search = camel_exchange_search_new ();
	camel_folder_search_set_folder (search, folder);
	camel_folder_search_set_summary (search, summary);
	matches = camel_folder_search_execute_expression (search, expression, ex);

	if (matches) {
		response = g_ptr_array_new ();
		for (i = 0; i < matches->len; i++)
			g_ptr_array_add (response, g_strdup (matches->pdata[i]));
		camel_folder_search_free_result (search, matches);
	} else
		response = NULL;

	for (i = 0; i < summary->len; i++)
		camel_folder_free_message_info (folder, summary->pdata[i]);
	g_ptr_array_free (summary, TRUE);

	camel_object_unref (CAMEL_OBJECT (search));

	return response;
}

static void
search_free (CamelFolder *folder, GPtrArray *uids)
{
	int i;

	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);
	g_ptr_array_free (uids, TRUE);
}

static void
transfer_messages_the_hard_way (CamelFolder *source, GPtrArray *uids,
				CamelFolder *dest,
				GPtrArray **transferred_uids,
				gboolean delete_originals, CamelException *ex)
{
	CamelExchangeFolder *exch_source = CAMEL_EXCHANGE_FOLDER (source);
	CamelException local_ex;
	CamelMessageInfo *info;
	GByteArray *ba;
	char *ret_uid;
	int i;

	if (transferred_uids)
		*transferred_uids = g_ptr_array_new ();
	camel_exception_init (&local_ex);

	for (i = 0; i < uids->len; i++) {
		info = camel_folder_summary_uid (source->summary, uids->pdata[i]);
		if (!info)
			continue;

		ba = get_message_data (source, uids->pdata[i], &local_ex);
		if (!ba) {
			camel_message_info_free(info);
			break;
		}

		append_message_data (dest, ba, NULL, info, &ret_uid, &local_ex);
		camel_message_info_free(info);
		g_byte_array_free (ba, TRUE);

		if (camel_exception_is_set (&local_ex))
			break;

		if (transferred_uids)
			g_ptr_array_add (*transferred_uids, ret_uid);
		else
			g_free (ret_uid);
	}

	if (camel_exception_is_set (&local_ex)) {
		camel_exception_xfer (ex, &local_ex);
		return;
	}

	if (delete_originals) {
		camel_stub_send (exch_source->stub, NULL,
				 CAMEL_STUB_CMD_EXPUNGE_UIDS,
				 CAMEL_STUB_ARG_FOLDER, source->full_name,
				 CAMEL_STUB_ARG_STRINGARRAY, uids,
				 CAMEL_STUB_ARG_END);
	}
}

static void
cache_xfer (CamelExchangeFolder *stub_source, CamelExchangeFolder *stub_dest,
	    GPtrArray *src_uids, GPtrArray *dest_uids, gboolean delete)
{
	CamelStream *src, *dest;
	int i;

	for (i = 0; i < src_uids->len; i++) {
		if (!*(char *)dest_uids->pdata[i])
			continue;

		src = camel_data_cache_get (stub_source->cache, "cache",
					    src_uids->pdata[i], NULL);
		if (!src)
			continue;

		dest = camel_data_cache_add (stub_dest->cache, "cache",
					     dest_uids->pdata[i], NULL);
		if (dest) {
			camel_stream_write_to_stream (src, dest);
			camel_object_unref (CAMEL_OBJECT (dest));
		}
		camel_object_unref (CAMEL_OBJECT (src));

		if (remove) {
			camel_data_cache_remove (stub_source->cache, "cache",
						 src_uids->pdata[i], NULL);
		}
	}
}

static void
transfer_messages_to (CamelFolder *source, GPtrArray *uids,
		      CamelFolder *dest, GPtrArray **transferred_uids,
		      gboolean delete_originals, CamelException *ex)
{
	CamelExchangeFolder *exch_source = CAMEL_EXCHANGE_FOLDER (source);
	CamelExchangeFolder *exch_dest = CAMEL_EXCHANGE_FOLDER (dest);
	GPtrArray *ret_uids = NULL;
	int hier_len;

	camel_operation_start (NULL, delete_originals ? _("Moving messages") :
			       _("Copying messages"));

	hier_len = strcspn (source->full_name, "/");
	if (strncmp (source->full_name, dest->full_name, hier_len) != 0) {
		transfer_messages_the_hard_way (source, uids, dest,
						transferred_uids,
						delete_originals, ex);
		return;
	}

	if (camel_stub_send (exch_source->stub, ex,
			     CAMEL_STUB_CMD_TRANSFER_MESSAGES,
			     CAMEL_STUB_ARG_FOLDER, source->full_name,
			     CAMEL_STUB_ARG_FOLDER, dest->full_name,
			     CAMEL_STUB_ARG_STRINGARRAY, uids,
			     CAMEL_STUB_ARG_UINT32, (guint32)delete_originals,
			     CAMEL_STUB_ARG_RETURN,
			     CAMEL_STUB_ARG_STRINGARRAY, &ret_uids,
			     CAMEL_STUB_ARG_END)) {
		if (ret_uids->len != 0)
			cache_xfer (exch_source, exch_dest, uids, ret_uids, FALSE);

		if (transferred_uids)
			*transferred_uids = ret_uids;
		else {
			int i;

			for (i = 0; i < ret_uids->len; i++)
				g_free (ret_uids->pdata[i]);
			g_ptr_array_free (ret_uids, TRUE);
		}
	} else if (transferred_uids)
		*transferred_uids = NULL;

	camel_operation_end (NULL);
}

/* A new post to a folder gets a 27-byte-long thread index. (The value
 * is apparently unique but meaningless.) Each reply to a post gets a
 * 32-byte-long thread index whose first 27 bytes are the same as the
 * parent's thread index. Each reply to any of those gets a
 * 37-byte-long thread index, etc. The Thread-Index header contains a
 * base64 representation of this value.
 */
static CamelSummaryMessageID *
find_parent (CamelExchangeFolder *exch, const char *thread_index)
{
	CamelSummaryMessageID *msgid;
	char *decoded, *parent;
	int dlen;

	decoded = g_strdup (thread_index);
	dlen = camel_base64_decode_simple (decoded, strlen (decoded));
	if (dlen < 5) {
		/* Shouldn't happen */
		g_free (decoded);
		return NULL;
	}

	parent = camel_base64_encode_simple (decoded, dlen - 5);
	g_free (decoded);

	msgid = g_hash_table_lookup (exch->thread_index_to_message_id,
				     parent);
	g_free (parent);
	return msgid;
}

/**
 * camel_exchange_folder_add_message:
 * @exch: the folder
 * @uid: UID of the new message
 * @flags: message flags
 * @size: size of the new message
 * @headers: RFC822 headers of the new message
 *
 * Append the new message described by @uid, @flags, @size, and
 * @headers to @exch's summary.
 **/
void
camel_exchange_folder_add_message (CamelExchangeFolder *exch,
				   const char *uid, guint32 flags,
				   guint32 size, const char *headers)
{
	CamelFolder *folder = CAMEL_FOLDER (exch);
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	CamelFolderChangeInfo *changes;
	CamelStream *stream;
	CamelMimeMessage *msg;

	info = camel_folder_summary_uid (folder->summary, uid);
	if (info) {
		camel_message_info_free(info);
		return;
	}

	stream = camel_stream_mem_new_with_buffer (headers, strlen (headers));
	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg), stream);
	camel_object_unref (CAMEL_OBJECT (stream));

	info = camel_folder_summary_info_new_from_message (folder->summary, msg);
	einfo = (CamelExchangeMessageInfo *)info;

	if (einfo->thread_index) {
		CamelSummaryMessageID *parent;

		if (einfo->info.references && einfo->info.references->size)
			g_hash_table_insert (exch->thread_index_to_message_id,
					     g_strdup (einfo->thread_index),
					     g_memdup (&einfo->info.references->references[0], sizeof (CamelSummaryMessageID)));

		parent = find_parent (exch, einfo->thread_index);
		if (parent) {
			if (einfo->info.references == NULL)
				einfo->info.references = g_malloc0(sizeof(CamelSummaryReferences));
			else
				einfo->info.references = g_realloc(einfo->info.references, sizeof(CamelSummaryReferences)
								   + (sizeof(CamelSummaryMessageID)
								      * einfo->info.references->size));

			memcpy(&einfo->info.references->references[einfo->info.references->size], parent, sizeof(*parent));
			einfo->info.references->size++;
		}
	}
	camel_object_unref (CAMEL_OBJECT (msg));

	info->uid = g_strdup (uid);
	einfo->info.flags = flags;
	einfo->info.size = size;

	camel_folder_summary_add (folder->summary, info);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_add_uid (changes, uid);
	camel_folder_change_info_recent_uid (changes, uid);
	camel_object_trigger_event (CAMEL_OBJECT (exch),
				    "folder_changed", changes);
	camel_folder_change_info_free (changes);
	return;
}

/**
 * camel_exchange_folder_remove_message:
 * @exch: the folder
 * @uid: message to remove
 *
 * Remove the message indicated by @uid from @exch's summary and cache.
 **/
void
camel_exchange_folder_remove_message (CamelExchangeFolder *exch,
				      const char *uid)
{
	CamelFolderSummary *summary = CAMEL_FOLDER (exch)->summary;
	CamelFolderChangeInfo *changes;
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;

	info = camel_folder_summary_uid (summary, uid);
	if (!info)
		return;

	einfo = (CamelExchangeMessageInfo *)info;
	if (einfo->thread_index) {
		gpointer key, value;

		if (g_hash_table_lookup_extended (exch->thread_index_to_message_id,
						  einfo->thread_index,
						  &key, &value)) {
			g_hash_table_remove (exch->thread_index_to_message_id, key);
			g_free (key);
			g_free (value);
		}
	}

	camel_folder_summary_remove (summary, info);
	camel_message_info_free (info);
	camel_data_cache_remove (exch->cache, "cache", uid, NULL);

	changes = camel_folder_change_info_new ();
	camel_folder_change_info_remove_uid (changes, uid);
	camel_object_trigger_event (CAMEL_OBJECT (exch), "folder_changed",
				    changes);
	camel_folder_change_info_free (changes);
}

/**
 * camel_exchange_folder_uncache_message:
 * @exch: the folder
 * @uid: message to uncache
 *
 * Remove the message indicated by @uid from @exch's cache, but NOT
 * from the summary.
 **/
void
camel_exchange_folder_uncache_message (CamelExchangeFolder *exch,
				       const char *uid)
{
	camel_data_cache_remove (exch->cache, "cache", uid, NULL);
}

/**
 * camel_exchange_folder_update_message_flags:
 * @exch: the folder
 * @uid: the message
 * @flags: new message flags
 *
 * Update the message flags of @uid in @exch's summary to be @flags.
 * Only the bits in the mask %CAMEL_EXCHANGE_SERVER_FLAGS are valid.
 **/
void
camel_exchange_folder_update_message_flags (CamelExchangeFolder *exch,
					    const char *uid, guint32 flags)
{
	CamelFolder *folder = CAMEL_FOLDER (exch);
	CamelMessageInfoBase *info;
	CamelFolderChangeInfo *changes;

	info = (CamelMessageInfoBase *)camel_folder_summary_uid (folder->summary, uid);
	if (!info)
		return;

	flags |= (info->flags & ~CAMEL_EXCHANGE_SERVER_FLAGS);

	if (info->flags != flags) {
		info->flags = flags;
		camel_folder_summary_touch (folder->summary);

		changes = camel_folder_change_info_new ();
		camel_folder_change_info_change_uid (changes, uid);
		camel_object_trigger_event (CAMEL_OBJECT (exch),
					    "folder_changed", changes);
		camel_folder_change_info_free (changes);
	}
}

/**
 * camel_exchange_folder_update_message_tag:
 * @exch: the folder
 * @uid: the message
 * @name: the tag name
 * @value: the new value for @name
 *
 * Update the value of tag @name of @uid in @exch's summary to be
 * @value (or remove the tag altogether if @value is %NULL).
 **/
void
camel_exchange_folder_update_message_tag (CamelExchangeFolder *exch,
					  const char *uid,
					  const char *name,
					  const char *value)
{
	CamelFolder *folder = CAMEL_FOLDER (exch);
	CamelMessageInfoBase *info;
	CamelFolderChangeInfo *changes;

	info = (CamelMessageInfoBase *)camel_folder_summary_uid (folder->summary, uid);
	if (!info)
		return;

	camel_tag_set (&info->user_tags, name, value);

	camel_folder_summary_touch (folder->summary);
	changes = camel_folder_change_info_new ();
	camel_folder_change_info_change_uid (changes, uid);
	camel_object_trigger_event (CAMEL_OBJECT (exch),
				    "folder_changed", changes);
	camel_folder_change_info_free (changes);
}

/**
 * camel_exchange_folder_construct:
 * @folder: the folder
 * @parent: @folder's parent store
 * @name: the full name of the folder
 * @camel_flags: the folder flags passed to camel_store_get_folder().
 * @folder_dir: local directory this folder can cache data into
 * @stub: the #CamelStub.
 * @ex: a #CamelException
 *
 * Constructs @folder by requesting the necessary data from the server
 * via @stub.
 *
 * Return value: success or failure.
 **/
gboolean
camel_exchange_folder_construct (CamelFolder *folder, CamelStore *parent,
				 const char *name, guint32 camel_flags,
				 const char *folder_dir,
				 CamelStub *stub, CamelException *ex)
{
	CamelExchangeFolder *exch = (CamelExchangeFolder *)folder;
	const char *short_name;
	char *summary_file;
	GPtrArray *summary, *uids;
	GByteArray *flags;
	guint32 folder_flags;
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	int i, len;

	short_name = strrchr (name, '/');
	if (!short_name++)
		short_name = name;
	camel_folder_construct (folder, parent, name, short_name);

	if (camel_mkdir (folder_dir, S_IRWXU) != 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not create directory %s: %s"),
				      folder_dir, g_strerror (errno));
		return FALSE;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	folder->summary = camel_exchange_summary_new (folder, summary_file);
	g_free (summary_file);
	if (!folder->summary) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not load summary for %s"),
				      name);
		return FALSE;
	}

	exch->cache = camel_data_cache_new (folder_dir, 0, ex);
	if (!exch->cache) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not create cache for %s"),
				      name);
		return FALSE;
	}

	exch->thread_index_to_message_id =
		g_hash_table_new (g_str_hash, g_str_equal);

	len = camel_folder_summary_count (folder->summary);
	for (i = 0; i < len; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		einfo = (CamelExchangeMessageInfo *)info;

		if (einfo->thread_index && einfo->info.references && einfo->info.references->size) {
			g_hash_table_insert (exch->thread_index_to_message_id,
					     g_strdup (einfo->thread_index),
					     g_memdup (&einfo->info.references->references[0], sizeof (CamelSummaryMessageID)));
		}

		camel_message_info_free(info);
	}

	if (stub) {
		gboolean ok, create = camel_flags & CAMEL_STORE_FOLDER_CREATE;

		exch->stub = stub;

		summary = camel_folder_get_summary (folder);
		uids = g_ptr_array_new ();
		g_ptr_array_set_size (uids, summary->len);
		flags = g_byte_array_new ();
		g_byte_array_set_size (flags, summary->len);

		for (i = 0; i < summary->len; i++) {
			info = summary->pdata[i];
			uids->pdata[i] = (char *)camel_message_info_uid (info);
			flags->data[i] = ((CamelMessageInfoBase *)info)->flags & CAMEL_EXCHANGE_SERVER_FLAGS;
			camel_tag_list_free (&((CamelMessageInfoBase *)info)->user_tags);
		}

		camel_operation_start (NULL, _("Scanning for changed messages"));
		ok = camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_GET_FOLDER,
				      CAMEL_STUB_ARG_FOLDER, name,
				      CAMEL_STUB_ARG_UINT32, create,
				      CAMEL_STUB_ARG_STRINGARRAY, uids,
				      CAMEL_STUB_ARG_BYTEARRAY, flags,
				      CAMEL_STUB_ARG_RETURN,
				      CAMEL_STUB_ARG_UINT32, &folder_flags,
				      CAMEL_STUB_ARG_STRING, &exch->source,
				      CAMEL_STUB_ARG_END);
		camel_operation_end (NULL);
		g_ptr_array_free (uids, TRUE);
		g_byte_array_free (flags, TRUE);
		camel_folder_free_summary (folder, summary);
		if (!ok)
			return FALSE;

		if (folder_flags & CAMEL_STUB_FOLDER_FILTER)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
		if (folder_flags & CAMEL_STUB_FOLDER_FILTER_JUNK)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;

		camel_exchange_summary_set_readonly (folder->summary, folder_flags & CAMEL_STUB_FOLDER_READONLY);

		camel_operation_start (NULL, _("Fetching summary information for new messages"));
		ok = camel_stub_send (exch->stub, ex, CAMEL_STUB_CMD_REFRESH_FOLDER,
				      CAMEL_STUB_ARG_FOLDER, folder->full_name,
				      CAMEL_STUB_ARG_END);
		camel_operation_end (NULL);
		if (!ok)
			return FALSE;
		camel_folder_summary_save (folder->summary);
	}

	if (camel_exchange_summary_get_readonly (folder->summary))
		folder->permanent_flags = 0;

	return TRUE;
}
