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

/* exchange-delegates-delegates: routines for the "delegates" page
 * of the delegates config control
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-delegates-control.h"
#include "exchange-account.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-user-dialog.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"

#include <e-util/e-dialog-utils.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktreeview.h>

extern const char *exchange_delegates_user_folder_names[];

const char *exchange_localfreebusy_path = "NON_IPM_SUBTREE/Freebusy%20Data/LocalFreebusy.EML";

static void user_edited (ExchangeDelegatesUser *user, gpointer data);

static void
set_sd_for_href (ExchangeDelegatesControl *control,
		 const char *href,
		 E2kSecurityDescriptor *sd)
{
	int i;

	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++) {
		if (!control->folder[i].uri)
			continue;

		if (!strcmp (href, control->folder[i].uri)) {
			control->folder[i].sd = sd;
			return;
		}
	}

	/* else, it's the freebusy folder */
	control->freebusy_folder.uri = g_strdup (href);
	control->freebusy_folder.sd = sd;
}

/* Given an array of ExchangeDelegatesUser containing display names
 * and entryids, and an array of E2kSecurityDescriptors containing
 * SIDs (which contain display names), add SIDs to the delegates. In
 * the easy case, we can just match the SIDs up with their
 * corresponding user by display name. However, there are two things
 * that can go wrong:
 *
 *   1. Some users may have been removed from the SDs
 *   2. Two users may have the same display name
 *
 * In both cases, we fall back to using the GC.
 */
static gboolean
fill_in_sids (ExchangeDelegatesControl *control)
{
	int u, u2, sd, needed_sids;
	ExchangeDelegatesUser *user, *user2;
	GList *sids, *s;
	E2kSid *sid;
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;
	gboolean ok = TRUE;

	needed_sids = 0;

	/* Mark users with duplicate names and count the number of
	 * non-duplicate names.
	 */
	for (u = 0; u < control->users->len; u++) {
		user = control->users->pdata[u];
		if (user->sid == (E2kSid *)-1)
			continue;
		for (u2 = u + 1; u2 < control->users->len; u2++) {
			user2 = control->users->pdata[u2];
			if (!strcmp (user->display_name, user2->display_name))
				user->sid = user2->sid = (E2kSid *)-1;
		}
		if (!user->sid)
			needed_sids++;
	}

	/* Scan security descriptors trying to match SIDs until we're
	 * not expecting to find any more.
	 */
	for (sd = 0; sd < EXCHANGE_DELEGATES_LAST && needed_sids; sd++) {
		sids = e2k_security_descriptor_get_sids (control->folder[sd].sd);
		for (s = sids; s && needed_sids; s = s->next) {
			sid = s->data;
			for (u = 0; u < control->users->len; u++) {
				user = control->users->pdata[u];
				if (user->sid)
					continue;
				if (!strcmp (user->display_name,
					     e2k_sid_get_display_name (sid))) {
					user->sid = sid;
					g_object_ref (sid);
					needed_sids--;
				}
			}
		}
		g_list_free (sids);
	}

	/* Now for each user whose SID hasn't yet been found, look it up. */
	gc = exchange_account_get_global_catalog (control->account);
	for (u = 0; u < control->users->len; u++) {
		user = control->users->pdata[u];
		if (user->sid && user->sid != (E2kSid *)-1)
			continue;

		status = e2k_global_catalog_lookup (
			gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
			e2k_entryid_to_dn (user->entryid),
			E2K_GLOBAL_CATALOG_LOOKUP_SID, &entry);
		if (status != E2K_GLOBAL_CATALOG_OK) {
			user->sid = NULL;
			ok = FALSE;
			continue;
		}
		user->sid = entry->sid;
		g_object_ref (user->sid);
		e2k_global_catalog_entry_free (gc, entry);
	}

	return ok;
}

static const char *sd_props[] = {
	E2K_PR_EXCHANGE_SD_BINARY,
	E2K_PR_EXCHANGE_SD_XML
};
static const int n_sd_props = sizeof (sd_props) / sizeof (sd_props[0]);

