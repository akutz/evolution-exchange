/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2000-2004 Novell, Inc.
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

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <camel/camel-db.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-offline-store.h>
#include <camel/camel-string-utils.h>

#include "camel-stub.h"
#include "camel-exchange-folder.h"
#include "camel-exchange-journal.h"
#include "camel-exchange-summary.h"

#define CAMEL_EXCHANGE_SUMMARY_VERSION (2)

#define EXTRACT_FIRST_DIGIT(val) val=strtoul (part, &part, 10);
#define EXTRACT_DIGIT(val) part++; val=strtoul (part, &part, 10);
#define EXTRACT_FIRST_STRING(val) len=strtoul (part, &part, 10); if (*part) part++; val=g_strndup (part, len); part+=len;
#define EXTRACT_STRING(val) if (*part) part++; len=strtoul (part, &part, 10); if (*part) part++; val=g_strndup (part, len); part+=len;

#define d(x)

static gint header_load (CamelFolderSummary *summary, FILE *in);
static gint header_save (CamelFolderSummary *summary, FILE *out);

static CamelMessageInfo *message_info_load (CamelFolderSummary *summary,
					    FILE *in);
static gint               message_info_save (CamelFolderSummary *summary,
					    FILE *out,
					    CamelMessageInfo *info);
static gint summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, CamelException *ex);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static CamelMessageInfo *message_info_new_from_header  (CamelFolderSummary *summary,
							struct _camel_header_raw *h);

static void message_info_free (CamelFolderSummary *summary, CamelMessageInfo *info);

static gboolean check_for_trash (CamelFolder *folder);
static gboolean expunge_mail (CamelFolder *folder, CamelMessageInfo *info);

static gboolean info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set);
static gboolean info_set_user_tag(CamelMessageInfo *info, const gchar *name, const gchar *value);

static CamelFolderSummaryClass *parent_class = NULL;

static void
exchange_summary_class_init (CamelObjectClass *klass)
{
	CamelFolderSummaryClass *camel_folder_summary_class =
		(CamelFolderSummaryClass *) klass;

	parent_class = CAMEL_FOLDER_SUMMARY_CLASS (camel_type_get_global_classfuncs (camel_folder_summary_get_type()));

	camel_folder_summary_class->summary_header_load = header_load;
	camel_folder_summary_class->summary_header_save = header_save;
	camel_folder_summary_class->message_info_load = message_info_load;
	camel_folder_summary_class->message_info_save = message_info_save;
	camel_folder_summary_class->message_info_new_from_header = message_info_new_from_header;
	camel_folder_summary_class->message_info_free = message_info_free;

	camel_folder_summary_class->summary_header_to_db = summary_header_to_db;
	camel_folder_summary_class->summary_header_from_db = summary_header_from_db;
	camel_folder_summary_class->message_info_to_db = message_info_to_db;
	camel_folder_summary_class->message_info_from_db = message_info_from_db;

	camel_folder_summary_class->info_set_flags = info_set_flags;
	camel_folder_summary_class->info_set_user_tag = info_set_user_tag;
}

static void
exchange_summary_init (CamelObject *obj, CamelObjectClass *klass)
{
	CamelFolderSummary *summary = (CamelFolderSummary *)obj;

	summary->message_info_size = sizeof (CamelExchangeMessageInfo);
	summary->content_info_size = sizeof (CamelMessageContentInfo);
}

CamelType
camel_exchange_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(
			camel_folder_summary_get_type (),
			"CamelExchangeSummary",
			sizeof (CamelExchangeSummary),
			sizeof (CamelExchangeSummaryClass),
			exchange_summary_class_init,
			NULL,
			exchange_summary_init,
			NULL);
	}

	return type;
}

/**
 * camel_exchange_summary_new:
 * @filename: filename to use for the summary
 *
 * Creates a new #CamelExchangeSummary based on @filename.
 *
 * Return value: the summary object.
 **/
CamelFolderSummary *
camel_exchange_summary_new (struct _CamelFolder *folder, const gchar *filename)
{
	CamelFolderSummary *summary;
	CamelException lex;

	camel_exception_init (&lex);
	summary = (CamelFolderSummary *)camel_object_new (CAMEL_EXCHANGE_SUMMARY_TYPE);
	summary->folder = folder;
	camel_folder_summary_set_filename (summary, filename);
	if (camel_folder_summary_load_from_db (summary, &lex) == -1) {
		g_warning ("Unable to load Exchage summary for folder %s: %s\n", folder->full_name, camel_exception_get_description(&lex));
		camel_folder_summary_clear_db (summary);
		camel_folder_summary_touch (summary);
	}

	camel_exception_clear (&lex);
	return summary;
}

