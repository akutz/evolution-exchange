/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

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

/* ExchangeFolderSize: Display the folder tree with the folder sizes */

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

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

typedef struct {
        char *folder_name;
        char *folder_size;
} folder_info;


struct _ExchangeFolderSizePrivate {
	
	GHashTable *table;
};

enum {
        COLUMN_NAME,
        COLUMN_SIZE,
        NUM_COLUMNS
};

static void
finalize (GObject *object)
{
	ExchangeFolderSize *fsize = EXCHANGE_FOLDER_SIZE (object);

	g_hash_table_destroy (fsize->priv->table);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* override virtual methods */
	object_class->dispose = dispose;
	object_class->finalize = finalize;

}

static void
init (GObject *object)
{
	ExchangeFolderSize *fsize = EXCHANGE_FOLDER_SIZE (object);

	fsize->priv = g_new0 (ExchangeFolderSizePrivate, 1);
	fsize->priv->table = g_hash_table_new (g_str_hash, g_str_equal);
}

E2K_MAKE_TYPE (exchange_folder_size, ExchangeFolderSize, class_init, init, PARENT_TYPE)

/**
 * exchange_folder_size_new:
 * @display_name: the delegate's (UTF8) display name
 *
 * Return value: a foldersize object with the table initialized
 **/
ExchangeFolderSize *
exchange_folder_size_new (void)
{
	ExchangeFolderSize *fsize;

	fsize = g_object_new (EXCHANGE_TYPE_FOLDER_SIZE, NULL);

	return fsize;
}

void
exchange_folder_size_update (ExchangeFolderSize *fsize, 
				const char *permanent_uri,
				const char *folder_name,
				const char *folder_size)
{
	folder_info *f_info;
	ExchangeFolderSizePrivate *priv;
	GHashTable *folder_size_table = priv->table;

	/*FIXME : Check if value is already present */

        f_info = (folder_info *)malloc(sizeof(folder_info));
        f_info->folder_name = g_strdup (folder_name);
        f_info->folder_size = g_strdup (folder_size);
        g_hash_table_insert (folder_size_table, g_strdup (permanent_uri), f_info); 
}

static void
add_entry (gpointer key, gpointer value, gpointer data)
{
        folder_info *f_info = value;
        GtkListStore *store = data;
        GtkTreeIter iter;

        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                              COLUMN_NAME, f_info->folder_name,
                              COLUMN_SIZE, f_info->folder_size,
                              -1);
}

void
exchange_folder_size_display (EFolder *folder, GtkWidget *parent)
{
        ExchangeFolderSizePrivate *priv;
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
        fsize = exchange_hierarchy_get_folder_size (hier);
	priv = fsize->priv;
	folder_size_table = priv->table;

        xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-folder-tree.glade", NULL, NULL);
        g_return_if_fail (xml != NULL);
        dialog = glade_xml_get_widget (xml, "folder_tree");
        table = glade_xml_get_widget (xml, "folder_treeview");

        e_dialog_set_transient_for (GTK_WINDOW (dialog), parent);
//      fsize->parent = parent;
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