/* Read the folder security descriptors and match them up with the
 * list of delegates.
 */
static gboolean
get_folder_security (ExchangeDelegatesControl *control)
{
	GPtrArray *hrefs;
	E2kConnection *conn;
	E2kResult *results;
	int status, count, i, u;
	xmlNode *xml_form;
	GByteArray *binary_form;
	ExchangeDelegatesUser *user;
	guint32 perms;

	/* If we've been here before, just return the success or
	 * failure result from last time.
	 */
	if (control->freebusy_folder.uri)
		return control->loaded_folders;

	if (!exchange_account_get_global_catalog (control->account)) {
		e_notice (control->delegates_table, GTK_MESSAGE_ERROR,
			  _("No Global Catalog server configured for this account.\nUnable to edit delegates."));
		return FALSE;
	}

	conn = exchange_account_get_connection (control->account);

	hrefs = g_ptr_array_new ();
	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++) {
		control->folder[i].uri = exchange_account_get_standard_uri (
			control->account, exchange_delegates_user_folder_names[i]);
		if (control->folder[i].uri) {
			g_ptr_array_add (hrefs, (char *)e2k_uri_relative (
						 control->account->home_uri,
						 control->folder[i].uri));
		}
	}
	g_ptr_array_add (hrefs, (char *)exchange_localfreebusy_path);

	E2K_DEBUG_HINT ('S');
	status = e2k_connection_bpropfind_sync (
		conn, control->account->home_uri,
		(const char **)hrefs->pdata, hrefs->len, "0",
		sd_props, n_sd_props, &results, &count);
	g_ptr_array_free (hrefs, TRUE);

	if (status != SOUP_ERROR_DAV_MULTISTATUS) {
		e_notice (control->delegates_table, GTK_MESSAGE_ERROR,
			  _("Could not read folder permissions.\nUnable to edit delegates."));
		return FALSE;
	}

	for (i = 0; i < count; i++) {
		xml_form = e2k_properties_get_prop (results[i].props,
						    E2K_PR_EXCHANGE_SD_XML);
		binary_form = e2k_properties_get_prop (results[i].props,
						       E2K_PR_EXCHANGE_SD_BINARY);

		if (xml_form && binary_form) {
			set_sd_for_href (control, results[i].href,
					 e2k_security_descriptor_new (xml_form, binary_form));
		}
	}

	e2k_results_free (results, count);

	if (!fill_in_sids (control)) {
		control->loaded_folders = FALSE;
		e_notice (control->delegates_table, GTK_MESSAGE_ERROR,
			  _("Could not determine folder permissions for delegates.\nUnable to edit delegates."));
		return FALSE;
	}

	/* Fill in delegate structures from the security descriptors */
	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++) {
		for (u = 0; u < control->users->len; u++) {
			user = control->users->pdata[u];
			perms = e2k_security_descriptor_get_permissions (
				control->folder[i].sd, user->sid);
			user->role[i] = e2k_permissions_role_find (perms);
		}
	}

	control->loaded_folders = TRUE;
	return TRUE;
}


static const char *delegation_props[] = {
	PR_DELEGATES_DISPLAY_NAMES,
	PR_DELEGATES_ENTRYIDS,
	PR_DELEGATES_SEE_PRIVATE,
	PR_CREATOR_ENTRYID
};
static const int n_delegation_props = sizeof (delegation_props) / sizeof (delegation_props[0]);

