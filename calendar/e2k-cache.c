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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e2k-cache.h"
#include "e2k-restriction.h"

#define PARENT_TYPE G_TYPE_OBJECT

typedef struct {
	E2kCache *cache;
	char *query;
	int status_code;
	E2kResult *results;
	int nresults;
} E2kCacheEntry;

static void e2k_cache_class_init (E2kCacheClass *klass);
static void e2k_cache_init       (E2kCache *cache);
static void e2k_cache_dispose    (GObject *object);

static GObjectClass *parent_class = NULL;

static void
e2k_cache_class_init (E2kCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->dispose = e2k_cache_dispose;
}

static void
e2k_cache_init (E2kCache *cache)
{
	cache->folder = NULL;
	cache->entries = g_hash_table_new (g_str_hash, g_str_equal);
}

static gboolean
free_cache_entry (gpointer key, gpointer value, gpointer user_data)
{
	E2kCacheEntry *entry = (E2kCacheEntry *) value;

	g_return_val_if_fail (entry != NULL, TRUE);

	g_free (entry->query);
	e2k_results_free (entry->results, entry->nresults);
	g_free (entry);

	return TRUE;
}

static void
e2k_cache_dispose (GObject *object)
{
	E2kCache *cache = (E2kCache *) object;

	g_return_if_fail (E2K_IS_CACHE (cache));

	if (cache->folder) {
		g_object_unref (cache->folder);
		cache->folder = NULL;
	}

	if (cache->entries) {
		g_hash_table_foreach_remove (cache->entries, free_cache_entry, NULL);
		g_hash_table_destroy (cache->entries);
		cache->entries = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

E2K_MAKE_TYPE (e2k_cache, E2kCache, e2k_cache_class_init, e2k_cache_init, PARENT_TYPE)

E2kCache *
e2k_cache_new (EFolder *folder)
{
	E2kCache *cache;

	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	cache = g_object_new (E2K_TYPE_CACHE, NULL);

	g_object_ref (folder);
	cache->folder = folder;

	return cache;
}

int
e2k_cache_search (E2kCache *cache,
		  const char **props, int nprops, E2kRestriction *rn,
		  E2kResult **results, int *nresults)
{
	int status;
	E2kCacheEntry *entry;
	char *where_clause;

	g_return_val_if_fail (E2K_IS_CACHE (cache), -1);

	/* search the query in the cache */
	where_clause = e2k_restriction_to_sql (rn);
	if (!where_clause)
		return SOUP_ERROR_MALFORMED;

	entry = g_hash_table_lookup (cache->entries, where_clause);
	if (entry) {
		g_free (where_clause);

		*results = e2k_results_copy (entry->results, entry->nresults);
		*nresults = entry->nresults;

		return entry->status_code;
	}

	/* not found, so run the query on the server */
	status = e_folder_exchange_search_sync (cache->folder,
						props, nprops, FALSE, rn, NULL,
						results, nresults);
	if (status != SOUP_ERROR_DAV_MULTISTATUS) {
		g_free (where_clause);
		return status;
	}

	/* add result to cache */
	entry = g_new0 (E2kCacheEntry, 1);
	entry->cache = cache;
	entry->query = where_clause;
	entry->status_code = status;
	entry->results = e2k_results_copy (*results, *nresults);
	entry->nresults = *nresults;

	g_hash_table_insert (cache->entries, entry->query, entry);

	return status;
}

void
e2k_cache_clear (E2kCache *cache)
{
	g_return_if_fail (E2K_IS_CACHE (cache));
	g_hash_table_foreach_remove (cache->entries, free_cache_entry, NULL);
}
