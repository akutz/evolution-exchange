/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

#include "xc-commands.h"
#include "xc-backend.h"
#include "xc-backend-view.h"
#include "e-folder-exchange.h"
#include "exchange-hierarchy-gal.h"
#include "e2k-utils.h"
#include "exchange-change-password.h"
#include "exchange-delegates.h"
#include "exchange-oof.h"
#include "exchange-permissions-dialog.h"

#include "e-folder-creation-dialog.h"
#include "e-folder-misc-dialogs.h"

#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-control.h>
#include <gal/widgets/e-popup-menu.h>
#include <e-util/e-dialog-utils.h>

static ExchangeAccount *
get_account_for_view (BonoboControl *view)
{
	EFolder *folder;
	ExchangeHierarchy *hier;

	folder = xc_backend_view_get_selected_folder (XC_BACKEND_VIEW (view));
	if (!folder)
		return NULL;

	/* FIXME */
	if (!E_IS_FOLDER_EXCHANGE (folder))
		return xc_backend_get_account_for_uri (global_backend, NULL);

	hier = e_folder_exchange_get_hierarchy (folder);
	if (!hier)
		return NULL;

	return hier->account;
}

static void
do_oof (BonoboUIComponent *component, gpointer user_data,
	const char *cname)
{
	BonoboControl *sidebar = user_data;
	GtkWidget *sidebar_widget = bonobo_control_get_widget (sidebar);

	exchange_oof (get_account_for_view (sidebar), sidebar_widget);
}

static void
do_delegates (BonoboUIComponent *component, gpointer user_data,
	      const char *cname)
{
	BonoboControl *sidebar = user_data;
	GtkWidget *sidebar_widget = bonobo_control_get_widget (sidebar);

	exchange_delegates (get_account_for_view (sidebar), sidebar_widget);
}

static void
do_change_password (BonoboUIComponent *component, gpointer user_data,
		    const char *cname)
{
	BonoboControl *sidebar = user_data;
	GtkWidget *sidebar_widget = bonobo_control_get_widget (sidebar);

	/*e_notice (sidebar_widget, GTK_MESSAGE_ERROR, "FIXME (do_change_password)");*/
	exchange_change_password ("", NULL, 1);
}

static void
do_quota (BonoboUIComponent *component, gpointer user_data,
	  const char *cname)
{
	BonoboControl *sidebar = user_data;
	GtkWidget *sidebar_widget = bonobo_control_get_widget (sidebar);

	e_notice (sidebar_widget, GTK_MESSAGE_ERROR, "FIXME (do_quota)");
}

static void
do_subscribe_user (BonoboUIComponent *component, gpointer user_data,
		   const char *cname)
{
	XCBackendView *view = user_data;

	e_folder_add_foreign_dialog (xc_backend_view_get_storage_set_view (view));
}

static void
do_unsubscribe_user (BonoboUIComponent *component, gpointer user_data,
		     const char *cname)
{
	XCBackendView *view = user_data;
	EStorageSetView *storage_set_view;

	storage_set_view = xc_backend_view_get_storage_set_view (view);
	e_folder_remove_foreign_dialog (storage_set_view,
					xc_backend_view_get_selected_folder (view),
					e_storage_set_view_get_current_folder (storage_set_view));
}

static BonoboUIVerb verbs [] = {
	BONOBO_UI_VERB ("ExchangeOOF", do_oof),
	BONOBO_UI_VERB ("ExchangeDelegation", do_delegates),
	BONOBO_UI_VERB ("ExchangePassword", do_change_password),
	BONOBO_UI_VERB ("ExchangeQuota", do_quota),
	BONOBO_UI_VERB ("ExchangeSubscribeUser", do_subscribe_user),
	BONOBO_UI_VERB ("ExchangeUnsubscribeUser", do_unsubscribe_user),

	BONOBO_UI_VERB_END
};

void
xc_commands_activate (BonoboControl *sidebar)
{
	BonoboUIComponent *uic;
	Bonobo_UIContainer remote_uih;

	uic = bonobo_control_get_ui_component (sidebar);
	g_return_if_fail (uic != NULL);

	remote_uih = bonobo_control_get_remote_ui_container (sidebar, NULL);
	bonobo_ui_component_set_container (uic, remote_uih, NULL);
	bonobo_object_release_unref (remote_uih, NULL);

	bonobo_ui_component_add_verb_list_with_data (uic, verbs, sidebar);
	bonobo_ui_component_freeze (uic, NULL);
	bonobo_ui_util_set_ui (uic, PREFIX,
			       CONNECTOR_UIDIR "/ximian-connector.xml",
			       "ximian-connector",
			       NULL);
	bonobo_ui_component_thaw (uic, NULL);
}