/* Fetch the list of delegates from the freebusy message. */
static gboolean
get_user_list (ExchangeDelegatesControl *control)
{
	E2kConnection *conn;
	int status, count;
	E2kResult *results;
	GPtrArray *display_names, *entryids, *privflags;
	GByteArray *entryid;
	ExchangeDelegatesUser *user;
	int i;

	conn = exchange_account_get_connection (control->account);
	E2K_DEBUG_HINT ('S');
	status = e2k_connection_bpropfind_sync (conn, control->account->home_uri,
						&exchange_localfreebusy_path, 1, "0",
						delegation_props, n_delegation_props,
						&results, &count);

	if (status != SOUP_ERROR_DAV_MULTISTATUS)
		return FALSE;
	if (count != 1 || !SOUP_ERROR_IS_SUCCESSFUL (results[0].status)) {
		e2k_results_free (results, count);
		return FALSE;
	}

	control->users = g_ptr_array_new ();
	control->added_users = g_ptr_array_new ();
	control->removed_users = g_ptr_array_new ();

	display_names = e2k_properties_get_prop (results[0].props, PR_DELEGATES_DISPLAY_NAMES);
	entryids      = e2k_properties_get_prop (results[0].props, PR_DELEGATES_ENTRYIDS);
	privflags     = e2k_properties_get_prop (results[0].props, PR_DELEGATES_SEE_PRIVATE);

	entryid       = e2k_properties_get_prop (results[0].props, PR_CREATOR_ENTRYID);
	control->creator_entryid = g_byte_array_new ();
	g_byte_array_append (control->creator_entryid, entryid->data, entryid->len);

	if (!display_names || !entryids || !privflags) {
		e2k_results_free (results, count);
		return TRUE;
	}

	for (i = 0; i < display_names->len && i < entryids->len && i < privflags->len; i++) {
		user = exchange_delegates_user_new (display_names->pdata[i]);
		user->see_private  = privflags->pdata[i] && atoi (privflags->pdata[i]);
		entryid            = entryids->pdata[i];
		user->entryid      = g_byte_array_new ();
		g_byte_array_append (user->entryid, entryid->data, entryid->len);

		g_signal_connect (user, "edited", G_CALLBACK (user_edited), control);

		g_ptr_array_add (control->users, user);
	}

	e2k_results_free (results, count);
	return TRUE;
}

/* Add or remove a delegate. Everyone must be in one of three states:
 *   1. only in users (because they started and ended there)
 *   2. in users and added_users (because they weren't in
 *      users to begin with, but got added)
 *   3. only in removed_users (because they were in users to
 *      begin with and got removed).
 * If you're added and then removed, or removed and then added, you have
 * to end up in state 1. That's what this is for.
 */
static void
add_remove_user (ExchangeDelegatesUser *user, 
		 GPtrArray *to_array, GPtrArray *from_array)
{
	ExchangeDelegatesUser *match;
	int i;

	for (i = 0; i < from_array->len; i++) {
		match = from_array->pdata[i];
		if (e2k_sid_binary_sid_equal (e2k_sid_get_binary_sid (match->sid),
					      e2k_sid_get_binary_sid (user->sid))) {
			g_ptr_array_remove_index_fast (from_array, i);
			g_object_unref (match);
			return;
		}
	}

	g_ptr_array_add (to_array, user);
	g_object_ref (user);
}

static void
set_perms_for_user (ExchangeDelegatesControl *control,
		    ExchangeDelegatesUser *user)
{
	int i, role;
	guint32 perms;

	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++) {
		perms = e2k_permissions_role_get_perms (user->role[i]);
		e2k_security_descriptor_set_permissions (control->folder[i].sd,
							 user->sid, perms);
	}
	role = user->role[EXCHANGE_DELEGATES_CALENDAR];
	if (role == E2K_PERMISSIONS_ROLE_AUTHOR)
		role = E2K_PERMISSIONS_ROLE_EDITOR;
	perms = e2k_permissions_role_get_perms (role);
	e2k_security_descriptor_set_permissions (control->freebusy_folder.sd,
						 user->sid, perms);
}

static void
user_edited (ExchangeDelegatesUser *user, gpointer control)
{
	set_perms_for_user (control, user);
	evolution_config_control_changed (control);
}

