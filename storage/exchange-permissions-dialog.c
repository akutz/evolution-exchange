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

#include "exchange-permissions-dialog.h"
#include "exchange-hierarchy.h"

#include "e2k-connection.h"
#include "e2k-global-catalog.h"
#include "e2k-propnames.h"
#include "e2k-security-descriptor.h"
#include "e2k-user-dialog.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e-folder-exchange.h"
#include "exchange-account.h"

#include <e-util/e-dialog-utils.h>
#include <gal/widgets/e-option-menu.h>
#include <glade/glade-xml.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkmenushell.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtkversion.h>

struct _ExchangePermissionsDialogPrivate {
	ExchangeAccount *account;
	char *base_uri, *folder_path;
	E2kSecurityDescriptor *sd;
	gboolean changed;
	gboolean frozen;

	/* The user list */
	GtkTreeView *list_view;
	GtkListStore *list_store;
	GtkTreeSelection *list_selection;
	E2kSid *selected_sid;

	/* The Role menu */
	GtkOptionMenu *role_optionmenu;
	GtkWidget *separator, *custom;
	E2kPermissionsRole selected_role;

	/* The toggles */
	GtkToggleButton *read_items_check, *create_items_check;
	GtkToggleButton *create_subfolders_check, *folder_visible_check;
	GtkToggleButton *folder_owner_check, *folder_contact_check;
	GtkToggleButton *edit_none_radio, *edit_own_radio, *edit_all_radio;
	GtkToggleButton *delete_none_radio, *delete_own_radio, *delete_all_radio;
	guint32 selected_perms;

	GdkNativeWindow parent;
};

enum {
	EXCHANGE_PERMISSIONS_DIALOG_NAME_COLUMN,
	EXCHANGE_PERMISSIONS_DIALOG_ROLE_COLUMN,
	EXCHANGE_PERMISSIONS_DIALOG_SID_COLUMN,

	EXCHANGE_PERMISSIONS_DIALOG_NUM_COLUMNS
};

#define PARENT_TYPE GTK_TYPE_DIALOG
static GtkDialogClass *parent_class = NULL;

