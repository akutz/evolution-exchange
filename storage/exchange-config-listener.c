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

/* ExchangeConfigListener: a class that listens to the config database
 * and handles creating the ExchangeAccount object and making sure that
 * default folders are updated as needed.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-config-listener.h"

#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-marshal.h"
#include "e2k-uri.h"
#include "mail-stub-listener.h"

#include <e-util/e-dialog-utils.h>
//#include <shell/e-folder-list.h>

#include <libedataserver/e-source.h>
#include <libedataserver/e-source-list.h>
#include <libedataserver/e-source-group.h>

#include <stdlib.h>
#include <string.h>

struct _ExchangeConfigListenerPrivate {
	GConfClient *gconf;
	guint idle_id;

	char *configured_uri, *configured_name;
	EAccount *configured_account;

	ExchangeAccount *exchange_account;
};


enum {
	EXCHANGE_ACCOUNT_CREATED,
	EXCHANGE_ACCOUNT_REMOVED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE E_TYPE_ACCOUNT_LIST
static EAccountListClass *parent_class = NULL;

static void dispose (GObject *object);
static void finalize (GObject *object);

static void account_added   (EAccountList *account_listener,
			     EAccount     *account);
static void account_changed (EAccountList *account_listener,
			     EAccount     *account);
static void account_removed (EAccountList *account_listener,
			     EAccount     *account);

static void
class_init (GObjectClass *object_class)
{
	EAccountListClass *e_account_list_class =
		E_ACCOUNT_LIST_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	e_account_list_class->account_added   = account_added;
	e_account_list_class->account_changed = account_changed;
	e_account_list_class->account_removed = account_removed;

	/* signals */
	signals[EXCHANGE_ACCOUNT_CREATED] =
		g_signal_new ("exchange_account_created",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeConfigListenerClass, exchange_account_created),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	signals[EXCHANGE_ACCOUNT_REMOVED] =
		g_signal_new ("exchange_account_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeConfigListenerClass, exchange_account_removed),
			      NULL, NULL,
			      e2k_marshal_NONE__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
init (GObject *object)
{
	ExchangeConfigListener *config_listener =
		EXCHANGE_CONFIG_LISTENER (object);

	config_listener->priv = g_new0 (ExchangeConfigListenerPrivate, 1);
}

static void
dispose (GObject *object)
{
	ExchangeConfigListener *config_listener =
		EXCHANGE_CONFIG_LISTENER (object);

	if (config_listener->priv->idle_id) {
		g_source_remove (config_listener->priv->idle_id);
		config_listener->priv->idle_id = 0;
	}

	if (config_listener->priv->gconf) {
		g_object_unref (config_listener->priv->gconf);
		config_listener->priv->gconf = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeConfigListener *config_listener =
		EXCHANGE_CONFIG_LISTENER (object);

	g_free (config_listener->priv->configured_name);
	g_free (config_listener->priv->configured_uri);
	g_free (config_listener->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_config_listener, ExchangeConfigListener, class_init, init, PARENT_TYPE)


#define EVOLUTION_URI_PREFIX     "evolution:/"
#define EVOLUTION_URI_PREFIX_LEN (sizeof (EVOLUTION_URI_PREFIX) - 1)

static void
remove_one_default_folder (GConfClient *gconf,
			   const char *evo_uri_path,
			   const char *exchange_uri_path,
			   const char *account_uri_prefix,
			   int account_uri_prefix_len)
{
	char *evo_uri;

	evo_uri = gconf_client_get_string (gconf, evo_uri_path, NULL);
	if (!evo_uri)
		return;
	if (!strncmp (evo_uri, account_uri_prefix, account_uri_prefix_len)) {
		gconf_client_unset (gconf, evo_uri_path, NULL);
		gconf_client_unset (gconf, exchange_uri_path, NULL);
	}
	g_free (evo_uri);
}

#if E_FOLDER_LIST
static void
remove_autocompletion_folders (GConfClient *gconf,
			       const char *account_uri_prefix,
			       int account_uri_prefix_len)
{
	EFolderListItem *list;
	GArray *to_be_deleted;
	char *xml;
	int i, len;

	xml = gconf_client_get_string (gconf, "/apps/evolution/addressbook/completion/uris", NULL);
	if (xml) {
		list = e_folder_list_parse_xml (xml);
		g_free (xml);
	} else
		list = NULL;

	if (!list) {
		g_warning ("Could not get list of autocompletion folders");
		return;
	}

	for (len = 0; list[len].uri; len++)
		;
	to_be_deleted = g_array_new (TRUE, TRUE, sizeof (EFolderListItem));

	for (i = 0; list[i].uri; i++) {
		if (!strncmp (list[i].uri, account_uri_prefix, account_uri_prefix_len)) {
			g_array_append_val (to_be_deleted, list[i]);
			memmove (&list[i], &list[i + 1],
				 (len - i) * sizeof (EFolderListItem));
			i--;
			len--;
		}
	}

	e_folder_list_free_items ((EFolderListItem *)to_be_deleted->data);
	g_array_free (to_be_deleted, FALSE);

	xml = e_folder_list_create_xml (list);
	gconf_client_set_string (gconf, "/apps/evolution/addressbook/completion/uris", xml, NULL);
	g_free (xml);
	e_folder_list_free_items (list);
}
#endif

static void
remove_defaults_for_account (ExchangeConfigListener *config_listener,
			     const char *account_name)
{
	char *account_uri_prefix;
	int account_uri_prefix_len;

	account_uri_prefix = g_strdup_printf ("evolution:/%s/", account_name);
	account_uri_prefix_len = strlen (account_uri_prefix);

	remove_one_default_folder (config_listener->priv->gconf,
				   "/apps/evolution/shell/default_folders/mail_path",
				   "/apps/evolution/shell/default_folders/mail_uri",
				   account_uri_prefix, account_uri_prefix_len);
	remove_one_default_folder (config_listener->priv->gconf,
				   "/apps/evolution/shell/default_folders/contacts_path",
				   "/apps/evolution/shell/default_folders/contacts_uri",
				   account_uri_prefix, account_uri_prefix_len);
	remove_one_default_folder (config_listener->priv->gconf,
				   "/apps/evolution/shell/default_folders/calendar_path",
				   "/apps/evolution/shell/default_folders/calendar_uri",
				   account_uri_prefix, account_uri_prefix_len);
	remove_one_default_folder (config_listener->priv->gconf,
				   "/apps/evolution/shell/default_folders/tasks_path",
				   "/apps/evolution/shell/default_folders/tasks_uri",
				   account_uri_prefix, account_uri_prefix_len);
#if E_FOLDER_LIST	
	remove_autocompletion_folders (config_listener->priv->gconf,
				       account_uri_prefix,
				       account_uri_prefix_len);
#endif

	gconf_client_unset (config_listener->priv->gconf,
			    "/apps/evolution/exchange/configured_account",
			    NULL);

	g_free (account_uri_prefix);
}

static EFolder *
standard_folder (ExchangeAccount *account, const char *folder_type)
{
	const char *uri;

	uri = exchange_account_get_standard_uri (account, folder_type);
	if (!uri)
		return NULL;
	return exchange_account_get_folder (account, uri);
}

static gboolean
add_one_default_folder (GConfClient *gconf, ExchangeAccount *account,
			char *folder_type, char *default_name)
{
	char *dbkey, *evolution_uri;
	EFolder *folder;

	folder = standard_folder (account, folder_type);
	if (!folder)
		return FALSE;

	evolution_uri = g_strdup_printf (EVOLUTION_URI_PREFIX "%s%s",
					 account->account_name,
					 e_folder_exchange_get_path (folder));
	dbkey = g_strdup_printf ("/apps/evolution/shell/default_folders/%s_path", default_name);
	gconf_client_set_string (gconf, dbkey, evolution_uri, NULL);
	g_free (dbkey);
	g_free (evolution_uri);

	dbkey = g_strdup_printf ("/apps/evolution/shell/default_folders/%s_uri", default_name);
	gconf_client_set_string (gconf, dbkey, e_folder_get_physical_uri (folder), NULL);
	g_free (dbkey);

	return TRUE;
}

#if E_FOLDER_LIST
static void
set_folder_list_item (EFolderListItem *item, ExchangeAccount *account,
		      EFolder *folder)
{
	item->uri = g_strdup_printf ("evolution:/%s%s",
				     account->account_name,
				     e_folder_exchange_get_path (folder));
	item->physical_uri = g_strdup (e_folder_get_physical_uri (folder));
	item->display_name = g_strdup (e_folder_get_name (folder));
}

static void
add_autocompletion_folders (GConfClient *gconf, ExchangeAccount *account)
{
	EFolderListItem *list = NULL;
	EFolder *folder;
	char *xml;
	int len;

	xml = gconf_client_get_string (gconf, "/apps/evolution/addressbook/completion/uris", NULL);
	if (xml) {
		list = e_folder_list_parse_xml (xml);
		g_free (xml);
	}
	if (list) {
		for (len = 0; list[len].uri; len++)
			;
		list = g_renew (EFolderListItem, list, len + 3);
	} else {
		len = 0;
		list = g_new (EFolderListItem, 3);
	}

	folder = standard_folder (account, "contacts");
	if (folder)
		set_folder_list_item (&list[len++], account, folder);

	folder = exchange_account_get_folder (account, "/gal");
	if (folder) {
		set_folder_list_item (&list[len++], account, folder);
		gconf_client_set_string (gconf, "/apps/evolution/addressbook/select_names/last_used_uri",
					 e_folder_get_physical_uri (folder), NULL);
	}

	list[len].uri = list[len].physical_uri = list[len].display_name = NULL;

	xml = e_folder_list_create_xml (list);
	gconf_client_set_string (gconf, "/apps/evolution/addressbook/completion/uris", xml, NULL);
	g_free (xml);
	e_folder_list_free_items (list);
}
#endif

static void
set_special_mail_folder (ExchangeAccount *account, const char *folder_type,
			 char **physical_uri)
{
	EFolder *folder;

	folder = standard_folder (account, folder_type);
	if (!folder)
		return;

	g_free (*physical_uri);
	*physical_uri = g_strdup (e_folder_get_physical_uri (folder));
}

static void
add_defaults_for_account (ExchangeConfigListener *config_listener,
			  E2kContext *ctx,
			  ExchangeAccount *account)
{
	EAccount *eaccount;
	char *configured_account;
	int defaulted;

	/* Look up whether we've defaulted folders for this account before */
	configured_account = gconf_client_get_string (
		config_listener->priv->gconf,
		"/apps/evolution/exchange/configured_account", NULL);
	if (configured_account &&
	    !strcmp (configured_account, account->account_name)) {
		g_free (configured_account);
		return;
	}
	g_free (configured_account);

	defaulted = 0;
	if (add_one_default_folder (config_listener->priv->gconf,
				    account, "calendar", "calendar"))
		defaulted++;
	if (add_one_default_folder (config_listener->priv->gconf,
				    account, "contacts", "contacts"))
		defaulted++;
	if (add_one_default_folder (config_listener->priv->gconf,
				    account, "tasks", "tasks"))
		defaulted++;
	if (add_one_default_folder (config_listener->priv->gconf,
				    account, "inbox", "mail"))
		defaulted++;

	/* Mark them as defaulted for this account so we don't try it again */
	gconf_client_set_string (config_listener->priv->gconf,
				 "/apps/evolution/exchange/configured_account",
				 account->account_name, NULL);

#if LDEAD
	add_autocompletion_folders (config_listener->priv->gconf, account);
#endif

	eaccount = config_listener->priv->configured_account;
	set_special_mail_folder (account, "drafts",
				 &eaccount->drafts_folder_uri);
	set_special_mail_folder (account, "sentitems",
				 &eaccount->sent_folder_uri);
	e_account_list_change (E_ACCOUNT_LIST (config_listener), eaccount);
	e_account_list_save (E_ACCOUNT_LIST (config_listener));

	if (defaulted != 4) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not set up default folders to point to your Exchange account.\n"
			    "You may want to update them by hand in the \"Folder Settings\"\n"
			    "section of the Settings dialog."));
	}
}


