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

/* exchange-delegates-delegators: routines for the "delegators" page
 * of the delegates config control
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-delegates-control.h"
#include "exchange-account.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "mail.xpm"

#include <string.h>

#include <e-util/e-dialog-utils.h>
#include <e-util/e-gtk-utils.h>
#include <e-util/e-xml-hash-utils.h>
#include <gconf/gconf-client.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrenderertoggle.h>
#include <gtk/gtkimage.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>

extern GConfClient *exchange_configdb;
extern ExchangeConfigListener *config_listener;

typedef struct {
	char *display_name, *dn, *email, *mailbox, *account_name;
	gboolean has_account;
} ExchangeDelegatesDelegator;

enum {
	EXCHANGE_DELEGATES_DELEGATORS_COLUMN_CHECKBOX,
	EXCHANGE_DELEGATES_DELEGATORS_COLUMN_NAME,

	EXCHANGE_DELEGATES_DELEGATORS_NUM_COLUMNS
};

static void
dsf_callback (ExchangeAccount *account, ExchangeAccountFolderResult result,
	      EFolder *folder, gpointer user_data)
{
	;
}

static void
add_account (ExchangeDelegatesControl *control,
	     ExchangeDelegatesDelegator *delegator)
{
	EAccount *account;

	account = e_account_new ();

	account->name = g_strdup_printf (_("%s's Delegate"),
					 delegator->display_name);
	account->enabled = FALSE;

	account->id->name          = g_strdup (delegator->display_name);
	account->id->address       = g_strdup (delegator->email);

	account->source->url       = g_strdup_printf ("exchange://%s@%s/",
						      delegator->mailbox,
						      control->account->exchange_server);
	account->transport->url    = g_strdup (control->my_account->transport->url);

	account->drafts_folder_uri = g_strdup (control->my_account->drafts_folder_uri);
	account->sent_folder_uri   = g_strdup (control->my_account->sent_folder_uri);

	e_list_append (E_LIST (config_listener), account);
	g_object_unref (account);
	e_account_list_save (E_ACCOUNT_LIST (config_listener));

	delegator->has_account = TRUE;

	exchange_account_async_discover_shared_folder (control->account,
						       delegator->email,
						       "calendar",
						       dsf_callback, NULL);
}

static void
remove_account (ExchangeDelegatesControl *control,
		ExchangeDelegatesDelegator *delegator)
{
	EIterator *iter;
	EAccount *account;

	for (iter = e_list_get_iterator (E_LIST (config_listener));
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		account = (EAccount *) e_iterator_get (iter);

		if (!strcmp (account->name, delegator->account_name)) {
			e_iterator_delete (iter);
			e_account_list_save (E_ACCOUNT_LIST (config_listener));
			break;
		}
	}
	g_object_unref (iter);
}

static ExchangeDelegatesDelegator *
delegator_new_from_xml (const char *xml)
{
	xmlDoc *doc;
	GHashTable *hash;
	ExchangeDelegatesDelegator *delegator;

	doc = e2k_parse_xml (xml, -1);
	hash = e_xml_to_hash (doc, E_XML_HASH_TYPE_PROPERTY);
	xmlFreeDoc (doc);

	delegator = g_new0 (ExchangeDelegatesDelegator, 1);
	delegator->display_name = g_hash_table_lookup (hash, "display_name");
	delegator->dn = g_hash_table_lookup (hash, "dn");
	delegator->email = g_hash_table_lookup (hash, "email");
	delegator->mailbox = g_hash_table_lookup (hash, "mailbox");

	g_hash_table_destroy (hash);
	return delegator;
}

static char *
delegator_to_xml (ExchangeDelegatesDelegator *delegator)
{
	xmlDoc *doc;
	GHashTable *hash;
	xmlChar *xbuf;
	int len;
	char *buf;

	hash = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (hash, "display_name", delegator->display_name);
	g_hash_table_insert (hash, "dn", delegator->dn);
	g_hash_table_insert (hash, "email", delegator->email);
	g_hash_table_insert (hash, "mailbox", delegator->mailbox);

	doc = e_xml_from_hash (hash, E_XML_HASH_TYPE_PROPERTY, "delegator");
	xmlDocDumpMemory (doc, &xbuf, &len);

	buf = g_strndup (xbuf, len);
	xmlFree (xbuf);
	return buf;
}

static void
free_delegator (ExchangeDelegatesDelegator *delegator)
{
	g_free (delegator->display_name);
	g_free (delegator->dn);
	g_free (delegator->email);
	g_free (delegator->mailbox);
	g_free (delegator->account_name);
	g_free (delegator);
}

static void
load_delegators (ExchangeDelegatesControl *control)
{
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;
	gboolean found;
	int i, d;
	GPtrArray *gc_DNs, *ex_delegators = NULL;
	ExchangeDelegatesDelegator *delegator;
	GtkTreeIter iter;
	EAccount *account;
	EIterator *iterator;
	GSList *cache, *l;

	gc = exchange_account_get_global_catalog (control->account);
	if (!gc)
		return;

	if (!control->self_dn) {
		exchange_delegates_control_get_self_dn (control);
		if (!control->self_dn)
			return;
	}

	/* Read the current delegators list from GC */
	status = e2k_global_catalog_lookup (
		gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_DN, control->self_dn,
		E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS, &entry);
	if (status != E2K_GLOBAL_CATALOG_OK)
		return;

	if (entry->delegators) {
		/* (Steal the array of delegator DNs) */
		gc_DNs = entry->delegators;
		entry->delegators = NULL;
	} else
		gc_DNs = g_ptr_array_new ();

	e2k_global_catalog_entry_free (gc, entry);

	control->delegators = g_ptr_array_new ();

	/* Read cached list of delegators */
	cache = gconf_client_get_list (exchange_configdb,
				       "/apps/evolution/exchange/delegators",
				       GCONF_VALUE_STRING, NULL);
	for (l = cache; l; l = l->next) {
		delegator = delegator_new_from_xml (l->data);
		if (!delegator->display_name || !delegator->dn || !delegator->email) {
			free_delegator (delegator);
			continue;
		}

		g_ptr_array_add (control->delegators, delegator);
	}

	/* Remove people who are no longer delegators from the
	 * delegators list, and make a list of delegator accounts
	 * to remove.
	 */
	for (d = 0; d < control->delegators->len; d++) {
		delegator = control->delegators->pdata[d];
		found = FALSE;
		for (i = 0; i < gc_DNs->len; i++) {
			if (!strcmp (delegator->dn, gc_DNs->pdata[i])) {
				g_free (gc_DNs->pdata[i]);
				g_ptr_array_remove_index_fast (gc_DNs, i);
				found = TRUE;
				break;
			}
		}

		if (!found) {
			if (!ex_delegators)
				ex_delegators = g_ptr_array_new ();
			g_ptr_array_add (ex_delegators, delegator);
			g_ptr_array_remove_index_fast (control->delegators, d);
			d--;
		}
	}

	/* Get info on remaining new delegators */
	for (d = 0; d < gc_DNs->len; d++) {
		status = e2k_global_catalog_lookup (
			gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_DN, gc_DNs->pdata[d],
			(E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
			 E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX), &entry);
		g_free (gc_DNs->pdata[d]);
		if (status != E2K_GLOBAL_CATALOG_OK)
			continue; /* FIXME? */

		delegator = g_new0 (ExchangeDelegatesDelegator, 1);
		delegator->dn = g_strdup (entry->dn);
		delegator->display_name = g_strdup (entry->display_name);
		delegator->email = g_strdup (entry->email);
		delegator->mailbox = g_strdup (entry->mailbox);
		g_ptr_array_add (control->delegators, delegator);

		e2k_global_catalog_entry_free (gc, entry);
	}
	g_ptr_array_free (gc_DNs, TRUE);

	/* Walk through the list of accounts, finding our own, and matching
	 * up the delegate accounts with the cached data.
	 */
	for (iterator = e_list_get_iterator (E_LIST (config_listener));
	     e_iterator_is_valid (iterator);
	     e_iterator_next (iterator)) {
		account = (EAccount *)e_iterator_get (iterator);

		if (!account->source || !account->source->url)
			continue;
		if (strncmp (account->source->url, "exchange://", 11) != 0)
			continue;

		if (account->enabled) {
			control->my_account = account;
			g_object_ref (control->my_account);
			continue;
		}

		for (d = 0; d < control->delegators->len; d++) {
			delegator = control->delegators->pdata[d];
			if (!strcmp (delegator->email, account->id->address)) {
				delegator->account_name =
					g_strdup (account->name);
				delegator->has_account = TRUE;
			}
		}
	}
	g_object_unref (iterator);

	/* Handle removed delegates */
	if (ex_delegators) {
		char *msg;

		if (ex_delegators->len > 1) {
			GString *str;

			str = g_string_new (_("You are no longer a delegate for the following users,\n"
					      "so the corresponding accounts will be removed.\n\n"));
			for (i = 0; i < ex_delegators->len; i++) {
				delegator = ex_delegators->pdata[i];
				g_string_append_printf (str, "    %s\n",
							delegator->display_name);
			}
			msg = str->str;
			g_string_free (str, FALSE);
		} else {
			delegator = ex_delegators->pdata[0];
			msg = g_strdup_printf (_("You are no longer a delegate for %s,\n"
						 "so that account will be removed."),
					       delegator->display_name);
		}
		e_notice (control->delegators_table, GTK_MESSAGE_WARNING, msg);
		g_free (msg);

		for (i = 0; i < ex_delegators->len; i++) {
			delegator = ex_delegators->pdata[i];
			remove_account (control, delegator);
		}

		for (i = 0; i < ex_delegators->len; i++)
			free_delegator (ex_delegators->pdata[i]);
		g_ptr_array_free (ex_delegators, TRUE);
	}

	/* Fill in the table */
	for (d = 0; d < control->delegators->len; d++) {
		delegator = control->delegators->pdata[d];

		gtk_list_store_append (control->delegators_model, &iter);
		gtk_list_store_set (control->delegators_model, &iter,
				    EXCHANGE_DELEGATES_DELEGATORS_COLUMN_CHECKBOX,
				    delegator->has_account,
				    EXCHANGE_DELEGATES_DELEGATORS_COLUMN_NAME,
				    delegator->display_name,
				    -1);
	}
}

