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

/* ExchangeHierarchyForeign: class for a hierarchy consisting of a
 * selected subset of folders from another user's mailbox.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-foreign.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"

#include <e-util/e-xml-hash-utils.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _ExchangeHierarchyForeignPrivate {
	gboolean checked_hide_private, checking_hide_private;
};

typedef struct {
	ExchangeAccountFolderCallback callback;
	gpointer user_data;
	gpointer op_data;
} ExchangeHierarchyForeignDeferredOp;

extern const char *exchange_localfreebusy_path;

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY_WEBDAV
static ExchangeHierarchyWebDAVClass *parent_class = NULL;

static void async_create_folder (ExchangeHierarchy *hier, EFolder *parent,
				 const char *name, const char *type,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void async_remove_folder (ExchangeHierarchy *hier, EFolder *folder,
				 ExchangeAccountFolderCallback callback,
				 gpointer user_data);
static void got_subtree  (E2kConnection *conn, SoupMessage *msg,
			  E2kResult *results, int nresults,
			  gpointer user_data);
static void async_scan_subtree (ExchangeHierarchy *hier, EFolder *folder,
				ExchangeAccountFolderCallback callback,
				gpointer user_data);
static void add_to_storage (ExchangeHierarchy *hier);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *exchange_hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	exchange_hierarchy_class->add_to_storage      = add_to_storage;
	exchange_hierarchy_class->async_create_folder = async_create_folder;
	exchange_hierarchy_class->async_remove_folder = async_remove_folder;
	exchange_hierarchy_class->async_scan_subtree  = async_scan_subtree;
}

static void
init (GObject *object)
{
	ExchangeHierarchyForeign *hfor = EXCHANGE_HIERARCHY_FOREIGN (object);
	ExchangeHierarchy *hier = EXCHANGE_HIERARCHY (object);

	hfor->priv = g_new0 (ExchangeHierarchyForeignPrivate, 1);

	hier->hide_private_items = TRUE;
}

static void
finalize (GObject *object)
{
	ExchangeHierarchyForeign *hfor = EXCHANGE_HIERARCHY_FOREIGN (object);

	g_free (hfor->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_foreign, ExchangeHierarchyForeign, class_init, init, PARENT_TYPE)

static void
got_delegation_props (E2kConnection *conn, SoupMessage *msg,
		      E2kResult *results, int nresults,
		      gpointer user_data)
{
	ExchangeHierarchyForeign *hfor = user_data;
	ExchangeHierarchy *hier = user_data;
	GPtrArray *entryids, *privflags;
	GByteArray *entryid;
	const char *my_dn, *delegate_dn;
	int i;

	hfor->priv->checking_hide_private = FALSE;
	hfor->priv->checked_hide_private = TRUE;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode) || nresults != 1 ||
	    !SOUP_ERROR_IS_SUCCESSFUL (results[0].status) || !results[0].props)
		goto done;

	my_dn = hier->account->legacy_exchange_dn;

	entryids  = e2k_properties_get_prop (results[0].props, PR_DELEGATES_ENTRYIDS);
	privflags = e2k_properties_get_prop (results[0].props, PR_DELEGATES_SEE_PRIVATE);
	if (!entryids || !privflags)
		goto done;

	for (i = 0; i < entryids->len && i < privflags->len; i++) {
		entryid = entryids->pdata[i];
		delegate_dn = e2k_entryid_to_dn (entryid);

		if (delegate_dn && !g_ascii_strcasecmp (delegate_dn, my_dn)) {
			if (privflags->pdata[i] && atoi (privflags->pdata[i]))
				hier->hide_private_items = FALSE;
			break;
		}
	}

 done:
	g_object_unref (hfor);
}

static const char *privacy_props[] = {
	PR_DELEGATES_ENTRYIDS,
	PR_DELEGATES_SEE_PRIVATE,
};
static const int n_privacy_props = sizeof (privacy_props) / sizeof (privacy_props[0]);

static void
check_hide_private (ExchangeHierarchy *hier)
{
	ExchangeHierarchyForeign *hfor = EXCHANGE_HIERARCHY_FOREIGN (hier);

	if (hfor->priv->checked_hide_private)
		return;

	if (!hfor->priv->checking_hide_private) {
		hfor->priv->checking_hide_private = TRUE;

		g_object_ref (hfor);
		e_folder_exchange_bpropfind (hier->toplevel,
					     &exchange_localfreebusy_path, 1, "0",
					     privacy_props, n_privacy_props,
					     got_delegation_props, hier);
	}

	while (hfor->priv->checking_hide_private)
		g_main_context_iteration (NULL, TRUE);

	return;
}

static void
remove_all_cb (ExchangeHierarchy *hier, EFolder *folder, gpointer user_data)
{
	exchange_hierarchy_removed_folder (hier, folder);
}

static void
hierarchy_foreign_cleanup (ExchangeHierarchy *hier)
{
	char *mf_path;

	exchange_hierarchy_webdav_offline_scan_subtree (hier, remove_all_cb,
							NULL);

	mf_path = e_folder_exchange_get_storage_file (hier->toplevel, "hierarchy.xml");
	unlink (mf_path);
	g_free (mf_path);

	exchange_hierarchy_removed_folder (hier, hier->toplevel);
}

static inline gboolean
folder_is_unreadable (E2kProperties *props)
{
	char *access;

	access = e2k_properties_get_prop (props, PR_ACCESS);
	return !access || !atoi (access);
}

struct create_data {
	ExchangeHierarchy *hier;
	GList *uris;
	ExchangeAccountFolderCallback callback;
	gpointer user_data;
};

static void async_create_internal (E2kConnection *conn, struct create_data *cd);

static void
got_folder (E2kConnection *conn, SoupMessage *msg,
	    E2kResult *results, int nresults,
	    gpointer user_data)
{
	struct create_data *cd = user_data;
	ExchangeAccountFolderResult result;
	EFolder *folder = NULL;
	int status;

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		if (nresults == 1)
			status = results[0].status;
		else
			status = SOUP_ERROR_NOT_FOUND;
	} else
		status = msg->errorcode;

	if (SOUP_ERROR_IS_SUCCESSFUL (status)) {
		GPtrArray *folders = NULL;

		exchange_hierarchy_webdav_parse_folders (
			EXCHANGE_HIERARCHY_WEBDAV (cd->hier), msg,
			cd->hier->toplevel, results, nresults, &folders);

		if (folders && folders->len && !folder_is_unreadable (results[0].props)) {
			folder = folders->pdata[0];
			g_ptr_array_free (folders, TRUE);
			exchange_hierarchy_new_folder (cd->hier, folder);
			result = EXCHANGE_ACCOUNT_FOLDER_OK;
		} else {
			if (folders)
				g_ptr_array_free (folders, TRUE);
			result = EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
		}
	} else if (status == SOUP_ERROR_NOT_FOUND)
		result = EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	else if (status == SOUP_ERROR_UNAUTHORIZED)
		result = EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
	else {
		g_warning ("hfor got_folder: %d %s", msg->errorcode, msg->errorphrase);
		result = EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR;
	}

	if (result != EXCHANGE_ACCOUNT_FOLDER_OK && cd->uris) {
		async_create_internal (conn, cd);
		return;
	}

	cd->callback (cd->hier->account, result, folder, cd->user_data);

	/* If the hierarchy is now empty, then we must have just been
	 * created but then the add failed. So remove it again.
	 */
	if (exchange_hierarchy_is_empty (cd->hier))
		hierarchy_foreign_cleanup (cd->hier);

	if (folder)
		g_object_unref (folder);
	g_object_unref (cd->hier);
	while (cd->uris) {
		g_free (cd->uris->data);
		cd->uris = g_list_remove_link (cd->uris, cd->uris);
	}
	g_free (cd);
}