static void
finalize (GObject *object)
{
	ExchangePermissionsDialog *dialog =
		EXCHANGE_PERMISSIONS_DIALOG (object);

	g_free (dialog->priv->base_uri);
	g_free (dialog->priv->folder_path);

	if (dialog->priv->sd)
		g_object_unref (dialog->priv->sd);

	g_free (dialog->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;
}

static void
init (GObject *object)
{
	ExchangePermissionsDialog *dialog =
		EXCHANGE_PERMISSIONS_DIALOG (object);

	dialog->priv = g_new0 (ExchangePermissionsDialogPrivate, 1);
}

E2K_MAKE_TYPE (exchange_permissions_dialog, ExchangePermissionsDialog, class_init, init, PARENT_TYPE)



static void get_widgets         (ExchangePermissionsDialog *dialog,
				 GladeXML *xml);
static void got_sd              (E2kConnection *conn, SoupMessage *msg,
			         E2kResult *results, int nresults,
			         gpointer user_data);
static void setup_user_list     (ExchangePermissionsDialog *dialog);
static void display_permissions (ExchangePermissionsDialog *dialog);
static void dialog_response     (ExchangePermissionsDialog *dialog,
				 int response, gpointer user_data);

static const char *sd_props[] = {
	E2K_PR_EXCHANGE_SD_BINARY,
	E2K_PR_EXCHANGE_SD_XML
};
static const int n_sd_props = sizeof (sd_props) / sizeof (sd_props[0]);

/**
 * exchange_permissions_dialog_new:
 * @account: an account
 * @folder: the folder whose permissions are to be editted
 * @parent: X Window ID of the dialog's parent
 *
 * Creates and displays a modeless permissions editor dialog for @folder.
 **/
void
exchange_permissions_dialog_new (ExchangeAccount *account,
				 EFolder *folder,
				 GdkNativeWindow parent)
{
	ExchangePermissionsDialog *dialog;
	const char *base_uri, *folder_path;
	E2kConnection *conn;
	ExchangeHierarchy *hier;
	GladeXML *xml;
	GtkWidget *box;
	char *title;

	conn = exchange_account_get_connection (account);
	g_return_if_fail (conn);
	xml = glade_xml_new (
		CONNECTOR_GLADEDIR "/exchange-permissions-dialog.glade",
		"permissions_vbox", PACKAGE);
	g_return_if_fail (xml != NULL);

	/* Create the dialog */
	dialog = g_object_new (EXCHANGE_TYPE_PERMISSIONS_DIALOG, NULL);

	title = g_strdup_printf (_("Permissions for %s"),
				 e_folder_get_name (folder));
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_free (title);

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_OK,
				NULL);

	dialog->priv->parent = parent;

	g_signal_connect (dialog, "response",
			  G_CALLBACK (dialog_response), NULL);

	dialog->priv->changed = FALSE;

	/* Put the widgets from the glade file into it */
	box = glade_xml_get_widget (xml, "permissions_vbox");
	g_object_ref (box);
	gtk_widget_unparent (box);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    box, TRUE, TRUE, 0);
	g_object_unref (box);

	get_widgets (dialog, xml);
	g_object_unref (xml);

	dialog->priv->account = account;
	g_object_ref (account);

	hier = e_folder_exchange_get_hierarchy (folder);
	base_uri = e_folder_exchange_get_internal_uri (hier->toplevel);
	dialog->priv->base_uri = g_strdup (base_uri);
	folder_path = e2k_uri_relative (
		dialog->priv->base_uri,
		e_folder_exchange_get_internal_uri (folder));
	dialog->priv->folder_path = g_strdup (folder_path);
		
	/* And fetch the security descriptor */
	e2k_connection_bpropfind (conn, base_uri, &folder_path, 1, "0",
				  sd_props, n_sd_props, got_sd, dialog);
}

static void
got_sd (E2kConnection *conn, SoupMessage *msg,
	E2kResult *results, int nresults,
	gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	xmlNode *xml_form;
	GByteArray *binary_form;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode) || nresults < 1) {
	lose:
		e_notice_with_xid (dialog->priv->parent, GTK_MESSAGE_ERROR,
				   _("Could not read folder permissions"));
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	xml_form = e2k_properties_get_prop (results[0].props,
					    E2K_PR_EXCHANGE_SD_XML);
	binary_form = e2k_properties_get_prop (results[0].props,
					       E2K_PR_EXCHANGE_SD_BINARY);
	if (!xml_form || !binary_form)
		goto lose;

	dialog->priv->sd = e2k_security_descriptor_new (xml_form, binary_form);
	if (!dialog->priv->sd)
		goto lose;

	setup_user_list (dialog);
	gtk_widget_show (GTK_WIDGET (dialog));
}

static void
dialog_response (ExchangePermissionsDialog *dialog, int response,
		 gpointer user_data)
{
	E2kConnection *conn;
	GByteArray *binsd;
	E2kProperties *props;
	E2kResult *results;
	int status, nresults;

	if (response != GTK_RESPONSE_OK || !dialog->priv->changed) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	conn = exchange_account_get_connection (dialog->priv->account);
	g_return_if_fail (conn != NULL);

	binsd = e2k_security_descriptor_to_binary (dialog->priv->sd);
	if (!binsd) {
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("Could not update folder permissions."));
		return;
	}

	gtk_widget_set_sensitive (GTK_WIDGET (dialog), FALSE);

	props = e2k_properties_new ();
	e2k_properties_set_binary (props, E2K_PR_EXCHANGE_SD_BINARY, binsd);
	E2K_DEBUG_HINT ('S');
	status = e2k_connection_bproppatch_sync (conn, dialog->priv->base_uri,
						 (const char **)&dialog->priv->folder_path, 1,
						 props, FALSE, &results,
						 &nresults);
	e2k_properties_free (props);
	if (status == SOUP_ERROR_DAV_MULTISTATUS) {
		status = results[0].status;
		e2k_results_free (results, nresults);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (dialog), TRUE);

	if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("Could not update folder permissions. %s"),
			  status == SOUP_ERROR_UNAUTHORIZED ?
			  _("(Permission denied.)") : "");
		return;
	}

	if (response == GTK_RESPONSE_OK)
		gtk_widget_destroy (GTK_WIDGET (dialog));
}


