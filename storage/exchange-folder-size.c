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

/* ExchangeFolderSize: Displaying the Exchange folder sizes
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "exchange-hierarchy.h"
#include "exchange-folder-size.h"

#include <e-util/e-dialog-utils.h>
#include <glade/glade-xml.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>

enum {
        COLUMN_NAME,
        COLUMN_SIZE,
        NUM_COLUMNS
};

typedef struct {
        char *folder_name;
        char *folder_size;
} folder_info;

typedef struct {
	char *self_dn;
	GtkWidget *parent;
	GtkListStore *model;
	GtkWidget *table;
} ExchangeFolderSize;

#if 0
static GHashTable *folder_size_table = NULL;

void 
update_folder_size_table (char *permanent_uri, char * folder_name, char *folder_size)
{
	folder_info *f_info;
	if (!folder_size_table)
		folder_size_table = g_hash_table_new (g_str_hash, g_str_equal);
	f_info = (folder_info *)malloc(sizeof(folder_info));
	f_info->folder_name = g_strdup (folder_name);
	f_info->folder_size = g_strdup (folder_size);
	g_hash_table_insert (folder_size_table, g_strdup (permanent_uri), f_info);	
}

#endif

static void
add_entry (gpointer key, gpointer value, gpointer data)
{
	//folder_info *f_info = value;
	char *folder_name = key;
	char *folder_size = value;
	GtkListStore *store = data;
	GtkTreeIter iter;

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	//			COLUMN_NAME, f_info->folder_name,
				COLUMN_NAME, folder_name,
	//			COULMN_SIZE, f_info->folder_size,
				COLUMN_SIZE, folder_size,
				-1);
}

void
exchange_folder_size (EFolder *folder, GtkWidget *parent)
{
	ExchangeFolderSize *fsize;
	ExchangeHierarchy *hier;
	GtkWidget *button;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GHashTable *folder_size_table;
	GladeXML *xml;
	GtkWidget *dialog, *table;
	GtkListStore *model;
	int i, response;
	
	g_return_if_fail (GTK_IS_WIDGET (parent));
	g_return_if_fail (E_IS_FOLDER (folder));

	hier = e_folder_exchange_get_hierarchy (folder);
	folder_size_table = exchange_hierarchy_get_folder_size_table (hier);

	xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-folder-tree.glade", NULL, NULL);
	g_return_if_fail (xml != NULL);
	dialog = glade_xml_get_widget (xml, "folder_tree");
 	table = glade_xml_get_widget (xml, "folder_treeview");

	e_dialog_set_transient_for (GTK_WINDOW (dialog), parent);
//	fsize->parent = parent;
	//g_object_weak_ref (G_OBJECT (parent), parent_destroyed, fsize);

	/* Set up the table */
	model = gtk_list_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
	if (folder_size_table)
		g_hash_table_foreach (folder_size_table, add_entry, model);

	column = gtk_tree_view_column_new_with_attributes (
		_("Folder Name"), gtk_cell_renderer_text_new (), "text", COLUMN_NAME, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (table),
				     column);
	column = gtk_tree_view_column_new_with_attributes (
		_("Folder Size"), gtk_cell_renderer_text_new (), "text", COLUMN_SIZE, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (table),
				     column);
	gtk_tree_view_set_model (GTK_TREE_VIEW (table),
				 GTK_TREE_MODEL (model));

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_object_unref (xml);
}
