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

/* mail-stub-exchange.c: an Exchange implementation of MailStub */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "mail-stub-exchange.h"
#include "mail-utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "e-folder-exchange.h"
#include "camel-stub-constants.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "exchange-hierarchy.h"
#include "mapi.h"

#define PARENT_TYPE MAIL_TYPE_STUB
static MailStubClass *parent_class = NULL;

typedef struct {
	char *uid, *href;
	guint32 seq, flags;
	guint32 change_flags, change_mask;
	GData *tag_updates;
} MailStubExchangeMessage;

typedef enum {
	MAIL_STUB_EXCHANGE_FOLDER_REAL,
	MAIL_STUB_EXCHANGE_FOLDER_POST,
	MAIL_STUB_EXCHANGE_FOLDER_NOTES,
	MAIL_STUB_EXCHANGE_FOLDER_OTHER
} MailStubExchangeFolderType;

typedef struct {
	MailStubExchange *mse;

	EFolder *folder;
	const char *name;
	MailStubExchangeFolderType type;
	guint32 access;

	GPtrArray *messages;
	GHashTable *messages_by_uid, *messages_by_href;
	guint32 seq, high_article_num, deleted_count;

	guint32 unread_count;
	gboolean scanned;

	GPtrArray *changed_messages;
	guint flag_timeout, pending_delete_ops;

	time_t last_activity;
	guint sync_deletion_timeout;
} MailStubExchangeFolder;

static void dispose (GObject *);

static void stub_connect (MailStub *stub);
static void get_folder (MailStub *stub, const char *name,
			GPtrArray *uids, GByteArray *flags);
static void get_trash_name (MailStub *stub);
static void sync_folder (MailStub *stub, const char *folder_name);
static void refresh_folder (MailStub *stub, const char *folder_name);
static void refresh_folder_internal (MailStub *stub, MailStubExchangeFolder *mfld,
				     gboolean background);
static void sync_deletions (MailStubExchange *mse, MailStubExchangeFolder *mfld);
static void expunge_uids (MailStub *stub, const char *folder_name, GPtrArray *uids);
static void append_message (MailStub *stub, const char *folder_name, guint32 flags,
			    const char *subject, const char *data, int length);
static void set_message_flags (MailStub *, const char *folder_name,
			       const char *uid, guint32 flags, guint32 mask);
static void set_message_tag (MailStub *, const char *folder_name,
			     const char *uid, const char *name, const char *value);
static void get_message (MailStub *stub, const char *folder_name, const char *uid);
static void search (MailStub *stub, const char *folder_name, const char *text);
static void transfer_messages (MailStub *stub, const char *source_name,
			       const char *dest_name, GPtrArray *uids,
			       gboolean delete_originals);
static void get_folder_info (MailStub *stub);
static void send_message (MailStub *stub, const char *from,
			  GPtrArray *recipients,
			  const char *data, int length);

static gboolean process_flags (gpointer user_data);

static void storage_folder_changed (EFolder *folder, gpointer user_data);

static void
class_init (GObjectClass *object_class)
{
	MailStubClass *stub_class = MAIL_STUB_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;

	stub_class->connect = stub_connect;
	stub_class->get_folder = get_folder;
	stub_class->get_trash_name = get_trash_name;
	stub_class->sync_folder = sync_folder;
	stub_class->refresh_folder = refresh_folder;
	stub_class->expunge_uids = expunge_uids;
	stub_class->append_message = append_message;
	stub_class->set_message_flags = set_message_flags;
	stub_class->set_message_tag = set_message_tag;
	stub_class->get_message = get_message;
	stub_class->search = search;
	stub_class->transfer_messages = transfer_messages;
	stub_class->get_folder_info = get_folder_info;
	stub_class->send_message = send_message;
}

static void
init (GObject *object)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (object);

	mse->folders_by_name = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
free_message (MailStubExchangeMessage *mmsg)
{
	g_datalist_clear (&mmsg->tag_updates);
	g_free (mmsg->uid);
	g_free (mmsg->href);
	g_free (mmsg);
}

static void
free_folder (gpointer key, gpointer value, gpointer conn)
{
	MailStubExchangeFolder *mfld = value;
	int i;

	e_folder_exchange_unsubscribe (mfld->folder);
	g_signal_handlers_disconnect_by_func (mfld->folder, storage_folder_changed, mfld);
	g_object_unref (mfld->folder);

	for (i = 0; i < mfld->messages->len; i++)
		free_message (mfld->messages->pdata[i]);
	g_ptr_array_free (mfld->messages, TRUE);
	g_hash_table_destroy (mfld->messages_by_uid);
	g_hash_table_destroy (mfld->messages_by_href);

	g_ptr_array_free (mfld->changed_messages, TRUE);
	if (mfld->flag_timeout) {
		g_warning ("unreffing mse with unsynced flags");
		g_source_remove (mfld->flag_timeout);
	}
	if (mfld->sync_deletion_timeout)
		g_source_remove (mfld->sync_deletion_timeout);
	g_free (mfld);
}

static void
dispose (GObject *object)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (object);

	if (mse->folders_by_name) {
		g_hash_table_foreach (mse->folders_by_name, free_folder,
				      mse->conn);
		g_hash_table_destroy (mse->folders_by_name);
		mse->folders_by_name = NULL;
	}

	if (mse->conn) {
		g_object_unref (mse->conn);
		mse->conn = NULL;
	}

	if (mse->new_folder_id != 0) {
		g_signal_handler_disconnect (mse->account, mse->new_folder_id);
		mse->new_folder_id = 0;
		g_signal_handler_disconnect (mse->account, mse->removed_folder_id);
		mse->removed_folder_id = 0;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

E2K_MAKE_TYPE (mail_stub_exchange, MailStubExchange, class_init, init, PARENT_TYPE)


static MailStubExchangeFolder *
folder_from_name (MailStubExchange *mse, const char *folder_name,
		  guint32 perms, gboolean background)
{
	MailStubExchangeFolder *mfld;

	mfld = g_hash_table_lookup (mse->folders_by_name, folder_name);
	if (!mfld) {
		if (!background)
			mail_stub_return_error (MAIL_STUB (mse), _("No such folder"));
		return NULL;
	}

	/* If sync_deletion_timeout is set, that means the user has been
	 * idle in Evolution for longer than a minute, during which
	 * time he has deleted messages using another email client,
	 * which we haven't bothered to sync up with yet. Do that now.
	 */
	if (mfld->sync_deletion_timeout) {
		g_source_remove (mfld->sync_deletion_timeout);
		mfld->sync_deletion_timeout = 0;
		sync_deletions (mse, mfld);
	}

	if (perms && !(mfld->access & perms)) {
		if (!background)
			mail_stub_return_error (MAIL_STUB (mse), _("Permission denied"));
		return NULL;
	}

	mfld->last_activity = time (NULL);
	return mfld;
}

static GHashTable *changes;

static gboolean
do_change (gpointer key, gpointer folder, gpointer account)
{
	exchange_account_update_folder (account, folder);
	return TRUE;
}

static gboolean
idle_update_folders (gpointer user_data)
{
	MailStubExchange *mse = user_data;
	static gboolean in_update; /* FIXME; should be per-mse */

	/* exchange_account_update_folder will eventually call
	 * GNOME_Evolution_StorageListener_notifyFolderUpdated. That
	 * may block and run the main loop from inside ORBit, which
	 * may cause this function to be re-entered if multiple folder
	 * changes occur close together. The problem is, if you make
	 * another call into ORBit while one is already being
	 * processed, there's no guarantee on the order they'll finish
	 * in, which could result in the newer notifyFolderUpdated
	 * finishing before the older one, resulting in an incorrect
	 * unread count on the folder. So, if we're re-entered, bail
	 * out and tell glib to call again later.
	 */
	if (in_update)
		return TRUE;

	in_update = TRUE;
	g_hash_table_foreach_remove (changes, do_change, mse->account);
	g_object_unref (mse);
	in_update = FALSE;

	return FALSE;
}

static void
folder_changed (MailStubExchangeFolder *mfld)
{
	/* Like Outlook, we don't track unread on public folders */
	if (mfld->type == MAIL_STUB_EXCHANGE_FOLDER_POST)
		return;

	/* If we haven't gone through refresh_folder yet then our
	 * count is most likely innacurate.
	 */
	if (!mfld->scanned)
		return;

	if (!changes)
		changes = g_hash_table_new (NULL, NULL);
	if (g_hash_table_size (changes) == 0) {
		g_idle_add (idle_update_folders, mfld->mse);
		g_object_ref (mfld->mse);
	}

	e_folder_set_unread_count (mfld->folder, mfld->unread_count);
	g_hash_table_insert (changes, mfld->folder, mfld->folder);
}

static int
find_message_index (MailStubExchangeFolder *mfld, int seq)
{
	MailStubExchangeMessage *mmsg;
	int low, high, mid;

	low = 0;
	high = mfld->messages->len - 1;

	while (low <= high) {
		mid = (low + high) / 2;
		mmsg = mfld->messages->pdata[mid];
		if (seq == mmsg->seq)
			return mid;
		else if (seq < mmsg->seq)
			high = mid - 1;
		else
			low = mid + 1;
	}

	return -1;
}

static inline MailStubExchangeMessage *
find_message (MailStubExchangeFolder *mfld, const char *uid)
{
	return g_hash_table_lookup (mfld->messages_by_uid, uid);
}

static inline MailStubExchangeMessage *
find_message_by_href (MailStubExchangeFolder *mfld, const char *href)
{
	return g_hash_table_lookup (mfld->messages_by_href, href);
}

static MailStubExchangeMessage *
new_message (const char *uid, const char *uri, guint32 seq, guint32 flags)
{
	MailStubExchangeMessage *mmsg;

	mmsg = g_new0 (MailStubExchangeMessage, 1);
	mmsg->uid = g_strdup (uid);
	mmsg->href = g_strdup (uri);
	mmsg->seq = seq;
	mmsg->flags = flags;

	return mmsg;
}

static void
message_remove_at_index (MailStub *stub, MailStubExchangeFolder *mfld, int index)
{
	MailStubExchangeMessage *mmsg;

	mmsg = mfld->messages->pdata[index];
	g_ptr_array_remove_index (mfld->messages, index);
	g_hash_table_remove (mfld->messages_by_uid, mmsg->uid);
	if (mmsg->href)
		g_hash_table_remove (mfld->messages_by_href, mmsg->href);
	if (!(mmsg->flags & MAIL_STUB_MESSAGE_SEEN)) {
		mfld->unread_count--;
		folder_changed (mfld);
	}

	if (mmsg->change_mask || mmsg->tag_updates) {
		int i;

		for (i = 0; i < mfld->changed_messages->len; i++) {
			if (mfld->changed_messages->pdata[i] == (gpointer)mmsg) {
				g_ptr_array_remove_index_fast (mfld->changed_messages, i);
				break;
			}
		}
		g_datalist_clear (&mmsg->tag_updates);
	}

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_REMOVED_MESSAGE,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_STRING, mmsg->uid,
			       CAMEL_STUB_ARG_END);

	g_free (mmsg->uid);
	g_free (mmsg->href);
	g_free (mmsg);
}