void
xc_commands_deactivate (BonoboControl *sidebar)
{
	BonoboUIComponent *uic;

	uic = bonobo_control_get_ui_component (sidebar);
	g_return_if_fail (uic != NULL);

	bonobo_ui_component_rm (uic, "/menu/Connector", NULL);
 	bonobo_ui_component_unset_container (uic, NULL);
}


/* Context menu */

typedef struct {
	EStorageSet *storage_set;
	EStorageSetView *storage_set_view;
	EFolder *folder;
	char *folder_path;
} XCFolderCommandData;

static XCFolderCommandData *
xc_folder_command_data_new (EStorageSetView *storage_set_view, EFolder *folder)
{
	XCFolderCommandData *fcd;

	fcd = g_new (XCFolderCommandData, 1);
	fcd->storage_set_view = g_object_ref (storage_set_view);
	fcd->storage_set = e_storage_set_view_get_storage_set (storage_set_view);
	fcd->folder = g_object_ref (folder);
	fcd->folder_path = e_storage_set_get_path_for_physical_uri  (
		e_storage_set_view_get_storage_set (storage_set_view),
		e_folder_get_physical_uri (folder));

	return fcd;
}

static void
xc_folder_command_data_free (XCFolderCommandData *fcd)
{
	g_object_unref (fcd->storage_set_view);
	g_object_unref (fcd->folder);
	g_free (fcd->folder_path);
	g_free (fcd);
}


static void
do_move_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_notice (item, GTK_MESSAGE_ERROR, "FIXME (do_move_folder)");
	xc_folder_command_data_free (fcd);
}

static void
do_copy_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_notice (item, GTK_MESSAGE_ERROR, "FIXME (do_copy_folder)");
	xc_folder_command_data_free (fcd);
}


static void
new_folder_callback (EStorageSet *storage_set,
		     EFolderCreationDialogResult result,
		     const char *path,
		     gpointer user_data)
{
	XCFolderCommandData *fcd = user_data;

	/* FIXME: do we need to do anything here? */

	if (result == E_FOLDER_CREATION_DIALOG_RESULT_SUCCESS ||
	    result == E_FOLDER_CREATION_DIALOG_RESULT_CANCEL)
		xc_folder_command_data_free (fcd);
}

static void
do_new_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_folder_creation_dialog  (fcd->storage_set_view,
				   fcd->folder,
				   e_folder_get_type_string (fcd->folder),
				   new_folder_callback, fcd);
}

static void
do_delete_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_folder_delete_dialog (fcd->storage_set_view,
				fcd->folder, fcd->folder_path);
	xc_folder_command_data_free (fcd);
}

static void
do_rename_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_folder_rename_dialog (fcd->storage_set_view, fcd->folder);
	xc_folder_command_data_free (fcd);
}

static void
do_add_foreign_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_folder_add_foreign_dialog (fcd->storage_set_view);
	xc_folder_command_data_free (fcd);
}

static void
do_remove_foreign_folder (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_folder_remove_foreign_dialog (fcd->storage_set_view,
					fcd->folder, fcd->folder_path);
	xc_folder_command_data_free (fcd);
}

static void
do_permissions (GtkWidget *item, XCFolderCommandData *fcd)
{
	ExchangeAccount *account;

	account = xc_backend_get_account_for_uri (global_backend, e_folder_exchange_get_internal_uri (fcd->folder));
	exchange_permissions_dialog_new (account, fcd->folder,
					 GTK_WIDGET (fcd->storage_set_view));
	xc_folder_command_data_free (fcd);
}

static void
do_add_favorite (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_notice (item, GTK_MESSAGE_ERROR, "FIXME (do_add_favorite)");
	xc_folder_command_data_free (fcd);
}

static void
do_remove_favorite (GtkWidget *item, XCFolderCommandData *fcd)
{
	e_notice (item, GTK_MESSAGE_ERROR, "FIXME (do_remove_favorite)");
	xc_folder_command_data_free (fcd);
}

#define XC_FOLDER_COMMAND_MASK_MOVECOPY    (1 << 0)
#define XC_FOLDER_COMMAND_MASK_CHANGE      (1 << 1)
#define XC_FOLDER_COMMAND_MASK_PERMISSIONS (1 << 2)

#define XC_FOLDER_COMMAND_MASK_NORMAL      (1 << 3)
#define XC_FOLDER_COMMAND_MASK_FAVORITES   (1 << 4)
#define XC_FOLDER_COMMAND_MASK_PUBLIC      (1 << 5)
#define XC_FOLDER_COMMAND_MASK_FOREIGN     (1 << 6)

#define E_POPUP_SEPARATOR_WITH_MASK(mask) { "", NULL, (NULL), NULL, mask }