static gint
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelExchangeSummary *exchange = (CamelExchangeSummary *) s;
	gchar *part;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_from_db (s, mir) == -1)
		return -1;

	part = mir->bdata;

	if (part) {
		EXTRACT_FIRST_DIGIT (exchange->version)
	}

	if (part) {
		EXTRACT_DIGIT (exchange->readonly)
	}

	if (part) {
		EXTRACT_DIGIT (exchange->high_article_num)
	}

	return 0;
}

static gint
header_load (CamelFolderSummary *summary, FILE *in)
{
	CamelExchangeSummary *exchange = (CamelExchangeSummary *) summary;
	guint32 version, readonly, high_article_num = 0;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_load (summary, in) == -1)
		return -1;

	if (camel_file_util_decode_uint32 (in, &version) == -1)
		return -1;

	if (camel_file_util_decode_uint32 (in, &readonly) == -1)
		return -1;

	/* Old summary file - We need to migrate.  Migration automagically happens when
	   camel_folder_summary_save is called
	*/
	if (camel_file_util_decode_uint32 (in, &high_article_num) == -1) {
		if (version > CAMEL_EXCHANGE_SUMMARY_VERSION)
			return -1;
	}

	/* During migration we will not have high_article_num stored in the summary and
	   essentially we will end up computing it atleast once.
	*/
	exchange->readonly = readonly;
	exchange->high_article_num = high_article_num;
	exchange->version = version;

	d(g_print ("%s:%s: high_article_num = [%d]\n", G_STRLOC, G_STRFUNC, high_article_num));

	return 0;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, CamelException *ex)
{
	CamelExchangeSummary *exchange = (CamelExchangeSummary *) s;
	struct _CamelFIRecord *fir;

	fir = CAMEL_FOLDER_SUMMARY_CLASS(parent_class)->summary_header_to_db (s, ex);
	if (!fir)
		return NULL;
	fir->bdata = g_strdup_printf ("%u %u %u", exchange->version, exchange->readonly, exchange->high_article_num);

	return fir;
}

static gint
header_save (CamelFolderSummary *summary, FILE *out)
{
	CamelExchangeSummary *exchange = (CamelExchangeSummary *) summary;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_save (summary, out) == -1)
		return -1;

	if (camel_file_util_encode_uint32 (out, exchange->version) == -1)
		return -1;

	if (camel_file_util_encode_uint32 (out, exchange->readonly) == -1)
		return -1;

	if (camel_file_util_encode_uint32 (out, exchange->high_article_num) == -1)
		return -1;

	d(g_print ("%s:%s: high_article_num = [%d]\n", G_STRLOC, G_STRFUNC, exchange->high_article_num));

	return 0;
}
static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;

	info = CAMEL_FOLDER_SUMMARY_CLASS(parent_class)->message_info_from_db (s, mir);
	if (info) {
		gchar *part = g_strdup (mir->bdata);
		gint len;
		einfo = (CamelExchangeMessageInfo *)info;
		EXTRACT_FIRST_STRING (einfo->thread_index)
		EXTRACT_FIRST_STRING (einfo->href)
	}

	return info;
}

static CamelMessageInfo *
message_info_load (CamelFolderSummary *summary, FILE *in)
{
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	gchar *thread_index, *href = NULL;

	info = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_load (summary, in);
	if (info) {
		einfo = (CamelExchangeMessageInfo *)info;

		if (camel_file_util_decode_string (in, &thread_index) == -1)
			goto error;

		if (thread_index && *thread_index)
			einfo->thread_index = thread_index;
		else
			g_free (thread_index);

		/* Old summary file - We need to migrate.  Migration automagically happens when
		   camel_folder_summary_save is called
		*/
		if (camel_file_util_decode_string (in, &href) == -1) {
			if (CAMEL_EXCHANGE_SUMMARY (summary)->version > CAMEL_EXCHANGE_SUMMARY_VERSION)
				goto error;
		}

		einfo->href = href;
		d(g_print ("%s:%s: einfo->href = [%s]\n", G_STRLOC, G_STRFUNC, einfo->href));
	}

	return info;
error:
	camel_message_info_free (info);
	return NULL;
}

static CamelMIRecord *
message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelExchangeMessageInfo *einfo = (CamelExchangeMessageInfo *)info;
	struct _CamelMIRecord *mir;

	mir = CAMEL_FOLDER_SUMMARY_CLASS(parent_class)->message_info_to_db (s, info);
	if (mir)
		mir->bdata = g_strdup_printf ("%d-%s %d-%s", einfo->thread_index ? (gint)strlen(einfo->thread_index):0 , einfo->thread_index ? einfo->thread_index : "", einfo->href ? (gint)strlen(einfo->href):0, einfo->href ? einfo->href:"");

	return mir;
}

