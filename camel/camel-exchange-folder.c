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

#include <glib/gi18n-lib.h>
#include <libedataserver/e-data-server-util.h>

#include "camel-exchange-folder.h"
#include "camel-exchange-search.h"
#include "camel-exchange-store.h"
#include "camel-exchange-summary.h"
#include "camel-exchange-journal.h"
#include "camel-exchange-utils.h"

#define CAMEL_EXCHANGE_SERVER_FLAGS \
	(CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_ANSWERED_ALL | \
	 CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_SEEN)

static const gchar *mailing_list_headers =
	"X-MAILING-LIST "
	"X-LOOP LIST-ID "
	"LIST-POST "
	"MAILING-LIST "
	"ORIGINATOR X-LIST "
	"RETURN-PATH X-BEENTHERE ";

G_DEFINE_TYPE (CamelExchangeFolder, camel_exchange_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static void
exchange_folder_append_message_data (CamelFolder *folder,
                                     GByteArray *message,
                                     const gchar *subject,
                                     const CamelMessageInfo *info,
                                     gchar **appended_uid,
                                     CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	CamelStream *stream_cache;
	CamelStore *parent_store;
	const gchar *full_name;
	gchar *new_uid;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	if (!subject)
		subject = camel_message_info_subject (info);;
	if (!subject)
		subject = _("No Subject");

	if (camel_exchange_utils_append_message (
			CAMEL_SERVICE (parent_store), full_name,
			info ? camel_message_info_flags (info) : 0,
			subject, message, &new_uid, ex)) {
		stream_cache = camel_data_cache_add (
			exch->cache, "cache", new_uid, NULL);
		if (stream_cache) {
			camel_stream_write (stream_cache,
					    (gchar *) message->data,
					    message->len);
			camel_stream_flush (stream_cache);
			g_object_unref (stream_cache);
		}
		if (appended_uid)
			*appended_uid = new_uid;
		else
			g_free (new_uid);
	} else if (appended_uid)
		*appended_uid = NULL;
}

static GByteArray *
exchange_folder_get_message_data (CamelFolder *folder,
                                  const gchar *uid,
                                  CamelException *ex)
{
	CamelExchangeFolder *exch;
	CamelExchangeStore *store;
	CamelStream *stream, *stream_mem;
	CamelStore *parent_store;
	GByteArray *ba;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	exch = CAMEL_EXCHANGE_FOLDER (folder);
	store = CAMEL_EXCHANGE_STORE (parent_store);

	stream = camel_data_cache_get (exch->cache, "cache", uid, NULL);
	if (stream) {
		ba = g_byte_array_new ();
		stream_mem = camel_stream_mem_new ();
		camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (stream_mem), ba);
		camel_stream_reset (stream);
		camel_stream_write_to_stream (stream, stream_mem);
		g_object_unref (CAMEL_OBJECT (stream_mem));
		g_object_unref (CAMEL_OBJECT (stream));

		return ba;
	}

	if (!camel_exchange_store_connected (store, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
                                     _("This message is not available in offline mode."));
		return NULL;
	}

	if (!camel_exchange_utils_get_message (
		CAMEL_SERVICE (parent_store), full_name, uid, &ba, ex))
		return NULL;

	stream = camel_data_cache_add (exch->cache, "cache", uid, ex);
	if (!stream) {
		g_byte_array_free (ba, TRUE);
		return NULL;
	}

	camel_stream_write (stream, (gchar *) ba->data, ba->len);
	camel_stream_flush (stream);
	g_object_unref (stream);

	return ba;
}

static void
fix_broken_multipart_related (CamelMimePart *part)
{
	CamelContentType *content_type;
	CamelDataWrapper *content;
	CamelMultipart *multipart, *new;
	CamelMimePart *subpart;
	gint i, count, broken_parts;

	content = camel_medium_get_content (CAMEL_MEDIUM (part));

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

		camel_medium_set_content (CAMEL_MEDIUM (part),
						 CAMEL_DATA_WRAPPER (new));
		g_object_unref (new);
	}
}

