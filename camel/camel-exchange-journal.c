/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright 2004 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include <glib/gi18n-lib.h>

#include <camel/camel-folder.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-data-cache.h>

#include "camel-exchange-journal.h"
#include "camel-exchange-store.h"
#include "camel-exchange-summary.h"


#define d(x) x


static void camel_exchange_journal_class_init (CamelExchangeJournalClass *klass);
static void camel_exchange_journal_init (CamelExchangeJournal *journal, CamelExchangeJournalClass *klass);
static void camel_exchange_journal_finalize (CamelObject *object);

static void exchange_entry_free (CamelOfflineJournal *journal, EDListNode *entry);
static EDListNode *exchange_entry_load (CamelOfflineJournal *journal, FILE *in);
static int exchange_entry_write (CamelOfflineJournal *journal, EDListNode *entry, FILE *out);
static int exchange_entry_play (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex);


static CamelOfflineJournalClass *parent_class = NULL;


CamelType
camel_exchange_journal_get_type (void)
{
	static CamelType type = 0;
	
	if (!type) {
		type = camel_type_register (camel_offline_journal_get_type (),
					    "CamelExchangeJournal",
					    sizeof (CamelExchangeJournal),
					    sizeof (CamelExchangeJournalClass),
					    (CamelObjectClassInitFunc) camel_exchange_journal_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_exchange_journal_init,
					    (CamelObjectFinalizeFunc) camel_exchange_journal_finalize);
	}
	
	return type;
}

static void
camel_exchange_journal_class_init (CamelExchangeJournalClass *klass)
{
	CamelOfflineJournalClass *journal_class = (CamelOfflineJournalClass *) klass;
	
	parent_class = (CamelOfflineJournalClass *) camel_type_get_global_classfuncs (CAMEL_TYPE_OFFLINE_JOURNAL);
	
	journal_class->entry_free = exchange_entry_free;
	journal_class->entry_load = exchange_entry_load;
	journal_class->entry_write = exchange_entry_write;
	journal_class->entry_play = exchange_entry_play;
}

static void
camel_exchange_journal_init (CamelExchangeJournal *journal, CamelExchangeJournalClass *klass)
{
	
}

static void
camel_exchange_journal_finalize (CamelObject *object)
{
	
}

static void
exchange_entry_free (CamelOfflineJournal *journal, EDListNode *entry)
{
	CamelExchangeJournalEntry *exchange_entry = (CamelExchangeJournalEntry *) entry;
	
	g_free (exchange_entry->uid);
	g_free (exchange_entry->original_uid);
	g_free (exchange_entry->folder_name);
	g_free (exchange_entry);
}

static EDListNode *
exchange_entry_load (CamelOfflineJournal *journal, FILE *in)
{
	CamelExchangeJournalEntry *entry;
	char *tmp;
	
	entry = g_malloc0 (sizeof (CamelExchangeJournalEntry));
	
	if (camel_file_util_decode_uint32 (in, (guint32 *) &entry->type) == -1)
		goto exception;
	
	switch (entry->type) {
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		
		break;
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &entry->original_uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &entry->folder_name) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &tmp) == -1)
			goto exception;
		if (g_ascii_strcasecmp (tmp, "True") == 0)
			entry->delete_original = TRUE;
		else
			entry->delete_original = FALSE;
		g_free (tmp);
		break;
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_DELETE:
		if (camel_file_util_decode_string (in, &entry->uid) == -1)
			goto exception;
		if (camel_file_util_decode_string (in, &tmp) == -1)
			goto exception;
		entry->flags = atoi (tmp);
		g_free (tmp);
		if (camel_file_util_decode_string (in, &tmp) == -1)
			goto exception;
		entry->set = atoi (tmp);
		g_free (tmp);
		break;
	default:
		goto exception;
	}
	
	return (EDListNode *) entry;
	
 exception:
	
	g_free (entry->folder_name);
	g_free (entry->original_uid);
	g_free (entry->uid);
	g_free (entry);
	
	return NULL;
}