static gint
message_info_save (CamelFolderSummary *summary, FILE *out, CamelMessageInfo *info)
{
	CamelExchangeMessageInfo *einfo = (CamelExchangeMessageInfo *)info;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_save (summary, out, info) == -1)
		return -1;

	if (camel_file_util_encode_string (out, einfo->thread_index ? einfo->thread_index : "") == -1)
		return -1;

	if (camel_file_util_encode_string (out, einfo->href ? einfo->href : "") == -1)
		return -1;

	d(g_print ("%s:%s: einfo->href = [%s]\n", G_STRLOC, G_STRFUNC, einfo->href));

	return 0;
}

static CamelMessageInfo *
message_info_new_from_header (CamelFolderSummary *summary, struct _camel_header_raw *h)
{
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	const gchar *thread_index;

	info = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_new_from_header (summary, h);
	if (!info)
		return info;

	einfo = (CamelExchangeMessageInfo *)info;
	thread_index = camel_header_raw_find (&h, "Thread-Index", NULL);
	if (thread_index)
		einfo->thread_index = g_strdup (thread_index + 1);

	return info;
}

static void
message_info_free (CamelFolderSummary *summary, CamelMessageInfo *info)
{
	CamelExchangeMessageInfo *einfo;

	einfo = (CamelExchangeMessageInfo *)info;

	g_free (einfo->href);
	g_free (einfo->thread_index);

	einfo->href = NULL;
	einfo->thread_index = NULL;

	CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_free (summary, info);
}

static gboolean
check_for_trash (CamelFolder *folder)
{
	CamelStore *store = (CamelStore *) folder->parent_store;
	CamelException lex;
	CamelFolder *trash;

	camel_exception_init (&lex);
	trash = camel_store_get_trash (store, &lex);

	if (camel_exception_is_set (&lex) || !trash)
		return FALSE;

	return folder == trash;
}

static gboolean
expunge_mail (CamelFolder *folder, CamelMessageInfo *info)
{
	CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) folder;
	GPtrArray *uids = g_ptr_array_new ();
	gchar *uid = g_strdup (info->uid);
	CamelException lex;

	g_ptr_array_add (uids, uid);

	camel_exception_init (&lex);
	camel_stub_send (exchange_folder->stub, &lex,
			 CAMEL_STUB_CMD_EXPUNGE_UIDS,
			 CAMEL_STUB_ARG_FOLDER, folder->full_name,
			 CAMEL_STUB_ARG_STRINGARRAY, uids,
			 CAMEL_STUB_ARG_END);

	g_ptr_array_free (uids, TRUE);
	return camel_exception_is_set (&lex);
}

static gboolean
info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set)
{
	CamelFolder *folder = (CamelFolder *) info->summary->folder;
	CamelOfflineStore *store = (CamelOfflineStore *) folder->parent_store;

	if (CAMEL_EXCHANGE_SUMMARY (info->summary)->readonly)
		return FALSE;

	if (store->state != CAMEL_OFFLINE_STORE_NETWORK_UNAVAIL) {
		if (folder && info->uid) {
			if ((flags & set & CAMEL_MESSAGE_DELETED) &&
			    check_for_trash (folder)) {
				return expunge_mail (folder, info);
			} else {
				camel_stub_send_oneway (((CamelExchangeFolder *)folder)->stub,
							CAMEL_STUB_CMD_SET_MESSAGE_FLAGS,
							CAMEL_STUB_ARG_FOLDER, folder->full_name,
							CAMEL_STUB_ARG_STRING, info->uid,
							CAMEL_STUB_ARG_UINT32, set,
							CAMEL_STUB_ARG_UINT32, flags,
							CAMEL_STUB_ARG_END);
				return CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->info_set_flags(info, flags, set);
			}
		}
	}
	else {
		if (folder && info->uid) {
			if ((flags & set & CAMEL_MESSAGE_DELETED) &&
			    check_for_trash (folder)) {
				/* FIXME: should add a separate journal entry for this case. */ ;
			} else {
				CamelExchangeFolder *exchange_folder = (CamelExchangeFolder *) folder;
				CamelExchangeJournal *journal = (CamelExchangeJournal *) exchange_folder->journal;
				camel_exchange_journal_delete (journal, info->uid, flags, set, NULL);
				return CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->info_set_flags(info, flags, set);
			}
		}
	}
	return FALSE;
}