static void
set_permissions (ExchangePermissionsDialog *dialog, guint32 perms)
{
	dialog->priv->selected_perms = perms;
	dialog->priv->selected_role = e2k_permissions_role_find (perms);
	e2k_security_descriptor_set_permissions (dialog->priv->sd,
						 dialog->priv->selected_sid,
						 dialog->priv->selected_perms);

	dialog->priv->changed = TRUE;
}


/* User list functions */

static void
list_view_selection_changed (GtkTreeSelection *selection, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	GtkTreeModel *model;
	GtkTreeIter iter;
	E2kSid *sid;

	if (!gtk_tree_selection_get_selected (selection, &model, &iter))
		return;
	gtk_tree_model_get (model, &iter,
			    EXCHANGE_PERMISSIONS_DIALOG_SID_COLUMN, &sid,
			    -1);

	dialog->priv->selected_sid = sid;
	dialog->priv->selected_perms =
		e2k_security_descriptor_get_permissions (dialog->priv->sd, sid);
	dialog->priv->selected_role =
		e2k_permissions_role_find (dialog->priv->selected_perms);

	/* "Default" or "Anonymous" can't be a Folder contact, but any
	 * real person can.
	 */
	gtk_widget_set_sensitive (GTK_WIDGET (dialog->priv->folder_contact_check),
				  e2k_sid_get_sid_type (sid) != E2K_SID_TYPE_WELL_KNOWN_GROUP);

	/* Update role menu and permissions checkboxes */
	display_permissions (dialog);
}

static void
add_user_to_list (ExchangePermissionsDialog *dialog, E2kSid *sid, gboolean select)
{
	guint32 perms;
	E2kPermissionsRole role;
	GtkTreeIter iter;

	perms = e2k_security_descriptor_get_permissions (dialog->priv->sd,
							 sid);
	role = e2k_permissions_role_find (perms);

	if (e2k_sid_get_sid_type (sid) == E2K_SID_TYPE_WELL_KNOWN_GROUP)
		gtk_list_store_insert (dialog->priv->list_store, &iter, 1);
	else
		gtk_list_store_append (dialog->priv->list_store, &iter);

	gtk_list_store_set (dialog->priv->list_store, &iter,
			    EXCHANGE_PERMISSIONS_DIALOG_NAME_COLUMN,
			    e2k_sid_get_display_name (sid),
			    EXCHANGE_PERMISSIONS_DIALOG_ROLE_COLUMN,
			    e2k_permissions_role_get_name (role),
			    EXCHANGE_PERMISSIONS_DIALOG_SID_COLUMN,
			    sid,
			    -1);

	if (select)
		gtk_tree_selection_select_iter (dialog->priv->list_selection, &iter);
}