static void
exchange_folder_transfer_messages_the_hard_way (CamelFolder *source,
                                                GPtrArray *uids,
                                                CamelFolder *dest,
                                                GPtrArray **transferred_uids,
                                                gboolean delete_originals,
                                                CamelException *ex)
{
	CamelException local_ex;
	CamelMessageInfo *info;
	CamelStore *parent_store;
	GByteArray *ba;
	const gchar *full_name;
	gchar *ret_uid;
	gint i;

	full_name = camel_folder_get_full_name (source);
	parent_store = camel_folder_get_parent_store (source);

	if (transferred_uids)
		*transferred_uids = g_ptr_array_new ();
	camel_exception_init (&local_ex);

	for (i = 0; i < uids->len; i++) {
		info = camel_folder_summary_uid (source->summary, uids->pdata[i]);
		if (!info)
			continue;

		ba = exchange_folder_get_message_data (
			source, uids->pdata[i], &local_ex);
		if (!ba) {
			camel_message_info_free(info);
			break;
		}

		exchange_folder_append_message_data (
			dest, ba, NULL, info, &ret_uid, &local_ex);
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

	if (delete_originals)
		camel_exchange_utils_expunge_uids (
			CAMEL_SERVICE (parent_store), full_name, uids, ex);
}

static void
exchange_folder_cache_xfer (CamelExchangeFolder *folder_source,
                            CamelExchangeFolder *folder_dest,
                            GPtrArray *src_uids,
                            GPtrArray *dest_uids,
                            gboolean delete)
{
	CamelStream *src, *dest;
	gint i;

	for (i = 0; i < src_uids->len; i++) {
		if (!*(gchar *)dest_uids->pdata[i])
			continue;

		src = camel_data_cache_get (folder_source->cache, "cache",
					    src_uids->pdata[i], NULL);
		if (!src)
			continue;

		dest = camel_data_cache_add (folder_dest->cache, "cache",
					     dest_uids->pdata[i], NULL);
		if (dest) {
			camel_stream_write_to_stream (src, dest);
			g_object_unref (dest);
		}
		g_object_unref (src);

		if (delete) {
			camel_data_cache_remove (folder_source->cache, "cache",
						 src_uids->pdata[i], NULL);
		}
	}
}

static void
free_index_and_mid (gpointer thread_index, gpointer message_id, gpointer d)
{
	g_free (thread_index);
	g_free (message_id);
}

static void
exchange_folder_dispose (GObject *object)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (object);

	if (exch->cache != NULL) {
		g_object_unref (exch->cache);
		exch->cache = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_exchange_folder_parent_class)->dispose (object);
}

static void
exchange_folder_finalize (GObject *object)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (object);

	if (exch->thread_index_to_message_id) {
		g_hash_table_foreach (
			exch->thread_index_to_message_id,
			free_index_and_mid, NULL);
		g_hash_table_destroy (exch->thread_index_to_message_id);
	}

	g_free (exch->source);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_exchange_folder_parent_class)->finalize (object);
}

static gboolean
exchange_folder_refresh_info (CamelFolder *folder,
                              CamelException *ex)
{
	CamelExchangeFolder *exch;
	CamelExchangeStore *store;
	CamelStore *parent_store;
	guint32 unread_count, visible_count;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	exch = CAMEL_EXCHANGE_FOLDER (folder);
	store = CAMEL_EXCHANGE_STORE (parent_store);

	if (camel_exchange_store_connected (store, ex)) {
		camel_offline_journal_replay (exch->journal, NULL);

		camel_exchange_utils_refresh_folder (
			CAMEL_SERVICE (parent_store), full_name, ex);
	}

	/* sync up the counts now */
	if (!camel_exchange_utils_sync_count (
		CAMEL_SERVICE (parent_store), full_name,
		&unread_count, &visible_count, ex)) {
		g_print("\n Error syncing up the counts");
	}

	folder->summary->unread_count = unread_count;
	folder->summary->visible_count = visible_count;

	return !camel_exception_is_set (ex);
}

