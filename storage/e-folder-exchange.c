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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-folder-exchange.h"
#include "exchange-account.h"
#include "exchange-hierarchy.h"
#include "e2k-uri.h"

#include <e-util/e-path.h>
#include <gal/util/e-util.h>
#include <gal/util/e-xml-utils.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _EFolderExchangePrivate {
	ExchangeHierarchy *hier;
	char *internal_uri, *outlook_class, *storage_dir;
	const char *path;
	gboolean has_subfolders;
};

#define PARENT_TYPE E_TYPE_FOLDER
static EFolderClass *parent_class = NULL;

#define EF_CLASS(hier) (E_FOLDER_CLASS (G_OBJECT_GET_CLASS (hier)))

static void dispose (GObject *object);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void
init (GObject *object)
{
	EFolderExchange *folder = E_FOLDER_EXCHANGE (object);

	folder->priv = g_new0 (EFolderExchangePrivate, 1);
}

static void
dispose (GObject *object)
{
	EFolderExchange *folder = E_FOLDER_EXCHANGE (object);

	if (folder->priv->hier) {
		g_object_unref (folder->priv->hier);
		folder->priv->hier = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	EFolderExchange *folder = E_FOLDER_EXCHANGE (object);

	g_free (folder->priv->internal_uri);
	g_free (folder->priv->outlook_class);
	g_free (folder->priv->storage_dir);
	g_free (folder->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (e_folder_exchange, EFolderExchange, class_init, init, PARENT_TYPE)


EFolder *
e_folder_exchange_new (ExchangeHierarchy *hier, const char *name,
		       const char *type, const char *outlook_class,
		       const char *physical_uri, const char *internal_uri)
{
	EFolderExchange *efe;
	EFolder *ef;

	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY (hier), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (type != NULL, NULL);
	g_return_val_if_fail (physical_uri != NULL, NULL);
	g_return_val_if_fail (internal_uri != NULL, NULL);

	efe = g_object_new (E_TYPE_FOLDER_EXCHANGE, NULL);
	ef = (EFolder *)efe;

	e_folder_construct (ef, name, type, "");
	e_folder_set_physical_uri (ef, physical_uri);

	efe->priv->hier = hier;
	g_object_ref (hier);
	efe->priv->internal_uri = g_strdup (internal_uri);
	efe->priv->path = e2k_uri_path (e_folder_get_physical_uri (ef));
	efe->priv->outlook_class = g_strdup (outlook_class);

	return ef;
}

const char *
e_folder_exchange_get_internal_uri (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->internal_uri;
}

void
e_folder_exchange_set_internal_uri (EFolder *folder, const char *internal_uri)
{
	EFolderExchange *efe;

	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));
	g_return_if_fail (internal_uri != NULL);

	efe = E_FOLDER_EXCHANGE (folder);
	g_free (efe->priv->internal_uri);
	efe->priv->internal_uri = g_strdup (internal_uri);
}

const char *
e_folder_exchange_get_path (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->path;
}

gboolean
e_folder_exchange_get_has_subfolders (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), FALSE);

	return E_FOLDER_EXCHANGE (folder)->priv->has_subfolders;
}

void
e_folder_exchange_set_has_subfolders (EFolder *folder,
				      gboolean has_subfolders)
{
	g_return_if_fail (E_IS_FOLDER_EXCHANGE (folder));

	E_FOLDER_EXCHANGE (folder)->priv->has_subfolders = has_subfolders;
}

const char *
e_folder_exchange_get_outlook_class (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->outlook_class;
}

ExchangeHierarchy *
e_folder_exchange_get_hierarchy (EFolder *folder)
{
	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	return E_FOLDER_EXCHANGE (folder)->priv->hier;
}	

char *
e_folder_exchange_get_storage_file (EFolder *folder, const char *filename)
{
	EFolderExchange *efe;
	char *path;

	g_return_val_if_fail (E_IS_FOLDER_EXCHANGE (folder), NULL);

	efe = (EFolderExchange *)folder;

	if (!efe->priv->storage_dir) {
		efe->priv->storage_dir = e_path_to_physical (
			efe->priv->hier->account->storage_dir,
			efe->priv->path);
		e_mkdir_hier (efe->priv->storage_dir, 0755);
	}

	path = g_build_filename (efe->priv->storage_dir, filename, NULL);
	return path;
}