static gboolean
is_active_exchange_account (EAccount *account)
{
	if (!account->enabled)
		return FALSE;
	if (!account->source || !account->source->url)
		return FALSE;
	return (strncmp (account->source->url, "exchange://", 11) == 0);
}

void
add_esource (ExchangeAccount *account, 
	     char *conf_key, 
	     const char *folder_name, 
	     const char *physical_uri,
	     ESourceList **source_list)
{
	ESource *source;
	ESourceGroup *source_group;
	char *relative_uri;

	relative_uri = g_strdup (physical_uri + strlen ("exchange://"));

        if ((source_group = e_source_list_peek_group_by_name (*source_list, 
					account->account_name)) == NULL){
		source_group = e_source_group_new (account->account_name, 
						   "exchange://");
		if (!e_source_list_add_group (*source_list, source_group, -1)) {
			g_object_unref (source_group);
        		g_free(relative_uri);
			return;
		}
		source = e_source_new (folder_name, relative_uri);
		e_source_group_add_source (source_group, source, -1);

		g_object_unref (source);
		g_object_unref (source_group);
	}
	else {
                /*group already exists*/
		/* FIXME - is this check needed ?*/
		if((source = e_source_group_peek_source_by_name (source_group, 
							folder_name)) == NULL){
        		source = e_source_new (folder_name, relative_uri);
			e_source_group_add_source (source_group, source, -1);
			g_object_unref (source);
		}
	}

        g_free(relative_uri);
}

