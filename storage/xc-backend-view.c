/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
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
#include "config.h"
#endif

#include "xc-backend-view.h"
#include "xc-commands.h"
#include <exchange-account.h>
#include <exchange-config-listener.h>
#include <exchange-storage.h>

#include <bonobo/bonobo-exception.h>
#include <gal/widgets/e-popup-menu.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkdrawingarea.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>
#include <shell/e-user-creatable-items-handler.h>

#include "e-storage-set.h"
#include "e-storage-set-view.h"

#define d(x)

#define PARENT_TYPE G_TYPE_OBJECT
static GObjectClass *parent_class = NULL;

struct XCBackendViewPrivate {
	ExchangeConfigListener *config_listener;
	EFolderTypeRegistry *registry;

	GtkTreeView *sidebar;
	GtkListStore *sidebar_model;
	BonoboControl *sidebar_control;

	GtkWidget *statusbar;
	BonoboControl *statusbar_control;

	GtkNotebook *body;
	BonoboControl *view_control;

	EUserCreatableItemsHandler *items_handler;
};

enum {
	XC_BACKEND_VIEW_ACCOUNT_NAME_COLUMN,
	XC_BACKEND_VIEW_ACCOUNT_COLUMN,
	XC_BACKEND_VIEW_STORAGE_SET_VIEW_COLUMN,

	XC_BACKEND_VIEW_NUM_COLUMNS
};

static void
dispose (GObject *object)
{
	XCBackendViewPrivate *priv = XC_BACKEND_VIEW (object)->priv;

	if (priv->config_listener) {
		g_object_unref (priv->config_listener);
		priv->config_listener = NULL;
	}

	if (priv->registry) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
folder_context_menu (EStorageSetView *storage_set_view, GdkEvent *event,
		     EFolder *folder, XCBackendView *view)
{
	xc_commands_context_menu (storage_set_view, folder, event);
}

static void
activated (BonoboControl *control, gboolean active, gpointer user_data)
{
	XCBackendView *view = user_data;
	BonoboUIComponent *uic;

	uic = bonobo_control_get_ui_component (control);
	g_return_if_fail (uic != NULL);

	if (active) {
		xc_commands_activate (view);
		e_user_creatable_items_handler_activate (view->priv->items_handler, uic);
	} else
		xc_commands_deactivate (view);
}

static void
class_init (XCBackendViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = dispose;
	object_class->finalize = finalize;
}

static void
init (XCBackendView *backend)
{
	backend->priv = g_new0 (XCBackendViewPrivate, 1);
}

E2K_MAKE_TYPE (xc_backend_view, XCBackendView, class_init, init, PARENT_TYPE)


static GtkWidget *
account_view_new (ExchangeAccount *account, EFolderTypeRegistry *registry)
{
	GtkWidget *storage_set_view, *scrolled;
	EStorageSet *storage_set;
	EStorage *storage;

	storage_set = e_storage_set_new (registry);
	storage = exchange_storage_new (account);
	e_storage_set_add_storage (storage_set, storage);
	g_object_unref (storage);

	storage_set_view = e_storage_set_create_new_view (storage_set);
	g_signal_connect (storage_set_view, "folder_context_menu",
			  G_CALLBACK (folder_context_menu), NULL);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
					     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled), storage_set_view);
	gtk_widget_show_all (scrolled);

	return scrolled;
}

static EStorageSetView *
account_view_get_storage_set_view (GtkWidget *page)
{
	if (!GTK_IS_SCROLLED_WINDOW (page))
		return NULL;

	page = gtk_bin_get_child (GTK_BIN (page));

	if (E_IS_STORAGE_SET_VIEW (page))
		return (EStorageSetView *)page;
	else
		return NULL;
}

static void
account_selection_changed (GtkTreeSelection *selection, gpointer user_data)
{
	XCBackendView *view = user_data;
	XCBackendViewPrivate *priv = view->priv;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *page;
	int page_num;

	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (priv->sidebar), &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    XC_BACKEND_VIEW_STORAGE_SET_VIEW_COLUMN, &page,
				    -1);
		page_num = gtk_notebook_page_num (priv->body, page);
	} else
		page_num = 0;

	gtk_notebook_set_current_page (priv->body, page_num);
}

static void
exchange_account_created (ExchangeConfigListener *config_listener,
			  ExchangeAccount *account, gpointer user_data)
{
	XCBackendView *view = user_data;
	XCBackendViewPrivate *priv = view->priv;
	GtkTreeIter iter;
	GtkWidget *page;

	page = account_view_new (account, priv->registry);
	gtk_notebook_append_page (priv->body, page, NULL);

	gtk_list_store_append (priv->sidebar_model, &iter);
	gtk_list_store_set (priv->sidebar_model, &iter,
			    XC_BACKEND_VIEW_ACCOUNT_NAME_COLUMN, account->account_name,
			    XC_BACKEND_VIEW_ACCOUNT_COLUMN, account,
			    XC_BACKEND_VIEW_STORAGE_SET_VIEW_COLUMN, page,
			    -1);
}

