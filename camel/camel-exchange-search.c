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

/* camel-exchange-search.c: exchange folder search */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-exchange-search.h"
#include "camel-exchange-folder.h"

static ESExpResult *
exchange_body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv,
			CamelFolderSearch *s);

static void
camel_exchange_search_class_init (CamelExchangeSearchClass *camel_exchange_search_class)
{
	/* virtual method overload */
	CamelFolderSearchClass *camel_folder_search_class =
		CAMEL_FOLDER_SEARCH_CLASS (camel_exchange_search_class);

	/* virtual method overload */
	camel_folder_search_class->body_contains = exchange_body_contains;
}

CamelType
camel_exchange_search_get_type (void)
{
	static CamelType camel_exchange_search_type = CAMEL_INVALID_TYPE;

	if (camel_exchange_search_type == CAMEL_INVALID_TYPE) {
		camel_exchange_search_type = camel_type_register (
			CAMEL_FOLDER_SEARCH_TYPE, "CamelExchangeSearch",
			sizeof (CamelExchangeSearch),
			sizeof (CamelExchangeSearchClass),
			(CamelObjectClassInitFunc) camel_exchange_search_class_init,
			NULL, NULL, NULL);
	}

	return camel_exchange_search_type;
}

static ESExpResult *
exchange_body_contains (struct _ESExp *f, int argc, struct _ESExpResult **argv,
			CamelFolderSearch *s)
{
	CamelExchangeFolder *folder = CAMEL_EXCHANGE_FOLDER (s->folder);
	char *value = argv[0]->value.string, *real_uid;
	const char *uid;
	ESExpResult *r;
	CamelMessageInfo *info;
	GHashTable *uid_hash = NULL;
	GPtrArray *found_uids;
	int i;

	if (s->current) {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_ARRAY_PTR);
		r->value.ptrarray = g_ptr_array_new ();
	}

	if (argc == 1 && *value == '\0') {
		/* optimise the match "" case - match everything */

		if (s->current)
			r->value.bool = TRUE;
		else {
			for (i = 0; i < s->summary->len; i++) {
				CamelMessageInfo *info = s->summary->pdata[i];
				g_ptr_array_add (r->value.ptrarray, (char *)camel_message_info_uid (info));
			}
		}
		return r;
	}

	/* FIXME: what if we have multiple string args? */
	if (!camel_stub_send (folder->stub, NULL,
			      CAMEL_STUB_CMD_SEARCH_FOLDER,
			      CAMEL_STUB_ARG_FOLDER, s->folder->full_name,
			      CAMEL_STUB_ARG_STRING, value,
			      CAMEL_STUB_ARG_RETURN,
			      CAMEL_STUB_ARG_STRINGARRAY, &found_uids,
			      CAMEL_STUB_ARG_END))
		return r;

	if (!found_uids->len) {
		g_ptr_array_free (found_uids, TRUE);
		return r;
	}

	if (s->current) {
		uid = camel_message_info_uid (s->current);
		for (i = 0; i < found_uids->len; i++) {
			if (!strcmp (uid, found_uids->pdata[i]))
				r->value.bool = TRUE;
			g_free (found_uids->pdata[i]);
		}
		g_ptr_array_free (found_uids, TRUE);
		return r;
	}

	/* if we need to setup a hash of summary items, this way we get
	   access to the summary memory which is locked for the duration of
	   the search, and wont vanish on us */
	if (uid_hash == NULL) {
		int i;

		uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i = 0; i < s->summary->len; i++) {
			info = s->summary->pdata[i];
			g_hash_table_insert (uid_hash, (char *)camel_message_info_uid (info), info);
		}
	}

	for (i = 0; i < found_uids->len; i++) {
		if (g_hash_table_lookup_extended (uid_hash, found_uids->pdata[i], (void *)&real_uid, (void *)&info))
			g_ptr_array_add (r->value.ptrarray, real_uid);
		g_free (found_uids->pdata[i]);
	}
	g_ptr_array_free (found_uids, TRUE);

	/* we could probably cache this globally, but its probably not worth it */
	if (uid_hash)
		g_hash_table_destroy (uid_hash);

	return r;
}

/**
 * camel_exchange_search_new:
 *
 * Return value: A new CamelExchangeSearch widget.
 **/
CamelFolderSearch *
camel_exchange_search_new (void)
{
	CamelFolderSearch *new = CAMEL_FOLDER_SEARCH (camel_object_new (camel_exchange_search_get_type ()));

	camel_folder_search_construct (new);
	return new;
}