static gboolean
exchange_folder_expunge (CamelFolder *folder,
                         CamelException *ex)
{
	CamelFolder *trash;
	GPtrArray *uids;
	CamelExchangeStore *store;
	CamelStore *parent_store;
	const gchar *full_name;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_EXCHANGE_STORE (parent_store);

	if (!camel_exchange_store_connected (store, ex)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				     _("You cannot expunge in offline mode."));
		return FALSE;
	}

	trash = camel_store_get_trash (parent_store, NULL);
	if (!trash) {
		printf ("Expunge failed, could not read trash folder\n");
		return TRUE;  /* XXX exception not set */
	}

	uids = camel_folder_get_uids (trash);
	full_name = camel_folder_get_full_name (trash);
	camel_exchange_utils_expunge_uids (
		CAMEL_SERVICE (parent_store), full_name, uids, ex);
	camel_folder_free_uids (trash, uids);
	g_object_unref (trash);

	return TRUE;
}

static gboolean
exchange_folder_sync (CamelFolder *folder,
                      gboolean expunge,
                      CamelException *ex)
{
	if (expunge)
		exchange_folder_expunge (folder, ex);

	camel_folder_summary_save_to_db (folder->summary, ex);

	return !camel_exception_is_set (ex);
}

static gboolean
exchange_folder_append_message (CamelFolder *folder,
                                CamelMimeMessage *message,
                                const CamelMessageInfo *info,
                                gchar **appended_uid,
                                CamelException *ex)
{
	CamelStream *stream;
	CamelExchangeStore *store;
	CamelStore *parent_store;
	GByteArray *byte_array;
	gchar *old_subject = NULL;
	GString *new_subject;
	gint i, len;

	parent_store = camel_folder_get_parent_store (folder);
	store = CAMEL_EXCHANGE_STORE (parent_store);

	/*
	   FIXME: We should add a top-level camel API camel_mime_message_prune_invalid_chars
	   which each of the provider will have to implement to remove things
	   that are invalid for their Transport mechanism. This will help in
	   avoiding  duplication of work. Now Sending and Importing both requires
	   substitution of \t and \n with blank.
	*/

	old_subject = g_strdup(camel_mime_message_get_subject (message));

	if (old_subject) {
		len = strlen (old_subject);
		new_subject = g_string_new("");
		for (i = 0; i < len; i++)
			if ((old_subject[i] != '\t') && (old_subject[i] != '\n'))
				new_subject = g_string_append_c (new_subject, old_subject[i]);
			else
				new_subject = g_string_append_c (new_subject, ' ');
		camel_mime_message_set_subject (message, new_subject->str);
		g_free (old_subject);
		g_string_free (new_subject, TRUE);
	}

	if (!camel_exchange_store_connected (store, ex)) {
		camel_exchange_journal_append ((CamelExchangeJournal *) ((CamelExchangeFolder *)folder)->journal, message, info, appended_uid, ex);
		return !camel_exception_is_set (ex);
	}

	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (message), stream);
	camel_stream_flush (stream);

	exchange_folder_append_message_data (
		folder, byte_array,
		camel_mime_message_get_subject (message),
		info, appended_uid, ex);

	g_object_unref (stream);

	return !camel_exception_is_set (ex);
}

