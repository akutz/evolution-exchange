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
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <camel/camel-exchange-summary.h>
#include <camel/camel-file-utils.h>

#define CAMEL_EXCHANGE_SUMMARY_VERSION (1)

static int header_load (CamelFolderSummary *summary, FILE *in);
static int header_save (CamelFolderSummary *summary, FILE *out);

static CamelMessageInfo *message_info_load (CamelFolderSummary *summary,
					    FILE *in);
static int               message_info_save (CamelFolderSummary *summary,
					    FILE *out,
					    CamelMessageInfo *info);
static CamelMessageInfo *message_info_new  (CamelFolderSummary *summary,
					    struct _camel_header_raw *h);

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
	camel_folder_summary_class->message_info_new = message_info_new;
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
camel_exchange_summary_new (const char *filename)
{
	CamelFolderSummary *summary;

	summary = (CamelFolderSummary *)camel_object_new (CAMEL_EXCHANGE_SUMMARY_TYPE);
	camel_folder_summary_set_filename (summary, filename);
	if (camel_folder_summary_load (summary) == -1) {
		camel_folder_summary_clear (summary);
		camel_folder_summary_touch (summary);
	}

	return summary;
}


static int
header_load (CamelFolderSummary *summary, FILE *in)
{
	CamelExchangeSummary *exchange = (CamelExchangeSummary *) summary;
	guint32 version, readonly;
	
	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_load (summary, in) == -1)
		return -1;
	
	if (camel_file_util_decode_uint32 (in, &version) == -1)
		return -1;
	if (version > CAMEL_EXCHANGE_SUMMARY_VERSION)
		return -1;

	if (camel_file_util_decode_uint32 (in, &readonly) == -1)
		return -1;
	exchange->readonly = readonly;
	
	return 0;
}

static int
header_save (CamelFolderSummary *summary, FILE *out)
{
	CamelExchangeSummary *exchange = (CamelExchangeSummary *) summary;
	
	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->summary_header_save (summary, out) == -1)
		return -1;
	
	if (camel_file_util_encode_uint32 (out, CAMEL_EXCHANGE_SUMMARY_VERSION) == -1)
		return -1;

	if (camel_file_util_encode_uint32 (out, exchange->readonly) == -1)
		return -1;
	
	return 0;
}

static CamelMessageInfo *
message_info_load (CamelFolderSummary *summary, FILE *in)
{
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	char *thread_index;

	info = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_load (summary, in);
	if (info) {
		einfo = (CamelExchangeMessageInfo *)info;

		if (camel_file_util_decode_string (in, &thread_index) == -1)
			goto error;

		if (thread_index && *thread_index)
			einfo->thread_index = thread_index;
		else
			g_free (thread_index);
	}

	return info;
error:
	camel_folder_summary_info_free (summary, info);
	return NULL;
}

static int
message_info_save (CamelFolderSummary *summary, FILE *out, CamelMessageInfo *info)
{
	CamelExchangeMessageInfo *einfo = (CamelExchangeMessageInfo *)info;

	if (CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_save (summary, out, info) == -1)
		return -1;

	return camel_file_util_encode_string (out, einfo->thread_index ? einfo->thread_index : "");
}

static CamelMessageInfo *
message_info_new (CamelFolderSummary *summary, struct _camel_header_raw *h)
{
	CamelMessageInfo *info;
	CamelExchangeMessageInfo *einfo;
	const char *thread_index;

	info = CAMEL_FOLDER_SUMMARY_CLASS (parent_class)->message_info_new (summary, h);
	if (!info)
		return info;

	einfo = (CamelExchangeMessageInfo *)info;
	thread_index = camel_header_raw_find (&h, "Thread-Index", NULL);
	if (thread_index)
		einfo->thread_index = g_strdup (thread_index + 1);

	return info;
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
				    const char *uid,
				    CamelMimeMessage *message,
				    CamelMessageInfo *info)
{
	CamelMessageInfo *mi;
	CamelFlag *flag;
	CamelTag *tag;

	/* Create summary entry */
	mi = camel_folder_summary_info_new_from_message (summary, message);

	/* Copy flags 'n' tags */
	mi->flags = info->flags;
	flag = info->user_flags;
	while (flag) {
		camel_flag_set (&mi->user_flags, flag->name, TRUE);
		flag = flag->next;
	}
	tag = info->user_tags;
	while (tag) {
		camel_tag_set (&mi->user_tags, tag->name, tag->value);
		tag = tag->next;
	}

	/* Set uid and add to summary */
	camel_message_info_set_uid (mi, g_strdup (uid));
	camel_folder_summary_add (summary, mi);
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
					     const char *uid,
					     CamelMessageInfo *info)
{
	CamelMessageInfo *mi;

	/* Create summary entry */
	mi = camel_folder_summary_info_new (summary);

	camel_message_info_dup_to (info, mi);

	/* Set uid and add to summary */
	camel_message_info_set_uid (mi, g_strdup (uid));
	camel_folder_summary_add (summary, mi);
}