static const char *folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_HTTPMAIL_UNREAD_COUNT,
	E2K_PR_DAV_DISPLAY_NAME,
	PR_ACCESS
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static void
async_create_internal (E2kConnection *conn, struct create_data *cd)
{
	char *uri;

	uri = cd->uris->data;
	cd->uris = g_list_remove_link (cd->uris, cd->uris);

	e2k_connection_propfind (conn, uri, "0",
				 folder_props, n_folder_props,
				 got_folder, cd);
	g_free (uri);
}

static struct {
	const char *name, *prop;
} std_folders[] = {
	{ N_("Calendar"),	E2K_PR_STD_FOLDER_CALENDAR },
	{ N_("Contacts"),	E2K_PR_STD_FOLDER_CONTACTS },
	{ N_("Deleted Items"),	E2K_PR_STD_FOLDER_DELETED_ITEMS },
	{ N_("Drafts"),		E2K_PR_STD_FOLDER_DRAFTS },
	{ N_("Inbox"),		E2K_PR_STD_FOLDER_INBOX },
	{ N_("Journal"),	E2K_PR_STD_FOLDER_JOURNAL },
	{ N_("Notes"),		E2K_PR_STD_FOLDER_NOTES },
	{ N_("Outbox"),		E2K_PR_STD_FOLDER_OUTBOX },
	{ N_("Sent Items"),	E2K_PR_STD_FOLDER_SENT_ITEMS },
	{ N_("Tasks"),		E2K_PR_STD_FOLDER_TASKS }
};
const static int n_std_folders = sizeof (std_folders) / sizeof (std_folders[0]);