static int
exchange_entry_write (CamelOfflineJournal *journal, EDListNode *entry, FILE *out)
{
	CamelExchangeJournalEntry *exchange_entry = (CamelExchangeJournalEntry *) entry;
	char *tmp;
	
	if (camel_file_util_encode_uint32 (out, exchange_entry->type) == -1)
		return -1;
	
	switch (exchange_entry->type) {
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_APPEND:
		if (camel_file_util_encode_string (out, exchange_entry->uid))
			return -1;
		break;
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER:
		if (camel_file_util_encode_string (out, exchange_entry->uid))
			return -1;
		if (camel_file_util_encode_string (out, exchange_entry->original_uid))
			return -1;
		if (camel_file_util_encode_string (out, exchange_entry->folder_name))
			return -1;
		tmp = exchange_entry->delete_original ? "True" : "False";
		if (camel_file_util_encode_string (out, tmp))
			return -1;
		break;
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_DELETE:
		if (camel_file_util_encode_string (out, exchange_entry->uid))
			return -1;
		tmp = g_strdup_printf ("%u", exchange_entry->flags);
		if (camel_file_util_encode_string (out, tmp))
			return -1;
		g_free (tmp);
		tmp = g_strdup_printf ("%u", exchange_entry->set);
		if (camel_file_util_encode_string (out, tmp))
			return -1;
		g_free (tmp);
		break;
	default:
		g_assert_not_reached ();
	}
	
	return 0;
}

static void
exchange_message_info_dup_to (CamelMessageInfoBase *dest, CamelMessageInfoBase *src)
{
	camel_flag_list_copy (&dest->user_flags, &src->user_flags);
	camel_tag_list_copy (&dest->user_tags, &src->user_tags);
	dest->date_received = src->date_received;
	dest->date_sent = src->date_sent;
	dest->flags = src->flags;
	dest->size = src->size;
}

static int
exchange_entry_play_delete (CamelOfflineJournal *journal, CamelExchangeJournalEntry *entry, CamelException *ex)
{
	CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) journal->folder;

	camel_stub_send_oneway (exchange_folder->stub,
				CAMEL_STUB_CMD_SET_MESSAGE_FLAGS,
				CAMEL_STUB_ARG_FOLDER,
				((CamelFolder *)exchange_folder)->full_name,
				CAMEL_STUB_ARG_STRING,
				entry->uid,
				CAMEL_STUB_ARG_UINT32,
				entry->set,
				CAMEL_STUB_ARG_UINT32,
				entry->flags,
				CAMEL_STUB_ARG_END);

	return 0;
}
				
static int
exchange_entry_play_append (CamelOfflineJournal *journal, CamelExchangeJournalEntry *entry, CamelException *ex)
{
	CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMimeMessage *message;
	CamelMessageInfo *info, *real;
	CamelStream *stream;
	CamelException lex;
	char *uid = NULL;
	
	/* if the message isn't in the cache, the user went behind our backs so "not our problem" */
	if (!exchange_folder->cache || !(stream = camel_data_cache_get (exchange_folder->cache, "cache", entry->uid, ex)))
		goto done;
	
	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream) == -1) {
		camel_object_unref (message);
		camel_object_unref (stream);
		goto done;
	}
	
	camel_object_unref (stream);
	
	if (!(info = camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Should have never happened, but create a new info to avoid further crashes */
		info = camel_message_info_new (NULL);
	}
	
	camel_exception_init (&lex);
	camel_folder_append_message (folder, message, info, &uid, &lex);
	
	if (camel_exception_is_set (&lex)) {
		camel_exception_xfer (ex, &lex);
		return -1;
	}

	real = camel_folder_summary_info_new_from_message (folder->summary, message);
	camel_object_unref (message);

	if (uid != NULL && real) {
		real->uid = g_strdup (uid);
		exchange_message_info_dup_to ((CamelMessageInfoBase *) real, (CamelMessageInfoBase *) info);
		camel_folder_summary_add (folder->summary, real);
		/* FIXME: should a folder_changed event be triggered? */
	}
	camel_message_info_free (info);
	g_free (uid);
	
 done:
	
	camel_exchange_folder_remove_message (exchange_folder, entry->uid);
	
	return 0;
}