static void
message_removed (MailStub *stub, MailStubExchangeFolder *mfld, const char *href)
{
	MailStubExchangeMessage *mmsg;
	guint index;

	mmsg = g_hash_table_lookup (mfld->messages_by_href, href);
	if (!mmsg)
		return;
	index = find_message_index (mfld, mmsg->seq);
	g_return_if_fail (index != -1);

	message_remove_at_index (stub, mfld, index);
}

static void
return_tag (MailStubExchangeFolder *mfld, const char *uid,
	    const char *name, const char *value)
{
	mail_stub_return_data (MAIL_STUB (mfld->mse),
			       CAMEL_STUB_RETVAL_CHANGED_TAG,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_STRING, uid,
			       CAMEL_STUB_ARG_STRING, name,
			       CAMEL_STUB_ARG_STRING, value,
			       CAMEL_STUB_ARG_END);
}

static void
change_flags (MailStubExchangeFolder *mfld, MailStubExchangeMessage *mmsg,
	      guint32 new_flags)
{
	if ((mmsg->flags ^ new_flags) & MAIL_STUB_MESSAGE_SEEN) {
		if (mmsg->flags & MAIL_STUB_MESSAGE_SEEN)
			mfld->unread_count++;
		else
			mfld->unread_count--;
		folder_changed (mfld);
	}
	mmsg->flags = new_flags;

	mail_stub_return_data (MAIL_STUB (mfld->mse),
			       CAMEL_STUB_RETVAL_CHANGED_FLAGS,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_STRING, mmsg->uid,
			       CAMEL_STUB_ARG_UINT32, mmsg->flags,
			       CAMEL_STUB_ARG_END);
}

static const char *
uidstrip (const char *repl_uid)
{
	/* The first two cases are just to prevent crashes in the face
	 * of extreme lossage. They shouldn't ever happen, and the
	 * rest of the code probably won't work right if they do.
	 */
	if (strncmp (repl_uid, "rid:", 4))
		return repl_uid;
	else if (strlen (repl_uid) < 36)
		return repl_uid;
	else
		return repl_uid + 36;
}

#define FIVE_SECONDS (5)
#define  ONE_MINUTE  (60)
#define FIVE_MINUTES (60*5)

static gboolean
timeout_sync_deletions (gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;

	sync_deletions (mfld->mse, mfld);
	return FALSE;
}

static void
notify_cb (E2kConnection *conn, const char *uri,
	   E2kConnectionChangeType type, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	time_t now;

	if (type == E2K_CONNECTION_OBJECT_ADDED)
		refresh_folder_internal (MAIL_STUB (mfld->mse), mfld, TRUE);
	else {
		now = time (NULL);

		/* If the user did something in Evolution in the
		 * last 5 seconds, assume that this notification is
		 * a result of that and ignore it.
		 */
		if (now < mfld->last_activity + FIVE_SECONDS)
			return;

		/* sync_deletions() is somewhat server-intensive, so
		 * we don't want to run it unnecessarily. In
		 * particular, if the user leaves Evolution running,
		 * goes home for the night, and then reads mail from
		 * home, we don't want to run sync_deletions() every
		 * time the user deletes a message; we just need to
		 * make sure we do it by the time the user gets back
		 * in the morning. On the other hand, if the user just
		 * switches to Outlook for just a moment and then
		 * comes back, we'd like to update fairly quickly.
		 *
		 * So, if the user has been idle for less than a
		 * minute, we update right away. Otherwise, we set a
		 * timer, and keep resetting it with each new
		 * notification, meaning we (hopefully) only sync
		 * after the user stops changing things.
		 *
		 * If the user returns to Evolution while we have a
		 * timer set, then folder_from_name() will immediately
		 * call sync_deletions.
		 */

		if (mfld->sync_deletion_timeout) {
			g_source_remove (mfld->sync_deletion_timeout);
			mfld->sync_deletion_timeout = 0;
		}

		if (now < mfld->last_activity + ONE_MINUTE)
			sync_deletions (mfld->mse, mfld);
		else if (now < mfld->last_activity + FIVE_MINUTES) {
			mfld->sync_deletion_timeout =
				g_timeout_add (ONE_MINUTE * 1000,
					       timeout_sync_deletions,
					       mfld);
		} else {
			mfld->sync_deletion_timeout =
				g_timeout_add (FIVE_MINUTES * 1000,
					       timeout_sync_deletions,
					       mfld);
		}
	}
}

static void
storage_folder_changed (EFolder *folder, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;

	if (e_folder_get_unread_count (folder) > mfld->unread_count)
		refresh_folder_internal (MAIL_STUB (mfld->mse), mfld, TRUE);
}

static void
got_folder_error (MailStubExchangeFolder *mfld, const char *error)
{
	mail_stub_return_error (MAIL_STUB (mfld->mse), error);
	free_folder (NULL, mfld, NULL);
}

static void
got_folder (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStubExchange *mse = mfld->mse;
	MailStubExchangeMessage *mmsg;
	ExchangeHierarchy *hier;
	int flags, i;

	if (SOUP_MESSAGE_IS_ERROR (msg)) {
		g_warning ("got_folder: %d %s", msg->errorcode, msg->errorphrase);
		got_folder_error (mfld, _("Could not open folder"));
		return;
	}

	/* Discard remaining messages that no longer exist. */
	for (i = 0; i < mfld->messages->len; i++) {
		mmsg = mfld->messages->pdata[i];
		if (!mmsg->href)
			message_remove_at_index (MAIL_STUB (mse), mfld, i--);
	}

	e_folder_exchange_subscribe (mfld->folder,
				     E2K_CONNECTION_OBJECT_ADDED, 30,
				     notify_cb, mfld);
	e_folder_exchange_subscribe (mfld->folder,
				     E2K_CONNECTION_OBJECT_REMOVED, 30,
				     notify_cb, mfld);
	e_folder_exchange_subscribe (mfld->folder,
				     E2K_CONNECTION_OBJECT_MOVED, 30,
				     notify_cb, mfld);
	g_signal_connect (mfld->folder, "changed",
			  G_CALLBACK (storage_folder_changed), mfld);

	g_hash_table_insert (mse->folders_by_name, (char *)mfld->name, mfld);
	folder_changed (mfld);

	flags = 0;
	if ((mfld->access & MAPI_ACCESS_MODIFY) == 0)
		flags |= CAMEL_STUB_FOLDER_READONLY;
	if (mse->account->filter_inbox && (mfld->folder == mse->inbox))
		flags |= CAMEL_STUB_FOLDER_FILTER;
	if (mfld->type == MAIL_STUB_EXCHANGE_FOLDER_POST)
		flags |= CAMEL_STUB_FOLDER_POST;

	hier = e_folder_exchange_get_hierarchy (mfld->folder);

	mail_stub_return_data (MAIL_STUB (mse), CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_UINT32, flags,
			       CAMEL_STUB_ARG_STRING, hier->source_uri,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (MAIL_STUB (mse));
}

static int
getting_folder (E2kConnection *conn, SoupMessage *msg,
		E2kResult *results, int nresults,
		int first, int total, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStubExchange *mse = mfld->mse;
	MailStubExchangeMessage *mmsg;
	const char *prop, *uid;
	guint32 article_num, flags;
	gboolean readonly = (mfld->access & MAPI_ACCESS_MODIFY) == 0;
	int m, i;

	for (m = first, i = 0; m < mfld->messages->len && i < nresults; i++) {
		prop = e2k_properties_get_prop (results[i].props, PR_INTERNET_ARTICLE_NUMBER);
		if (!prop)
			continue;
		article_num = strtoul (prop, NULL, 10);

		prop = e2k_properties_get_prop (results[i].props, E2K_PR_REPL_UID);
		if (!prop)
			continue;
		uid = uidstrip (prop);

		flags = mail_util_props_to_camel_flags (results[i].props,
							!readonly);

		mmsg = mfld->messages->pdata[m];
		while (strcmp (uid, mmsg->uid)) {
			message_remove_at_index (MAIL_STUB (mse), mfld, m);
			if (m == mfld->messages->len) {
				mmsg = NULL;
				break;
			}
			mmsg = mfld->messages->pdata[m];
		}
		if (!mmsg)
			break;

		mmsg->href = g_strdup (results[i].href);
		g_hash_table_insert (mfld->messages_by_href, mmsg->href, mmsg);
		if (article_num > mfld->high_article_num)
			mfld->high_article_num = article_num;
		if (mmsg->flags != flags)
			change_flags (mfld, mmsg, flags);

		prop = e2k_properties_get_prop (results[i].props, E2K_PR_HTTPMAIL_MESSAGE_FLAG);
		if (prop)
			return_tag (mfld, mmsg->uid, "follow-up", prop);
		prop = e2k_properties_get_prop (results[i].props, E2K_PR_MAILHEADER_REPLY_BY);
		if (prop)
			return_tag (mfld, mmsg->uid, "due-by", prop);
		prop = e2k_properties_get_prop (results[i].props, E2K_PR_MAILHEADER_COMPLETED);
		if (prop)
			return_tag (mfld, mmsg->uid, "completed-on", prop);

		m++;
	}

	if (i < nresults) {
		/* There are messages that Camel doesn't know about
		 * and which we are therefore ignoring for a while. If
		 * any of them have an article number lower than the
		 * highest article number we've seen, bump
		 * high_article_num down so that that message gets
		 * caught by refresh_info later too.
		 */

		for (; i < nresults; i++) {
			prop = e2k_properties_get_prop (results[i].props, PR_INTERNET_ARTICLE_NUMBER);
			if (prop) {
				article_num = strtoul (prop, NULL, 10);
				if (article_num < mfld->high_article_num)
					mfld->high_article_num = article_num - 1;
			}
		}
	}

	mail_stub_return_progress (MAIL_STUB (mse), (m * 100) / total);

	return nresults;
}

static const char *open_folder_sync_props[] = {
	E2K_PR_REPL_UID,
	PR_INTERNET_ARTICLE_NUMBER,
	PR_ACTION_FLAG,
	PR_DELEGATED_BY_RULE,
	E2K_PR_HTTPMAIL_READ,
	E2K_PR_HTTPMAIL_MESSAGE_FLAG,
	E2K_PR_MAILHEADER_REPLY_BY,
	E2K_PR_MAILHEADER_COMPLETED
};
static const int n_open_folder_sync_props = sizeof (open_folder_sync_props) / sizeof (open_folder_sync_props[0]);

static void
got_folder_props (E2kConnection *conn, SoupMessage *msg,
		  E2kResult *results, int nresults,
		  gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	const char *prop;
	E2kRestriction *rn;

	if (SOUP_MESSAGE_IS_ERROR (msg) &&
	    msg->errorcode != SOUP_ERROR_CANT_AUTHENTICATE) {
		g_warning ("got_folder_props: %d %s", msg->errorcode, msg->errorphrase);
		got_folder_error (mfld, _("Could not open folder"));
		return;
	}

	if (nresults) {
		prop = e2k_properties_get_prop (results[0].props, PR_ACCESS);
		if (prop)
			mfld->access = atoi (prop);
		else
			mfld->access = ~0;
	} else if (msg->errorcode == SOUP_ERROR_CANT_AUTHENTICATE)
		mfld->access = 0;
	else
		mfld->access = ~0;

	if (!(mfld->access & MAPI_ACCESS_READ)) {
		got_folder_error (mfld, _("Could not open folder: Permission denied"));
		return;
	}

	prop = e2k_properties_get_prop (results[0].props, PR_DELETED_COUNT_TOTAL);
	if (prop)
		mfld->deleted_count = atoi (prop);

	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
					   E2K_RELOP_EQ, FALSE),
		NULL);

	E2K_DEBUG_HINT ('M');
	e_folder_exchange_search_with_progress (mfld->folder,
						open_folder_sync_props,
						n_open_folder_sync_props,
						rn, E2K_PR_DAV_CREATION_DATE,
						50, TRUE, getting_folder,
						got_folder, mfld);
	e2k_restriction_unref (rn);
}

