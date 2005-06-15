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
#include "xc-backend-view.h"
#include <e-folder-exchange.h>
#include "exchange-component.h"
#include <e2k-utils.h>
#include <exchange-account.h>
#include <exchange-hierarchy.h>
#include <exchange-folder-size.h>

#include "e-folder-creation-dialog.h"
#include "e-folder-misc-dialogs.h"

#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-control.h>
#include <gal/widgets/e-popup-menu.h>
#include <e-util/e-dialog-utils.h>

static inline GtkWidget *
widget_for_view (XCBackendView *view)
{
	return (GtkWidget *)xc_backend_view_get_storage_set_view (view);
}

static void
do_oof (BonoboUIComponent *component, gpointer user_data,
	const char *cname)
{
	XCBackendView *view = user_data;

	exchange_oof (xc_backend_view_get_selected_account (view),
		      widget_for_view (view));
}

static void
do_delegates (BonoboUIComponent *component, gpointer user_data,
	      const char *cname)
{
	XCBackendView *view = user_data;

	exchange_delegates (xc_backend_view_get_selected_account (view),
			    widget_for_view (view));
}

static void
do_change_password (BonoboUIComponent *component, gpointer user_data,
		    const char *cname)
{
	XCBackendView *view = user_data;
	ExchangeAccount *account;
	char *old_password, *new_password;

	account = xc_backend_view_get_selected_account (view);

	old_password = exchange_account_get_password (account);
	new_password = exchange_get_new_password (old_password, 1);

	exchange_account_set_password (account, old_password, new_password);

	g_free (old_password);
	g_free (new_password);
}

static void
do_folder_size_menu (BonoboUIComponent *component, gpointer user_data,
	  const char *cname)
{
	XCBackendView *view = user_data;
	//EStorageSetView *storage_set_view;
	EFolder *folder = xc_backend_view_get_selected_folder (view);

	if (!folder)
		/* FIXME : e_notice (_("Select a folder to view the sizes\n")); */
		return;

	//storage_set_view = xc_backend_view_get_storage_set_view (view);
	exchange_folder_size_display (folder, widget_for_view (view));
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
	/* BONOBO_UI_VERB ("ExchangeOOF", do_oof), */
	/* BONOBO_UI_VERB ("ExchangeDelegation", do_delegates), */
	/* BONOBO_UI_VERB ("ExchangePassword", do_change_password), */
	/* BONOBO_UI_VERB ("ExchangeFolderSize", do_folder_size_menu), */
	BONOBO_UI_VERB ("ExchangeSubscribeUser", do_subscribe_user),
	BONOBO_UI_VERB ("ExchangeUnsubscribeUser", do_unsubscribe_user),

	BONOBO_UI_VERB_END
};

void
xc_commands_activate (XCBackendView *view)
{
	BonoboControl *control;
	BonoboUIComponent *uic;
	Bonobo_UIContainer remote_uih;

	control = xc_backend_view_get_view (view);
	uic = bonobo_control_get_ui_component (control);
	g_return_if_fail (uic != NULL);

	remote_uih = bonobo_control_get_remote_ui_container (control, NULL);
	bonobo_ui_component_set_container (uic, remote_uih, NULL);
	bonobo_object_release_unref (remote_uih, NULL);

	bonobo_ui_component_add_verb_list_with_data (uic, verbs, view);
	bonobo_ui_component_freeze (uic, NULL);
	bonobo_ui_util_set_ui (uic, PREFIX,
			       CONNECTOR_UIDIR "/ximian-connector.xml",
			       "ximian-connector",
			       NULL);
	bonobo_ui_component_thaw (uic, NULL);
}

void
xc_commands_deactivate (XCBackendView *view)
{
	BonoboControl *control;
	BonoboUIComponent *uic;

	control = xc_backend_view_get_view (view);
	uic = bonobo_control_get_ui_component (control);
	g_return_if_fail (uic != NULL);

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

static inline ExchangeAccount *
xc_folder_get_account (XCFolderCommandData *fcd)
{
	return exchange_component_get_account_for_uri (global_exchange_component, e_folder_exchange_get_internal_uri (fcd->folder));
}

#if 0
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
#endif


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
	exchange_permissions_dialog_new (xc_folder_get_account (fcd),
					 fcd->folder,
					 GTK_WIDGET (fcd->storage_set_view));
	xc_folder_command_data_free (fcd);
}

static void
do_folder_size (GtkWidget *item, XCFolderCommandData *fcd)
{
	exchange_folder_size_display (fcd->folder, GTK_WIDGET (fcd->storage_set_view));

	xc_folder_command_data_free (fcd);
}

static void
favorites_error (GtkWidget *parent_widget,
		 const char *fmt,
		 ExchangeAccountFolderResult result)
{
	const char *msg;

	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS:
		msg = _("Folder already exists");
		break;
	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		msg = _("Folder does not exist");
		break;
	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		msg = _("Permission denied");
		break;
	case EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR:
	default:
		msg = _("Generic error");
		break;
	}

	e_notice (parent_widget, GTK_MESSAGE_ERROR, fmt, msg);
}

static void
do_add_favorite (GtkWidget *item, XCFolderCommandData *fcd)
{
	ExchangeAccountFolderResult result;
	ExchangeAccount *exacct;

	exacct = xc_folder_get_account (fcd);
	if (!exacct)
		goto err_end;

	result = exchange_account_add_favorite (exacct, fcd->folder);
	if (result != EXCHANGE_ACCOUNT_FOLDER_OK) {
		favorites_error (GTK_WIDGET (fcd->storage_set_view),
				 _("Could not add favorite: %s"), result);
	}

err_end :
	xc_folder_command_data_free (fcd);
}

static void
do_remove_favorite (GtkWidget *item, XCFolderCommandData *fcd)
{
	ExchangeAccountFolderResult result;

	result = exchange_account_remove_favorite (xc_folder_get_account (fcd),
						   fcd->folder);
	if (result != EXCHANGE_ACCOUNT_FOLDER_OK) {
		favorites_error (GTK_WIDGET (fcd->storage_set_view),
				 _("Could not remove favorite: %s"), result);
	}
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
	/*
	FIXME: See 62440
	E_POPUP_ITEM (N_("_Move Folder..."),
		      G_CALLBACK (do_move_folder),
		      XC_FOLDER_COMMAND_MASK_MOVECOPY),
	E_POPUP_ITEM (N_("_Copy Folder..."),
		      G_CALLBACK (do_copy_folder),
		      XC_FOLDER_COMMAND_MASK_MOVECOPY),
	*/
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
/*
	E_POPUP_ITEM (N_("_Permissions..."),
		      G_CALLBACK (do_permissions),
		      XC_FOLDER_COMMAND_MASK_PERMISSIONS),
*/
	E_POPUP_ITEM (N_("_Show Folder Sizes"),
		      G_CALLBACK (do_folder_size),
		      XC_FOLDER_COMMAND_MASK_PERMISSIONS),

	E_POPUP_TERMINATOR,
};

void
xc_commands_context_menu (EStorageSetView *storage_set_view,
			  EFolder *folder, GdkEvent *event)
{
	guint32 disable_mask = 0, hide_mask = 0;
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