static void
async_create_folder (ExchangeHierarchy *hier, EFolder *parent,
		     const char *name, const char *type,
		     ExchangeAccountFolderCallback callback,
		     gpointer user_data)
{
	struct create_data *cd;
	char *literal_uri = NULL, *standard_uri = NULL;
	const char *home_uri;
	int i;

	/* For now, no nesting */
	if (parent != hier->toplevel || strchr (name + 1, '/')) {
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR,
			  NULL, user_data);
		return;
	}

	check_hide_private (hier);

	home_uri = e_folder_exchange_get_internal_uri (hier->toplevel);
	literal_uri = e2k_uri_concat (home_uri, name);
	if (exchange_account_get_folder (hier->account, literal_uri))
		goto already_exists;

	for (i = 0; i < n_std_folders; i++) {
		if (!g_ascii_strcasecmp (std_folders[i].name, name) ||
		    !g_utf8_collate (_(std_folders[i].name), name))
			break;
	}
	if (i < n_std_folders) {
		standard_uri = exchange_account_get_standard_uri_for (
			hier->account, home_uri, std_folders[i].prop);
		if (standard_uri) {
			if (!strcmp (literal_uri, standard_uri)) {
				g_free (standard_uri);
				standard_uri = NULL;
			} else if (exchange_account_get_folder (hier->account, standard_uri))
				goto already_exists;
		}
	}

	cd = g_new0 (struct create_data, 1);
	cd->hier = hier;
	g_object_ref (hier);
	cd->callback = callback;
	cd->user_data = user_data;

	cd->uris = g_list_prepend (NULL, literal_uri);
	if (standard_uri)
		cd->uris = g_list_prepend (cd->uris, standard_uri);

	async_create_internal (exchange_account_get_connection (hier->account), cd);
	return;

 already_exists:
	g_free (literal_uri);
	g_free (standard_uri);
	if (exchange_hierarchy_is_empty (hier))
		hierarchy_foreign_cleanup (hier);
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS,
		  NULL, user_data);
}

static void
async_remove_folder (ExchangeHierarchy *hier, EFolder *folder,
		     ExchangeAccountFolderCallback callback,
		     gpointer user_data)
{
	if (folder == hier->toplevel)
		hierarchy_foreign_cleanup (hier);
	else
		exchange_hierarchy_removed_folder (hier, folder);
	callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OK, NULL, user_data);

	if (folder != hier->toplevel && exchange_hierarchy_is_empty (hier))
		hierarchy_foreign_cleanup (hier);
}

struct scan_data {
	ExchangeHierarchy *hier;
	ExchangeAccountFolderCallback callback;
	gpointer user_data;
};