static void
has_identity_toggled (GtkCellRendererToggle *cell,
		      char *path_str, gpointer data)
{
	ExchangeDelegatesControl *control = data;
	GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
	GtkTreeIter iter;
	int *indices, row;
	ExchangeDelegatesDelegator *delegator;

	gtk_tree_model_get_iter (GTK_TREE_MODEL (control->delegators_model),
				 &iter, path);
	indices = gtk_tree_path_get_indices (path);
	row = indices[0];
	gtk_tree_path_free (path);

	delegator = control->delegators->pdata[row];
	delegator->has_account = !delegator->has_account;

	gtk_list_store_set (control->delegators_model, &iter,
			    EXCHANGE_DELEGATES_DELEGATORS_COLUMN_CHECKBOX,
			    delegator->has_account,
			    -1);

	evolution_config_control_changed (EVOLUTION_CONFIG_CONTROL (control));
}

static void
switch_page (GtkNotebook *notebook, GtkNotebookPage *page,
	     guint page_num, gpointer user_data)
{
	ExchangeDelegatesControl *control = user_data;

	if (page_num != 1)
		return;

	g_signal_handlers_disconnect_by_func (notebook, switch_page, user_data);
	load_delegators (control);
}

void
exchange_delegates_delegators_construct (ExchangeDelegatesControl *control)
{
	GtkWidget *box, *notebook, *header;
	GdkPixbuf *mail;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	box = glade_xml_get_widget (control->xml, "delegators_vbox");

	/* Set up the table */
	control->delegators_model =
		gtk_list_store_new (EXCHANGE_DELEGATES_DELEGATORS_NUM_COLUMNS,
				    G_TYPE_BOOLEAN, G_TYPE_STRING);
	control->delegators_table = glade_xml_get_widget (control->xml, "delegators_table");

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (renderer, "toggled",
			  G_CALLBACK (has_identity_toggled), control);
	column = gtk_tree_view_column_new_with_attributes (
		_("Has Mail Identity"), renderer,
		"active", EXCHANGE_DELEGATES_DELEGATORS_COLUMN_CHECKBOX, NULL);
	mail = gdk_pixbuf_new_from_xpm_data ((const char **)mail_xpm);
	gdk_pixbuf_render_pixmap_and_mask (mail, &pixmap, &mask, 127);
	g_object_unref (mail);
	header = gtk_image_new_from_pixmap (pixmap, mask);
	gtk_widget_show (header);
	g_object_unref (pixmap);
	g_object_unref (mask);
	gtk_tree_view_column_set_widget (column, header);
	gtk_tree_view_append_column (GTK_TREE_VIEW (control->delegators_table),
				     column);

	column = gtk_tree_view_column_new_with_attributes (
		_("Delegator Name"), gtk_cell_renderer_text_new (),
		"text", EXCHANGE_DELEGATES_DELEGATORS_COLUMN_NAME, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (control->delegators_table),
				     column);

	gtk_tree_view_set_model (GTK_TREE_VIEW (control->delegators_table),
				 GTK_TREE_MODEL (control->delegators_model));

	/* Set up delayed loading of delegators */
	notebook = glade_xml_get_widget (control->xml, "delegates_notebook");
	e_signal_connect_while_alive (notebook, "switch_page",
				      G_CALLBACK (switch_page), control,
				      notebook);
}