static const char *open_folder_props[] = {
	PR_ACCESS,
	PR_DELETED_COUNT_TOTAL
};
static const int n_open_folder_props = sizeof (open_folder_props) / sizeof (open_folder_props[0]);

static void
get_folder (MailStub *stub, const char *name,
	    GPtrArray *uids, GByteArray *flags)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;
	EFolder *folder;
	char *path;
	const char *outlook_class;
	int i;

	path = g_strdup_printf ("/%s", name);
	folder = exchange_account_get_folder (mse->account, path);
	g_free (path);
	if (!folder) {
		mail_stub_return_error (stub, _("No such folder."));
		return;
	}

	mfld = g_new0 (MailStubExchangeFolder, 1);
	mfld->mse = MAIL_STUB_EXCHANGE (stub);
	mfld->folder = folder;
	g_object_ref (folder);
	mfld->name = e_folder_exchange_get_path (folder) + 1;

	if (!strcmp (e_folder_get_type_string (folder), "mail/public"))
		mfld->type = MAIL_STUB_EXCHANGE_FOLDER_POST;
	else {
		outlook_class = e_folder_exchange_get_outlook_class (folder);
		if (!outlook_class)
			mfld->type = MAIL_STUB_EXCHANGE_FOLDER_OTHER;
		else if (!g_ascii_strncasecmp (outlook_class, "IPF.Note", 8))
			mfld->type = MAIL_STUB_EXCHANGE_FOLDER_REAL;
		else if (!g_ascii_strncasecmp (outlook_class, "IPF.Post", 8))
			mfld->type = MAIL_STUB_EXCHANGE_FOLDER_POST;
		else if (!g_ascii_strncasecmp (outlook_class, "IPF.StickyNote", 14))
			mfld->type = MAIL_STUB_EXCHANGE_FOLDER_NOTES;
		else
			mfld->type = MAIL_STUB_EXCHANGE_FOLDER_OTHER;
	}

	mfld->messages = g_ptr_array_new ();
	mfld->messages_by_uid = g_hash_table_new (g_str_hash, g_str_equal);
	mfld->messages_by_href = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < uids->len; i++) {
		mmsg = new_message (uids->pdata[i], NULL, mfld->seq++, flags->data[i]);
		g_ptr_array_add (mfld->messages, mmsg);
		g_hash_table_insert (mfld->messages_by_uid, mmsg->uid, mmsg);
		if (!(mmsg->flags & MAIL_STUB_MESSAGE_SEEN))
			mfld->unread_count++;
	}
	mfld->changed_messages = g_ptr_array_new ();

	E2K_DEBUG_HINT ('M');
	e_folder_exchange_propfind (mfld->folder, "0",
				    open_folder_props, n_open_folder_props,
				    got_folder_props, mfld);
}

static void
get_trash_name (MailStub *stub)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);

	if (!mse->deleted_items) {
		mail_stub_return_error (stub, _("Could not open Deleted Items folder"));
		return;
	}

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRING, e_folder_exchange_get_path (mse->deleted_items) + 1,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);
}

static void
sync_folder (MailStub *stub, const char *folder_name)
{
	MailStubExchangeFolder *mfld;

	mfld = folder_from_name (MAIL_STUB_EXCHANGE (stub), folder_name, 0, FALSE);
	if (!mfld)
		return;

	while (mfld->flag_timeout)
		process_flags (mfld);
	while (mfld->pending_delete_ops)
		g_main_context_iteration (NULL, TRUE);

	mail_stub_return_ok (stub);
}

struct sync_deleted_data {
	MailStubExchangeFolder *mfld;
	int new_deleted_count, highest_unverified_index, highest_verified_seq;
	gboolean changes;
};

static void
synced_deleted (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	struct sync_deleted_data *sdd = user_data;
	MailStubExchangeFolder *mfld = sdd->mfld;
	MailStubExchangeMessage *mmsg;

	if (SOUP_MESSAGE_IS_ERROR (msg)) {
		g_warning ("synced_deleted: %d %s", msg->errorcode, msg->errorphrase);
	}

	/* Clear out the remaining messages in mfld */
	while (sdd->highest_unverified_index > -1) {
		mfld->deleted_count++;
		mmsg = mfld->messages->pdata[sdd->highest_unverified_index--];
		message_removed (MAIL_STUB (mfld->mse), mfld, mmsg->href);
		sdd->changes = TRUE;
	}

	if (sdd->changes)
		mail_stub_push_changes (MAIL_STUB (mfld->mse));

	g_free (sdd);
}

static int
syncing_deleted (E2kConnection *conn, SoupMessage *msg,
		 E2kResult *results, int nresults,
		 int first, int total, gpointer user_data)
{
	struct sync_deleted_data *sdd = user_data;
	MailStubExchangeFolder *mfld = sdd->mfld;
	MailStubExchange *mse = mfld->mse;
	MailStubExchangeMessage *mmsg, *my_mmsg;
	const char *prop;
	int i, my_i;
	int read;

	if (sdd->highest_verified_seq == -1) {
		/* Round 1. (Fight!) */
		my_i = mfld->messages->len - 1;
	} else {
		/* Find the first message we haven't verified yet.
		 * Normally this would be the message immediately
		 * before the last message we verified in the last
		 * round, but if messages have been deleted since
		 * the last round, it may be lower.
		 */
		my_i = sdd->highest_unverified_index;
		if (my_i >= mfld->messages->len)
			my_i = mfld->messages->len - 1;

		do {
			mmsg = mfld->messages->pdata[my_i];
			if (mmsg->seq < sdd->highest_verified_seq)
				break;
		} while (--my_i >= 0);
	}

	/* Walk through messages backwards. */
	for (i = nresults - 1; i >= 0 && my_i >= 0; i--, my_i--) {
		mmsg = find_message_by_href (mfld, results[i].href);
		if (!mmsg || mmsg->seq >= sdd->highest_verified_seq) {
			/* This is a new message or a message we already
			 * verified. Skip it. Keep my_i from changing.
			 */
			my_i++;
			continue;
		}

		/* See if its read flag changed while we weren't watching */
		prop = e2k_properties_get_prop (results[i].props, E2K_PR_HTTPMAIL_READ);
		read = (prop && atoi (prop)) ? MAIL_STUB_MESSAGE_SEEN : 0;
		if ((mmsg->flags & MAIL_STUB_MESSAGE_SEEN) != read) {
			change_flags (mfld, mmsg,
				      mmsg->flags ^ MAIL_STUB_MESSAGE_SEEN);
		}

		my_mmsg = mfld->messages->pdata[my_i];

		/* If the messages don't match, remove messages from the
		 * folder until they do. (We know there has to eventually
		 * be a matching message or the find_message_by_href
		 * above would have failed.)
		 */
		while (my_mmsg->seq != mmsg->seq) {
			mfld->deleted_count++;
			message_removed (MAIL_STUB (mse), mfld, my_mmsg->href);
			sdd->changes = TRUE;
			my_i--;
			my_mmsg = mfld->messages->pdata[my_i];
		}
		sdd->highest_verified_seq = mmsg->seq;
	}

	if (my_i == -1 || my_i == first - 1) {
		/* Nothing left to check, or we've gotten in sync */
		if (mfld->deleted_count != sdd->new_deleted_count)
			mfld->deleted_count = sdd->new_deleted_count;
		sdd->highest_unverified_index = -1;
		return 0;
	}

	/* Set up the next round */
	sdd->highest_unverified_index = my_i;
	return nresults > 100 ? nresults : nresults * 2;
}