static void
add_clicked (GtkButton *button, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	GtkWidget *user_dialog;
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;
	E2kSid *sid2;
	const guint8 *bsid, *bsid2;
	char *email;
	int result;
	gboolean valid;
	GtkTreeIter iter;

	gc = exchange_account_get_global_catalog (dialog->priv->account);
	if (!gc) {
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("Unable to add user to access control list:\n"
			    "No Global Catalog server is configured for this account."));
		return;
	}

	user_dialog = e2k_user_dialog_new (GTK_WIDGET (dialog),
					   _("Add User:"), _("Add User"));
	result = gtk_dialog_run (GTK_DIALOG (user_dialog));
	if (result == GTK_RESPONSE_OK)
		email = e2k_user_dialog_get_user (E2K_USER_DIALOG (user_dialog));
	else
		email = NULL;
	gtk_widget_destroy (user_dialog);
	if (email == NULL)
		return;

	status = e2k_global_catalog_lookup (
		gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL, email,
		E2K_GLOBAL_CATALOG_LOOKUP_SID, &entry);
	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		break;
	case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("No such user %s"), email);
		break;
	case E2K_GLOBAL_CATALOG_NO_DATA:
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("%s cannot be added to an access control list"),
			  email);
		break;
	default:
		e_notice (dialog, GTK_MESSAGE_ERROR,
			  _("Unknown error looking up %s"), email);
		break;
	}
	g_free (email);
	if (status != E2K_GLOBAL_CATALOG_OK)
		return;

	/* Make sure the user isn't already there. */
	bsid = e2k_sid_get_binary_sid (entry->sid);
	valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (dialog->priv->list_store), &iter);
	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (dialog->priv->list_store), &iter,
				    EXCHANGE_PERMISSIONS_DIALOG_SID_COLUMN, &sid2,
				    -1);
		bsid2 = e2k_sid_get_binary_sid (sid2);
		if (e2k_sid_binary_sid_equal (bsid, bsid2)) {
			e_notice (dialog, GTK_MESSAGE_ERROR,
				  _("%s is already in the list"),
				  entry->display_name);
			e2k_global_catalog_entry_free (gc, entry);
			gtk_tree_selection_select_iter (dialog->priv->list_selection, &iter);
			return;
		}

		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (dialog->priv->list_store), &iter);
	}

	add_user_to_list (dialog, entry->sid, TRUE);

	/* Calling set_permissions will cause the sd to take a
	 * ref on the sid, allowing us to unref it.
	 */
	set_permissions (dialog, 0);
	e2k_global_catalog_entry_free (gc, entry);
}

static void
remove_clicked (GtkButton *button, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	E2kSid *sid;
	GdkModifierType modifiers;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected (dialog->priv->list_selection,
					      &model, &iter))
		return;
	gtk_tree_model_get (model, &iter,
			    EXCHANGE_PERMISSIONS_DIALOG_SID_COLUMN, &sid,
			    -1);
	gdk_window_get_pointer (NULL, NULL, NULL, &modifiers);

	if (e2k_sid_get_sid_type (sid) == E2K_SID_TYPE_WELL_KNOWN_GROUP &&
	    !(modifiers & GDK_SHIFT_MASK)) {
		/* You shouldn't normally delete "Default" or "Anonymous". */
		set_permissions (dialog, 0);
	} else {
		gtk_list_store_remove (dialog->priv->list_store, &iter);
		e2k_security_descriptor_remove_sid (dialog->priv->sd, sid);

#if GTK_CHECK_VERSION (2, 2, 0)
		if (!gtk_list_store_iter_is_valid (dialog->priv->list_store, &iter)) {
			/* Select the new last row. Love that API... */
			gtk_tree_model_iter_nth_child (model, &iter, NULL,
						       gtk_tree_model_iter_n_children (model, NULL) - 1);
		}
#endif
		gtk_tree_selection_select_iter (dialog->priv->list_selection, &iter);

		dialog->priv->changed = TRUE;
	}
}

static void
setup_user_list (ExchangePermissionsDialog *dialog)
{
	E2kSecurityDescriptor *sd = dialog->priv->sd;
	E2kSid *default_entry;
	GList *sids;

	/* FIXME */
	/* gtk_clist_freeze (dialog->priv->clist); */

	/* Always put "Default" first. */
	default_entry = e2k_security_descriptor_get_default (sd);
	add_user_to_list (dialog, default_entry, TRUE);

	sids = e2k_security_descriptor_get_sids (sd);
	while (sids) {
		if (sids->data != default_entry)
			add_user_to_list (dialog, sids->data, FALSE);
		sids = sids->next;
	}
	g_list_free (sids);

	/* gtk_clist_thaw (dialog->priv->clist); */
}