static gboolean
info_set_user_tag(CamelMessageInfo *info, const gchar *name, const gchar *value)
{
	gint res;

	if (CAMEL_EXCHANGE_SUMMARY (info->summary)->readonly)
		return FALSE;

	res = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->info_set_user_tag(info, name, value);
	if (res && info->summary->folder && info->uid) {
		camel_stub_send_oneway (((CamelExchangeFolder *)info->summary->folder)->stub,
					CAMEL_STUB_CMD_SET_MESSAGE_TAG,
					CAMEL_STUB_ARG_FOLDER, info->summary->folder->full_name,
					CAMEL_STUB_ARG_STRING, info->uid,
					CAMEL_STUB_ARG_STRING, name,
					CAMEL_STUB_ARG_STRING, value,
					CAMEL_STUB_ARG_END);
	}

	return res;
}

/**
 * camel_exchange_summary_get_readonly:
 * @summary: the summary
 *
 * Tests if the folder represented by @summary is read-only.
 *
 * Return value: %TRUE or %FALSE
 **/
gboolean
camel_exchange_summary_get_readonly (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SUMMARY (summary), FALSE);

	return CAMEL_EXCHANGE_SUMMARY (summary)->readonly;
}

/**
 * camel_exchange_summary_set_readonly:
 * @summary: the summary
 * @readonly: the read-only state of @summary
 *
 * Sets @summary's read-only state to @readonly. (This means that
 * @summary is the summary for a read-only folder, not necessarily
 * that the file itself is read-only.)
 **/
void
camel_exchange_summary_set_readonly (CamelFolderSummary *summary,
				     gboolean readonly)
{
	CamelExchangeSummary *es;

	g_return_if_fail (CAMEL_IS_EXCHANGE_SUMMARY (summary));

	es = CAMEL_EXCHANGE_SUMMARY (summary);
	if (es->readonly != readonly)
		camel_folder_summary_touch (summary);
	es->readonly = readonly;
}

/**
 * camel_exchange_summary_get_article_num:
 * @summary: the summary
 *
 * Returns the highest article number of a message present in the folder represented by @summary.
 *
 * Return value: Highest article number for a message present in the folder.
 **/
guint32
camel_exchange_summary_get_article_num (CamelFolderSummary *summary)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SUMMARY (summary), FALSE);

	return CAMEL_EXCHANGE_SUMMARY (summary)->high_article_num;
}

/**
 * camel_exchange_summary_set_article_num:
 * @summary: the summary
 * @article_num: Highest article number of a message present in the folder.
 *
 * Sets @summary's high-article-number to @article_num.
 **/
void
camel_exchange_summary_set_article_num (CamelFolderSummary *summary,
					guint32 article_num)
{
	CamelExchangeSummary *es;

	g_return_if_fail (CAMEL_IS_EXCHANGE_SUMMARY (summary));

	es = CAMEL_EXCHANGE_SUMMARY (summary);
	if (!es->readonly)
		camel_folder_summary_touch (summary);
	es->high_article_num = article_num;
}

/**
 * camel_exchange_summary_add_offline:
 * @summary: the summary
 * @uid: the UID of the new message
 * @message: the new message
 * @info: the message info
 *
 * Adds a new entry to @summary with UID @uid, corresponding to
 * @message and @info.
 **/
void
camel_exchange_summary_add_offline (CamelFolderSummary *summary,
				    const gchar *uid,
				    CamelMimeMessage *message,
				    CamelMessageInfo *info)
{
	CamelMessageInfoBase *mi;
	const CamelFlag *flag;
	const CamelTag *tag;

	/* Create summary entry */
	mi = (CamelMessageInfoBase *)camel_folder_summary_info_new_from_message (summary, message);

	/* Copy flags 'n' tags */
	mi->flags = camel_message_info_flags(info);

	flag = camel_message_info_user_flags(info);
	while (flag) {
		camel_message_info_set_user_flag((CamelMessageInfo *)mi, flag->name, TRUE);
		flag = flag->next;
	}
	tag = camel_message_info_user_tags(info);
	while (tag) {
		camel_message_info_set_user_tag((CamelMessageInfo *)mi, tag->name, tag->value);
		tag = tag->next;
	}

	mi->size = camel_message_info_size(info);
	mi->uid = camel_pstring_strdup (uid);
	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

/**
 * camel_exchange_summary_add_offline_uncached:
 * @summary: the summary
 * @uid: the UID of the new message
 * @info: the message info
 *
 * Adds a new entry to @summary with UID @uid, corresponding to
 * @info.
 **/
void
camel_exchange_summary_add_offline_uncached (CamelFolderSummary *summary,
					     const gchar *uid,
					     CamelMessageInfo *info)
{
	CamelMessageInfo *mi;

	/* Create summary entry */
	mi = camel_message_info_clone(info);

	/* Set uid and add to summary */
	mi->uid = camel_pstring_strdup (uid);
	camel_folder_summary_add (summary, mi);
}