static void
add_button_clicked_cb (GtkWidget *widget, gpointer data)
{
	ExchangeDelegatesControl *control = data;
	E2kGlobalCatalog *gc;
	GtkWidget *dialog, *parent_window;
	const char *delegate_exchange_dn;
	char *email;
	ExchangeDelegatesUser *user, *match;
	int response, u;
	GtkTreeIter iter;

	if (!get_folder_security (control))
		return;

	gc = exchange_account_get_global_catalog (control->account);

	parent_window = gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW);
	dialog = e2k_user_dialog_new (parent_window,
				      _("Delegate To:"), _("Delegate To"));
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response != GTK_RESPONSE_OK) {
		gtk_widget_destroy (dialog);
		return;
	}
	email = e2k_user_dialog_get_user (E2K_USER_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	if (email == NULL)
		return;

	user = exchange_delegates_user_new_from_gc (gc, email,
						    control->creator_entryid);
	if (!user) {
		e_notice (parent_window, GTK_MESSAGE_ERROR,
			  _("Could not make %s a delegate"), email);
		g_free (email);
		return;
	}
	g_free (email);

	delegate_exchange_dn = e2k_entryid_to_dn (user->entryid);
	if (delegate_exchange_dn && !g_ascii_strcasecmp (delegate_exchange_dn, control->account->legacy_exchange_dn)) {
		g_object_unref (user);
		e_notice (parent_window, GTK_MESSAGE_ERROR,
			  _("You cannot make yourself your own delegate"));
		return;
	}

	for (u = 0; u < control->users->len; u++) {
		match = control->users->pdata[u];
		if (e2k_sid_binary_sid_equal (e2k_sid_get_binary_sid (user->sid),
					      e2k_sid_get_binary_sid (match->sid))) {
			e_notice (parent_window, GTK_MESSAGE_INFO,
				  _("%s is already a delegate"),
				  user->display_name);
			g_object_unref (user);
			exchange_delegates_user_edit (match, parent_window);
			return;
		}
	}

	if (!exchange_delegates_user_edit (user, parent_window)) {
		g_object_unref (user);
		return;
	}
	user_edited (user, control);
	g_signal_connect (user, "edited", G_CALLBACK (user_edited), control);

	add_remove_user (user, control->added_users, control->removed_users);
	g_ptr_array_add (control->users, user);

	/* Add the user to the table */
	gtk_list_store_append (control->delegates_model, &iter);
	gtk_list_store_set (control->delegates_model, &iter,
			    0, user->display_name,
			    -1);
}

static int
get_selected_row (GtkWidget *tree_view, GtkTreeIter *iter)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreePath *path;
	int *indices, row;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
	if (!gtk_tree_selection_get_selected (selection, &model, iter))
		return -1;

	path = gtk_tree_model_get_path (model, iter);
	indices = gtk_tree_path_get_indices (path);
	row = indices[0];
	gtk_tree_path_free (path);

	return row;
}

static void
edit_button_clicked_cb (GtkWidget *widget, gpointer data)
{
	ExchangeDelegatesControl *control = data;
	GtkWidget *parent_window;
	GtkTreeIter iter;
	int row;

	if (!get_folder_security (control))
		return;

	row = get_selected_row (control->delegates_table, &iter);
	g_return_if_fail (row >= 0 && row < control->users->len);

	parent_window = gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW);
	exchange_delegates_user_edit (control->users->pdata[row],
				      parent_window);
}

static gboolean
table_button_cb (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	ExchangeDelegatesControl *control = data;
	GtkWidget *parent_window;
	GtkTreeIter iter;
	int row;

	if (event->type != GDK_2BUTTON_PRESS)
		return FALSE;

	row = get_selected_row (control->delegates_table, &iter);
	if (row < 0 || row >= control->users->len)
		return FALSE;

	if (!get_folder_security (control))
		return FALSE;

	parent_window = gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW);
	exchange_delegates_user_edit (control->users->pdata[row],
				      parent_window);
	return TRUE;
}