static void
exchange_account_removed (ExchangeConfigListener *config_listener,
			  ExchangeAccount *account, gpointer user_data) 
{
	XCBackendView *view = user_data;
	XCBackendViewPrivate *priv = view->priv;
	GtkTreeIter iter;
	gboolean valid;
	ExchangeAccount *cmp_account;
	GtkWidget *page;

	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->sidebar_model), &iter);
	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (priv->sidebar_model), &iter,
				    XC_BACKEND_VIEW_ACCOUNT_COLUMN, &cmp_account,
				    XC_BACKEND_VIEW_STORAGE_SET_VIEW_COLUMN, &page,
				    -1);
		if (account == cmp_account)
			break;

		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->sidebar_model), &iter);
	}
	g_return_if_fail (valid);

	gtk_list_store_remove (priv->sidebar_model, &iter);
	gtk_notebook_remove_page (priv->body, gtk_notebook_page_num (priv->body, page));
}

XCBackendView *
xc_backend_view_new (ExchangeConfigListener *config_listener,
		     EFolderTypeRegistry    *registry)
{
	XCBackendView *view;
	XCBackendViewPrivate *priv;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GSList *accounts, *acc;
	GtkWidget *widget, *scrolled;

	view = g_object_new (XC_TYPE_BACKEND_VIEW, NULL);
	priv = view->priv;

	priv->config_listener = g_object_ref (config_listener);
	g_signal_connect (config_listener, "exchange_account_created",
			  G_CALLBACK (exchange_account_created), view);
	g_signal_connect (config_listener, "exchange_account_removed",
			  G_CALLBACK (exchange_account_removed), view);

	priv->registry = g_object_ref (registry);

	widget = gtk_notebook_new ();
	gtk_widget_show (widget);
	priv->body = GTK_NOTEBOOK (widget);
	gtk_notebook_set_show_tabs (priv->body, FALSE);
	gtk_notebook_set_show_border (priv->body, FALSE);
	gtk_notebook_append_page (priv->body, gtk_drawing_area_new (), NULL);
	priv->view_control = bonobo_control_new (widget);
	g_signal_connect (priv->view_control, "activate",
			  G_CALLBACK (activated), view);

	priv->sidebar_model = gtk_list_store_new (3, G_TYPE_STRING, EXCHANGE_TYPE_ACCOUNT, GTK_TYPE_WIDGET);
	widget = gtk_tree_view_new_with_model (GTK_TREE_MODEL (priv->sidebar_model));
	gtk_widget_show (widget);
	priv->sidebar = GTK_TREE_VIEW (widget);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled), widget);
	gtk_widget_show (scrolled);
	priv->sidebar_control = bonobo_control_new (scrolled);

	gtk_tree_view_set_headers_visible (priv->sidebar, FALSE);
	column = gtk_tree_view_column_new_with_attributes (
		_("Account Name"), gtk_cell_renderer_text_new (),
		"text", XC_BACKEND_VIEW_ACCOUNT_NAME_COLUMN, NULL);
	gtk_tree_view_append_column (priv->sidebar, column);

	accounts = exchange_config_listener_get_accounts (config_listener);
	for (acc = accounts; acc; acc = acc->next)
		exchange_account_created (config_listener, acc->data, view);
	g_slist_free (accounts);

	selection = gtk_tree_view_get_selection (priv->sidebar);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (account_selection_changed), view);

	priv->statusbar = gtk_drawing_area_new ();
	gtk_widget_show (priv->statusbar);
	priv->statusbar_control = bonobo_control_new (priv->statusbar);

	priv->items_handler = e_user_creatable_items_handler_new ("exchange", NULL, NULL);

	return view;
}

BonoboControl *
xc_backend_view_get_sidebar (XCBackendView *view)
{
	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);

	return view->priv->sidebar_control;
}

BonoboControl *
xc_backend_view_get_view (XCBackendView *view)
{
	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);

	return view->priv->view_control;
}

BonoboControl *
xc_backend_view_get_statusbar (XCBackendView *view)
{
	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);

	return view->priv->statusbar_control;
}

EStorageSetView *
xc_backend_view_get_storage_set_view (XCBackendView *view)
{
	XCBackendViewPrivate *priv;
	GtkWidget *page;

	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);
	priv = view->priv;

	page = gtk_notebook_get_nth_page (priv->body, gtk_notebook_get_current_page (priv->body));
	return account_view_get_storage_set_view (page);
}

ExchangeAccount *
xc_backend_view_get_selected_account (XCBackendView *view)
{
	XCBackendViewPrivate *priv;
	ExchangeAccount *account;
	GtkTreeModel *model;
	GtkTreeIter iter;

	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);
	priv = view->priv;

	if (!gtk_tree_selection_get_selected (gtk_tree_view_get_selection (priv->sidebar), &model, &iter))
		return NULL;

	gtk_tree_model_get (model, &iter,
			    XC_BACKEND_VIEW_ACCOUNT_COLUMN, &account,
			    -1);
	return account;
}

EFolder *
xc_backend_view_get_selected_folder (XCBackendView *view)
{
	EStorageSetView *storage_set_view;
	const char *path;

	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);

	storage_set_view = xc_backend_view_get_storage_set_view (view);
	if (!storage_set_view)
		return NULL;

	path = e_storage_set_view_get_current_folder (storage_set_view);
	if (!path)
		return NULL;

	return e_storage_set_get_folder (e_storage_set_view_get_storage_set (storage_set_view), path);
}
