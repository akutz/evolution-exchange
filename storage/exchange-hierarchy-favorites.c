/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* ExchangeHierarchyFavorites: class for the "Favorites" Public Folders
 * hierarchy (and favorites-handling code).
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-favorites.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _ExchangeHierarchyFavoritesPrivate {
	char *public_uri, *shortcuts_uri;
};

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY_SOMEDAV
static ExchangeHierarchySomeDAVClass *parent_class = NULL;

static GPtrArray *get_hrefs (ExchangeHierarchySomeDAV *hsd);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchySomeDAVClass *somedav_class =
		EXCHANGE_HIERARCHY_SOMEDAV_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	somedav_class->get_hrefs = get_hrefs;
}

static void
init (GObject *object)
{
	ExchangeHierarchyFavorites *hfav = EXCHANGE_HIERARCHY_FAVORITES (object);

	hfav->priv = g_new0 (ExchangeHierarchyFavoritesPrivate, 1);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchyFavorites *hfav = EXCHANGE_HIERARCHY_FAVORITES (object);

	g_free (hfav->priv->public_uri);
	g_free (hfav->priv->shortcuts_uri);
	g_free (hfav->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_favorites, ExchangeHierarchyFavorites, class_init, init, PARENT_TYPE)

static const char *shortcuts_props[] = {
	PR_FAV_DISPLAY_NAME,		/* PR_DISPLAY_NAME of referent */
	PR_FAV_DISPLAY_ALIAS,		/* if set, user-chosen display name */
	PR_FAV_PUBLIC_SOURCE_KEY,	/* PR_SOURCE_KEY of referent */
	PR_FAV_PARENT_SOURCE_KEY,	/* PR_FAV_PUBLIC_SOURCE_KEY of parent */
	PR_FAV_LEVEL_MASK		/* depth in hierarchy (first level is 1) */
};
static const int n_shortcuts_props = G_N_ELEMENTS (shortcuts_props);

static GPtrArray *
get_hrefs (ExchangeHierarchySomeDAV *hsd)
{
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (hsd);
	ExchangeHierarchyFavorites *hfav = EXCHANGE_HIERARCHY_FAVORITES (hsd);
	E2kContext *ctx = exchange_account_get_context (hier->account);
	GPtrArray *hrefs;
	E2kResultIter *iter;
	E2kResult *result, *results;
	E2kHTTPStatus status;
	GByteArray *source_key;
	const char *prop = E2K_PR_DAV_HREF;
	char *perm_url;
	int i, nresults;

	hrefs = g_ptr_array_new ();

	/* Scan the shortcut links and use PROPFIND to resolve the
	 * permanent_urls. Unfortunately it doesn't seem to be possible
	 * to BPROPFIND a group of permanent_urls.
	 */
	iter = e2k_context_search_start (ctx, NULL, hfav->priv->shortcuts_uri, 
					 shortcuts_props, n_shortcuts_props,
					 NULL, NULL, TRUE);
	while ((result = e2k_result_iter_next (iter))) {
		source_key = e2k_properties_get_prop (result->props, PR_FAV_PUBLIC_SOURCE_KEY);
		if (!source_key)
			continue;
		perm_url = e2k_entryid_to_permanenturl (source_key, hfav->priv->public_uri);

		status = e2k_context_propfind (ctx, NULL, perm_url,
					       &prop, 1, &results, &nresults);
		if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status) && nresults) {
			g_ptr_array_add (hrefs, g_strdup (results[0].href));
			e2k_results_free (results, nresults);
		}

		g_free (perm_url);
	}

	status = e2k_result_iter_free (iter);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		/* FIXME: need to be able to return an error */
		for (i = 0; i < hrefs->len; i++)
			g_free (hrefs->pdata[i]);
		g_ptr_array_free (hrefs, TRUE);
		hrefs = NULL;
	}

	return hrefs;
}	

/**
 * exchange_hierarchy_favorites_new:
 * @account: an #ExchangeAccount
 * @hierarchy_name: the name of the hierarchy
 * @physical_uri_prefix: prefix for physical URIs in this hierarchy
 * @home_uri: the home URI of the owner's mailbox
 * @public_uri: the URI of the public folder tree
 * @owner_name: display name of the owner of the hierarchy
 * @owner_email: email address of the owner of the hierarchy
 * @source_uri: account source URI for folders in this hierarchy
 *
 * Creates a new Favorites hierarchy
 *
 * Return value: the new hierarchy.
 **/
ExchangeHierarchy *
exchange_hierarchy_favorites_new (ExchangeAccount *account,
				  const char *hierarchy_name,
				  const char *physical_uri_prefix,
				  const char *home_uri,
				  const char *public_uri,
				  const char *owner_name,
				  const char *owner_email,
				  const char *source_uri)
{
	ExchangeHierarchy *hier;
	ExchangeHierarchyFavorites *hfav;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);

	hier = g_object_new (EXCHANGE_TYPE_HIERARCHY_FAVORITES, NULL);

	hfav = (ExchangeHierarchyFavorites *)hier;
	hfav->priv->public_uri = g_strdup (public_uri);
	hfav->priv->shortcuts_uri = e2k_uri_concat (home_uri, "NON_IPM_SUBTREE/Shortcuts");

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hier),
					     account,
					     EXCHANGE_HIERARCHY_FAVORITES,
					     hierarchy_name,
					     physical_uri_prefix,
					     public_uri,
					     owner_name, owner_email,
					     source_uri,
					     FALSE, "public-folder");
	return hier;
}