static void
add_sources (ExchangeAccount *account)
{
	char *conf_key_cal="/apps/evolution/calendar/sources";
	char *conf_key_tasks="/apps/evolution/tasks/sources";
	char *conf_key_contacts="/apps/evolution/addressbook/sources";
	const char *folder_name, *physical_uri;
	GPtrArray *exchange_folders;
	EFolder *folder;
	ESourceList *cal_source_list, *task_source_list, *cont_source_list;
	int i;

	exchange_folders = exchange_account_get_folders (account);                                                                                 
        if (exchange_folders) {
		cal_source_list = e_source_list_new_for_gconf ( 
					gconf_client_get_default (), 
					conf_key_cal);
		task_source_list = e_source_list_new_for_gconf ( 
					gconf_client_get_default (), 
					conf_key_tasks);
		cont_source_list = e_source_list_new_for_gconf (
					gconf_client_get_default (), 
					conf_key_contacts);

		for (i = 0; i < exchange_folders->len; i++) {
			folder = exchange_folders->pdata[i];
			if (!(strcmp (e_folder_get_type_string (folder), 
				      "calendar"))){
				folder_name = e_folder_get_name (folder);
				physical_uri = e_folder_get_physical_uri (folder);
        			add_esource (account, 
					     conf_key_cal, 
					     folder_name, 
					     physical_uri,
					     &cal_source_list);
				continue;
			}
			if (!(strcmp (e_folder_get_type_string (folder), 
				      "tasks"))){
				folder_name = e_folder_get_name (folder);
				physical_uri = e_folder_get_physical_uri (folder);
        			add_esource (account, 
					     conf_key_tasks, 
					     folder_name, 
					     physical_uri,
					     &task_source_list);
				continue;
			}
			if (!(strcmp (e_folder_get_type_string (folder), 
				      "contacts"))){
				folder_name = e_folder_get_name (folder);
				physical_uri = e_folder_get_physical_uri (folder);
        			add_esource (account, 
					     conf_key_contacts, 
					     folder_name, 
					     physical_uri,
					     &cont_source_list);
				continue;
			}
			continue;
		}
		e_source_list_sync (cal_source_list, NULL);
		g_object_unref (cal_source_list);
		e_source_list_sync (task_source_list, NULL);
		g_object_unref (task_source_list);
		e_source_list_sync (cont_source_list, NULL);
		g_object_unref (cont_source_list);
		g_ptr_array_free (exchange_folders, TRUE);
        }
}