static void
got_subtree (E2kConnection *conn, SoupMessage *msg,
	     E2kResult *results, int nresults,
	     gpointer user_data)
{
	struct scan_data *sd = user_data;
	ExchangeAccountFolderResult result;
	GPtrArray *folders;
	EFolder *folder;
	E2kResult tmp;
	int i;

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		if (nresults == 0) {
			result = EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
			goto done;
		}

		/* If you have "folder visible" permission but nothing
		 * else, you'll be able to fetch properties, but not
		 * see anything in the folder. In that case, PR_ACCESS
		 * will be 0, and we remove the folder from the list.
		 */
		for (i = 0; i < nresults; i++) {
			if (!SOUP_ERROR_IS_SUCCESSFUL (results[i].status) ||
			    folder_is_unreadable (results[i].props)) {
				tmp = results[i];
				results[i] = results[nresults - 1];
				results[nresults - 1] = tmp;
				i--;
				nresults--;
			}
		}

		if (nresults == 0) {
			result = EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
			goto done;
		}
	}

	result = exchange_hierarchy_webdav_parse_folders (
		EXCHANGE_HIERARCHY_WEBDAV (sd->hier), msg,
		sd->hier->toplevel, results, nresults, &folders);
	if (result == EXCHANGE_ACCOUNT_FOLDER_OK) {
		for (i = 0; i < folders->len; i++) {
			folder = folders->pdata[i];
			exchange_hierarchy_new_folder (sd->hier, folder);
			g_object_unref (folder);
		}
		g_ptr_array_free (folders, TRUE);
	}

 done:
	if (sd->callback)
		sd->callback (sd->hier->account, result, sd->hier->toplevel, sd->user_data);

	g_object_unref (sd->hier);
	g_free (sd);
}

static void
add_href (ExchangeHierarchy *hier, EFolder *folder, gpointer hrefs)
{
	g_ptr_array_add (hrefs, g_strdup (e_folder_exchange_get_internal_uri (folder)));
}

static void
async_scan_subtree_real (ExchangeHierarchy *hier, EFolder *folder,
			 ExchangeAccountFolderCallback callback,
			 gpointer user_data)
{
	struct scan_data *sd;
	GPtrArray *hrefs;
	int i;

	if (folder != hier->toplevel)
		goto empty;

	check_hide_private (hier);

	hrefs = g_ptr_array_new ();
	exchange_hierarchy_webdav_offline_scan_subtree (hier, add_href, hrefs);
	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		goto empty;
	}

	sd = g_new (struct scan_data, 1);
	sd->hier = hier;
	g_object_ref (hier);
	sd->callback = callback;
	sd->user_data = user_data;

	e_folder_exchange_bpropfind (hier->toplevel,
				     (const char **)hrefs->pdata, hrefs->len, "0",
				     folder_props, n_folder_props,
				     got_subtree, sd);

	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_free (hrefs, TRUE);
	return;

 empty:
	if (callback)
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OK, folder, user_data);
}

static void
async_scan_subtree (ExchangeHierarchy *hier, EFolder *folder,
		    ExchangeAccountFolderCallback callback,
		    gpointer user_data)
{
	/* Since we call async_scan_subtree_real in add_to_storage
	 * below, this call must be redundant.
	 */
	if (callback)
		callback (hier->account, EXCHANGE_ACCOUNT_FOLDER_OK, folder, user_data);
}

static void
add_to_storage (ExchangeHierarchy *hier)
{
	EXCHANGE_HIERARCHY_CLASS (parent_class)->add_to_storage (hier);

	async_scan_subtree_real (hier, hier->toplevel, NULL, NULL);
}

/**
 * exchange_hierarchy_foreign_add_folder:
 * @hier: the hierarchy
 * @folder_name: the name of the folder to add
 *
 * Adds a new folder to @hier.
 **/
void
exchange_hierarchy_foreign_async_add_folder (ExchangeHierarchy *hier,
					     const char *folder_name,
					     ExchangeAccountFolderCallback callback,
					     gpointer user_data)
{
	async_create_folder (hier, hier->toplevel, folder_name, NULL,
			     callback, user_data);
}