static void
got_sync_deleted_props (E2kConnection *conn, SoupMessage *msg,
			E2kResult *results, int nresults,
			gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	struct sync_deleted_data *sdd;
	const char *prop;
	int deleted_count = -1, visible_count = -1;
	E2kRestriction *rn;

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode) || !nresults) {
		g_warning ("got_sync_deleted_props: %d %s", msg->errorcode, msg->errorphrase);
		return;
	}

	prop = e2k_properties_get_prop (results[0].props,
					PR_DELETED_COUNT_TOTAL);
	if (prop)
		deleted_count = atoi (prop);

	prop = e2k_properties_get_prop (results[0].props,
					E2K_PR_DAV_VISIBLE_COUNT);
	if (prop)
		visible_count = atoi (prop);

	if (visible_count >= mfld->messages->len) {
		if (mfld->deleted_count == deleted_count)
			return;

		if (mfld->deleted_count == 0) {
			mfld->deleted_count = deleted_count;
			return;
		}
	}

	sdd = g_new0 (struct sync_deleted_data, 1);
	sdd->mfld = mfld;
	sdd->highest_verified_seq = -1;
	sdd->highest_unverified_index = mfld->messages->len - 1;
	sdd->new_deleted_count = deleted_count;

	prop = E2K_PR_HTTPMAIL_READ;
	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
					   E2K_RELOP_EQ, FALSE),
		NULL);

	E2K_DEBUG_HINT ('M');
	e_folder_exchange_search_with_progress (mfld->folder,
						&prop, 1, rn,
						E2K_PR_DAV_CREATION_DATE,
						20, FALSE, syncing_deleted,
						synced_deleted, sdd);
	e2k_restriction_unref (rn);
}

static const char *sync_deleted_props[] = {
	PR_DELETED_COUNT_TOTAL,
	E2K_PR_DAV_VISIBLE_COUNT
};
static const int n_sync_deleted_props = sizeof (sync_deleted_props) / sizeof (sync_deleted_props[0]);

static void
sync_deletions (MailStubExchange *mse, MailStubExchangeFolder *mfld)
{
	E2K_DEBUG_HINT ('M');
	e_folder_exchange_propfind (mfld->folder, "0",
				    sync_deleted_props, n_sync_deleted_props,
				    got_sync_deleted_props, mfld);
}

struct refresh_data {
	MailStub *stub;
	gboolean background;
	MailStubExchangeFolder *mfld;
	GArray *messages;
	GHashTable *mapi_messages;
};

struct refresh_message {
	char *uid, *href, *headers, *fff, *reply_by, *completed;
	guint32 flags, size, article_num;
};

static int
refresh_message_compar (const void *a, const void *b)
{
	const struct refresh_message *rma = a, *rmb = b;

	return strcmp (rma->uid, rmb->uid);
}

static void
free_rd (struct refresh_data *rd)
{
	if (rd->background)
		g_object_unref (rd->stub);
	if (rd->messages) {
		struct refresh_message *rm;
		int i;

		rm = (struct refresh_message *)rd->messages->data;
		for (i = 0; i < rd->messages->len; i++) {
			g_free (rm[i].uid);
			g_free (rm[i].href);
			g_free (rm[i].headers);
			g_free (rm[i].fff);
			g_free (rm[i].reply_by);
			g_free (rm[i].completed);
		}
		g_array_free (rd->messages, TRUE);
	}
	g_hash_table_destroy (rd->mapi_messages);
	g_free (rd);
}

static void
finish_refresh (struct refresh_data *rd)
{
	struct refresh_message rm;
	MailStubExchangeFolder *mfld = rd->mfld;
	MailStubExchangeMessage *mmsg;
	int i;

	mail_stub_return_data (rd->stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	qsort (rd->messages->data, rd->messages->len,
	       sizeof (rm), refresh_message_compar);
	for (i = 0; i < rd->messages->len; i++) {
		rm = g_array_index (rd->messages, struct refresh_message, i);

		/* If we already have a message with this UID, then
		 * that means it's not a new message, it's just that
		 * the article number changed.
		 */
		mmsg = find_message (mfld, rm.uid);
		if (mmsg) {
			if (rm.flags != mmsg->flags)
				change_flags (mfld, mmsg, rm.flags);
		} else {
			mmsg = new_message (rm.uid, rm.href, mfld->seq++, rm.flags);
			g_ptr_array_add (mfld->messages, mmsg);
			g_hash_table_insert (mfld->messages_by_uid,
					     mmsg->uid, mmsg);
			g_hash_table_insert (mfld->messages_by_href,
					     mmsg->href, mmsg);

			if (!(mmsg->flags & MAIL_STUB_MESSAGE_SEEN))
				mfld->unread_count++;

			mail_stub_return_data (rd->stub, CAMEL_STUB_RETVAL_NEW_MESSAGE,
					       CAMEL_STUB_ARG_FOLDER, mfld->name,
					       CAMEL_STUB_ARG_STRING, rm.uid,
					       CAMEL_STUB_ARG_UINT32, rm.flags,
					       CAMEL_STUB_ARG_UINT32, rm.size,
					       CAMEL_STUB_ARG_STRING, rm.headers,
					       CAMEL_STUB_ARG_END);
		}

		if (rm.article_num > mfld->high_article_num)
			mfld->high_article_num = rm.article_num;

		if (rm.fff)
			return_tag (mfld, rm.uid, "follow-up", rm.fff);
		if (rm.reply_by)
			return_tag (mfld, rm.uid, "due-by", rm.reply_by);
		if (rm.completed)
			return_tag (mfld, rm.uid, "completed-on", rm.completed);
	}
	mail_stub_return_data (rd->stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	mfld->scanned = TRUE;
	folder_changed (mfld);

	if (rd->background)
		mail_stub_push_changes (rd->stub);
	else
		mail_stub_return_ok (rd->stub);
	free_rd (rd);
}

static void
got_new_mapi_messages (E2kConnection *conn, SoupMessage *msg, 
		       E2kResult *results, int nresults,
		       gpointer user_data)
{
	struct refresh_data *rd = user_data;
	struct refresh_message *rm;
	const char *href;
	gpointer key, value;
	int i, n;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("got_new_mapi_messages: %d %s", msg->errorcode, msg->errorphrase);
		if (!rd->background)
			mail_stub_return_error (rd->stub, _("Could not get new messages"));
		free_rd (rd);
		return;
	}

	for (i = 0; i < nresults; i++) {
		if (!SOUP_ERROR_IS_SUCCESSFUL (results[i].status))
			continue;

		href = strrchr (results[i].href, '/');
		if (!href++)
			href = results[i].href;

		if (!g_hash_table_lookup_extended (rd->mapi_messages, href,
						   &key, &value))
			continue;
		n = GPOINTER_TO_INT (value);

		rm = &((struct refresh_message *)rd->messages->data)[n];
		rm->headers = mail_util_mapi_to_smtp_headers (results[i].props);
	}

	finish_refresh (rd);
}

static void
add_message (gpointer href, gpointer message, gpointer hrefs)
{
	g_ptr_array_add (hrefs, href);
}

static const char *mapi_message_props[] = {
	E2K_PR_MAILHEADER_SUBJECT,
	E2K_PR_MAILHEADER_FROM,
	E2K_PR_MAILHEADER_TO,
	E2K_PR_MAILHEADER_CC,
	E2K_PR_MAILHEADER_DATE,
	E2K_PR_MAILHEADER_RECEIVED,
	E2K_PR_MAILHEADER_MESSAGE_ID,
	E2K_PR_MAILHEADER_IN_REPLY_TO,
	E2K_PR_MAILHEADER_REFERENCES,
	E2K_PR_MAILHEADER_THREAD_INDEX,
	E2K_PR_DAV_CONTENT_TYPE
};
static const int n_mapi_message_props = sizeof (mapi_message_props) / sizeof (mapi_message_props[0]);

static void
got_new_smtp_messages (E2kConnection *conn, SoupMessage *msg, 
		       E2kResult *results, int nresults,
		       gpointer user_data)
{
	struct refresh_data *rd = user_data;
	struct refresh_message rm;
	char *prop, *uid, *href;
	gboolean has_read_flag = (rd->mfld->access & MAPI_ACCESS_READ);
	int i;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("got_new_smtp_messages: %d %s", msg->errorcode, msg->errorphrase);
		if (!rd->background)
			mail_stub_return_error (rd->stub, _("Could not get new messages"));
		free_rd (rd);
		return;
	}

	for (i = 0; i < nresults; i++) {
		if (!SOUP_ERROR_IS_SUCCESSFUL (results[i].status))
			continue;

		uid = e2k_properties_get_prop (results[i].props,
					       E2K_PR_REPL_UID);
		if (!uid)
			continue;
		prop = e2k_properties_get_prop (results[i].props,
						PR_INTERNET_ARTICLE_NUMBER);
		if (!prop)
			continue;

		rm.uid = g_strdup (uidstrip (uid));
		rm.href = g_strdup (results[i].href);
		rm.article_num = strtoul (prop, NULL, 10);

		rm.flags = mail_util_props_to_camel_flags (results[i].props,
							   has_read_flag);

		prop = e2k_properties_get_prop (results[i].props, E2K_PR_HTTPMAIL_MESSAGE_FLAG);
		if (prop)
			rm.fff = g_strdup (prop);
		else
			rm.fff = NULL;
		prop = e2k_properties_get_prop (results[i].props, E2K_PR_MAILHEADER_REPLY_BY);
		if (prop)
			rm.reply_by = g_strdup (prop);
		else
			rm.reply_by = NULL;
		prop = e2k_properties_get_prop (results[i].props, E2K_PR_MAILHEADER_COMPLETED);
		if (prop)
			rm.completed = g_strdup (prop);
		else
			rm.completed = NULL;

		prop = e2k_properties_get_prop (results[i].props, E2K_PR_DAV_CONTENT_LENGTH);
		rm.size = prop ? strtoul (prop, NULL, 10) : 0;

		rm.headers = mail_util_extract_transport_headers (results[i].props);

		g_array_append_val (rd->messages, rm);

		if (!rm.headers) {
			href = strrchr (rm.href, '/');
			if (!href++)
				href = rm.href;

			g_hash_table_insert (rd->mapi_messages, href,
					     GINT_TO_POINTER (rd->messages->len - 1));
		}
	}

	if (g_hash_table_size (rd->mapi_messages) > 0) {
		GPtrArray *hrefs = g_ptr_array_new ();

		g_hash_table_foreach (rd->mapi_messages, add_message, hrefs);
		E2K_DEBUG_HINT ('M');
		e_folder_exchange_bpropfind (rd->mfld->folder,
					     (const char **)hrefs->pdata,
					     hrefs->len, "0",
					     mapi_message_props,
					     n_mapi_message_props,
					     got_new_mapi_messages, rd);
		g_ptr_array_free (hrefs, TRUE);
		return;
	}

	finish_refresh (rd);
}