static void
delete_button_clicked_cb (GtkWidget *widget, gpointer data)
{
	ExchangeDelegatesControl *control = data;
	ExchangeDelegatesUser *user;
	GtkWidget *dialog;
	int row, btn, i;
	GtkTreeIter iter;

	if (!get_folder_security (control))
		return;

	row = get_selected_row (control->delegates_table, &iter);
	g_return_if_fail (row >= 0 && row < control->users->len);

	user = control->users->pdata[row];

	dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 _("Delete the delegate %s?"),
					 user->display_name);
	e_dialog_set_transient_for (GTK_WINDOW (dialog), widget); 

	btn = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	if (btn != GTK_RESPONSE_YES)
		return;

	add_remove_user (user, control->removed_users, control->added_users);

	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++) {
		e2k_security_descriptor_remove_sid (control->folder[i].sd,
						    user->sid);
	}
	e2k_security_descriptor_remove_sid (control->freebusy_folder.sd,
					    user->sid);

	/* Remove the user from the table */
	gtk_list_store_remove (control->delegates_model, &iter);
	g_ptr_array_remove_index (control->users, row);
	g_object_unref (user);

	evolution_config_control_changed (EVOLUTION_CONFIG_CONTROL (control));
}

void
exchange_delegates_delegates_construct (ExchangeDelegatesControl *control)
{
	GtkWidget *hbox, *button;
	ExchangeDelegatesUser *user;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	int i;

	hbox = glade_xml_get_widget (control->xml, "delegates_hbox");

	/* Set up the buttons */
	button = glade_xml_get_widget (control->xml, "add_button");
	g_signal_connect (button, "clicked",
			  G_CALLBACK (add_button_clicked_cb), control);
	button = glade_xml_get_widget (control->xml, "edit_button");
	g_signal_connect (button, "clicked",
			  G_CALLBACK (edit_button_clicked_cb), control);
	button = glade_xml_get_widget (control->xml, "delete_button");
	g_signal_connect (button, "clicked",
			  G_CALLBACK (delete_button_clicked_cb), control);

	/* Set up the table */
	control->delegates_model = gtk_list_store_new (1, G_TYPE_STRING);
 	control->delegates_table = glade_xml_get_widget (control->xml, "delegates_table");
	column = gtk_tree_view_column_new_with_attributes (
		_("Name"), gtk_cell_renderer_text_new (), "text", 0, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (control->delegates_table),
				     column);
	gtk_tree_view_set_model (GTK_TREE_VIEW (control->delegates_table),
				 GTK_TREE_MODEL (control->delegates_model));

	/* Get list of delegate users */
	if (get_user_list (control)) {
		for (i = 0; i < control->users->len; i++) {
			user = control->users->pdata[i];

			gtk_list_store_append (control->delegates_model, &iter);
			gtk_list_store_set (control->delegates_model, &iter,
					    0, user->display_name,
					    -1);
		}
		g_signal_connect (control->delegates_table,
				  "button_press_event",
				  G_CALLBACK (table_button_cb), control);
	} else {
		button = glade_xml_get_widget (control->xml, "add_button");
		gtk_widget_set_sensitive (button, FALSE);
		button = glade_xml_get_widget (control->xml, "edit_button");
		gtk_widget_set_sensitive (button, FALSE);
		button = glade_xml_get_widget (control->xml, "delete_button");
		gtk_widget_set_sensitive (button, FALSE);

		gtk_list_store_append (control->delegates_model, &iter);
		gtk_list_store_set (control->delegates_model, &iter,
				    0, _("Error reading delegates list."),
				    -1);
	}
}


static gboolean
proppatch_sd (E2kConnection *conn, ExchangeDelegatesFolder *folder)
{
	GByteArray *binsd;
	E2kProperties *props;
	E2kResult *results;
	const char *href = "";
	int status, count;

	binsd = e2k_security_descriptor_to_binary (folder->sd);
	if (!binsd)
		return FALSE;

	props = e2k_properties_new ();
	e2k_properties_set_binary (props, E2K_PR_EXCHANGE_SD_BINARY, binsd);
	E2K_DEBUG_HINT ('S');
	status = e2k_connection_bproppatch_sync (conn, folder->uri,
						 &href, 1, props, FALSE,
						 &results, &count);
	e2k_properties_free (props);

	if (status == SOUP_ERROR_DAV_MULTISTATUS) {
		status = results[0].status;
		e2k_results_free (results, count);
	}
	return SOUP_ERROR_IS_SUCCESSFUL (status);
}