static CamelMimeMessage *
exchange_folder_get_message (CamelFolder *folder,
                             const gchar *uid,
                             CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);
	CamelMimeMessage *msg;
	CamelStream *stream;
	CamelStream *filtered_stream;
	CamelMimeFilter *crlffilter;
	GByteArray *ba;
	gchar **list_headers = NULL;
	gboolean found_list = FALSE;

	ba = exchange_folder_get_message_data (folder, uid, ex);
	if (!ba)
		return NULL;

	stream = camel_stream_mem_new_with_byte_array (ba);

	crlffilter = camel_mime_filter_crlf_new (CAMEL_MIME_FILTER_CRLF_DECODE, CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	filtered_stream = camel_stream_filter_new (stream);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), crlffilter);
	g_object_unref (crlffilter);
	g_object_unref (stream);

	msg = camel_mime_message_new ();
	camel_data_wrapper_construct_from_stream (CAMEL_DATA_WRAPPER (msg),
						  CAMEL_STREAM (filtered_stream));
	g_object_unref (filtered_stream);
	camel_mime_message_set_source (msg, exch->source);

	if (camel_medium_get_header (CAMEL_MEDIUM (msg), "Sender")) {
		list_headers = g_strsplit (mailing_list_headers, " ", 0);
		if (list_headers) {
			gint i = 0;
			while (list_headers[i]) {
				if (camel_medium_get_header (CAMEL_MEDIUM (msg), list_headers[i])) {
					found_list = TRUE;
					break;
				}
				i++;
			}
			g_strfreev (list_headers);
		}

		if (!found_list)
			camel_medium_set_header (CAMEL_MEDIUM (msg), "X-Evolution-Mail-From-Delegate", "yes");
	}

	fix_broken_multipart_related (CAMEL_MIME_PART (msg));
	return msg;
}

static gint
exchange_folder_cmp_uids (CamelFolder *folder,
                          const gchar *uid1,
                          const gchar *uid2)
{
	g_return_val_if_fail (uid1 != NULL, 0);
	g_return_val_if_fail (uid2 != NULL, 0);

	return strcmp (uid1, uid2);
}

static GPtrArray *
exchange_folder_search_by_expression (CamelFolder *folder,
                                      const gchar *expression,
                                      CamelException *ex)
{
	CamelFolderSearch *search;
	GPtrArray *matches;

	search = camel_exchange_search_new ();
	camel_folder_search_set_folder (search, folder);
	matches = camel_folder_search_search (search, expression, NULL, ex);

	g_object_unref (search);

	return matches;
}

static GPtrArray *
exchange_folder_search_by_uids (CamelFolder *folder,
                                const gchar *expression,
                                GPtrArray *uids,
                                CamelException *ex)
{
	CamelFolderSearch *search;
	GPtrArray *matches;

	search = camel_exchange_search_new ();
	camel_folder_search_set_folder (search, folder);
	camel_folder_search_set_summary (search, uids);
	matches = camel_folder_search_execute_expression (search, expression, ex);

	g_object_unref (search);

	return matches;
}

static gboolean
exchange_folder_transfer_messages_to (CamelFolder *source,
                                      GPtrArray *uids,
                                      CamelFolder *dest,
                                      GPtrArray **transferred_uids,
                                      gboolean delete_originals,
                                      CamelException *ex)
{
	CamelExchangeFolder *exch_source = CAMEL_EXCHANGE_FOLDER (source);
	CamelExchangeFolder *exch_dest = CAMEL_EXCHANGE_FOLDER (dest);
	CamelExchangeStore *store;
	CamelStore *parent_store;
	CamelMessageInfo *info;
	GPtrArray *ret_uids = NULL;
	const gchar *source_full_name;
	const gchar *dest_full_name;
	gint hier_len, i;

	parent_store = camel_folder_get_parent_store (source);
	store = CAMEL_EXCHANGE_STORE (parent_store);

	camel_operation_start (NULL, delete_originals ? _("Moving messages") :
			       _("Copying messages"));

	/* Check for offline operation */
	if (!camel_exchange_store_connected (store, ex)) {
		CamelExchangeJournal *journal = (CamelExchangeJournal *) exch_dest->journal;
		CamelMimeMessage *message;

		for (i = 0; i < uids->len; i++) {
			info = camel_folder_summary_uid (source->summary, uids->pdata[i]);
			if (!info)
				continue;

			if (!(message = exchange_folder_get_message (
				source, camel_message_info_uid (info), ex)))
				break;

			camel_exchange_journal_transfer (journal, exch_source, message,
							 info, uids->pdata[i], NULL,
							 delete_originals, ex);

			g_object_unref (message);

			if (camel_exception_is_set (ex))
				break;
		}
		goto end;
	}

	source_full_name = camel_folder_get_full_name (source);
	dest_full_name = camel_folder_get_full_name (dest);

	hier_len = strcspn (source_full_name, "/");
	if (strncmp (source_full_name, dest_full_name, hier_len) != 0) {
		exchange_folder_transfer_messages_the_hard_way (
			source, uids, dest, transferred_uids,
			delete_originals, ex);
		return !camel_exception_is_set (ex);
	}

	if (camel_exchange_utils_transfer_messages (CAMEL_SERVICE (store),
				source_full_name,
				dest_full_name,
				uids,
				delete_originals,
				&ret_uids,
				ex)) {
		if (ret_uids->len != 0)
			exchange_folder_cache_xfer (
				exch_source, exch_dest, uids, ret_uids, FALSE);

		if (transferred_uids)
			*transferred_uids = ret_uids;
		else {
			g_ptr_array_foreach (ret_uids, (GFunc) g_free, NULL);
			g_ptr_array_free (ret_uids, TRUE);
		}
	} else if (transferred_uids)
		*transferred_uids = NULL;
end:
	camel_operation_end (NULL);

	return !camel_exception_is_set (ex);
}