void 
remove_esource (ExchangeAccount *account, 
		char *conf_key, 
		const char *physical_uri,
		ESourceList **source_list,
		gboolean is_account)
{
	ESourceGroup *group;
	ESource *source;
	GSList *groups;
	GSList *sources;
	gboolean found_group;
	char *relative_uri;
	const char *source_uid;

	relative_uri = g_strdup (physical_uri + strlen ("exchange://"));
	groups = e_source_list_peek_groups (*source_list);
	found_group = FALSE;

	for ( ; groups != NULL && !found_group; groups = g_slist_next (groups)) {
		group = E_SOURCE_GROUP (groups->data);

		if (strcmp (e_source_group_peek_name (group), account->account_name) == 0
                    &&
                   strcmp (e_source_group_peek_base_uri (group), "exchange://" ) == 0) {

			sources = e_source_group_peek_sources (group);

			for( ; sources != NULL; sources = g_slist_next (sources)) {

				source = E_SOURCE (sources->data);

				if (strcmp (e_source_peek_relative_uri (source), 
					    relative_uri) == 0) {
					if (is_account) {
						/* Account Deleted - Remove the group */
						e_source_list_remove_group (
								*source_list, 
								group);
					}
					else {
						/* Folder Deleted - Remove only the source */
						source_uid = 
						e_source_peek_uid (source);
						e_source_group_remove_source_by_uid (
									group, 
									source_uid);
						
					}
                                        found_group = TRUE;
                                        break;
                                }
                        }
                }
        }
        g_free(relative_uri);
}