static ExchangeHierarchy *
hierarchy_foreign_new (ExchangeAccount *account,
		       const char *hierarchy_name,
		       const char *physical_uri_prefix,
		       const char *internal_uri_prefix,
		       const char *owner_name,
		       const char *owner_email,
		       const char *source_uri)
{
	ExchangeHierarchyForeign *hfor;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);
	g_return_val_if_fail (hierarchy_name != NULL, NULL);
	g_return_val_if_fail (physical_uri_prefix != NULL, NULL);
	g_return_val_if_fail (internal_uri_prefix != NULL, NULL);
	g_return_val_if_fail (owner_name != NULL, NULL);
	g_return_val_if_fail (owner_email != NULL, NULL);
	g_return_val_if_fail (source_uri != NULL, NULL);

	hfor = g_object_new (EXCHANGE_TYPE_HIERARCHY_FOREIGN, NULL);

	exchange_hierarchy_webdav_construct (EXCHANGE_HIERARCHY_WEBDAV (hfor),
					     account,
					     EXCHANGE_HIERARCHY_FOREIGN,
					     hierarchy_name,
					     physical_uri_prefix,
					     internal_uri_prefix,
					     owner_name, owner_email,
					     source_uri,
					     FALSE, "folder", 2);

	return EXCHANGE_HIERARCHY (hfor);
}

/**
 * exchange_hierarchy_foreign_new:
 * @account: an #ExchangeAccount
 * @hierarchy_name: the name of the hierarchy
 * @physical_uri_prefix: the prefix of physical URIs in this hierarchy
 * @internal_uri_prefix: the prefix of internal (http) URIs in this hierarchy
 * @owner_name: display name of the owner of the hierarchy
 * @owner_email: email address of the owner of the hierarchy
 * @source_uri: evolution-mail source uri for the hierarchy
 *
 * Creates a new (initially empty) hierarchy for another user's
 * folders.
 *
 * Return value: the new hierarchy.
 **/
ExchangeHierarchy *
exchange_hierarchy_foreign_new (ExchangeAccount *account,
				const char *hierarchy_name,
				const char *physical_uri_prefix,
				const char *internal_uri_prefix,
				const char *owner_name,
				const char *owner_email,
				const char *source_uri)
{
	ExchangeHierarchy *hier;
	char *mf_path;
	GHashTable *props;
	xmlDoc *doc;

	hier = hierarchy_foreign_new (account, hierarchy_name,
				      physical_uri_prefix,
				      internal_uri_prefix,
				      owner_name, owner_email,
				      source_uri);

	props = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (props, "name", (char *)hierarchy_name);
	g_hash_table_insert (props, "physical_uri_prefix",
			     (char *)physical_uri_prefix);
	g_hash_table_insert (props, "internal_uri_prefix",
			     (char *)internal_uri_prefix);
	g_hash_table_insert (props, "owner_name", (char *)owner_name);
	g_hash_table_insert (props, "owner_email", (char *)owner_email);
	g_hash_table_insert (props, "source_uri", (char *)source_uri);

	mf_path = e_folder_exchange_get_storage_file (hier->toplevel, "hierarchy.xml");
	doc = e_xml_from_hash (props, E_XML_HASH_TYPE_PROPERTY,
			       "foreign-hierarchy");
	xmlSaveFile (mf_path, doc);
	g_hash_table_destroy (props);
	g_free (mf_path);

	return hier;
}

/**
 * exchange_hierarchy_foreign_new_from_dir:
 * @account: an #ExchangeAccount
 * @folder_path: pathname to a directory containing a hierarchy.xml file
 *
 * Recreates a new hierarchy from saved values.
 *
 * Return value: the new hierarchy.
 **/
ExchangeHierarchy *
exchange_hierarchy_foreign_new_from_dir (ExchangeAccount *account,
					 const char *folder_path)
{
	ExchangeHierarchy *hier;
	char *mf_path;
	GHashTable *props;
	xmlDoc *doc;

	mf_path = g_build_filename (folder_path, "hierarchy.xml", NULL);
	doc = xmlParseFile (mf_path);
	g_free (mf_path);
	if (!doc)
		return NULL;

	props = e_xml_to_hash (doc, E_XML_HASH_TYPE_PROPERTY);
	xmlFreeDoc (doc);

	hier = hierarchy_foreign_new (account,
				      g_hash_table_lookup (props, "name"),
				      g_hash_table_lookup (props, "physical_uri_prefix"),
				      g_hash_table_lookup (props, "internal_uri_prefix"),
				      g_hash_table_lookup (props, "owner_name"),
				      g_hash_table_lookup (props, "owner_email"),
				      g_hash_table_lookup (props, "source_uri"));

	e_xml_destroy_hash (props);
	return hier;
}