static EPopupMenu popup_menu [] = {
	E_POPUP_ITEM (N_("_Move Folder..."),
		      G_CALLBACK (do_move_folder),
		      XC_FOLDER_COMMAND_MASK_MOVECOPY),
	E_POPUP_ITEM (N_("_Copy Folder..."),
		      G_CALLBACK (do_copy_folder),
		      XC_FOLDER_COMMAND_MASK_MOVECOPY),
	E_POPUP_ITEM (N_("_Rename Folder..."),
		      G_CALLBACK (do_rename_folder),
		      XC_FOLDER_COMMAND_MASK_CHANGE),

	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("_New Folder..."),
		      G_CALLBACK (do_new_folder),
		      XC_FOLDER_COMMAND_MASK_NORMAL),
	E_POPUP_ITEM (N_("_Delete Folder"),
		      G_CALLBACK (do_delete_folder),
		      XC_FOLDER_COMMAND_MASK_CHANGE|
		      XC_FOLDER_COMMAND_MASK_NORMAL),

	E_POPUP_ITEM (N_("_Add Other User's Folder..."),
		      G_CALLBACK (do_add_foreign_folder),
		      XC_FOLDER_COMMAND_MASK_FOREIGN),
	E_POPUP_ITEM (N_("_Remove Other User's Folder"),
		      G_CALLBACK (do_remove_foreign_folder),
		      XC_FOLDER_COMMAND_MASK_FOREIGN),

	E_POPUP_SEPARATOR,

	E_POPUP_ITEM (N_("Add to _Favorites"),
		      G_CALLBACK (do_add_favorite),
		      XC_FOLDER_COMMAND_MASK_MOVECOPY|
		      XC_FOLDER_COMMAND_MASK_PUBLIC),
	E_POPUP_ITEM (N_("Remove from Fa_vorites"),
		      G_CALLBACK (do_remove_favorite),
		      XC_FOLDER_COMMAND_MASK_CHANGE|
		      XC_FOLDER_COMMAND_MASK_FAVORITES),

	E_POPUP_SEPARATOR_WITH_MASK(XC_FOLDER_COMMAND_MASK_PUBLIC),

	E_POPUP_ITEM (N_("_Permissions..."),
		      G_CALLBACK (do_permissions),
		      XC_FOLDER_COMMAND_MASK_PERMISSIONS),

	E_POPUP_TERMINATOR,
};

void
xc_commands_context_menu (EStorageSetView *storage_set_view,
			  EFolder *folder, GdkEvent *event)
{
	guint32 disable_mask, hide_mask;
	ExchangeHierarchy *hier;
	XCFolderCommandData *fcd;

	if (!E_IS_FOLDER_EXCHANGE (folder)) {
		/* This is the top-level folder representing the
		 * account. There is no context menu here.
		 */
		return;
	}

	hier = e_folder_exchange_get_hierarchy (folder);

	switch (hier->type) {
	case EXCHANGE_HIERARCHY_PERSONAL:
		hide_mask = (XC_FOLDER_COMMAND_MASK_FAVORITES |
			     XC_FOLDER_COMMAND_MASK_PUBLIC |
			     XC_FOLDER_COMMAND_MASK_FOREIGN);
		disable_mask = 0;
		break;

	case EXCHANGE_HIERARCHY_FAVORITES:
		hide_mask = (XC_FOLDER_COMMAND_MASK_NORMAL |
			     XC_FOLDER_COMMAND_MASK_FOREIGN);
		disable_mask = XC_FOLDER_COMMAND_MASK_MOVECOPY;
		break;

	case EXCHANGE_HIERARCHY_PUBLIC:
		hide_mask = XC_FOLDER_COMMAND_MASK_FOREIGN;
		disable_mask = XC_FOLDER_COMMAND_MASK_FAVORITES;
		break;

	case EXCHANGE_HIERARCHY_GAL:
		/* No context menu here either. */
		return;

	case EXCHANGE_HIERARCHY_FOREIGN:
		hide_mask = (XC_FOLDER_COMMAND_MASK_NORMAL |
			     XC_FOLDER_COMMAND_MASK_FAVORITES |
			     XC_FOLDER_COMMAND_MASK_PUBLIC);
		disable_mask = XC_FOLDER_COMMAND_MASK_MOVECOPY;
		break;
	}

	if (folder == hier->toplevel) {
		disable_mask |= (XC_FOLDER_COMMAND_MASK_CHANGE |
				 XC_FOLDER_COMMAND_MASK_MOVECOPY);

		if (hier->type != EXCHANGE_HIERARCHY_PERSONAL)
			disable_mask |= XC_FOLDER_COMMAND_MASK_PERMISSIONS;
	}

	fcd = xc_folder_command_data_new (storage_set_view, folder);
	e_popup_menu_run (popup_menu, event, disable_mask, hide_mask, fcd);
}