static const char *new_message_props[] = {
	E2K_PR_REPL_UID,
	PR_INTERNET_ARTICLE_NUMBER,
	PR_TRANSPORT_MESSAGE_HEADERS,
	E2K_PR_HTTPMAIL_READ,
	E2K_PR_HTTPMAIL_HAS_ATTACHMENT,
	PR_ACTION_FLAG,
	PR_DELEGATED_BY_RULE,
	E2K_PR_HTTPMAIL_MESSAGE_FLAG,
	E2K_PR_MAILHEADER_REPLY_BY,
	E2K_PR_MAILHEADER_COMPLETED,
	E2K_PR_DAV_CONTENT_LENGTH
};
static const int num_new_message_props = sizeof (new_message_props) / sizeof (new_message_props[0]);

static void
refresh_folder_internal (MailStub *stub, MailStubExchangeFolder *mfld,
			 gboolean background)
{
	struct refresh_data *rd;
	E2kRestriction *rn;

	rd = g_new0 (struct refresh_data, 1);
	rd->stub = stub;
	rd->mfld = mfld;
	rd->background = background;
	if (rd->background)
		g_object_ref (rd->stub);
	rd->messages = g_array_new (FALSE, FALSE, sizeof (struct refresh_message));
	rd->mapi_messages = g_hash_table_new (g_str_hash, g_str_equal);

	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_int (PR_INTERNET_ARTICLE_NUMBER,
					  E2K_RELOP_GT,
					  mfld->high_article_num),
		NULL);
	E2K_DEBUG_HINT ('M');
	e_folder_exchange_search (rd->mfld->folder,
				  new_message_props, num_new_message_props,
				  FALSE, rn, NULL,
				  got_new_smtp_messages, rd);
	e2k_restriction_unref (rn);
}

static void
refresh_folder (MailStub *stub, const char *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;

	mfld = folder_from_name (mse, folder_name, 0, FALSE);
	if (!mfld)
		return;

	refresh_folder_internal (stub, mfld, FALSE);
	sync_deletions (mse, mfld);
}

static void
expunged (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStub *stub = MAIL_STUB (mfld->mse);

	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode)) {
		g_warning ("expunged: %d %s", msg->errorcode, msg->errorphrase);
		mail_stub_return_error (stub, _("Could not empty Deleted Items folder"));
	} else
		mail_stub_return_ok (stub);
}

static int
expunging (E2kConnection *conn, SoupMessage *msg,
	   E2kResult *results, int nresults,
	   int first, int total, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStub *stub = MAIL_STUB (mfld->mse);
	int i;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);
	for (i = 0; i < nresults; i++)
		message_removed (stub, mfld, results[i].href);
	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	mfld->deleted_count += nresults;

	mail_stub_return_progress (stub, (first + nresults) * 100 / total);
	return TRUE;
}

static void
expunge_uids (MailStub *stub, const char *folder_name, GPtrArray *uids)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;
	GPtrArray *hrefs;
	int i;

	if (!uids->len) {
		mail_stub_return_ok (stub);
		return;
	}

	mfld = folder_from_name (mse, folder_name, MAPI_ACCESS_DELETE, FALSE);
	if (!mfld)
		return;

	hrefs = g_ptr_array_new ();
	for (i = 0; i < uids->len; i++) {
		mmsg = find_message (mfld, uids->pdata[i]);
		if (mmsg)
			g_ptr_array_add (hrefs, strrchr (mmsg->href, '/') + 1);
	}

	if (!hrefs->len) {
		/* Can only happen if there's a bug somewhere else, but we
		 * don't want to crash.
		 */
		g_ptr_array_free (hrefs, TRUE);
		mail_stub_return_ok (stub);
		return;
	}

	E2K_DEBUG_HINT ('M');
	e_folder_exchange_bdelete (mfld->folder,
				   (const char **)hrefs->pdata, hrefs->len,
				   expunging, expunged, mfld);
	g_ptr_array_free (hrefs, TRUE);
}

static void
bflag_set (E2kConnection *conn, SoupMessage *msg,
	   E2kResult *results, int nresults,
	   gpointer user_data)
{
	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode))
		g_warning ("bflag_set: %d %s", msg->errorcode, msg->errorphrase);
}

static void
flag_set (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	if (!SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode))
		g_warning ("flag_set: %d %s", msg->errorcode, msg->errorphrase);
}

static void
mark_one_read (E2kConnection *conn, const char *uri, gboolean read)
{
	E2kProperties *props;

	props = e2k_properties_new ();
	e2k_properties_set_bool (props, E2K_PR_HTTPMAIL_READ, read);

	E2K_DEBUG_HINT ('M');
	e2k_connection_proppatch (conn, uri, props, FALSE, flag_set, NULL);
	e2k_properties_free (props);
}

static void
mark_read (EFolder *folder, GPtrArray *hrefs, gboolean read)
{
	E2kProperties *props;

	props = e2k_properties_new ();
	e2k_properties_set_bool (props, E2K_PR_HTTPMAIL_READ, read);

	E2K_DEBUG_HINT ('M');
	e_folder_exchange_bproppatch (folder,
				      (const char **)hrefs->pdata,
				      hrefs->len, props,
				      FALSE, bflag_set, NULL);
	e2k_properties_free (props);
}

struct append_message_data {
	MailStub *stub;
	MailStubExchangeFolder *mfld;
	guint32 flags;
};

static void
appended_message (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	struct append_message_data *amd = user_data;
	const char *header;
	char *ruid;

	if (msg->errorcode != SOUP_ERROR_CREATED) {
		g_warning ("appended_message: %d %s", msg->errorcode, msg->errorphrase);
		mail_stub_return_error (amd->stub,
					msg->errorcode == SOUP_ERROR_DAV_OUT_OF_SPACE ?
					_("Could not append message; mailbox is over quota") :
					_("Could not append message"));
		g_free (amd);
		return;
	}

	if (amd->flags & MAIL_STUB_MESSAGE_SEEN) {
		header = soup_message_get_header (msg->response_headers, "Location");
		if (header)
			mark_one_read (conn, header, TRUE);
	}

	header = soup_message_get_header (msg->response_headers, "Repl-UID");
	if (header && *header == '<' && strlen (header) > 3)
		ruid = g_strndup (header + 1, strlen (header) - 2);
	else
		ruid = NULL;
	mail_stub_return_data (amd->stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRING, ruid ? uidstrip (ruid) : "",
			       CAMEL_STUB_ARG_END);
	g_free (ruid);

	mail_stub_return_ok (amd->stub);
	g_free (amd);
}

static void
append_message (MailStub *stub, const char *folder_name, guint32 flags,
		const char *subject, const char *data, int length)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	struct append_message_data *amd;

	mfld = folder_from_name (mse, folder_name, MAPI_ACCESS_CREATE_CONTENTS, FALSE);
	if (!mfld)
		return;

	amd = g_new (struct append_message_data, 1);
	amd->stub = stub;
	amd->mfld = mfld;
	amd->flags = flags;
	E2K_DEBUG_HINT ('M');
	e_folder_exchange_append (mfld->folder, subject,
				  "message/rfc822", data, length,
				  appended_message, amd);
}

static inline void
change_pending (MailStubExchangeFolder *mfld)
{
	if (!mfld->pending_delete_ops && !mfld->changed_messages->len)
		g_object_ref (mfld->mse);
}

static inline void
change_complete (MailStubExchangeFolder *mfld)
{
	if (!mfld->pending_delete_ops && !mfld->changed_messages->len)
		g_object_unref (mfld->mse);
}