static gboolean
get_user_dn (E2kGlobalCatalog *gc, ExchangeDelegatesUser *user)
{
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;
	const char *exchange_dn;

	exchange_dn = e2k_entryid_to_dn (user->entryid);
	status = e2k_global_catalog_lookup (
		gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		exchange_dn, 0, &entry);
	if (status != E2K_GLOBAL_CATALOG_OK)
		return FALSE;

	user->dn = g_strdup (entry->dn);
	e2k_global_catalog_entry_free (gc, entry);
	return TRUE;
}

void
exchange_delegates_delegates_apply (ExchangeDelegatesControl *control)
{
	ExchangeDelegatesUser *user;
	E2kGlobalCatalog *gc;
	E2kConnection *conn;
	GPtrArray *display_names, *entryids, *privflags;
	GByteArray *entryid_dup;
	char *error = NULL;
	E2kProperties *props;
	int i, status, nresults;
	E2kResult *results;

	if (!control->loaded_folders)
		return;

	/* We can't do this atomically/transactionally, so we need to
	 * make sure that if we fail at any step, things are still in
	 * a semi-consistent state. So we do:
	 *
	 *   1. Remove old delegates from AD
	 *   2. Update LocalFreebusy.EML (the canonical list of delegates)
	 *   3. Add new delegates to AD
	 *   4. Update security descriptors
	 *
	 * If step 1 fails, nothing is changed.
	 *
	 * If step 2 fails, delegates who should have been removed
	 * will have been removed from AD but nothing else, so they
	 * will still show up as being delegates and the user can try
	 * to remove them again later.
	 *
	 * If step 3 fails, delegates who should have been added will
	 * not be in AD, but will be listed as delegates, so the user
	 * can remove them and try adding them again later.
	 *
	 * If step 4 fails, the user can still correct the folder
	 * permissions by hand.
	 */

	gc = exchange_account_get_global_catalog (control->account);
	if (!gc) {
		error = g_strdup (_("Could not access Active Directory"));
		goto done;
	}

	if ((control->removed_users || control->added_users) && !control->self_dn) {
		exchange_delegates_control_get_self_dn (control);
		if (!control->self_dn) {
			error = g_strdup (_("Could not find self in Active Directory"));
			goto done;
		}
	}

	/* 1. Remove old delegates from AD */
	while (control->removed_users && control->removed_users->len) {
		user = control->removed_users->pdata[0];
		if (!user->dn && !get_user_dn (gc, user)) {
			error = g_strdup_printf (
				_("Could not find delegate %s in Active Directory"),
				user->display_name);
			goto done;
		}

		status = e2k_global_catalog_remove_delegate (gc, control->self_dn,
							     user->dn);
		if (status != E2K_GLOBAL_CATALOG_OK &&
		    status != E2K_GLOBAL_CATALOG_NO_DATA) {
			error = g_strdup_printf (
				_("Could not remove delegate %s"),
				user->display_name);
			goto done;
		}

		g_object_unref (user);
		g_ptr_array_remove_index_fast (control->removed_users, 0);
	}

	/* 2. Update LocalFreebusy.EML */
	conn = exchange_account_get_connection (control->account);

	if (control->users->len) {
		display_names = g_ptr_array_new ();
		entryids = g_ptr_array_new ();
		privflags = g_ptr_array_new ();

		for (i = 0; i < control->users->len; i++) {
			user = control->users->pdata[i];
			g_ptr_array_add (display_names, g_strdup (user->display_name)); 
			entryid_dup = g_byte_array_new ();
			g_byte_array_append (entryid_dup, user->entryid->data,
					     user->entryid->len);
			g_ptr_array_add (entryids, entryid_dup);
			g_ptr_array_add (privflags, g_strdup_printf ("%d", user->see_private));
		}

		props = e2k_properties_new (); 
		e2k_properties_set_string_array (
			props, PR_DELEGATES_DISPLAY_NAMES, display_names);
		e2k_properties_set_binary_array (
			props, PR_DELEGATES_ENTRYIDS, entryids);
		e2k_properties_set_int_array (
			props, PR_DELEGATES_SEE_PRIVATE, privflags);
	} else if (control->removed_users) {
		props = e2k_properties_new (); 
		e2k_properties_remove (props, PR_DELEGATES_DISPLAY_NAMES);
		e2k_properties_remove (props, PR_DELEGATES_ENTRYIDS);
		e2k_properties_remove (props, PR_DELEGATES_SEE_PRIVATE);
	} else
		props = NULL;

	if (props) {
		status = e2k_connection_bproppatch_sync (
			conn, control->account->home_uri,
			&exchange_localfreebusy_path, 1,
			props, FALSE, &results, &nresults);
		e2k_properties_free (props);
		if (status == SOUP_ERROR_DAV_MULTISTATUS) {
			status = results[0].status;
			e2k_results_free (results, nresults);
		}
		if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
			error = g_strdup (_("Could not update list of delegates."));
			goto done;
		}
	}

	/* 3. Add new delegates to AD */
	while (control->added_users && control->added_users->len) {
		user = control->added_users->pdata[0];
		/* An added user must have come from the GC so
		 * we know user->dn is set.
		 */
		status = e2k_global_catalog_add_delegate (gc, control->self_dn,
							  user->dn);
		if (status != E2K_GLOBAL_CATALOG_OK &&
		    status != E2K_GLOBAL_CATALOG_EXISTS) {
			error = g_strdup_printf (
				_("Could not add delegate %s"),
				user->display_name);
			goto done;
		}
		g_ptr_array_remove_index_fast (control->added_users, 0);
		g_object_unref (user);
	}

	/* 4. Update security descriptors */
	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++)
		proppatch_sd (conn, &control->folder[i]);
	proppatch_sd (conn, &control->freebusy_folder);

 done:
	if (error) {
		e_notice (control->delegates_table, GTK_MESSAGE_ERROR,
			  _("Failed to update delegates:\n%s"), error);
		g_free (error);
	}
}