/* Role option menu functions */

static void
role_changed (GtkOptionMenu *option, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	int role;

	if (dialog->priv->frozen)
		return;

	role = gtk_option_menu_get_history (option);
	if (role == dialog->priv->selected_role)
		return;
	if (role >= E2K_PERMISSIONS_ROLE_NUM_ROLES) {
		/* The user selected "Custom". Since "Custom" will
		 * only be there to select when it's already
		 * selected, this is a no-op.
		 */
		return;
	}

	set_permissions (dialog, e2k_permissions_role_get_perms (role));
	display_permissions (dialog);
}

static void
display_role (ExchangePermissionsDialog *dialog)
{
	int role = dialog->priv->selected_role;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected (dialog->priv->list_selection,
					      &model, &iter))
		return;
	gtk_list_store_set (dialog->priv->list_store, &iter,
			    EXCHANGE_PERMISSIONS_DIALOG_ROLE_COLUMN,
			    e2k_permissions_role_get_name (role),
			    -1);

	if (role == E2K_PERMISSIONS_ROLE_CUSTOM) {
		gtk_widget_show (dialog->priv->separator);
		gtk_widget_show (dialog->priv->custom);
		role = E2K_PERMISSIONS_ROLE_NUM_ROLES + 1;
	} else {
		gtk_widget_hide (dialog->priv->separator);
		gtk_widget_hide (dialog->priv->custom);
	}
	gtk_option_menu_set_history (dialog->priv->role_optionmenu, role);
}



/* Toggle buttons */
static void
check_toggled (GtkToggleButton *toggle, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	ExchangePermissionsDialogPrivate *priv = dialog->priv;
	guint32 new_perms, value;

	if (dialog->priv->frozen)
		return;

	value = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (toggle), "mapi_permission"));

	if (gtk_toggle_button_get_active (toggle))
		new_perms = priv->selected_perms | value;
	else
		new_perms = priv->selected_perms & ~value;

	if (new_perms == priv->selected_perms)
		return;

	set_permissions (dialog, new_perms);
	display_role (dialog);
}

static void
radio_toggled (GtkToggleButton *toggle, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	ExchangePermissionsDialogPrivate *priv = dialog->priv;
	guint32 new_perms, value, mask;

	if (dialog->priv->frozen || !gtk_toggle_button_get_active (toggle))
		return;

	value = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (toggle), "mapi_permission"));
	mask = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (toggle), "mapi_mask"));

	new_perms = (priv->selected_perms & ~mask) | value;
	if (new_perms == priv->selected_perms)
		return;

	set_permissions (dialog, new_perms);
	display_role (dialog);
}

static void
rv_toggle (GtkToggleButton *toggled, gpointer user_data)
{
	ExchangePermissionsDialog *dialog = user_data;
	GtkToggleButton *visible = dialog->priv->folder_visible_check;
	GtkToggleButton *read = dialog->priv->read_items_check;

	if (dialog->priv->frozen)
		return;

	/* If you turn off "Folder visible", then "Read items" turns
	 * off too. Contrariwise, if you turn on "Read items", then
	 * "Folder visible" turns on too.
	 */
	if (toggled == visible && !gtk_toggle_button_get_active (toggled))
		gtk_toggle_button_set_active (read, FALSE);
	else if (toggled == read && gtk_toggle_button_get_active (toggled))
		gtk_toggle_button_set_active (visible, TRUE);
}