static void
deleted_messages (E2kConnection *conn, SoupMessage *msg,
		  E2kResult *results, int nresults,
		  gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStub *stub = MAIL_STUB (mfld->mse);
	int i;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("deleted: %d %s", msg->errorcode, msg->errorphrase);
		goto done;
	}

	if (nresults > 1) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, mfld->name,
				       CAMEL_STUB_ARG_END);
	}

	for (i = 0; i < nresults; i++) {
		if (!e2k_properties_get_prop (results[i].props, E2K_PR_DAV_LOCATION))
			continue;

		message_removed (stub, mfld, results[i].href);
	}

	if (nresults > 1) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, mfld->name,
				       CAMEL_STUB_ARG_END);
	}

	mfld->deleted_count += nresults;

 done:
	mail_stub_push_changes (stub);
	mfld->pending_delete_ops--;
	change_complete (mfld);
}

static void
set_replied_flags (MailStubExchange *mse, MailStubExchangeMessage *mmsg)
{
	E2kProperties *props;

	props = e2k_properties_new ();

	if (mmsg->change_flags & MAIL_STUB_MESSAGE_ANSWERED) {
		e2k_properties_set_int (props, PR_ACTION, MAPI_ACTION_REPLIED);
		e2k_properties_set_int (props, PR_ACTION_FLAG, (mmsg->change_flags & MAIL_STUB_MESSAGE_ANSWERED_ALL) ?
					MAPI_ACTION_FLAG_REPLIED_TO_ALL :
					MAPI_ACTION_FLAG_REPLIED_TO_SENDER);
		e2k_properties_set_date (props, PR_ACTION_DATE,
					 e2k_make_timestamp (time (NULL)));
	} else {
		e2k_properties_remove (props, PR_ACTION);
		e2k_properties_remove (props, PR_ACTION_FLAG);
		e2k_properties_remove (props, PR_ACTION_DATE);
	}

	E2K_DEBUG_HINT ('M');
	e2k_connection_proppatch (mse->conn, mmsg->href, props, FALSE,
				  flag_set, NULL);
	e2k_properties_free (props);
}

static void
update_tags (MailStubExchange *mse, MailStubExchangeMessage *mmsg)
{
	E2kProperties *props;
	const char *value;
	int flag_status;

	flag_status = MAPI_FOLLOWUP_UNFLAGGED;
	props = e2k_properties_new ();

	value = g_datalist_get_data (&mmsg->tag_updates, "follow-up");
	if (value) {
		if (*value) {
			e2k_properties_set_string (
				props, E2K_PR_HTTPMAIL_MESSAGE_FLAG,
				g_strdup (value));
			flag_status = MAPI_FOLLOWUP_FLAGGED;
		} else {
			e2k_properties_remove (
				props, E2K_PR_HTTPMAIL_MESSAGE_FLAG);
		}
	}

	value = g_datalist_get_data (&mmsg->tag_updates, "due-by");
	if (value) {
		if (*value) {
			e2k_properties_set_string (
				props, E2K_PR_MAILHEADER_REPLY_BY,
				g_strdup (value));
		} else {
			e2k_properties_remove (
				props, E2K_PR_MAILHEADER_REPLY_BY);
		}
	}

	value = g_datalist_get_data (&mmsg->tag_updates, "completed-on");
	if (value) {
		if (*value) {
			e2k_properties_set_string (
				props, E2K_PR_MAILHEADER_COMPLETED,
				g_strdup (value));
			flag_status = MAPI_FOLLOWUP_COMPLETED;
		} else {
			e2k_properties_remove (
				props, E2K_PR_MAILHEADER_COMPLETED);
		}
	}
	g_datalist_clear (&mmsg->tag_updates);

	e2k_properties_set_int (props, PR_FLAG_STATUS, flag_status);

	E2K_DEBUG_HINT ('M');
	e2k_connection_proppatch (mse->conn, mmsg->href, props, FALSE,
				  flag_set, NULL);
	e2k_properties_free (props);
}

static gboolean
process_flags (gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStubExchange *mse = mfld->mse;
	MailStubExchangeMessage *mmsg;
	GPtrArray *seen = NULL, *unseen = NULL;
	GString *deleted = NULL;
	int i;

	for (i = 0; i < mfld->changed_messages->len; i++) {
		mmsg = mfld->changed_messages->pdata[i];

		if (mmsg->change_mask & MAIL_STUB_MESSAGE_SEEN) {
			if (mmsg->change_flags & MAIL_STUB_MESSAGE_SEEN) {
				if (!seen)
					seen = g_ptr_array_new ();
				g_ptr_array_add (seen, strrchr (mmsg->href, '/') + 1);
				mmsg->flags |= MAIL_STUB_MESSAGE_SEEN;
			} else {
				if (!unseen)
					unseen = g_ptr_array_new ();
				g_ptr_array_add (unseen, strrchr (mmsg->href, '/') + 1);
				mmsg->flags &= ~MAIL_STUB_MESSAGE_SEEN;
			}
			mmsg->change_mask &= ~MAIL_STUB_MESSAGE_SEEN;
		}

		if (mmsg->change_mask & MAIL_STUB_MESSAGE_ANSWERED) {
			set_replied_flags (mse, mmsg);
			mmsg->change_mask &= ~(MAIL_STUB_MESSAGE_ANSWERED | MAIL_STUB_MESSAGE_ANSWERED_ALL);
		}

		if (mmsg->tag_updates)
			update_tags (mse, mmsg);

		if (!mmsg->change_mask)
			g_ptr_array_remove_index_fast (mfld->changed_messages, i--);
	}

	if (seen || unseen) {
		if (seen) {
			mark_read (mfld->folder, seen, TRUE);
			g_ptr_array_free (seen, TRUE);
		}
		if (unseen) {
			mark_read (mfld->folder, unseen, FALSE);
			g_ptr_array_free (unseen, TRUE);
		}

		if (mfld->changed_messages->len == 0) {
			mfld->flag_timeout = 0;
			change_complete (mfld);
			return FALSE;
		} else
			return TRUE;
	}

	for (i = 0; i < mfld->changed_messages->len; i++) {
		mmsg = mfld->changed_messages->pdata[i];
		if (mmsg->change_mask & mmsg->change_flags & MAIL_STUB_MESSAGE_DELETED) {
			if (!deleted)
				deleted = g_string_new (NULL);
			g_string_append_printf (deleted, "<href>%s</href>",
						strrchr (mmsg->href, '/') + 1);
		}
	}

	if (deleted) {
		E2K_DEBUG_HINT ('M');
		change_pending (mfld);
		mfld->pending_delete_ops++;
		e_folder_exchange_transfer (mfld->folder, mse->deleted_items,
					    deleted->str, TRUE,
					    deleted_messages, mfld);
		g_string_free (deleted, TRUE);
	}

	if (mfld->changed_messages->len) {
		g_ptr_array_set_size (mfld->changed_messages, 0);
		change_complete (mfld);
	}
	mfld->flag_timeout = 0;
	return FALSE;
}

static void
change_message (MailStubExchange *mse, MailStubExchangeFolder *mfld,
		MailStubExchangeMessage *mmsg)
{
	change_pending (mfld);
	g_ptr_array_add (mfld->changed_messages, mmsg);
	if (mfld->flag_timeout)
		g_source_remove (mfld->flag_timeout);
	mfld->flag_timeout = g_timeout_add (1000, process_flags, mfld);
}

static void
set_message_flags (MailStub *stub, const char *folder_name, const char *uid,
		   guint32 flags, guint32 mask)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;

	mfld = folder_from_name (mse, folder_name, MAPI_ACCESS_MODIFY, TRUE);
	if (!mfld)
		return;

	mmsg = find_message (mfld, uid);
	if (!mmsg)
		return;

	/* Although we don't actually process the flag change right
	 * away, we need to update the folder's unread count to match
	 * what the user now believes it is. (We take advantage of the
	 * fact that the mailer will never delete a message without
	 * also marking it read.)
	 */
	if (mask & MAIL_STUB_MESSAGE_SEEN) {
		if (((mmsg->flags ^ flags) & MAIL_STUB_MESSAGE_SEEN) == 0) {
			/* The user is just setting it to what it
			 * already is, so ignore it.
			 */
			mask &= ~MAIL_STUB_MESSAGE_SEEN;
		} else {
			mmsg->flags ^= MAIL_STUB_MESSAGE_SEEN;
			if (mmsg->flags & MAIL_STUB_MESSAGE_SEEN)
				mfld->unread_count--;
			else
				mfld->unread_count++;
			folder_changed (mfld);
		}
	}

	/* If the user tries to delete a message in a non-person
	 * hierarchy, we ignore it (which will cause camel to delete
	 * it the hard way next time it syncs).
	 */
	if (mask & flags & MAIL_STUB_MESSAGE_DELETED) {
		ExchangeHierarchy *hier;

		hier = e_folder_exchange_get_hierarchy (mfld->folder);
		if (hier->type != EXCHANGE_HIERARCHY_PERSONAL)
			mask &= ~MAIL_STUB_MESSAGE_DELETED;
	}

	/* If there's nothing left to change, return. */
	if (!mask)
		return;

	mmsg->change_flags |= (flags & mask);
	mmsg->change_flags &= ~(~flags & mask);
	mmsg->change_mask |= mask;

	change_message (mse, mfld, mmsg);
}

static void
set_message_tag (MailStub *stub, const char *folder_name, const char *uid,
		 const char *name, const char *value)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;

	mfld = folder_from_name (mse, folder_name, MAPI_ACCESS_MODIFY, TRUE);
	if (!mfld)
		return;

	mmsg = find_message (mfld, uid);
	if (!mmsg)
		return;

	g_datalist_set_data_full (&mmsg->tag_updates, name,
				  g_strdup (value), g_free);
	change_message (mse, mfld, mmsg);
}

struct get_message_data {
	MailStub *stub;
	MailStubExchangeFolder *mfld;
	char *href;
	int flags;
	GByteArray *data;
};