static int 
exchange_entry_play_transfer (CamelOfflineJournal *journal, CamelExchangeJournalEntry *entry, CamelException *ex)
{
	CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) journal->folder;
	CamelFolder *folder = journal->folder;
	CamelMessageInfo *info, *real;
	GPtrArray *xuids, *uids;
	CamelException lex;
	CamelFolder *src;
	CamelExchangeStore *store;
	CamelStream *stream;
	CamelMimeMessage *message;

	if (!exchange_folder->cache || !(stream = camel_data_cache_get (exchange_folder->cache, "cache", entry->uid, ex)))
		goto done;
	
	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *) message, stream) == -1) {
		camel_object_unref (message);
		camel_object_unref (stream);
		goto done;
	}
	
	camel_object_unref (stream);

	if (!(info = camel_folder_summary_uid (folder->summary, entry->uid))) {
		/* Note: this should never happen, but rather than crash lets make a new info */
		info = camel_message_info_new (NULL);
	}

	if (!entry->folder_name) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("No folder name found\n"));
		goto exception;
	}
	
	store = (CamelExchangeStore *) folder->parent_store;
	g_mutex_lock (store->folders_lock);
	src = (CamelFolder *) g_hash_table_lookup (store->folders, entry->folder_name);
	g_mutex_unlock (store->folders_lock);
	
	if (src) {
		uids = g_ptr_array_sized_new (1);
		g_ptr_array_add (uids, entry->original_uid);

		camel_exception_init (&lex);
		camel_folder_transfer_messages_to (src, uids, folder, &xuids, entry->delete_original, &lex);
		if (!camel_exception_is_set (&lex)) {
			real = camel_folder_summary_info_new_from_message (folder->summary, message);
			camel_object_unref (message);
			real->uid = g_strdup ((char *)xuids->pdata[0]);
			/* Transfer flags */
			exchange_message_info_dup_to ((CamelMessageInfoBase *) real, (CamelMessageInfoBase *) info);
			camel_folder_summary_add (folder->summary, real);
			/* FIXME: should a folder_changed event be triggered? */
		} else {
			camel_exception_xfer (ex, &lex);
			goto exception;
		}

		g_ptr_array_free (xuids, TRUE);
		g_ptr_array_free (uids, TRUE);
		/* camel_object_unref (src); FIXME: should we? */ 
	} 
	else {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Folder doesn't exist"));
		goto exception;
	}

	camel_message_info_free (info);
done:
	camel_exchange_folder_remove_message (exchange_folder, entry->uid);

	return 0;

exception:
	
	camel_message_info_free (info);

	return -1;
}

static int
exchange_entry_play (CamelOfflineJournal *journal, EDListNode *entry, CamelException *ex)
{
	CamelExchangeJournalEntry *exchange_entry = (CamelExchangeJournalEntry *) entry;
	
	switch (exchange_entry->type) {
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_APPEND:
		return exchange_entry_play_append (journal, exchange_entry, ex);
 	case CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER:
 		return exchange_entry_play_transfer (journal, exchange_entry, ex);
	case CAMEL_EXCHANGE_JOURNAL_ENTRY_DELETE:
		return exchange_entry_play_delete (journal, exchange_entry, ex);
	default:
		g_assert_not_reached ();
		return -1;
	}
}



CamelOfflineJournal *
camel_exchange_journal_new (CamelExchangeFolder *folder, const char *filename)
{
	CamelOfflineJournal *journal;
	
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_FOLDER (folder), NULL);
	
	journal = (CamelOfflineJournal *) camel_object_new (camel_exchange_journal_get_type ());
	camel_offline_journal_construct (journal, (CamelFolder *) folder, filename);
	
	return journal;
}

static gboolean
update_cache (CamelExchangeJournal *exchange_journal, CamelMimeMessage *message,
	 	const CamelMessageInfo *mi, char **updated_uid, CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) exchange_journal;
	CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) journal->folder;
	CamelFolder *folder = (CamelFolder *) journal->folder;
	CamelMessageInfo *info;
	CamelStream *cache;
	guint32 nextuid;
	char *uid;

	if (exchange_folder->cache == NULL) {
		camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot append message in offline mode: cache unavailable"));
		return FALSE;
	}
	
	nextuid = camel_folder_summary_next_uid (folder->summary);
	uid = g_strdup_printf ("-%u", nextuid);
	
	if (!(cache = camel_data_cache_add (exchange_folder->cache, "cache", uid, ex))) {
		folder->summary->nextuid--;
		g_free (uid);
		return FALSE;
	}
	
	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *) message, cache) == -1
	    || camel_stream_flush (cache) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message in offline mode: %s"),
				      g_strerror (errno));
		camel_data_cache_remove (exchange_folder->cache, "cache", uid, NULL);
		folder->summary->nextuid--;
		camel_object_unref (cache);
		g_free (uid);
		return FALSE;
	}
	
	camel_object_unref (cache);
	
	info = camel_folder_summary_info_new_from_message (folder->summary, message);
	info->uid = g_strdup (uid);
	
	exchange_message_info_dup_to ((CamelMessageInfoBase *) info, (CamelMessageInfoBase *) mi);

	camel_folder_summary_add (folder->summary, info);
	
	if (updated_uid)
		*updated_uid = g_strdup (uid);

	g_free (uid);

	return TRUE;
}