static void
display_permissions (ExchangePermissionsDialog *dialog)
{
	GtkToggleButton *radio;
	guint32 perms = dialog->priv->selected_perms;

	dialog->priv->frozen = TRUE;

	/* Set up check boxes */
	gtk_toggle_button_set_active (dialog->priv->read_items_check,
				      perms & E2K_PERMISSION_READ_ANY);
	gtk_toggle_button_set_active (dialog->priv->create_items_check,
				      perms & E2K_PERMISSION_CREATE);
	gtk_toggle_button_set_active (dialog->priv->create_subfolders_check,
				      perms & E2K_PERMISSION_CREATE_SUBFOLDER);
	gtk_toggle_button_set_active (dialog->priv->folder_owner_check,
				      perms & E2K_PERMISSION_OWNER);
	gtk_toggle_button_set_active (dialog->priv->folder_contact_check,
				      (perms & E2K_PERMISSION_CONTACT) &&
				      GTK_WIDGET_SENSITIVE (dialog->priv->folder_contact_check));
	gtk_toggle_button_set_active (dialog->priv->folder_visible_check,
				      perms & E2K_PERMISSION_FOLDER_VISIBLE);

	/* Set up radio buttons */
	if (perms & E2K_PERMISSION_EDIT_ANY)
		radio = dialog->priv->edit_all_radio;
	else if (perms & E2K_PERMISSION_EDIT_OWNED)
		radio = dialog->priv->edit_own_radio;
	else
		radio = dialog->priv->edit_none_radio;
	gtk_toggle_button_set_active (radio, TRUE);

	if (perms & E2K_PERMISSION_DELETE_ANY)
		radio = dialog->priv->delete_all_radio;
	else if (perms & E2K_PERMISSION_DELETE_OWNED)
		radio = dialog->priv->delete_own_radio;
	else
		radio = dialog->priv->delete_none_radio;
	gtk_toggle_button_set_active (radio, TRUE);

	/* And role menu */
	display_role (dialog);

	dialog->priv->frozen = FALSE;
}



static void
get_widgets (ExchangePermissionsDialog *dialog, GladeXML *xml)
{
	GtkWidget *menu, *button;
	GtkTreeViewColumn *column;

#define GET_WIDGET(name, type) dialog->priv->name = type (glade_xml_get_widget (xml, #name))

	GET_WIDGET (list_view, GTK_TREE_VIEW);
	column = gtk_tree_view_column_new_with_attributes (
		_("Name"), gtk_cell_renderer_text_new (),
		"text", EXCHANGE_PERMISSIONS_DIALOG_NAME_COLUMN, NULL);
	gtk_tree_view_append_column (dialog->priv->list_view, column);
	column = gtk_tree_view_column_new_with_attributes (
		_("Role"), gtk_cell_renderer_text_new (),
		"text", EXCHANGE_PERMISSIONS_DIALOG_ROLE_COLUMN, NULL);
	gtk_tree_view_append_column (dialog->priv->list_view, column);

	dialog->priv->list_selection = gtk_tree_view_get_selection (dialog->priv->list_view);
	gtk_tree_selection_set_mode (dialog->priv->list_selection,
				     GTK_SELECTION_SINGLE);
	g_signal_connect (dialog->priv->list_selection, "changed",
			  G_CALLBACK (list_view_selection_changed), dialog);

	dialog->priv->list_store = gtk_list_store_new (
		EXCHANGE_PERMISSIONS_DIALOG_NUM_COLUMNS,
		G_TYPE_STRING, G_TYPE_STRING, E2K_TYPE_SID);
	gtk_tree_view_set_model (dialog->priv->list_view,
				 GTK_TREE_MODEL (dialog->priv->list_store));

	button = glade_xml_get_widget (xml, "add_button");
	g_signal_connect (button, "clicked",
			  G_CALLBACK (add_clicked), dialog);
	button = glade_xml_get_widget (xml, "remove_button");
	g_signal_connect (button, "clicked",
			  G_CALLBACK (remove_clicked), dialog);

	GET_WIDGET (role_optionmenu, GTK_OPTION_MENU);
	g_signal_connect (dialog->priv->role_optionmenu, "changed",
			  G_CALLBACK (role_changed), dialog);
	menu = gtk_option_menu_get_menu (dialog->priv->role_optionmenu);
	dialog->priv->separator = gtk_menu_item_new ();
	gtk_widget_set_sensitive (dialog->priv->separator, FALSE);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), dialog->priv->separator);
	dialog->priv->custom = gtk_menu_item_new_with_label (
		e2k_permissions_role_get_name (E2K_PERMISSIONS_ROLE_CUSTOM));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), dialog->priv->custom);