static void
get_message_error (struct get_message_data *gmd, SoupMessage *msg)
{
	g_warning ("got_message: %d %s", msg->errorcode, msg->errorphrase);
	if (msg->errorcode == SOUP_ERROR_NOT_FOUND) {
		/* We don't change mfld->deleted_count, because the
		 * message may actually have gone away before the last
		 * time we recorded that.
		 */
		message_removed (gmd->stub, gmd->mfld, gmd->href);
		mail_stub_return_error (gmd->stub, _("Message has been deleted"));
	} else
		mail_stub_return_error (gmd->stub, _("Error retrieving message"));
	g_free (gmd->href);
	g_free (gmd);
}

static void
got_message_body (struct get_message_data *gmd, const char *body, int length)
{
	mail_stub_return_data (gmd->stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_BYTEARRAY, body, length,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (gmd->stub);

	g_free (gmd->href);
	g_free (gmd);
}

static void
got_stickynote (E2kConnection *conn, SoupMessage *msg, 
		E2kResult *results, int nresults,
		gpointer user_data)
{
	struct get_message_data *gmd = user_data;
	GString *message;

	if (SOUP_MESSAGE_IS_ERROR (msg) || nresults == 0) {
		get_message_error (gmd, msg);
		return;
	}

	message = mail_util_stickynote_to_rfc822 (results[0].props);
	got_message_body (gmd, message->str, message->len);
	g_string_free (message, TRUE);
}

static void
got_fake_headers (E2kConnection *conn, SoupMessage *msg, 
		  E2kResult *results, int nresults,
		  gpointer user_data)
{
	struct get_message_data *gmd = user_data;
	GByteArray *message;
	char *headers;

	message = gmd->data;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		get_message_error (gmd, msg);
		g_byte_array_free (message, TRUE);
		return;
	}

	if (nresults) {
		headers = mail_util_mapi_to_smtp_headers (results[0].props);
		g_byte_array_prepend (message, headers, strlen (headers));
	}

	got_message_body (gmd, message->data, message->len);
	g_byte_array_free (message, TRUE);
}

static void
got_delegated_by_props (E2kConnection *conn, SoupMessage *msg, 
			E2kResult *results, int nresults,
			gpointer user_data)
{
	struct get_message_data *gmd = user_data;
	GByteArray *message;
	char *delegator_dn, *delegator_uri;
	ExchangeAccount *account;
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;
	EFolder *folder;

	message = gmd->data;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		get_message_error (gmd, msg);
		g_byte_array_free (message, TRUE);
		return;
	}

	if (nresults) {
		delegator_dn = e2k_properties_get_prop (results[0].props, PR_RCVD_REPRESENTING_EMAIL_ADDRESS);

		account = MAIL_STUB_EXCHANGE (gmd->stub)->account;
		gc = exchange_account_get_global_catalog (account);
		if (!gc)
			goto done;

		status = e2k_global_catalog_lookup (
			gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
			delegator_dn, E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
			&entry);
		if (status != E2K_GLOBAL_CATALOG_OK)
			goto done;

		delegator_uri = exchange_account_get_foreign_uri (
			account, entry, E2K_PR_STD_FOLDER_CALENDAR);
		if (delegator_uri) {
			folder = exchange_account_get_folder (account, delegator_uri);
			if (folder) {
				mail_util_demangle_delegated_meeting (
					message, entry->display_name,
					entry->email,
					e_folder_get_physical_uri (folder));
			}
			g_free (delegator_uri);
		}
		e2k_global_catalog_entry_free (gc, entry);
	}

 done:
	got_message_body (gmd, message->data, message->len);
	g_byte_array_free (message, TRUE);
}

static void
got_message (E2kConnection *conn, SoupMessage *msg, gpointer user_data)
{
	struct get_message_data *gmd = user_data;
	const char *content_type;
	const char *body = msg->response.body;
	int length = msg->response.length;

	if (SOUP_MESSAGE_IS_ERROR (msg)) {
		get_message_error (gmd, msg);
		return;
	}

	content_type = soup_message_get_header (msg->response_headers,
						"Content-Type");

	/* Public folders especially can contain non-email objects.
	 * In that case, we fake the headers (which in this case
	 * should include Content-Type, Content-Disposition, etc,
	 * courtesy of mp:x67200102.
	 */
	if (!content_type || g_ascii_strncasecmp (content_type, "message/", 8)) {
		gmd->data = g_byte_array_new ();
		g_byte_array_append (gmd->data, body, length);

		E2K_DEBUG_HINT ('M');
		e2k_connection_propfind (conn, gmd->href, "0",
					 mapi_message_props, n_mapi_message_props,
					 got_fake_headers, gmd);
		return;
	}

	/* If this is a delegated meeting request, we need to know who
	 * delegated it to us.
	 */
	if (gmd->flags & MAIL_STUB_MESSAGE_DELEGATED) {
		const char *prop = PR_RCVD_REPRESENTING_EMAIL_ADDRESS;

		gmd->data = g_byte_array_new ();
		g_byte_array_append (gmd->data, body, length);

		E2K_DEBUG_HINT ('M');
		e2k_connection_propfind (conn, gmd->href, "0", &prop, 1,
					 got_delegated_by_props, gmd);
		return;
	}

	/* If you PUT a message/rfc821 message to the sendmsg URI
	 * without "Saveinsent: f", then the copy saved to Sent Items
	 * will still have Content-Type: message/rfc821 and will
	 * include the SMTP gunk.
	 */
	if (!g_ascii_strcasecmp (content_type, "message/rfc821")) {
		const char *p = strstr (body, "\r\n\r\n");

		if (p && strstr (p, "\r\n\r\n")) {
			length -= p + 4 - body;
			body = p + 4;
		}
	}

	got_message_body (gmd, body, length);
}

static const char *stickynote_props[] = {
	E2K_PR_MAILHEADER_SUBJECT,
	E2K_PR_DAV_LAST_MODIFIED,
	E2K_PR_OUTLOOK_STICKYNOTE_COLOR,
	E2K_PR_OUTLOOK_STICKYNOTE_HEIGHT,
	E2K_PR_OUTLOOK_STICKYNOTE_WIDTH,
	E2K_PR_HTTPMAIL_TEXT_DESCRIPTION,
};
static const int n_stickynote_props = sizeof (stickynote_props) / sizeof (stickynote_props[0]);

static void
get_message (MailStub *stub, const char *folder_name, const char *uid)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;
	struct get_message_data *gmd;

	mfld = folder_from_name (mse, folder_name, MAPI_ACCESS_READ, FALSE);
	if (!mfld)
		return;

	mmsg = find_message (mfld, uid);
	if (!mmsg) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_REMOVED_MESSAGE,
				       CAMEL_STUB_ARG_FOLDER, folder_name,
				       CAMEL_STUB_ARG_STRING, uid,
				       CAMEL_STUB_ARG_END);
		mail_stub_return_error (stub, _("No such message"));
		return;
	}

	gmd = g_new (struct get_message_data, 1);
	gmd->stub = stub;
	gmd->mfld = mfld;
	gmd->href = g_strdup (mmsg->href);
	gmd->flags = mmsg->flags;
	gmd->data = NULL;

	if (mfld->type == MAIL_STUB_EXCHANGE_FOLDER_NOTES) {
		E2K_DEBUG_HINT ('M');
		e2k_connection_propfind (mse->conn, mmsg->href, "0",
					 stickynote_props, n_stickynote_props,
					 got_stickynote, gmd);
	} else {
		E2K_DEBUG_HINT ('M');
		e2k_connection_get (mse->conn, mmsg->href, got_message, gmd);
	}
}

static void
searched (E2kConnection *conn, SoupMessage *msg,
	  E2kResult *results, int nresults,
	  gpointer user_data)
{
	MailStub *stub = user_data;
	GPtrArray *uids;
	int i;

	if (msg->errorcode == SOUP_ERROR_DAV_UNPROCESSABLE) {
		mail_stub_return_error (stub, _("Mailbox does not support full-text searching"));
		return;
	}

	uids = g_ptr_array_new ();
	for (i = 0; i < nresults; i++)
		g_ptr_array_add (uids, (char *)uidstrip (e2k_properties_get_prop (results[i].props, E2K_PR_REPL_UID)));

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRINGARRAY, uids,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);

	g_ptr_array_free (uids, TRUE);
}

static void
search (MailStub *stub, const char *folder_name, const char *text)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	E2kRestriction *rn;
	const char *prop;

	mfld = folder_from_name (mse, folder_name, 0, FALSE);
	if (!mfld)
		return;

	prop = E2K_PR_REPL_UID;
	rn = e2k_restriction_content (PR_BODY, E2K_FL_SUBSTRING, text);
	E2K_DEBUG_HINT ('M');
	e_folder_exchange_search (mfld->folder,
				  &prop, 1, FALSE, rn, NULL,
				  searched, stub);
	e2k_restriction_unref (rn);
}

struct transfer_messages_data {
	MailStub *stub;
	MailStubExchangeFolder *source;
	gboolean delete_originals;
	GHashTable *order;
	int nmessages;
};

static void
free_tmd (struct transfer_messages_data *tmd)
{
	g_hash_table_destroy (tmd->order);
	g_free (tmd);
}