void
exchange_delegates_delegators_apply (ExchangeDelegatesControl *control)
{
	ExchangeDelegatesDelegator *delegator;
	GSList *cache;
	int d;

	if (!control->delegators)
		return;

	/* Update accounts */
	for (d = 0; d < control->delegators->len; d++) {
		delegator = control->delegators->pdata[d];

		if (delegator->has_account && !delegator->account_name)
			add_account (control, delegator);
		else if (!delegator->has_account && delegator->account_name)
			remove_account (control, delegator);
	}

	/* Update cached delegators list */
	for (d = 0, cache = NULL; d < control->delegators->len; d++) {
		delegator = control->delegators->pdata[d];
		cache = g_slist_prepend (cache, delegator_to_xml (delegator));
	}
	gconf_client_set_list (exchange_configdb,
			       "/apps/evolution/exchange/delegators",
			       GCONF_VALUE_STRING, cache, NULL);

	while (cache) {
		g_free (cache->data);
		cache = g_slist_remove (cache, cache->data);
	}
}

void
exchange_delegates_delegators_destroy (ExchangeDelegatesControl *control)
{
	if (control->delegators_model) {
		g_object_unref (control->delegators_model);
		control->delegators_model = NULL;
	}

	if (control->my_account) {
		g_object_unref (control->my_account);
		control->my_account = NULL;
	}
}