#define GET_TOGGLE(name, value, callback) \
	GET_WIDGET (name, GTK_TOGGLE_BUTTON); \
	g_object_set_data (G_OBJECT (dialog->priv->name), \
			   "mapi_permission", \
			   GUINT_TO_POINTER (value)); \
	g_signal_connect (dialog->priv->name, "toggled", \
			  G_CALLBACK (callback), dialog)

#define GET_CHECK(name, value) \
	GET_TOGGLE (name, value, check_toggled)

#define GET_RADIO(name, value, mask) \
	GET_TOGGLE (name, value, radio_toggled); \
	g_object_set_data (G_OBJECT (dialog->priv->name), \
			   "mapi_mask", \
			   GUINT_TO_POINTER (mask))

	GET_CHECK (read_items_check, E2K_PERMISSION_READ_ANY);
	GET_CHECK (create_items_check, E2K_PERMISSION_CREATE);
	GET_RADIO (edit_none_radio, 0, E2K_PERMISSION_EDIT_MASK);
	GET_RADIO (delete_none_radio, 0, E2K_PERMISSION_DELETE_MASK);
	GET_RADIO (edit_own_radio, E2K_PERMISSION_EDIT_OWNED, E2K_PERMISSION_EDIT_MASK);
	GET_RADIO (delete_own_radio, E2K_PERMISSION_DELETE_OWNED, E2K_PERMISSION_DELETE_MASK);
	GET_RADIO (edit_all_radio, (E2K_PERMISSION_EDIT_ANY | E2K_PERMISSION_EDIT_OWNED), E2K_PERMISSION_EDIT_MASK);
	GET_RADIO (delete_all_radio, (E2K_PERMISSION_DELETE_ANY | E2K_PERMISSION_DELETE_OWNED), E2K_PERMISSION_DELETE_MASK);
	GET_CHECK (create_subfolders_check, E2K_PERMISSION_CREATE_SUBFOLDER);
	GET_CHECK (folder_owner_check, E2K_PERMISSION_OWNER);
	GET_CHECK (folder_contact_check, E2K_PERMISSION_CONTACT);
	GET_CHECK (folder_visible_check, E2K_PERMISSION_FOLDER_VISIBLE);

	g_signal_connect (dialog->priv->folder_visible_check,
			  "toggled", G_CALLBACK (rv_toggle), dialog);
	g_signal_connect (dialog->priv->read_items_check,
			  "toggled", G_CALLBACK (rv_toggle), dialog);
}

GtkWidget *exchange_permissions_role_optionmenu_new (char *widget_name, char *string1, char *string2, int int1, int int2);

GtkWidget *
exchange_permissions_role_optionmenu_new (char *widget_name, char *string1, char *string2, int int1, int int2)
{
	GtkWidget *menu;
	const char **roles;
	int role;

	roles = g_new (const char *, E2K_PERMISSIONS_ROLE_NUM_ROLES + 1);
	for (role = 0; role < E2K_PERMISSIONS_ROLE_NUM_ROLES; role++)
		roles[role] = e2k_permissions_role_get_name (role);
	roles[role] = NULL;

	menu = e_option_menu_new_from_array (roles);
	g_free (roles);

	gtk_widget_show (menu);
	return menu;
}