void
exchange_delegates_delegates_destroy (ExchangeDelegatesControl *control)
{
	int i;

	if (control->delegates_model) {
		g_object_unref (control->delegates_model);
		control->delegates_model = NULL;
	}

	if (control->creator_entryid) {
		g_byte_array_free (control->creator_entryid, TRUE);
		control->creator_entryid = NULL;
	}

	if (control->users) {
		for (i = 0; i < control->users->len; i++)
			g_object_unref (control->users->pdata[i]);
		g_ptr_array_free (control->users, TRUE);
		control->users = NULL;
	}
	if (control->added_users) {
		for (i = 0; i < control->added_users->len; i++)
			g_object_unref (control->added_users->pdata[i]);
		g_ptr_array_free (control->added_users, TRUE);
		control->added_users = NULL;
	}
	if (control->removed_users) {
		for (i = 0; i < control->removed_users->len; i++)
			g_object_unref (control->removed_users->pdata[i]);
		g_ptr_array_free (control->removed_users, TRUE);
		control->removed_users = NULL;
	}

	for (i = 0; i < EXCHANGE_DELEGATES_LAST; i++) {
		if (control->folder[i].sd) {
			g_object_unref (control->folder[i].sd);
			control->folder[i].sd = NULL;
		}
	}
	if (control->freebusy_folder.sd) {
		g_object_unref (control->freebusy_folder.sd);
		control->freebusy_folder.sd = NULL;
	}
	if (control->freebusy_folder.uri) {
		g_free ((char *)control->freebusy_folder.uri);
		control->freebusy_folder.uri = NULL;
	}

}