static void
remove_sources(ExchangeAccount *account)
{
	char *conf_key_cal="/apps/evolution/calendar/sources";
	char *conf_key_tasks="/apps/evolution/tasks/sources";
	char *conf_key_contacts="/apps/evolution/addressbook/sources";
	const char *physical_uri;
	EFolder *folder; 
	ESourceList *cal_source_list, *task_source_list, *cont_source_list;
	GPtrArray *exchange_folders;
	int i;

	exchange_folders = exchange_account_get_folders (account);
	if (exchange_folders) {
		cal_source_list = e_source_list_new_for_gconf ( 
					gconf_client_get_default (), 
					conf_key_cal);
		task_source_list = e_source_list_new_for_gconf ( 
					gconf_client_get_default (), 
					conf_key_tasks);
		cont_source_list = e_source_list_new_for_gconf (
					gconf_client_get_default (), 
					conf_key_contacts);

		for (i = 0; i < exchange_folders->len; i++) {
			folder = exchange_folders->pdata[i];
			if (!(strcmp (e_folder_get_type_string (folder), 
				      "calendar"))){
				physical_uri = e_folder_get_physical_uri (folder);
        			remove_esource (account, 
						conf_key_cal, 
						physical_uri,
						&cal_source_list,
						TRUE);
				continue;
			}
			if (!(strcmp (e_folder_get_type_string (folder), "tasks"))){
				physical_uri = e_folder_get_physical_uri (folder);
        			remove_esource (account, 
						conf_key_tasks, 
						physical_uri,
						&task_source_list,
						TRUE);
				continue;
			}
			if (!(strcmp (e_folder_get_type_string (folder), "contacts"))){
				physical_uri = e_folder_get_physical_uri (folder);
        			remove_esource (account, 
						conf_key_contacts, 
						physical_uri,
						&cont_source_list,
						TRUE);
				continue;
			}
			continue;
		}
		e_source_list_sync (cal_source_list, NULL);
		g_object_unref (cal_source_list);
		e_source_list_sync (task_source_list, NULL);
		g_object_unref (task_source_list);
		e_source_list_sync (cont_source_list, NULL);
		g_object_unref (cont_source_list);
        	g_ptr_array_free (exchange_folders, TRUE);
        }
}

static void
account_added (EAccountList *account_list, EAccount *account)
{
	ExchangeConfigListener *config_listener;
	ExchangeAccount *exchange_account;

	if (!is_active_exchange_account (account))
		return;

	config_listener = EXCHANGE_CONFIG_LISTENER (account_list);
	if (config_listener->priv->configured_account) {
		/* Multiple accounts configured. */
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("You may only configure a single Exchange account"));
		return;
	}

	/* New account! Yippee! */
	exchange_account = exchange_account_new (account_list, account);
	if (!exchange_account) {
		g_warning ("Could not parse exchange uri '%s'",
			   account->source->url);
		return;
	}

	config_listener->priv->exchange_account = exchange_account;
	config_listener->priv->configured_account = account;

	g_free (config_listener->priv->configured_uri);
	config_listener->priv->configured_uri = g_strdup (account->source->url);
	g_free (config_listener->priv->configured_name);
	config_listener->priv->configured_name = g_strdup (account->name);

	if (account == e_account_list_get_default (account_list)) {
		g_signal_connect_swapped (config_listener->priv->exchange_account,
					  "connected",
					  G_CALLBACK (add_defaults_for_account),
					  config_listener);
	}

	g_signal_emit (config_listener, signals[EXCHANGE_ACCOUNT_CREATED], 0,
		       exchange_account);
	exchange_account_connect (exchange_account);

	add_sources (exchange_account);
}

struct account_update_data {
	EAccountList *account_list;
	EAccount *account;
};

static void
configured_account_destroyed (gpointer user_data, GObject *where_account_was)
{
	struct account_update_data *aud = user_data;

	if (!EXCHANGE_CONFIG_LISTENER (aud->account_list)->priv->configured_account)
		account_added (aud->account_list, aud->account);

	g_object_unref (aud->account_list);
	g_object_unref (aud->account);
	g_free (aud);
}