static guint32
exchange_folder_count_by_expression (CamelFolder *folder,
                                     const gchar *expression,
                                     CamelException *ex)
{
	CamelFolderSearch *search;
	guint32 matches;

	search = camel_exchange_search_new ();
	camel_folder_search_set_folder (search, folder);
	matches = camel_folder_search_count (search, expression, ex);

	g_object_unref (search);

	return matches;
}

static gchar *
exchange_folder_get_filename (CamelFolder *folder,
                              const gchar *uid,
                              CamelException *ex)
{
	CamelExchangeFolder *exch = CAMEL_EXCHANGE_FOLDER (folder);

	return camel_data_cache_get_filename (exch->cache, "cache", uid, NULL);
}

static void
camel_exchange_folder_class_init (CamelExchangeFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = exchange_folder_dispose;
	object_class->finalize = exchange_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->refresh_info = exchange_folder_refresh_info;
	folder_class->sync = exchange_folder_sync;
	folder_class->expunge = exchange_folder_expunge;
	folder_class->append_message = exchange_folder_append_message;
	folder_class->get_message = exchange_folder_get_message;
	folder_class->cmp_uids = exchange_folder_cmp_uids;
	folder_class->search_by_expression = exchange_folder_search_by_expression;
	folder_class->search_by_uids = exchange_folder_search_by_uids;
	folder_class->transfer_messages_to = exchange_folder_transfer_messages_to;
	folder_class->count_by_expression = exchange_folder_count_by_expression;
	folder_class->get_filename = exchange_folder_get_filename;
}

static void
camel_exchange_folder_init (CamelExchangeFolder *exchange_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (exchange_folder);

	folder->folder_flags =
		CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
		CAMEL_FOLDER_HAS_SEARCH_CAPABILITY;
	folder->permanent_flags =
		CAMEL_EXCHANGE_SERVER_FLAGS | CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_JUNK | CAMEL_MESSAGE_USER;
}

/* A new post to a folder gets a 27-byte-long thread index. (The value
 * is apparently unique but meaningless.) Each reply to a post gets a
 * 32-byte-long thread index whose first 27 bytes are the same as the
 * parent's thread index. Each reply to any of those gets a
 * 37-byte-long thread index, etc. The Thread-Index header contains a
 * base64 representation of this value.
 */