gboolean
e_folder_exchange_save_to_file (EFolder *folder, const char *filename)
{
	xmlDoc *doc;
	xmlNode *root;
	const char *name, *type, *outlook_class;
	const char *physical_uri, *internal_uri;
	int status;

	name = e_folder_get_name (folder);
	type = e_folder_get_type_string (folder);
	outlook_class = e_folder_exchange_get_outlook_class (folder);
	physical_uri = e_folder_get_physical_uri (folder);
	internal_uri = e_folder_exchange_get_internal_uri (folder);

	g_return_val_if_fail (name && type && physical_uri && internal_uri,
			      FALSE);

	doc = xmlNewDoc ("1.0");
	root = xmlNewDocNode (doc, NULL, "connector-folder", NULL);
	xmlNewProp (root, "version", "1");
	xmlDocSetRootElement (doc, root);

	xmlNewChild (root, NULL, "displayname", name);
	xmlNewChild (root, NULL, "type", type);
	xmlNewChild (root, NULL, "outlook_class", outlook_class);
	xmlNewChild (root, NULL, "physical_uri", physical_uri);
	xmlNewChild (root, NULL, "internal_uri", internal_uri);

	status = xmlSaveFile (filename, doc);
	xmlFreeDoc (doc);
	if (status < 0)
		unlink (filename);

	return status == 0;
}

EFolder *
e_folder_exchange_new_from_file (ExchangeHierarchy *hier, const char *filename)
{
	EFolder *folder = NULL;
	xmlDoc *doc;
	xmlNode *root, *node;
	char *version, *display_name = NULL;
	char *type = NULL, *outlook_class = NULL;
	char *physical_uri = NULL, *internal_uri = NULL;

	doc = xmlParseFile (filename);
	if (!doc)
		return NULL;

	root = xmlDocGetRootElement (doc);
	if (root == NULL || strcmp (root->name, "connector-folder") != 0)
		return NULL;
	version = xmlGetProp (root, "version");
	if (!version)
		return NULL;
	if (strcmp (version, "1") != 0) {
		xmlFree (version);
		return NULL;
	}
	xmlFree (version);

	node = e_xml_get_child_by_name (root, "displayname");
	if (!node)
		goto done;
	display_name = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, "type");
	if (!node)
		goto done;
	type = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, "outlook_class");
	if (!node)
		goto done;
	outlook_class = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, "physical_uri");
	if (!node)
		goto done;
	physical_uri = xmlNodeGetContent (node);

	node = e_xml_get_child_by_name (root, "internal_uri");
	if (!node)
		goto done;
	internal_uri = xmlNodeGetContent (node);

	if (!display_name || !type || !physical_uri || !internal_uri)
		goto done;

	folder = e_folder_exchange_new (hier, display_name,
					type, outlook_class,
					physical_uri, internal_uri);

 done:
	xmlFree (display_name);
	xmlFree (type);
	xmlFree (outlook_class);
	xmlFree (physical_uri);
	xmlFree (internal_uri);

	return folder;
}



/* E2kConnection wrappers */
#define E_FOLDER_EXCHANGE_CONNECTION(efe) (exchange_account_get_connection (((EFolderExchange *)efe)->priv->hier->account))
#define E_FOLDER_EXCHANGE_URI(efe) (((EFolderExchange *)efe)->priv->internal_uri)

void
e_folder_exchange_propfind (EFolder *folder, const char *depth,
			    const char **props, int nprops,
			    E2kResultsCallback callback, gpointer user_data)
{
	e2k_connection_propfind (E_FOLDER_EXCHANGE_CONNECTION (folder),
				 E_FOLDER_EXCHANGE_URI (folder),
				 depth, props, nprops, callback, user_data);
}

int
e_folder_exchange_propfind_sync (EFolder *folder, const char *depth,
				 const char **props, int nprops,
				 E2kResult **results, int *nresults)
{
	return e2k_connection_propfind_sync (
		E_FOLDER_EXCHANGE_CONNECTION (folder),
		E_FOLDER_EXCHANGE_URI (folder),
		depth, props, nprops,
		results, nresults);
}

void
e_folder_exchange_bpropfind (EFolder *folder,
			     const char **hrefs, int nhrefs,
			     const char *depth,
			     const char **props, int nprops,
			     E2kResultsCallback callback, gpointer user_data)
{
	e2k_connection_bpropfind (E_FOLDER_EXCHANGE_CONNECTION (folder),
				  E_FOLDER_EXCHANGE_URI (folder),
				  hrefs, nhrefs, depth, props, nprops,
				  callback, user_data);
}

int
e_folder_exchange_bpropfind_sync (EFolder *folder,
				  const char **hrefs, int nhrefs,
				  const char *depth,
				  const char **props, int nprops,
				  E2kResult **results, int *nresults)
{
	return e2k_connection_bpropfind_sync (
		E_FOLDER_EXCHANGE_CONNECTION (folder),
		E_FOLDER_EXCHANGE_URI (folder),
		hrefs, nhrefs, depth, props, nprops,
		results, nresults);
}

void
e_folder_exchange_search (EFolder *folder,
			  const char **props, int nprops,
			  gboolean folders_only,
			  E2kRestriction *rn, const char *orderby,
			  E2kResultsCallback callback, gpointer user_data)
{
	e2k_connection_search (E_FOLDER_EXCHANGE_CONNECTION (folder),
			       E_FOLDER_EXCHANGE_URI (folder),
			       props, nprops, folders_only, rn, orderby,
			       callback, user_data);
}

