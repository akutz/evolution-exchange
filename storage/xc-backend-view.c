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

#include <bonobo/bonobo-exception.h>
#include <gal/widgets/e-popup-menu.h>
#include <gtk/gtkscrolledwindow.h>

#include "e-storage-set.h"
#include "e-storage-set-view.h"

#include "xc-commands.h"

#define d(x)

#define PARENT_TYPE BONOBO_TYPE_CONTROL
static BonoboControlClass *parent_class = NULL;

struct XCBackendViewPrivate {
	EStorageSet     *storage_set;
	EStorageSetView *storage_set_view;
};

static void
dispose (GObject *object)
{
	XCBackendViewPrivate *priv = XC_BACKEND_VIEW (object)->priv;

	if (priv->storage_set) {
		g_object_unref (priv->storage_set);
		priv->storage_set = NULL;
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
activated (BonoboControl *sidebar, gboolean active, gpointer user_data)
{
	if (active)
		xc_commands_activate (sidebar);
	else
		xc_commands_deactivate (sidebar);
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

BonoboControl *
xc_backend_view_new (EStorageSet *storage_set)
{
	XCBackendView *view;
	XCBackendViewPrivate *priv;
	GtkWidget *storage_set_view, *scrolled;

	view = g_object_new (XC_TYPE_BACKEND_VIEW, NULL);
	priv = view->priv;

	priv->storage_set = g_object_ref (storage_set);

	storage_set_view = e_storage_set_create_new_view (storage_set);
	priv->storage_set_view = E_STORAGE_SET_VIEW (storage_set_view);

	gtk_widget_show (storage_set_view);
	g_signal_connect (storage_set_view, "folder_context_menu",
			  G_CALLBACK (folder_context_menu), view);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
					     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scrolled), storage_set_view);
	gtk_widget_show (scrolled);
	bonobo_control_construct (BONOBO_CONTROL (view), scrolled);

	g_signal_connect (view, "activate", G_CALLBACK (activated), view);

	return BONOBO_CONTROL (view);
}

EStorageSetView *
xc_backend_view_get_storage_set_view (XCBackendView *view)
{
	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);

	return view->priv->storage_set_view;
}

EFolder *
xc_backend_view_get_selected_folder (XCBackendView *view)
{
	const char *path;

	g_return_val_if_fail (XC_IS_BACKEND_VIEW (view), NULL);

	path = e_storage_set_view_get_current_folder (view->priv->storage_set_view);
	if (!path)
		return NULL;

	return e_storage_set_get_folder (view->priv->storage_set, path);
}