static CamelSummaryMessageID *
find_parent (CamelExchangeFolder *exch, const gchar *thread_index)
{
	CamelSummaryMessageID *msgid;
	guchar *decoded;
	gchar *parent;
	gsize dlen;

	decoded = g_base64_decode (thread_index, &dlen);
	if (dlen < 5) {
		/* Shouldn't happen */
		g_free (decoded);
		return NULL;
	}

	parent = g_base64_encode (decoded, dlen - 5);
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
				   const gchar *uid, guint32 flags,
				   guint32 size, const gchar *headers,
				   const gchar *href)
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
	g_object_unref (stream);

	info = camel_folder_summary_info_new_from_message (folder->summary, msg, NULL);
	einfo = (CamelExchangeMessageInfo *)info;

	if (einfo->thread_index) {
		CamelSummaryMessageID *parent;

		if (einfo->info.message_id.id.id)
			g_hash_table_insert (exch->thread_index_to_message_id,
					     g_strdup (einfo->thread_index),
					     g_memdup (&einfo->info.message_id, sizeof (CamelSummaryMessageID)));

		parent = find_parent (exch, einfo->thread_index);
		if (parent && einfo->info.references == NULL) {
			einfo->info.references = g_malloc(sizeof(CamelSummaryReferences));
			memcpy(&einfo->info.references->references[0], parent, sizeof(*parent));
			einfo->info.references->size = 1;
		}
	}
	g_object_unref (msg);

	info->uid = camel_pstring_strdup (uid);
	einfo->info.flags = flags;
	einfo->info.size = size;
	einfo->href = g_strdup (href);

	camel_folder_summary_add (folder->summary, info);

	if (!(flags & CAMEL_MESSAGE_SEEN)) {
		folder->summary->unread_count++;
		folder->summary->visible_count++;
	}

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
				      const gchar *uid)
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
				       const gchar *uid)
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
					    const gchar *uid, guint32 flags)
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
 * camel_exchange_folder_update_message_flags_ex:
 * @exch: the folder
 * @uid: the message
 * @flags: new message flags
 * @mask: the flag mask
 *
 * Update the message flags of @uid in @exch's summary based on @flags and @mask.
 * Only the bits in the mask %CAMEL_EXCHANGE_SERVER_FLAGS are valid to be set or unset.
 **/