static void
transferred_messages (E2kConnection *conn, SoupMessage *msg,
		      E2kResult *results, int nresults,
		      gpointer user_data)
{
	struct transfer_messages_data *tmd = user_data;
	MailStubExchangeMessage *mmsg;
	gpointer key, value;
	GPtrArray *new_uids;
	char *uid;
	int i, num;

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		g_warning ("transferred_messages: %d %s", msg->errorcode, msg->errorphrase);
		mail_stub_return_error (tmd->stub, _("Unable to move/copy messages"));
		free_tmd (tmd);
		return;
	}

	if (tmd->delete_originals && nresults > 1) {
		mail_stub_return_data (tmd->stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, tmd->source->name,
				       CAMEL_STUB_ARG_END);
	}

	new_uids = g_ptr_array_new ();
	g_ptr_array_set_size (new_uids, tmd->nmessages);
	for (i = 0; i < new_uids->len; i++)
		new_uids->pdata[i] = "";

	for (i = 0; i < nresults; i++) {
		if (!e2k_properties_get_prop (results[i].props,
					      E2K_PR_DAV_LOCATION))
			continue;
		uid = e2k_properties_get_prop (results[i].props,
					       E2K_PR_REPL_UID);
		if (!uid)
			continue;

		if (tmd->delete_originals)
			tmd->source->deleted_count++;

		mmsg = find_message_by_href (tmd->source, results[i].href);
		if (!mmsg)
			continue;

		if (!g_hash_table_lookup_extended (tmd->order, mmsg,
						   &key, &value))
			continue;
		num = GPOINTER_TO_UINT (value);
		if (num > new_uids->len)
			continue;

		new_uids->pdata[num] = (char *)uidstrip (uid);

		if (tmd->delete_originals)
			message_removed (tmd->stub, tmd->source, results[i].href);
	}

	if (tmd->delete_originals && nresults > 1) {
		mail_stub_return_data (tmd->stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, tmd->source->name,
				       CAMEL_STUB_ARG_END);
	}

	mail_stub_return_data (tmd->stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRINGARRAY, new_uids,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (tmd->stub);

	g_ptr_array_free (new_uids, TRUE);
	free_tmd (tmd);
}

static void
transfer_messages (MailStub *stub, const char *source_name,
		   const char *dest_name, GPtrArray *uids,
		   gboolean delete_originals)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *source, *dest;
	MailStubExchangeMessage *mmsg;
	struct transfer_messages_data *tmd;
	GString *hrefs;
	int i;

	source = folder_from_name (mse, source_name, delete_originals ? MAPI_ACCESS_DELETE : 0, FALSE);
	if (!source)
		return;
	dest = folder_from_name (mse, dest_name, MAPI_ACCESS_CREATE_CONTENTS, FALSE);
	if (!dest)
		return;

	tmd = g_new0 (struct transfer_messages_data, 1);
	tmd->stub = stub;
	tmd->source = source;
	tmd->nmessages = uids->len;
	tmd->delete_originals = delete_originals;
	tmd->order = g_hash_table_new (NULL, NULL);

	hrefs = g_string_new (NULL);
	for (i = 0; i < uids->len; i++) {
		mmsg = find_message (source, uids->pdata[i]);
		if (!mmsg)
			continue;

		g_hash_table_insert (tmd->order, mmsg, GINT_TO_POINTER (i));
		g_string_append_printf (hrefs, "<href>%s</href>",
					strrchr (mmsg->href, '/') + 1);
	}

	E2K_DEBUG_HINT ('M');
	e_folder_exchange_transfer (source->folder, dest->folder,
				    hrefs->str, delete_originals,
				    transferred_messages, tmd);
	g_string_free (hrefs, TRUE);
}

static void
account_new_folder (ExchangeAccount *account, EFolder *folder, MailStub *stub)
{
	ExchangeHierarchy *hier;

	if (strcmp (e_folder_get_type_string (folder), "mail") != 0)
		return;

	hier = e_folder_exchange_get_hierarchy (folder);
	if (hier->type != EXCHANGE_HIERARCHY_PERSONAL)
		return;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_CREATED,
			       CAMEL_STUB_ARG_STRING, e_folder_get_name (folder),
			       CAMEL_STUB_ARG_STRING, e_folder_get_physical_uri (folder),
			       CAMEL_STUB_ARG_END);

	mail_stub_push_changes (stub);
}

static void
account_removed_folder (ExchangeAccount *account, EFolder *folder, MailStub *stub)
{
	ExchangeHierarchy *hier;

	if (strcmp (e_folder_get_type_string (folder), "mail") != 0)
		return;

	hier = e_folder_exchange_get_hierarchy (folder);
	if (hier->type != EXCHANGE_HIERARCHY_PERSONAL)
		return;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_DELETED,
			       CAMEL_STUB_ARG_STRING, e_folder_get_name (folder),
			       CAMEL_STUB_ARG_STRING, e_folder_get_physical_uri (folder),
			       CAMEL_STUB_ARG_END);

	mail_stub_push_changes (stub);
}

static void
get_folder_info (MailStub *stub)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	GPtrArray *folders, *names, *uris;
	ExchangeHierarchy *hier;
	EFolder *folder;
	int i;

	exchange_account_rescan_tree (mse->account);

	folders = exchange_account_get_folders (mse->account);
	names = g_ptr_array_new ();
	uris = g_ptr_array_new ();
	if (folders) {
		for (i = 0; i < folders->len; i++) {
			folder = folders->pdata[i];
			if (strcmp (e_folder_get_type_string (folder), "mail"))
				continue;
			hier = e_folder_exchange_get_hierarchy (folder);
			if (hier->type != EXCHANGE_HIERARCHY_PERSONAL)
				continue;

			g_ptr_array_add (names, (char *)e_folder_get_name (folder));
			g_ptr_array_add (uris, (char *)e_folder_get_physical_uri (folder));
		}
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
				       CAMEL_STUB_ARG_STRINGARRAY, names,
				       CAMEL_STUB_ARG_STRINGARRAY, uris,
				       CAMEL_STUB_ARG_END);
		g_ptr_array_free (folders, TRUE);
	}

	if (mse->new_folder_id == 0) {
		mse->new_folder_id = g_signal_connect (
			mse->account, "new_folder",
			G_CALLBACK (account_new_folder), stub);
		mse->removed_folder_id = g_signal_connect (
			mse->account, "removed_folder",
			G_CALLBACK (account_removed_folder), stub);
	}

	mail_stub_return_ok (stub);
}

struct send_message_data {
	MailStub *stub;
	char *from_addr;
};

static void
sent_message (SoupMessage *msg, gpointer user_data)
{
	struct send_message_data *smd = user_data;
	MailStub *stub = smd->stub;
	char *errmsg;

	if (SOUP_ERROR_IS_SUCCESSFUL (msg->errorcode))
		mail_stub_return_ok (stub);
	else if (msg->errorcode == SOUP_ERROR_NOT_FOUND)
		mail_stub_return_error (stub, _("Server won't accept mail via Exchange transport"));
	else if (msg->errorcode == SOUP_ERROR_FORBIDDEN) {
		errmsg = g_strdup_printf (_("Your account does not have permission "
					    "to use <%s>\nas a From address."),
					  smd->from_addr);
		mail_stub_return_error (stub, errmsg);
		g_free (errmsg);
	} else if (msg->errorcode == SOUP_ERROR_DAV_OUT_OF_SPACE ||
		   msg->errorcode == SOUP_ERROR_INTERNAL) {
		/* (500 is what it actually returns, 507 is what it should
		 * return, so we handle that too in case the behavior
		 * changes in the future.)
		 */
		E2K_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES;
		mail_stub_return_error (stub, _("Could not send message.\n"
						"This might mean that your account is over quota."));
	} else {
		g_warning ("sent_message: %d %s", msg->errorcode, msg->errorphrase);
		mail_stub_return_error (stub, _("Could not send message"));
	}

	g_free (smd->from_addr);
	g_free (smd);
}

static void
send_message (MailStub *stub, const char *from, GPtrArray *recipients,
	      const char *body, int length)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	struct send_message_data *smd;
	SoupMessage *msg;
	char *timestamp, hostname[256];
	GString *data;
	int i;

	if (!mse->mail_submission_uri) {
		mail_stub_return_error (stub, _("No mail submission URI for this mailbox"));
		return;
	}

	data = g_string_new (NULL);
	g_string_append_printf (data, "MAIL FROM:<%s>\r\n", from);
	for (i = 0; i < recipients->len; i++) {
		g_string_append_printf (data, "RCPT TO:<%s>\r\n",
					(char *)recipients->pdata[i]);
	}
	g_string_append (data, "\r\n");

	/* Exchange doesn't add a "Received" header to messages
	 * received via WebDAV.
	 */
	if (gethostname (hostname, sizeof (hostname)) != 0)
		strcpy (hostname, "localhost");
	timestamp = e2k_make_timestamp_rfc822 (time (NULL));
	g_string_append_printf (data, "Received: from %s by %s; %s\r\n",
				hostname, mse->account->exchange_server,
				timestamp);
	g_free (timestamp);
	
	g_string_append_len (data, body, length);

	msg = e2k_soup_message_new_full (mse->conn, mse->mail_submission_uri,
					 SOUP_METHOD_PUT, "message/rfc821",
					 SOUP_BUFFER_SYSTEM_OWNED,
					 data->str, data->len);
	g_string_free (data, FALSE);
	soup_message_add_header (msg->request_headers, "Saveinsent", "f");
	soup_message_set_http_version (msg, SOUP_HTTP_1_0);

	smd = g_new (struct send_message_data, 1);
	smd->stub = stub;
	smd->from_addr = g_strdup (from);
	e2k_soup_message_queue (msg, sent_message, smd);
}

static void
stub_connect (MailStub *stub)
{
	mail_stub_return_ok (stub);
}

MailStub *
mail_stub_exchange_new (ExchangeAccount *account, int cmd_fd, int status_fd)
{
	MailStubExchange *mse;
	MailStub *stub;
	const char *uri;

	stub = g_object_new (MAIL_TYPE_STUB_EXCHANGE, NULL);
	mail_stub_construct (stub, cmd_fd, status_fd);

	mse = (MailStubExchange *)stub;
	mse->account = account;
	mse->conn = exchange_account_get_connection (account);
	g_object_ref (mse->conn);

	mse->mail_submission_uri = exchange_account_get_standard_uri (account, "sendmsg");
	uri = exchange_account_get_standard_uri (account, "inbox");
	mse->inbox = exchange_account_get_folder (account, uri);
	uri = exchange_account_get_standard_uri (account, "deleteditems");
	mse->deleted_items = exchange_account_get_folder (account, uri);

	return stub;
}