void
camel_exchange_journal_append (CamelExchangeJournal *exchange_journal, CamelMimeMessage *message,
			       const CamelMessageInfo *mi, char **appended_uid, CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) exchange_journal;
	CamelExchangeJournalEntry *entry;
	char *uid;
	
	if (!update_cache (exchange_journal, message, mi, &uid, ex))
		return;
	
	entry = g_new (CamelExchangeJournalEntry, 1);
	entry->type = CAMEL_EXCHANGE_JOURNAL_ENTRY_APPEND;
	entry->uid = uid;
	
	e_dlist_addtail (&journal->queue, (EDListNode *) entry);
	
	if (appended_uid)
		*appended_uid = g_strdup (uid);

}

static int
find_real_source_for_message (CamelExchangeFolder *folder,
			      const char **folder_name,
			      const char **uid,
			      gboolean delete_original)
{
	CamelOfflineJournal *journal = folder->journal;
	EDListNode *entry, *next;
	CamelExchangeJournalEntry *ex_entry;
	const char *offline_uid = *uid;
	int type = -1;
	
	if (*offline_uid != '-') {
		return CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER;
	}

	entry = journal->queue.head;
	while (entry->next) {
		next = entry->next;
		
		ex_entry = (CamelExchangeJournalEntry *) entry;
		if (!g_ascii_strcasecmp (ex_entry->uid, offline_uid)) {
			if (ex_entry->type == CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER) {
				*uid = ex_entry->original_uid;
				*folder_name = ex_entry->folder_name;
				type = CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER;
			} else if (ex_entry->type == CAMEL_EXCHANGE_JOURNAL_ENTRY_APPEND) {
				type = CAMEL_EXCHANGE_JOURNAL_ENTRY_APPEND;
			}

			if (delete_original) {
				e_dlist_remove (entry);
			}
		}
		
		entry = next;
	}

	return type;
}

void 
camel_exchange_journal_transfer (CamelExchangeJournal *exchange_journal, CamelExchangeFolder *source_folder,
				CamelMimeMessage *message, const CamelMessageInfo *mi,
				const char *original_uid, char **transferred_uid, gboolean delete_original,
				CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) exchange_journal;
	CamelExchangeJournalEntry *entry;
	char *uid;
	const char *real_source_folder = NULL, *real_uid = NULL;
	int type;
	
	if (!update_cache (exchange_journal, message, mi, &uid, ex))
		return;

	real_uid = original_uid;
	real_source_folder = ((CamelFolder *)source_folder)->full_name;
	
	type = find_real_source_for_message (source_folder, &real_source_folder,
					     &real_uid, delete_original);
	
	if(delete_original) {
		camel_exchange_folder_remove_message (source_folder, original_uid);
	}

	entry = g_new (CamelExchangeJournalEntry, 1);
	entry->type = type;
	entry->uid = uid;

	if (type == CAMEL_EXCHANGE_JOURNAL_ENTRY_TRANSFER) {
		entry->original_uid = g_strdup (real_uid);
		entry->folder_name = g_strdup (real_source_folder);
		entry->delete_original = delete_original;
	}

	e_dlist_addtail (&journal->queue, (EDListNode *) entry);
	
	if (transferred_uid)
		*transferred_uid = g_strdup (uid);
}

void
camel_exchange_journal_delete (CamelExchangeJournal *exchange_journal,
			       const char *uid, guint32 flags, guint32 set,
			       CamelException *ex)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) exchange_journal;
	CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) journal->folder;
	CamelExchangeJournalEntry *entry;

	if (set & flags & CAMEL_MESSAGE_DELETED)
		camel_exchange_folder_remove_message (exchange_folder, uid);

	entry = g_new0 (CamelExchangeJournalEntry, 1);
	entry->type = CAMEL_EXCHANGE_JOURNAL_ENTRY_DELETE;
	entry->uid = g_strdup (uid);
	entry->flags = flags;
	entry->set = set;

	e_dlist_addtail (&journal->queue, (EDListNode *) entry);
}