void
camel_exchange_folder_update_message_flags_ex (CamelExchangeFolder *exch,
					       const gchar *uid, guint32 flags,
					       guint32 mask)
{
	CamelFolder *folder = CAMEL_FOLDER (exch);
	CamelMessageInfoBase *info;
	CamelFolderChangeInfo *changes;

	info = (CamelMessageInfoBase *)camel_folder_summary_uid (folder->summary, uid);
	if (!info)
		return;

	mask &= CAMEL_EXCHANGE_SERVER_FLAGS;
	if (!mask) {
		return;
	}

	if ((info->flags & mask) != (flags & mask)) {
		info->flags &= ~mask;
		info->flags |= (flags & mask);
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
					  const gchar *uid,
					  const gchar *name,
					  const gchar *value)
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
 * @offline_state : offline status
 * @ex: a #CamelException
 *
 * Return value: success or failure.
 **/
gboolean
camel_exchange_folder_construct (CamelFolder *folder,
                                 guint32 camel_flags,
                                 const gchar *folder_dir,
                                 gint offline_state,
                                 CamelException *ex)
{
	CamelExchangeFolder *exch = (CamelExchangeFolder *)folder;
	gchar *summary_file, *journal_file, *path;
	GPtrArray *summary, *uids, *hrefs;
	GByteArray *flags;
	guint32 folder_flags;
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	CamelStore *parent_store;
	const gchar *full_name;
	gint i, len = 0;

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	if (g_mkdir_with_parents (folder_dir, S_IRWXU) != 0) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Could not create directory %s: %s"),
				      folder_dir, g_strerror (errno));
		return FALSE;
	}

	summary_file = g_strdup_printf ("%s/summary", folder_dir);
	folder->summary = camel_exchange_summary_new (folder, summary_file);
	g_free (summary_file);
	if (!folder->summary) {
		camel_exception_setv (
			ex, CAMEL_EXCEPTION_SYSTEM,
			_("Could not load summary for %s"), full_name);
		return FALSE;
	}

	exch->cache = camel_data_cache_new (folder_dir, ex);
	if (!exch->cache) {
		camel_exception_setv (
			ex, CAMEL_EXCEPTION_SYSTEM,
			_("Could not create cache for %s"), full_name);
		return FALSE;
	}

	journal_file = g_strdup_printf ("%s/journal", folder_dir);
	exch->journal = camel_exchange_journal_new (exch, journal_file);
	g_free (journal_file);
	if (!exch->journal) {
		camel_exception_setv (
			ex, CAMEL_EXCEPTION_SYSTEM,
			_("Could not create journal for %s"), full_name);
		return FALSE;
	}

	path = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set (folder, NULL, CAMEL_OBJECT_STATE_FILE, path, NULL);
	g_free (path);
	camel_object_state_read (CAMEL_OBJECT (folder));

	exch->thread_index_to_message_id =
		g_hash_table_new (g_str_hash, g_str_equal);

	len = camel_folder_summary_count (folder->summary);
	for (i = 0; i < len; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		einfo = (CamelExchangeMessageInfo *)info;

		if (einfo->thread_index && einfo->info.message_id.id.id) {
			g_hash_table_insert (exch->thread_index_to_message_id,
					     g_strdup (einfo->thread_index),
					     g_memdup (&einfo->info.message_id, sizeof (CamelSummaryMessageID)));
		}

		camel_message_info_free(info);
	}

	if (parent_store != NULL) {
		gboolean ok, create = camel_flags & CAMEL_STORE_FOLDER_CREATE, readonly = FALSE;

		camel_folder_summary_prepare_fetch_all (folder->summary, ex);

		summary = camel_folder_get_summary (folder);
		uids = g_ptr_array_new ();
		g_ptr_array_set_size (uids, summary->len);
		flags = g_byte_array_new ();
		g_byte_array_set_size (flags, summary->len);
		hrefs = g_ptr_array_new ();
		g_ptr_array_set_size (hrefs, summary->len);

		for (i = 0; i < summary->len; i++) {
			uids->pdata[i] = g_strdup(summary->pdata[i]);
			info = camel_folder_summary_uid (folder->summary, uids->pdata[i]);
			flags->data[i] = ((CamelMessageInfoBase *)info)->flags & CAMEL_EXCHANGE_SERVER_FLAGS;
			hrefs->pdata[i] = ((CamelExchangeMessageInfo *)info)->href;
			//camel_tag_list_free (&((CamelMessageInfoBase *)info)->user_tags);
		}

		camel_operation_start (NULL, _("Scanning for changed messages"));
		ok = camel_exchange_utils_get_folder (
			CAMEL_SERVICE (parent_store),
			full_name, create, uids, flags, hrefs,
			CAMEL_EXCHANGE_SUMMARY (folder->summary)->high_article_num,
			&folder_flags, &exch->source, &readonly, ex);
		camel_operation_end (NULL);
		g_ptr_array_free (uids, TRUE);
		g_byte_array_free (flags, TRUE);
		g_ptr_array_free (hrefs, TRUE);
		camel_folder_free_summary (folder, summary);
		if (!ok)
			return FALSE;

		if (folder_flags & CAMEL_FOLDER_FILTER_RECENT)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
		if (folder_flags & CAMEL_FOLDER_FILTER_JUNK)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;

		camel_exchange_summary_set_readonly (folder->summary, readonly);

		if (offline_state == CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL)
			return TRUE;

		if (len)
			return TRUE;

		camel_operation_start (NULL, _("Fetching summary information for new messages"));
		ok = camel_exchange_utils_refresh_folder (
			CAMEL_SERVICE (parent_store), full_name, ex);
		camel_operation_end (NULL);
		if (!ok)
			return FALSE;

		camel_folder_summary_save_to_db (folder->summary, ex);
	}

	if (camel_exchange_summary_get_readonly (folder->summary))
		folder->permanent_flags = 0;

	return TRUE;
}