int
e_folder_exchange_search_sync (EFolder *folder,
			       const char **props, int nprops,
			       gboolean folders_only,
			       E2kRestriction *rn, const char *orderby,
			       E2kResult **results, int *nresults)
{
	return e2k_connection_search_sync (
		E_FOLDER_EXCHANGE_CONNECTION (folder),
		E_FOLDER_EXCHANGE_URI (folder),
		props, nprops, folders_only, rn, orderby,
		results, nresults);
}

void
e_folder_exchange_search_with_progress (EFolder *folder,
					const char **props, int nprops,
					E2kRestriction *rn,
					const char *orderby,
					int increment_size,
					gboolean ascending,
					E2kProgressCallback progress_callback,
					E2kSimpleCallback done_callback,
					gpointer user_data)
{
	e2k_connection_search_with_progress (
		E_FOLDER_EXCHANGE_CONNECTION (folder),
		E_FOLDER_EXCHANGE_URI (folder),
		props, nprops, rn, orderby,
		increment_size, ascending,
		progress_callback, done_callback, user_data);
}


void
e_folder_exchange_subscribe (EFolder *folder,
			     E2kConnectionChangeType type, int min_interval,
			     E2kConnectionChangeCallback callback,
			     gpointer user_data)
{
	e2k_connection_subscribe (E_FOLDER_EXCHANGE_CONNECTION (folder),
				  E_FOLDER_EXCHANGE_URI (folder),
				  type, min_interval, callback, user_data);
}

void
e_folder_exchange_unsubscribe (EFolder *folder)
{
	e2k_connection_unsubscribe (E_FOLDER_EXCHANGE_CONNECTION (folder),
				    E_FOLDER_EXCHANGE_URI (folder));
}

void
e_folder_exchange_transfer (EFolder *source, EFolder *dest,
			    const char *source_hrefs, gboolean delete_original,
			    E2kResultsCallback callback, gpointer user_data)
{
	e2k_connection_transfer (E_FOLDER_EXCHANGE_CONNECTION (source),
				 E_FOLDER_EXCHANGE_URI (source),
				 E_FOLDER_EXCHANGE_URI (dest),
				 source_hrefs, delete_original,
				 callback, user_data);
}

void
e_folder_exchange_append (EFolder *folder,
			  const char *object_name, const char *content_type,
			  const char *body, int length,
			  E2kSimpleCallback callback, gpointer user_data)
{
	e2k_connection_append (E_FOLDER_EXCHANGE_CONNECTION (folder),
			       E_FOLDER_EXCHANGE_URI (folder),
			       object_name, content_type, body, length,
			       callback, user_data);
}

void
e_folder_exchange_bproppatch (EFolder *folder,
			      const char **hrefs, int nhrefs,
			      E2kProperties *props, gboolean create,
			      E2kResultsCallback callback, gpointer user_data)
{
	e2k_connection_bproppatch (E_FOLDER_EXCHANGE_CONNECTION (folder),
				   E_FOLDER_EXCHANGE_URI (folder),
				   hrefs, nhrefs, props, create,
				   callback, user_data);
}


void
e_folder_exchange_bdelete (EFolder *folder,
			   const char **hrefs, int nhrefs,
			   E2kProgressCallback progress_callback,
			   E2kSimpleCallback done_callback,
			   gpointer user_data)
{
	e2k_connection_bdelete (E_FOLDER_EXCHANGE_CONNECTION (folder),
				E_FOLDER_EXCHANGE_URI (folder),
				hrefs, nhrefs, progress_callback,
				done_callback, user_data);
}

void
e_folder_exchange_mkcol (EFolder *folder, E2kProperties *props,
			 E2kSimpleCallback callback, gpointer user_data)
{
	e2k_connection_mkcol (E_FOLDER_EXCHANGE_CONNECTION (folder),
			      E_FOLDER_EXCHANGE_URI (folder),
			      props, callback, user_data);
}

void
e_folder_exchange_delete (EFolder *folder,
			  E2kSimpleCallback callback, gpointer user_data)
{
	e2k_connection_delete (E_FOLDER_EXCHANGE_CONNECTION (folder),
			       E_FOLDER_EXCHANGE_URI (folder),
			       callback, user_data);
}

void
e_folder_exchange_transfer_dir (EFolder *source, EFolder *dest,
				gboolean delete_original,
				E2kSimpleCallback callback, gpointer user_data)
{
	e2k_connection_transfer_dir (E_FOLDER_EXCHANGE_CONNECTION (source),
				     E_FOLDER_EXCHANGE_URI (source),
				     E_FOLDER_EXCHANGE_URI (dest),
				     delete_original, callback, user_data);

}