static void
account_changed (EAccountList *account_list, EAccount *account)
{
	ExchangeConfigListener *config_listener =
		EXCHANGE_CONFIG_LISTENER (account_list);

	if (account != config_listener->priv->configured_account) {
		if (!is_active_exchange_account (account))
			return;

		/* The user has converted an existing non-Exchange
		 * account to an Exchange account, so treat it like an
		 * add.
		 */
		account_added (account_list, account);
		return;
	} else if (!is_active_exchange_account (account)) {
		/* The user has disabled the Exchange account or
		 * converted it to non-Exchange, so treat it like a
		 * remove.
		 */
		account_removed (account_list, account);
		return;
	}

	if (!strcmp (config_listener->priv->configured_uri, account->source->url) &&
	    !strcmp (config_listener->priv->configured_name, account->name)) {
		/* The user changed something we don't care about. */
		return;
	}

	/* OK, so he modified the active account in a way we care
	 * about. If the user hasn't connected yet, we're still ok.
	 */
	if (!exchange_account_get_context (config_listener->priv->exchange_account)) {
		/* Good. Remove the current account, and wait for it
		 * to actually go away (which may not happen immediately
		 * since there may be a function higher up on the stack
		 * still holding a ref on it). Then create the new one.
		 * (We have to wait for it to go away because the new
		 * storage probably still has the same name as the old
		 * one, so trying to create it before the old one is
		 * removed would fail.)
		 */
		struct account_update_data *aud;

		aud = g_new (struct account_update_data, 1);
		aud->account = g_object_ref (account);
		aud->account_list = g_object_ref (account_list);
		g_object_weak_ref (G_OBJECT (config_listener->priv->exchange_account), configured_account_destroyed, aud);

		account_removed (account_list, account);
		return;
	}

	/* Nope. Let the user know we're ignoring him. */
	e_notice (NULL, GTK_MESSAGE_WARNING,
		  _("Changes to Exchange account configuration will "
		    "take\nplace after you quit and restart Evolution."));

	/* But note the new URI so if he changes something else, we
	 * only warn again if he changes again.
	 */
	g_free (config_listener->priv->configured_uri);
	config_listener->priv->configured_uri = g_strdup (account->source->url);
	g_free (config_listener->priv->configured_name);
	config_listener->priv->configured_name = g_strdup (account->name);
}

static void
account_removed (EAccountList *account_list, EAccount *account)
{
	ExchangeConfigListener *config_listener =
		EXCHANGE_CONFIG_LISTENER (account_list);
	ExchangeConfigListenerPrivate *priv = config_listener->priv;

	if (account != priv->configured_account)
		return;

	remove_defaults_for_account (config_listener, account->name);

	/* Remove ESources */
	remove_sources(priv->exchange_account);

	if (!exchange_account_get_context (priv->exchange_account)) {
		/* The account isn't connected yet, so we can destroy
		 * it without problems.
		 */
		g_signal_emit (config_listener,
			       signals[EXCHANGE_ACCOUNT_REMOVED], 0,
			       priv->exchange_account);

		g_object_unref (priv->exchange_account);
		priv->exchange_account = NULL;

		priv->configured_account = NULL;
		g_free (priv->configured_uri);
		priv->configured_uri = NULL;
		g_free (priv->configured_name);
		priv->configured_name = NULL;
	} else {
		/* The account is in use. We can't remove it. */
		e_notice (NULL, GTK_MESSAGE_INFO,
			  _("The Exchange account will be removed when you quit Evolution"));
	}
}

static gboolean
idle_construct (gpointer data)
{
	ExchangeConfigListener *config_listener = data;

	config_listener->priv->idle_id = 0;
	e_account_list_construct (E_ACCOUNT_LIST (config_listener),
				  config_listener->priv->gconf);
	return FALSE;
}

/**
 * exchange_config_listener_new:
 *
 * This creates and returns a new #ExchangeConfigListener, which
 * monitors GConf and creates and (theoretically) destroys accounts
 * accordingly. It will emit an %account_created signal when a new
 * account is created (or shortly after the listener itself is created
 * if an account already exists).
 *
 * Due to various constraints, the user is currently limited to a
 * single account, and it is not possible to destroy an existing
 * account. Thus, the %account_created signal will never be emitted
 * more than once currently.
 *
 * Return value: the new config listener.
 **/
ExchangeConfigListener *
exchange_config_listener_new (void)
{
	ExchangeConfigListener *config_listener;

	config_listener = g_object_new (EXCHANGE_TYPE_CONFIG_LISTENER, NULL);
	config_listener->priv->gconf = gconf_client_get_default ();

	config_listener->priv->idle_id =
		g_idle_add (idle_construct, config_listener);

	return config_listener;
}

GSList *
exchange_config_listener_get_accounts (ExchangeConfigListener *config_listener)
{
	g_return_val_if_fail (EXCHANGE_IS_CONFIG_LISTENER (config_listener), NULL);

	if (config_listener->priv->exchange_account)
		return g_slist_append (NULL, config_listener->priv->exchange_account);
	else
		return NULL;
}

