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

#include <exchange-constants.h>
#include <e-folder-exchange.h>
#include "camel-stub-constants.h"
#include <e2k-propnames.h>
#include <e2k-restriction.h>
#include <e2k-uri.h>
#include <e2k-utils.h>
#include "exchange-component.h" // for using global_exchange_component
#include "exchange-config-listener.h"
#include <exchange-hierarchy.h>
#include <mapi.h>

#define d(x)

#define PARENT_TYPE MAIL_TYPE_STUB
static MailStubClass *parent_class = NULL;

/* FIXME : Have this as part of the appropriate class in 2.5 */
/* static gulong offline_listener_handler_id; */

typedef struct {
	gchar *uid, *href;
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
	const gchar *name;
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

static void stub_connect (MailStub *stub, gchar *pwd);
static void get_folder (MailStub *stub, const gchar *name, gboolean create,
			GPtrArray *uids, GByteArray *flags, GPtrArray *hrefs, guint32 high_article_num);
static void get_trash_name (MailStub *stub);
static void sync_folder (MailStub *stub, const gchar *folder_name);
static void refresh_folder (MailStub *stub, const gchar *folder_name);
static void sync_count (MailStub *stub, const gchar *folder_name);
static void refresh_folder_internal (MailStub *stub, MailStubExchangeFolder *mfld,
				     gboolean background);
static gboolean sync_deletions (MailStubExchange *mse, MailStubExchangeFolder *mfld);
static void expunge_uids (MailStub *stub, const gchar *folder_name, GPtrArray *uids);
static void append_message (MailStub *stub, const gchar *folder_name, guint32 flags,
			    const gchar *subject, const gchar *data, gint length);
static void set_message_flags (MailStub *, const gchar *folder_name,
			       const gchar *uid, guint32 flags, guint32 mask);
static void set_message_tag (MailStub *, const gchar *folder_name,
			     const gchar *uid, const gchar *name, const gchar *value);
static void get_message (MailStub *stub, const gchar *folder_name, const gchar *uid);
static void search (MailStub *stub, const gchar *folder_name, const gchar *text);
static void transfer_messages (MailStub *stub, const gchar *source_name,
			       const gchar *dest_name, GPtrArray *uids,
			       gboolean delete_originals);
static void get_folder_info (MailStub *stub, const gchar *top,
			     guint32 store_flags);
static void send_message (MailStub *stub, const gchar *from,
			  GPtrArray *recipients,
			  const gchar *data, gint length);
static void create_folder (MailStub *, const gchar *parent_name,
			   const gchar *folder_name);
static void delete_folder (MailStub *, const gchar *folder_name);
static void rename_folder (MailStub *, const gchar *old_name,
			   const gchar *new_name);
static void subscribe_folder (MailStub *, const gchar *folder_name);
static void unsubscribe_folder (MailStub *, const gchar *folder_name);
static void is_subscribed_folder (MailStub *, const gchar *folder_name);

static gboolean process_flags (gpointer user_data);

static void storage_folder_changed (EFolder *folder, gpointer user_data);

/* static void linestatus_listener (ExchangeComponent *component,
						    gint linestatus,
						    gpointer data); */
static void folder_update_linestatus (gpointer key, gpointer value, gpointer data);
static void free_folder (gpointer value);
static gboolean get_folder_online (MailStubExchangeFolder *mfld, gboolean background);
static void  get_folder_info_data (MailStub *stub, const gchar *top, guint32 store_flags,
				   GPtrArray **names, GPtrArray **uris,
				   GArray **unread, GArray **flags);

static GStaticRecMutex g_changed_msgs_mutex = G_STATIC_REC_MUTEX_INIT;

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
	stub_class->sync_count = sync_count;
	stub_class->expunge_uids = expunge_uids;
	stub_class->append_message = append_message;
	stub_class->set_message_flags = set_message_flags;
	stub_class->set_message_tag = set_message_tag;
	stub_class->get_message = get_message;
	stub_class->search = search;
	stub_class->transfer_messages = transfer_messages;
	stub_class->get_folder_info = get_folder_info;
	stub_class->send_message = send_message;
	stub_class->create_folder = create_folder;
	stub_class->delete_folder = delete_folder;
	stub_class->rename_folder = rename_folder;
	stub_class->subscribe_folder = subscribe_folder;
	stub_class->unsubscribe_folder = unsubscribe_folder;
	stub_class->is_subscribed_folder = is_subscribed_folder;
}

static void
init (GObject *object)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (object);

	mse->folders_by_name = g_hash_table_new_full (g_str_hash, g_str_equal,
						      NULL, free_folder);
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
free_folder (gpointer value)
{
	MailStubExchangeFolder *mfld = value;
	gint i;

	d(g_print ("%s:%s:%d: freeing mfld: name=[%s]\n", __FILE__, __PRETTY_FUNCTION__, __LINE__,
		   mfld->name));

	e_folder_exchange_unsubscribe (mfld->folder);
	g_signal_handlers_disconnect_by_func (mfld->folder, storage_folder_changed, mfld);
	g_object_unref (mfld->folder);
	mfld->folder = NULL;

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
		g_hash_table_destroy (mse->folders_by_name);
		mse->folders_by_name = NULL;
	}

	if (mse->ctx) {
		g_object_unref (mse->ctx);
		mse->ctx = NULL;
	}

	if (mse->new_folder_id != 0) {
		g_signal_handler_disconnect (mse->account, mse->new_folder_id);
		mse->new_folder_id = 0;
		g_signal_handler_disconnect (mse->account, mse->removed_folder_id);
		mse->removed_folder_id = 0;
	}

/*	if (g_signal_handler_is_connected (G_OBJECT (global_exchange_component),
					   offline_listener_handler_id)) {
		g_signal_handler_disconnect (G_OBJECT (global_exchange_component),
					      offline_listener_handler_id);
		offline_listener_handler_id = 0;
	}
*/
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

E2K_MAKE_TYPE (mail_stub_exchange, MailStubExchange, class_init, init, PARENT_TYPE)

static MailStubExchangeFolder *
folder_from_name (MailStubExchange *mse, const gchar *folder_name,
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

	if ((perms == MAPI_ACCESS_MODIFY || perms == MAPI_ACCESS_DELETE) &&
	    !(mfld->access & perms)) {
		/* try with MAPI_ACCESS_CREATE_CONTENTS */
		perms = MAPI_ACCESS_CREATE_CONTENTS;
	}

	if (perms && !(mfld->access & perms)) {
		if (!background)
			mail_stub_return_error (MAIL_STUB (mse), _("Permission denied"));
		return NULL;
	}

	mfld->last_activity = time (NULL);
	return mfld;
}

static void
folder_changed (MailStubExchangeFolder *mfld)
{
	e_folder_set_unread_count (mfld->folder, mfld->unread_count);
}

static gint
find_message_index (MailStubExchangeFolder *mfld, gint seq)
{
	MailStubExchangeMessage *mmsg;
	gint low, high, mid;

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
find_message (MailStubExchangeFolder *mfld, const gchar *uid)
{
	return g_hash_table_lookup (mfld->messages_by_uid, uid);
}

static inline MailStubExchangeMessage *
find_message_by_href (MailStubExchangeFolder *mfld, const gchar *href)
{
	return g_hash_table_lookup (mfld->messages_by_href, href);
}

static MailStubExchangeMessage *
new_message (const gchar *uid, const gchar *uri, guint32 seq, guint32 flags)
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
message_remove_at_index (MailStub *stub, MailStubExchangeFolder *mfld, gint index)
{
	MailStubExchangeMessage *mmsg;

	mmsg = mfld->messages->pdata[index];
	d(printf("Deleting mmsg %p\n", mmsg));
	g_static_rec_mutex_lock (&g_changed_msgs_mutex);
	g_ptr_array_remove_index (mfld->messages, index);
	g_hash_table_remove (mfld->messages_by_uid, mmsg->uid);
	if (mmsg->href)
		g_hash_table_remove (mfld->messages_by_href, mmsg->href);
	if (!(mmsg->flags & MAIL_STUB_MESSAGE_SEEN)) {
		mfld->unread_count--;
		folder_changed (mfld);
	}
	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	if (mmsg->change_mask || mmsg->tag_updates) {
		gint i;

		g_static_rec_mutex_lock (&g_changed_msgs_mutex);

		for (i = 0; i < mfld->changed_messages->len; i++) {
			if (mfld->changed_messages->pdata[i] == (gpointer)mmsg) {
				g_ptr_array_remove_index_fast (mfld->changed_messages, i);
				break;
			}
		}
		g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

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
message_removed (MailStub *stub, MailStubExchangeFolder *mfld, const gchar *href)
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
return_tag (MailStubExchangeFolder *mfld, const gchar *uid,
	    const gchar *name, const gchar *value)
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

static const gchar *
uidstrip (const gchar *repl_uid)
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
notify_cb (E2kContext *ctx, const gchar *uri,
	   E2kContextChangeType type, gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	time_t now;

	if (type == E2K_CONTEXT_OBJECT_ADDED)
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
got_folder_error (MailStubExchangeFolder *mfld, const gchar *error)
{
	mail_stub_return_error (MAIL_STUB (mfld->mse), error);
	free_folder (mfld);
}

static const gchar *open_folder_sync_props[] = {
	E2K_PR_REPL_UID,
	PR_INTERNET_ARTICLE_NUMBER,
	PR_ACTION_FLAG,
	PR_IMPORTANCE,
	PR_DELEGATED_BY_RULE,
	E2K_PR_HTTPMAIL_READ,
	E2K_PR_HTTPMAIL_MESSAGE_FLAG,
	E2K_PR_MAILHEADER_REPLY_BY,
	E2K_PR_MAILHEADER_COMPLETED
};
static const gint n_open_folder_sync_props = sizeof (open_folder_sync_props) / sizeof (open_folder_sync_props[0]);

static const gchar *open_folder_props[] = {
	PR_ACCESS,
	PR_DELETED_COUNT_TOTAL
};
static const gint n_open_folder_props = sizeof (open_folder_props) / sizeof (open_folder_props[0]);

static void
mse_get_folder_online_sync_updates (gpointer key, gpointer value,
				    gpointer user_data)
{
	guint index, seq, i;
	MailStubExchangeFolder *mfld = (MailStubExchangeFolder *)user_data;
	/*MailStub *stub = MAIL_STUB (mfld->mse);*/
	MailStubExchangeMessage *mmsg = NULL;

	index = GPOINTER_TO_UINT (key);
	seq = GPOINTER_TO_UINT (value);

	g_static_rec_mutex_lock (&g_changed_msgs_mutex);

	/* Camel DB Summary changes are not fetching all the messages at start-up.
	   Use this else it would crash badly.
	*/
	if (index >= mfld->messages->len) {
		g_static_rec_mutex_unlock (&g_changed_msgs_mutex);
		return;
	}

	mmsg = mfld->messages->pdata[index];
	if (mmsg->seq != seq) {
		for (i = 0; i < mfld->messages->len; i++) {
			mmsg = mfld->messages->pdata[i];
			if (mmsg->seq == seq)
				break;
		}
		seq = i;
	}
	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	/* FIXME FIXME FIXME: Some miscalculation happens here,though,
	not a serious one
	*/
	/* message_remove_at_index already handles lock/unlock */
	/*message_remove_at_index (stub, mfld, seq);*/

}
static gboolean
get_folder_contents_online (MailStubExchangeFolder *mfld, gboolean background)
{
	MailStubExchangeMessage *mmsg, *mmsg_cpy;
	MailStub *stub = MAIL_STUB (mfld->mse);
	E2kHTTPStatus status;
	gboolean readonly = FALSE;
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	const gchar *prop, *uid;
	guint32 article_num, camel_flags, high_article_num;
	gint i, total = -1;
	guint m;

	GPtrArray *msgs_copy = NULL;
	GHashTable *rm_idx_uid = NULL;

	/* Make a copy of the mfld->messages array for our processing */
	msgs_copy = g_ptr_array_new ();

	/* Store the index/seq of the messages to be removed from mfld->messages */
	rm_idx_uid = g_hash_table_new (g_direct_hash, g_direct_equal);

	g_static_rec_mutex_lock (&g_changed_msgs_mutex);
	for (i = 0; i < mfld->messages->len; i++) {
		mmsg = mfld->messages->pdata[i];
		mmsg_cpy = new_message (mmsg->uid, mmsg->href, mmsg->seq, mmsg->flags);
		g_ptr_array_add (msgs_copy, mmsg_cpy);
	}
	high_article_num = 0;
	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
					   E2K_RELOP_EQ, FALSE),
		NULL);

	iter = e_folder_exchange_search_start (mfld->folder, NULL,
					       open_folder_sync_props,
					       n_open_folder_sync_props,
					       rn, E2K_PR_DAV_CREATION_DATE,
					       TRUE);
	e2k_restriction_unref (rn);

	m = 0;
	total = e2k_result_iter_get_total (iter);
	while (m < msgs_copy->len && (result = e2k_result_iter_next (iter))) {
		prop = e2k_properties_get_prop (result->props,
						PR_INTERNET_ARTICLE_NUMBER);
		if (!prop)
			continue;
		article_num = strtoul (prop, NULL, 10);

		prop = e2k_properties_get_prop (result->props,
						E2K_PR_REPL_UID);
		if (!prop)
			continue;
		uid = uidstrip (prop);

		camel_flags = mail_util_props_to_camel_flags (result->props,
							      !readonly);

		mmsg_cpy = msgs_copy->pdata[m];
		while (strcmp (uid, mmsg_cpy->uid)) {
			/* Remove mmsg from our msgs_copy array */
			g_ptr_array_remove_index (msgs_copy, m);

			/* Put the index/uid as key/value in the rm_idx_uid hashtable.
			   This hashtable will be used to sync with mfld->messages.
			 */
			g_hash_table_insert (rm_idx_uid, GUINT_TO_POINTER(m),
					     GUINT_TO_POINTER(mmsg_cpy->seq));
			g_free (mmsg_cpy->uid);
			g_free (mmsg_cpy->href);
			g_free (mmsg_cpy);

			if (m == msgs_copy->len) {
				mmsg_cpy = NULL;
				if (article_num < high_article_num)
					high_article_num = article_num - 1;
				break;
			}
			mmsg_cpy = msgs_copy->pdata[m];
		}
		if (!mmsg_cpy)
			break;

		if (article_num > high_article_num)
			high_article_num = article_num;

		g_static_rec_mutex_lock (&g_changed_msgs_mutex);
		mmsg = mfld->messages->pdata[m];

		/* Validate mmsg == mmsg_cpy - this may fail if user has deleted some messages,
		   while we were updating in a separate thread.
		*/
		if (mmsg->seq != mmsg_cpy->seq) {
			/* We don't want to scan all of mfld->messages, as some new messages
			   would have got added to the array and hence restrict to the original
			   array of messages that we loaded from summary.
			*/
			for (i = 0; i < msgs_copy->len; i++) {
				mmsg = mfld->messages->pdata[i];
				if (mmsg->seq == mmsg_cpy->seq)
					break;
			}
		}

		if (!mmsg->href) {
			mmsg->href = g_strdup (result->href);
			if (mmsg_cpy->href)
				g_free (mmsg_cpy->href);
			mmsg_cpy->href = g_strdup (result->href);
			/* Do not allow duplicates */
			if (!g_hash_table_lookup (mfld->messages_by_href, mmsg->href))
				g_hash_table_insert (mfld->messages_by_href, mmsg->href, mmsg);
		}

		if (mmsg->flags != camel_flags)
			change_flags (mfld, mmsg, camel_flags);

		g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

		if (article_num > high_article_num)
			high_article_num = article_num;

		prop = e2k_properties_get_prop (result->props, E2K_PR_HTTPMAIL_MESSAGE_FLAG);
		if (prop)
			return_tag (mfld, mmsg->uid, "follow-up", prop);
		prop = e2k_properties_get_prop (result->props, E2K_PR_MAILHEADER_REPLY_BY);
		if (prop)
			return_tag (mfld, mmsg->uid, "due-by", prop);
		prop = e2k_properties_get_prop (result->props, E2K_PR_MAILHEADER_COMPLETED);
		if (prop)
			return_tag (mfld, mmsg->uid, "completed-on", prop);

		m++;
#if 0
		if (!background) {
			mail_stub_return_progress (stub, (m * 100) / total);
		}
#endif
	}

	/* If there are further messages beyond mfld->messages->len,
	 * then that means camel doesn't know about them yet, and so
	 * we need to ignore them for a while. But if any of them have
	 * an article number lower than the highest article number
	 * we've seen, bump high_article_num down so that that message
	 * gets caught by refresh_info later too.
	 */
	while ((result = e2k_result_iter_next (iter))) {
		prop = e2k_properties_get_prop (result->props,
						PR_INTERNET_ARTICLE_NUMBER);
		if (prop) {
			article_num = strtoul (prop, NULL, 10);
			if (article_num <= high_article_num)
				high_article_num = article_num - 1;
		}

		m++;
#if 0
		if (!background) {
			mail_stub_return_progress (stub, (m * 100) / total);
		}
#endif
	}
	status = e2k_result_iter_free (iter);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("got_folder: %d", status);
		if (!background) {
			got_folder_error (mfld, _("Could not open folder"));
		}
		return FALSE;
	}

	/* Discard remaining messages that no longer exist.
	   Do not increment 'i', because the remove_index is decrementing array length. */
	for (i = 0; i < msgs_copy->len;) {
		mmsg_cpy = msgs_copy->pdata[i];
		if (!mmsg_cpy->href) {
			/* Put the index/uid as key/value in the rm_idx_uid hashtable.
			   This hashtable will be used to sync with mfld->messages.
			 */
			g_hash_table_insert (rm_idx_uid, GUINT_TO_POINTER(m),
					     GUINT_TO_POINTER(mmsg_cpy->seq));
		}

		/* Remove mmsg from our msgs_copy array */
		g_ptr_array_remove_index (msgs_copy, i);

		g_free (mmsg_cpy->uid);
		g_free (mmsg_cpy->href);
		g_free (mmsg_cpy);
	}

	g_static_rec_mutex_lock (&g_changed_msgs_mutex);
	mfld->high_article_num = high_article_num;
	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_SET_ARTICLE_NUM,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_UINT32, mfld->high_article_num,
			       CAMEL_STUB_ARG_END);

	g_hash_table_foreach (rm_idx_uid, mse_get_folder_online_sync_updates,
			      mfld);

	g_ptr_array_free (msgs_copy, TRUE);
	g_hash_table_destroy (rm_idx_uid);

	return TRUE;
}

struct _get_folder_thread_data {
	MailStubExchangeFolder *mfld;
	gboolean background;
};

static gpointer
get_folder_contents_online_func (gpointer data)
{
	MailStubExchangeFolder* mfld;
	gboolean background;

	struct _get_folder_thread_data *gf_thread_data = (struct _get_folder_thread_data *)data;
	if (!gf_thread_data)
		return NULL;

	mfld = gf_thread_data->mfld;
	background = gf_thread_data->background;

	get_folder_contents_online (mfld, background);

	g_free (gf_thread_data);

	return NULL;
}

static gboolean
get_folder_online (MailStubExchangeFolder *mfld, gboolean background)
{
	MailStub *stub = MAIL_STUB (mfld->mse);
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;
	gboolean readonly;
	const gchar *prop;

	mfld->changed_messages = g_ptr_array_new ();

	status = e_folder_exchange_propfind (mfld->folder, NULL,
					     open_folder_props,
					     n_open_folder_props,
					     &results, &nresults);
	if (status == E2K_HTTP_UNAUTHORIZED) {
		if (!background) {
			got_folder_error (mfld, _("Could not open folder: Permission denied"));
		}
		return FALSE;
	} else if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("got_folder_props: %d", status);
		if (!background) {
			got_folder_error (mfld, _("Could not open folder"));
		}
		return FALSE;
	}

	if (nresults) {
		prop = e2k_properties_get_prop (results[0].props, PR_ACCESS);
		if (prop)
			mfld->access = atoi (prop);
		else
			mfld->access = ~0;
	} else
		mfld->access = ~0;

	if (!(mfld->access & MAPI_ACCESS_READ)) {
		if (!background) {
			got_folder_error (mfld, _("Could not open folder: Permission denied"));
		}
		if (nresults)
			e2k_results_free (results, nresults);
		return FALSE;
	}
	readonly = (mfld->access & (MAPI_ACCESS_MODIFY | MAPI_ACCESS_CREATE_CONTENTS)) == 0;

	prop = e2k_properties_get_prop (results[0].props, PR_DELETED_COUNT_TOTAL);
	if (prop)
		mfld->deleted_count = atoi (prop);

	/*
	   TODO: Varadhan - June 16, 2007 - Compare deleted_count with
	   that of CamelFolder and appropriately sync mfld->messages.
	   Also, sync flags and camel_flags of all messages - No reliable
	   way to fetch only changed messages as Read/UnRead flags do not
	   change the PR_LAST_MODIFICATION_TIME property of a message.
	*/
	if (g_hash_table_size (mfld->messages_by_href) < 1) {
		if (!get_folder_contents_online (mfld, background))
			return FALSE;
	} else {
		struct _get_folder_thread_data *gf_thread_data = NULL;

		gf_thread_data = g_new0 (struct _get_folder_thread_data, 1);
		gf_thread_data->mfld = mfld;
		gf_thread_data->background = background;

		/* FIXME: Pass a GError and handle the error */
		g_thread_create (get_folder_contents_online_func,
				 gf_thread_data, FALSE,
				 NULL);
	}

	e_folder_exchange_subscribe (mfld->folder,
				     E2K_CONTEXT_OBJECT_ADDED, 30,
				     notify_cb, mfld);
	e_folder_exchange_subscribe (mfld->folder,
				     E2K_CONTEXT_OBJECT_REMOVED, 30,
				     notify_cb, mfld);
	e_folder_exchange_subscribe (mfld->folder,
				     E2K_CONTEXT_OBJECT_MOVED, 30,
				     notify_cb, mfld);
	if (background) {
		mail_stub_push_changes (stub);
	}
	if (nresults)
		e2k_results_free (results, nresults);
	return TRUE;
}

static void
get_folder (MailStub *stub, const gchar *name, gboolean create,
	    GPtrArray *uids, GByteArray *flags, GPtrArray *hrefs,
	    guint32 high_article_num)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;
	EFolder *folder;
	gchar *path;
	const gchar *outlook_class;
	guint32 camel_flags;
	gint i, mode;
	ExchangeHierarchy *hier;

	path = g_strdup_printf ("/%s", name);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder && !create) {
		mail_stub_return_error (stub, _("No such folder"));
		g_free (path);
		return;
	} else if (!folder) {
		ExchangeAccountFolderResult result;

		result = exchange_account_create_folder (mse->account, path, "mail");
		folder = exchange_account_get_folder (mse->account, path);
		if (result != EXCHANGE_ACCOUNT_FOLDER_OK || !folder) {
			mail_stub_return_error (stub, _("Could not create folder."));
			g_free (path);
			return;
		}
	}
	g_free (path);

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

		if (hrefs->pdata[i] && *((gchar *)hrefs->pdata[i])) {
			mmsg->href = g_strdup (hrefs->pdata[i]);
			g_hash_table_insert (mfld->messages_by_href, mmsg->href, mmsg);
		}
		if (!(mmsg->flags & MAIL_STUB_MESSAGE_SEEN))
			mfld->unread_count++;
	}

	mfld->high_article_num = high_article_num;

	exchange_component_is_offline (global_exchange_component, &mode);
	if (mode == ONLINE_MODE) {
		if (!get_folder_online (mfld, FALSE)) {
			return;
		}
	}
	g_signal_connect (mfld->folder, "changed",
			  G_CALLBACK (storage_folder_changed), mfld);

	g_hash_table_insert (mse->folders_by_name, (gchar *)mfld->name, mfld);
	folder_changed (mfld);

	camel_flags = 0;
	if ((mfld->access & (MAPI_ACCESS_MODIFY | MAPI_ACCESS_CREATE_CONTENTS)) == 0)
		camel_flags |= CAMEL_STUB_FOLDER_READONLY;
	if (mse->account->filter_inbox && (mfld->folder == mse->inbox))
		camel_flags |= CAMEL_STUB_FOLDER_FILTER;
	if (mse->account->filter_junk) {
		if ((mfld->folder != mse->deleted_items) &&
		    ((mfld->folder == mse->inbox) ||
		    !mse->account->filter_junk_inbox_only))
			camel_flags |= CAMEL_STUB_FOLDER_FILTER_JUNK;
	}
	if (mfld->type == MAIL_STUB_EXCHANGE_FOLDER_POST)
		camel_flags |= CAMEL_STUB_FOLDER_POST;

	hier = e_folder_exchange_get_hierarchy (mfld->folder);

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_UINT32, camel_flags,
			       CAMEL_STUB_ARG_STRING, hier->source_uri,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);
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
sync_folder (MailStub *stub, const gchar *folder_name)
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

static const gchar *sync_deleted_props[] = {
	PR_DELETED_COUNT_TOTAL,
	E2K_PR_DAV_VISIBLE_COUNT
};
static const gint n_sync_deleted_props = sizeof (sync_deleted_props) / sizeof (sync_deleted_props[0]);

static gboolean
sync_deletions (MailStubExchange *mse, MailStubExchangeFolder *mfld)
{
	MailStub *stub = MAIL_STUB (mse);
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;
	const gchar *prop;
	gint deleted_count = -1, visible_count = -1, mode;
	E2kRestriction *rn;
	E2kResultIter *iter;
	E2kResult *result;
	gint my_i, read;
	MailStubExchangeMessage *mmsg;
	gboolean changes = FALSE;
	GHashTable *known_messages;

	exchange_component_is_offline (global_exchange_component, &mode);
	if (mode != ONLINE_MODE)
		return FALSE;

	status = e_folder_exchange_propfind (mfld->folder, NULL,
					     sync_deleted_props,
					     n_sync_deleted_props,
					     &results, &nresults);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status) || !nresults) {
		g_warning ("got_sync_deleted_props: %d", status);
		return FALSE;
	}

	prop = e2k_properties_get_prop (results[0].props,
					PR_DELETED_COUNT_TOTAL);
	if (prop)
		deleted_count = atoi (prop);

	prop = e2k_properties_get_prop (results[0].props,
					E2K_PR_DAV_VISIBLE_COUNT);
	if (prop)
		visible_count = atoi (prop);

	e2k_results_free (results, nresults);

	if (visible_count >= mfld->messages->len) {
		if (mfld->deleted_count == deleted_count)
			return FALSE;

		if (mfld->deleted_count == 0) {
			mfld->deleted_count = deleted_count;
			return FALSE;
		}
	}

	prop = E2K_PR_HTTPMAIL_READ;
	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
					   E2K_RELOP_EQ, FALSE),
		NULL);

	iter = e_folder_exchange_search_start (mfld->folder, NULL,
					       &prop, 1, rn,
					       E2K_PR_DAV_CREATION_DATE,
					       FALSE);
	e2k_restriction_unref (rn);

	known_messages = g_hash_table_new (g_direct_hash, g_direct_equal);

	my_i = mfld->messages->len - 1;
	while ((result = e2k_result_iter_next (iter))) {
		mmsg = find_message_by_href (mfld, result->href);
		if (!mmsg) {
			/* oops, message from the server not found in our list;
			   return failure to possibly do full resync again? */
			g_message ("%s: Oops, message %s not found in %s", G_STRFUNC, result->href, mfld->name);
			continue;
		}

		g_hash_table_insert (known_messages, mmsg, mmsg);

		/* See if its read flag changed while we weren't watching */
		prop = e2k_properties_get_prop (result->props,
						E2K_PR_HTTPMAIL_READ);
		read = (prop && atoi (prop)) ? MAIL_STUB_MESSAGE_SEEN : 0;
		if ((mmsg->flags & MAIL_STUB_MESSAGE_SEEN) != read) {
			changes = TRUE;
			change_flags (mfld, mmsg,
				      mmsg->flags ^ MAIL_STUB_MESSAGE_SEEN);
		}

	}
	status = e2k_result_iter_free (iter);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		g_warning ("synced_deleted: %d", status);

	/* Clear out removed messages from mfld */
	for (my_i = mfld->messages->len - 1; my_i >= 0; my_i --) {
		mmsg = mfld->messages->pdata[my_i];
		if (!g_hash_table_lookup (known_messages, mmsg)) {
			mfld->deleted_count++;
			message_remove_at_index (stub, mfld, my_i);
			changes = TRUE;
		}
	}

	g_hash_table_destroy (known_messages);

	if (changes)
		mail_stub_push_changes (stub);

	return changes;
}

struct refresh_message {
	gchar *uid, *href, *headers, *fff, *reply_by, *completed;
	guint32 flags, size, article_num;
};

static gint
refresh_message_compar (gconstpointer a, gconstpointer b)
{
	const struct refresh_message *rma = a, *rmb = b;

	return strcmp (rma->uid, rmb->uid);
}

static const gchar *mapi_message_props[] = {
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
static const gint n_mapi_message_props = sizeof (mapi_message_props) / sizeof (mapi_message_props[0]);

static const gchar *new_message_props[] = {
	E2K_PR_REPL_UID,
	PR_INTERNET_ARTICLE_NUMBER,
	PR_TRANSPORT_MESSAGE_HEADERS,
	E2K_PR_HTTPMAIL_READ,
	E2K_PR_HTTPMAIL_HAS_ATTACHMENT,
	PR_ACTION_FLAG,
	PR_IMPORTANCE,
	PR_DELEGATED_BY_RULE,
	E2K_PR_HTTPMAIL_MESSAGE_FLAG,
	E2K_PR_MAILHEADER_REPLY_BY,
	E2K_PR_MAILHEADER_COMPLETED,
	E2K_PR_DAV_CONTENT_LENGTH
};
static const gint num_new_message_props = sizeof (new_message_props) / sizeof (new_message_props[0]);

static void
refresh_folder_internal (MailStub *stub, MailStubExchangeFolder *mfld,
			 gboolean background)
{
	E2kRestriction *rn;
	GArray *messages;
	GHashTable *mapi_message_hash;
	GPtrArray *mapi_hrefs;
	gboolean has_read_flag = (mfld->access & MAPI_ACCESS_READ);
	E2kResultIter *iter;
	E2kResult *result;
	gchar *prop, *uid, *href;
	struct refresh_message rm, *rmp;
	E2kHTTPStatus status;
	gint got, total, i, n, mode;
	gpointer key, value;
	MailStubExchangeMessage *mmsg;

	g_object_ref (stub);

	exchange_component_is_offline (global_exchange_component, &mode);
	if (mode == OFFLINE_MODE) {
		if (background)
			mail_stub_push_changes (stub);
		else
			mail_stub_return_ok (stub);

		g_object_unref (stub); /* Is this needed ? */
		return;
	}

	messages = g_array_new (FALSE, FALSE, sizeof (struct refresh_message));
	mapi_message_hash = g_hash_table_new (g_str_hash, g_str_equal);
	mapi_hrefs = g_ptr_array_new ();

	/*
	 * STEP 1: Fetch information about new messages, including SMTP
	 * headers when available.
	 */

	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_COLLECTION,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_bool (E2K_PR_DAV_IS_HIDDEN,
					   E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_int (PR_INTERNET_ARTICLE_NUMBER,
					  E2K_RELOP_GT,
					  mfld->high_article_num),
		NULL);
	iter = e_folder_exchange_search_start (mfld->folder, NULL,
					       new_message_props,
					       num_new_message_props,
					       rn, NULL, TRUE);
	e2k_restriction_unref (rn);

	got = 0;
	total = e2k_result_iter_get_total (iter);
	while ((result = e2k_result_iter_next (iter))) {
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (result->status)) {
			g_message ("%s: got unsuccessful at %s (%s)", G_STRFUNC, mfld->name, result->href ? result->href : "[null]");
			continue;
		}

		uid = e2k_properties_get_prop (result->props, E2K_PR_REPL_UID);
		if (!uid)
			continue;
		prop = e2k_properties_get_prop (result->props,
						PR_INTERNET_ARTICLE_NUMBER);
		if (!prop)
			continue;

		rm.uid = g_strdup (uidstrip (uid));
		rm.href = g_strdup (result->href);
		rm.article_num = strtoul (prop, NULL, 10);

		rm.flags = mail_util_props_to_camel_flags (result->props,
							   has_read_flag);

		prop = e2k_properties_get_prop (result->props,
						E2K_PR_HTTPMAIL_MESSAGE_FLAG);
		if (prop)
			rm.fff = g_strdup (prop);
		else
			rm.fff = NULL;
		prop = e2k_properties_get_prop (result->props,
						E2K_PR_MAILHEADER_REPLY_BY);
		if (prop)
			rm.reply_by = g_strdup (prop);
		else
			rm.reply_by = NULL;
		prop = e2k_properties_get_prop (result->props,
						E2K_PR_MAILHEADER_COMPLETED);
		if (prop)
			rm.completed = g_strdup (prop);
		else
			rm.completed = NULL;

		prop = e2k_properties_get_prop (result->props,
						E2K_PR_DAV_CONTENT_LENGTH);
		rm.size = prop ? strtoul (prop, NULL, 10) : 0;

		rm.headers = mail_util_extract_transport_headers (result->props);

		g_array_append_val (messages, rm);

		if (rm.headers) {
			got++;
			mail_stub_return_progress (stub, (got * 100) / total);
		} else {
			href = strrchr (rm.href, '/');
			if (!href++)
				href = rm.href;

			g_hash_table_insert (mapi_message_hash, href,
					     GINT_TO_POINTER (messages->len - 1));
			g_ptr_array_add (mapi_hrefs, href);
		}
	}
	status = e2k_result_iter_free (iter);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("got_new_smtp_messages: %d", status);
		if (!background)
			mail_stub_return_error (stub, _("Could not get new messages"));
		goto done;
	}

	if (mapi_hrefs->len == 0)
		goto return_data;

	/*
	 * STEP 2: Fetch MAPI property data for non-SMTP messages.
	 */

	iter = e_folder_exchange_bpropfind_start (mfld->folder, NULL,
						  (const gchar **)mapi_hrefs->pdata,
						  mapi_hrefs->len,
						  mapi_message_props,
						  n_mapi_message_props);
	while ((result = e2k_result_iter_next (iter))) {
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (result->status))
			continue;

		href = strrchr (result->href, '/');
		if (!href++)
			href = result->href;

		if (!g_hash_table_lookup_extended (mapi_message_hash, href,
						   &key, &value))
			continue;
		n = GPOINTER_TO_INT (value);

		rmp = &((struct refresh_message *)messages->data)[n];
		rmp->headers = mail_util_mapi_to_smtp_headers (result->props);

		got++;
		mail_stub_return_progress (stub, (got * 100) / total);
	}
	status = e2k_result_iter_free (iter);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("got_new_mapi_messages: %d", status);
		if (!background)
			mail_stub_return_error (stub, _("Could not get new messages"));
		goto done;
	}

	/*
	 * STEP 3: Organize the data, update our records and Camel's
	 */

 return_data:
	mail_stub_return_progress (stub, 100);
	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	qsort (messages->data, messages->len,
	       sizeof (rm), refresh_message_compar);
	for (i = 0; i < messages->len; i++) {
		rm = g_array_index (messages, struct refresh_message, i);

		/* If we already have a message with this UID, then
		 * that means it's not a new message, it's just that
		 * the article number changed.
		 */
		mmsg = find_message (mfld, rm.uid);
		if (mmsg) {
			if (rm.flags != mmsg->flags)
				change_flags (mfld, mmsg, rm.flags);
		} else {
			if (g_hash_table_lookup (mfld->messages_by_href, rm.href)) {
				mfld->deleted_count++;
				message_removed (stub, mfld, rm.href);
			}

			mmsg = new_message (rm.uid, rm.href, mfld->seq++, rm.flags);
			g_ptr_array_add (mfld->messages, mmsg);
			g_hash_table_insert (mfld->messages_by_uid,
					     mmsg->uid, mmsg);
			g_hash_table_insert (mfld->messages_by_href,
					     mmsg->href, mmsg);

			if (!(mmsg->flags & MAIL_STUB_MESSAGE_SEEN))
				mfld->unread_count++;

			mail_stub_return_data (stub, CAMEL_STUB_RETVAL_NEW_MESSAGE,
					       CAMEL_STUB_ARG_FOLDER, mfld->name,
					       CAMEL_STUB_ARG_STRING, rm.uid,
					       CAMEL_STUB_ARG_UINT32, rm.flags,
					       CAMEL_STUB_ARG_UINT32, rm.size,
					       CAMEL_STUB_ARG_STRING, rm.headers,
					       CAMEL_STUB_ARG_STRING, rm.href,
					       CAMEL_STUB_ARG_END);
		}

		if (rm.article_num > mfld->high_article_num) {
			mfld->high_article_num = rm.article_num;
			mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_SET_ARTICLE_NUM,
					       CAMEL_STUB_ARG_FOLDER, mfld->name,
					       CAMEL_STUB_ARG_UINT32, mfld->high_article_num,
					       CAMEL_STUB_ARG_END);
		}

		if (rm.fff)
			return_tag (mfld, rm.uid, "follow-up", rm.fff);
		if (rm.reply_by)
			return_tag (mfld, rm.uid, "due-by", rm.reply_by);
		if (rm.completed)
			return_tag (mfld, rm.uid, "completed-on", rm.completed);
	}
	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	mfld->scanned = TRUE;
	folder_changed (mfld);

	if (background)
		mail_stub_push_changes (stub);
	else
		mail_stub_return_ok (stub);

 done:
	/*
	 * CLEANUP
	 */
	rmp = (struct refresh_message *)messages->data;
	for (i = 0; i < messages->len; i++) {
		g_free (rmp[i].uid);
		g_free (rmp[i].href);
		g_free (rmp[i].headers);
		g_free (rmp[i].fff);
		g_free (rmp[i].reply_by);
		g_free (rmp[i].completed);
	}
	g_array_free (messages, TRUE);

	g_hash_table_destroy (mapi_message_hash);
	g_ptr_array_free (mapi_hrefs, TRUE);

	g_object_unref (stub);
}

static void
sync_count (MailStub *stub, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	guint32 unread_count = 0, visible_count = 0;

	mfld = folder_from_name (mse, folder_name, 0, FALSE);
	if (!mfld) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
				       CAMEL_STUB_ARG_UINT32, unread_count,
				       CAMEL_STUB_ARG_UINT32, visible_count,
				       CAMEL_STUB_ARG_END);
		mail_stub_return_ok (stub);
		return;
	}

	unread_count = mfld->unread_count;
	visible_count = mfld->messages->len;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_UINT32, unread_count,
			       CAMEL_STUB_ARG_UINT32, visible_count,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);
}

static void
refresh_folder (MailStub *stub, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;

	mfld = folder_from_name (mse, folder_name, 0, FALSE);
	if (!mfld)
		return;

	refresh_folder_internal (stub, mfld, FALSE);
	if (!sync_deletions (mse, mfld)) {
		/* sync_deletions didn't call this, thus call it now */
		mail_stub_push_changes (stub);
	}
}

static void
expunge_uids (MailStub *stub, const gchar *folder_name, GPtrArray *uids)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;
	GPtrArray *hrefs;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	gint i, ndeleted;
	gboolean some_error = FALSE;

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

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	iter = e_folder_exchange_bdelete_start (mfld->folder, NULL,
						(const gchar **)hrefs->pdata,
						hrefs->len);
	ndeleted = 0;
	while ((result = e2k_result_iter_next (iter))) {
		if (result->status == E2K_HTTP_UNAUTHORIZED) {
			some_error = TRUE;
			continue;
		}
		message_removed (stub, mfld, result->href);
		mfld->deleted_count++;
		ndeleted++;

		mail_stub_return_progress (stub, ndeleted * 100 / hrefs->len);
	}
	status = e2k_result_iter_free (iter);

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
			       CAMEL_STUB_ARG_FOLDER, mfld->name,
			       CAMEL_STUB_ARG_END);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		g_warning ("expunged: %d", status);
		mail_stub_return_error (stub, _("Could not empty Deleted Items folder"));
	} else if (some_error) {
		mail_stub_return_error (stub, _("Permission denied. Could not delete certain messages."));
	} else {
		mail_stub_return_ok (stub);
	}

	g_ptr_array_free (hrefs, TRUE);
}

static void
mark_one_read (E2kContext *ctx, const gchar *uri, gboolean read)
{
	E2kProperties *props;
	E2kHTTPStatus status;

	props = e2k_properties_new ();
	e2k_properties_set_bool (props, E2K_PR_HTTPMAIL_READ, read);

	status = e2k_context_proppatch (ctx, NULL, uri, props, FALSE, NULL);
	e2k_properties_free (props);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		g_warning ("mark_one_read: %d", status);
}

static void
mark_read (EFolder *folder, GPtrArray *hrefs, gboolean read)
{
	E2kProperties *props;
	E2kResultIter *iter;
	E2kHTTPStatus status;

	props = e2k_properties_new ();
	e2k_properties_set_bool (props, E2K_PR_HTTPMAIL_READ, read);

	iter = e_folder_exchange_bproppatch_start (folder, NULL,
						   (const gchar **)hrefs->pdata,
						   hrefs->len, props, FALSE);
	e2k_properties_free (props);

	while (e2k_result_iter_next (iter))
		;
	status = e2k_result_iter_free (iter);

	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		g_warning ("mark_read: %d", status);
}

static gboolean
test_uri (E2kContext *ctx, const gchar *test_name, gpointer messages_by_href)
{
	return g_hash_table_lookup (messages_by_href, test_name) == NULL;
}

static void
append_message (MailStub *stub, const gchar *folder_name, guint32 flags,
		const gchar *subject, const gchar *data, gint length)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	E2kHTTPStatus status;
	gchar *ru_header = NULL, *repl_uid, *location = NULL;

	mfld = folder_from_name (mse, folder_name, MAPI_ACCESS_CREATE_CONTENTS, FALSE);
	if (!mfld)
		return;

	status = e_folder_exchange_put_new (mfld->folder, NULL, subject,
					    test_uri, mfld->messages_by_href,
					    "message/rfc822", data, length,
					    &location, &ru_header);
	if (status != E2K_HTTP_CREATED) {
		g_warning ("appended_message: %d", status);
		mail_stub_return_error (stub,
					status == E2K_HTTP_INSUFFICIENT_SPACE_ON_RESOURCE ?
					_("Could not append message; mailbox is over quota") :
					_("Could not append message"));
		return;
	}

	if (location) {
		if (flags & MAIL_STUB_MESSAGE_SEEN)
			mark_one_read (mse->ctx, location, TRUE);
		else
			mark_one_read (mse->ctx, location, FALSE);
	}

	if (ru_header && *ru_header == '<' && strlen (ru_header) > 3)
		repl_uid = g_strndup (ru_header + 1, strlen (ru_header) - 2);
	else
		repl_uid = NULL;
	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRING, repl_uid ? uidstrip (repl_uid) : "",
			       CAMEL_STUB_ARG_END);

	g_free (repl_uid);
	g_free (ru_header);
	g_free (location);

	mail_stub_return_ok (stub);
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
set_replied_flags (MailStubExchange *mse, MailStubExchangeMessage *mmsg)
{
	E2kProperties *props;
	E2kHTTPStatus status;

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

	status = e2k_context_proppatch (mse->ctx, NULL, mmsg->href, props,
					FALSE, NULL);
	e2k_properties_free (props);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		g_warning ("set_replied_flags: %d", status);
}

static void
set_important_flag (MailStubExchange *mse, MailStubExchangeMessage *mmsg)
{
	E2kProperties *props;
	E2kHTTPStatus status;

	props = e2k_properties_new ();

	if (mmsg->change_flags & MAIL_STUB_MESSAGE_FLAGGED) {
		e2k_properties_set_int (props, PR_IMPORTANCE, MAPI_IMPORTANCE_HIGH);
	}
	else {
		e2k_properties_set_int (props, PR_IMPORTANCE, MAPI_IMPORTANCE_NORMAL);
	}

	status = e2k_context_proppatch (mse->ctx, NULL, mmsg->href, props,
					FALSE, NULL);
	e2k_properties_free (props);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		g_warning ("set_important_flag: %d", status);
}

static void
update_tags (MailStubExchange *mse, MailStubExchangeMessage *mmsg)
{
	E2kProperties *props;
	const gchar *value;
	gint flag_status;
	E2kHTTPStatus status;

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

	status = e2k_context_proppatch (mse->ctx, NULL, mmsg->href, props,
					FALSE, NULL);
	e2k_properties_free (props);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		g_warning ("update_tags: %d", status);
}

static gboolean
process_flags (gpointer user_data)
{
	MailStubExchangeFolder *mfld = user_data;
	MailStubExchange *mse = mfld->mse;
	MailStubExchangeMessage *mmsg;
	GPtrArray *seen = NULL, *unseen = NULL, *deleted = NULL;
	gint i;
	guint32 hier_type = e_folder_exchange_get_hierarchy (mfld->folder)->type;

	g_static_rec_mutex_lock (&g_changed_msgs_mutex);

	for (i = 0; i < mfld->changed_messages->len; i++) {
		mmsg = mfld->changed_messages->pdata[i];
		d(printf("Process flags %p\n", mmsg));
		if (!mmsg->href) {
			d(g_print ("%s:%s:%d: mfld = [%s], type=[%d]\n", __FILE__, __GNUC_PRETTY_FUNCTION__,
				   __LINE__, mfld->name, mfld->type));
		}

		if (mmsg->change_mask & MAIL_STUB_MESSAGE_SEEN) {
			if (mmsg->change_flags & MAIL_STUB_MESSAGE_SEEN) {
				if (!seen)
					seen = g_ptr_array_new ();
				g_ptr_array_add (seen, g_strdup (strrchr (mmsg->href, '/') + 1));
				mmsg->flags |= MAIL_STUB_MESSAGE_SEEN;
			} else {
				if (!unseen)
					unseen = g_ptr_array_new ();
				g_ptr_array_add (unseen, g_strdup (strrchr (mmsg->href, '/') + 1));
				mmsg->flags &= ~MAIL_STUB_MESSAGE_SEEN;
			}
			mmsg->change_mask &= ~MAIL_STUB_MESSAGE_SEEN;
		}

		if (mmsg->change_mask & MAIL_STUB_MESSAGE_ANSWERED) {
			set_replied_flags (mse, mmsg);
			mmsg->change_mask &= ~(MAIL_STUB_MESSAGE_ANSWERED | MAIL_STUB_MESSAGE_ANSWERED_ALL);
		}

		if (mmsg->change_mask & MAIL_STUB_MESSAGE_FLAGGED) {
			set_important_flag (mse, mmsg);
			mmsg->change_mask &= ~MAIL_STUB_MESSAGE_FLAGGED;
		}

		if (mmsg->tag_updates)
			update_tags (mse, mmsg);

		if (!mmsg->change_mask)
			g_ptr_array_remove_index_fast (mfld->changed_messages, i--);
	}

	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	if (seen || unseen) {
		if (seen) {
			mark_read (mfld->folder, seen, TRUE);
			g_ptr_array_foreach (seen, (GFunc)g_free, NULL);
			g_ptr_array_free (seen, TRUE);
		}
		if (unseen) {
			mark_read (mfld->folder, unseen, FALSE);
			g_ptr_array_foreach (unseen, (GFunc)g_free, NULL);
			g_ptr_array_free (unseen, TRUE);
		}

		if (mfld->changed_messages->len == 0) {
			mfld->flag_timeout = 0;
			change_complete (mfld);
			return FALSE;
		} else
			return TRUE;
	}

	g_static_rec_mutex_lock (&g_changed_msgs_mutex);

	for (i = 0; i < mfld->changed_messages->len; i++) {
		mmsg = mfld->changed_messages->pdata[i];
		if (mmsg->change_mask & mmsg->change_flags & MAIL_STUB_MESSAGE_DELETED) {
			if (!deleted)
				deleted = g_ptr_array_new ();
			g_ptr_array_add (deleted, strrchr (mmsg->href, '/') + 1);
		}
	}
	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	if (deleted) {
		MailStub *stub = MAIL_STUB (mse);
		E2kResultIter *iter;
		E2kResult *result;
		E2kHTTPStatus status;

		change_pending (mfld);
		mfld->pending_delete_ops++;
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, mfld->name,
				       CAMEL_STUB_ARG_END);

		if (hier_type == EXCHANGE_HIERARCHY_PERSONAL) {
			iter = e_folder_exchange_transfer_start (mfld->folder, NULL,
								 mse->deleted_items,
								 deleted, TRUE);
		} else {
			/* This is for public folder hierarchy. We cannot move
			   a mail item deleted from a public folder to the
			   deleted items folder. This code updates the UI to
			   show the mail folder again if the deletion fails in
			   such public folder */
			iter = e_folder_exchange_bdelete_start (mfld->folder, NULL,
								(const gchar **)deleted->pdata,
								deleted->len);
		}
		g_ptr_array_free (deleted, FALSE);
		while ((result = e2k_result_iter_next (iter))) {
			if (hier_type == EXCHANGE_HIERARCHY_PERSONAL) {
				if (!e2k_properties_get_prop (result->props,
							      E2K_PR_DAV_LOCATION)) {
					continue;
				}
			} else if (result->status == E2K_HTTP_UNAUTHORIZED) {
				mail_stub_return_data (MAIL_STUB (mfld->mse),
						       CAMEL_STUB_RETVAL_CHANGED_FLAGS_EX,
						       CAMEL_STUB_ARG_FOLDER, mfld->name,
						       CAMEL_STUB_ARG_STRING, mmsg->uid,
						       CAMEL_STUB_ARG_UINT32, 0,
						       CAMEL_STUB_ARG_UINT32, MAIL_STUB_MESSAGE_DELETED,
						       CAMEL_STUB_ARG_END);
				continue;
			}

			message_removed (stub, mfld, result->href);
			mfld->deleted_count++;
		}
		status = e2k_result_iter_free (iter);

		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, mfld->name,
				       CAMEL_STUB_ARG_END);

		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			g_warning ("deleted: %d", status);

		mail_stub_push_changes (stub);
		mfld->pending_delete_ops--;
		change_complete (mfld);
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
	gint i;

	g_static_rec_mutex_lock (&g_changed_msgs_mutex);

	for (i=0; i<mfld->changed_messages->len; i++)  {
		if (mfld->changed_messages->pdata[i] == mmsg)
			break;
	}

	if (i == mfld->changed_messages->len) {
		change_pending (mfld);
		g_ptr_array_add (mfld->changed_messages, mmsg);
	}
	g_static_rec_mutex_unlock (&g_changed_msgs_mutex);

	if (mfld->flag_timeout)
		g_source_remove (mfld->flag_timeout);
	mfld->flag_timeout = g_timeout_add (1000, process_flags, mfld);
}

static void
set_message_flags (MailStub *stub, const gchar *folder_name, const gchar *uid,
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

#if 0
	/* If we allow camel stub to delete these messages hard way, it may
	   fail to delete a mail because of permissions, but will append
	   a mail in deleted items */

	if (mask & flags & MAIL_STUB_MESSAGE_DELETED) {
		ExchangeHierarchy *hier;

		hier = e_folder_exchange_get_hierarchy (mfld->folder);
		if (hier->type != EXCHANGE_HIERARCHY_PERSONAL)
			mask &= ~MAIL_STUB_MESSAGE_DELETED;
	}
#endif

	/* If there's nothing left to change, return. */
	if (!mask)
		return;

	mmsg->change_flags |= (flags & mask);
	mmsg->change_flags &= ~(~flags & mask);
	mmsg->change_mask |= mask;

	change_message (mse, mfld, mmsg);
}

static void
set_message_tag (MailStub *stub, const gchar *folder_name, const gchar *uid,
		 const gchar *name, const gchar *value)
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

static const gchar *stickynote_props[] = {
	E2K_PR_MAILHEADER_SUBJECT,
	E2K_PR_DAV_LAST_MODIFIED,
	E2K_PR_OUTLOOK_STICKYNOTE_COLOR,
	E2K_PR_OUTLOOK_STICKYNOTE_HEIGHT,
	E2K_PR_OUTLOOK_STICKYNOTE_WIDTH,
	E2K_PR_HTTPMAIL_TEXT_DESCRIPTION,
};
static const gint n_stickynote_props = sizeof (stickynote_props) / sizeof (stickynote_props[0]);

static E2kHTTPStatus
get_stickynote (E2kContext *ctx, E2kOperation *op, const gchar *uri,
		gchar **body, gint *len)
{
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;
	GString *message;

	status = e2k_context_propfind (ctx, op, uri,
				       stickynote_props, n_stickynote_props,
				       &results, &nresults);

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		message = mail_util_stickynote_to_rfc822 (results[0].props);
		*body = message->str;
		*len = message->len;
		g_string_free (message, FALSE);
		e2k_results_free (results, nresults);
	}

	return status;
}

static E2kHTTPStatus
build_message_from_document (E2kContext *ctx, E2kOperation *op,
			     const gchar *uri,
			     gchar **body, gint *len)
{
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;
	GString *message;
	gchar *headers;

	status = e2k_context_propfind (ctx, op, uri,
				       mapi_message_props,
				       n_mapi_message_props,
				       &results, &nresults);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return status;
	if (!nresults)
		return E2K_HTTP_MALFORMED;

	headers = mail_util_mapi_to_smtp_headers (results[0].props);

	message = g_string_new (headers);
	g_string_append_len (message, *body, *len);

	g_free (headers);
	g_free (*body);

	*len = message->len;
	*body = g_string_free (message, FALSE);

	e2k_results_free (results, nresults);
	return status;
}

static E2kHTTPStatus
unmangle_delegated_meeting_request (MailStubExchange *mse, E2kOperation *op,
				    const gchar *uri,
				    gchar **body, gint *len)
{
	const gchar *prop = PR_RCVD_REPRESENTING_EMAIL_ADDRESS;
	GString *message;
	gchar *delegator_dn, *delegator_uri, *delegator_folder_physical_uri = NULL;
	ExchangeAccount *account;
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus gcstatus;
	EFolder *folder = NULL;
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;
	MailUtilDemangleType unmangle_type = MAIL_UTIL_DEMANGLE_DELGATED_MEETING;

	status = e2k_context_propfind (mse->ctx, op, uri, &prop, 1,
				       &results, &nresults);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return status;
	if (!nresults)
		return E2K_HTTP_MALFORMED;

	delegator_dn = e2k_properties_get_prop (results[0].props, PR_RCVD_REPRESENTING_EMAIL_ADDRESS);
	if (!delegator_dn) {
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	account = mse->account;
	gc = exchange_account_get_global_catalog (account);
	if (!gc) {
		g_warning ("\nNo GC: could not unmangle meeting request");
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	gcstatus = e2k_global_catalog_lookup (
		gc, NULL, /* FIXME; cancellable */
		E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		delegator_dn, E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
		&entry);
	if (gcstatus != E2K_GLOBAL_CATALOG_OK) {
		g_warning ("\nGC lookup failed: could not unmangle meeting request");
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	delegator_uri = exchange_account_get_foreign_uri (
		account, entry, E2K_PR_STD_FOLDER_CALENDAR);

	if (delegator_uri) {
		folder = exchange_account_get_folder (account, delegator_uri);
		if (folder)
			delegator_folder_physical_uri = g_strdup (e_folder_get_physical_uri (folder));
		g_free (delegator_uri);
	}

	message = g_string_new_len (*body, *len);
	mail_util_demangle_meeting_related_message (message, entry->display_name,
						entry->email,
						delegator_folder_physical_uri,
						exchange_account_get_email_id (account),
						unmangle_type);
	g_free (*body);
	*body = message->str;
	*len = message->len;
	*body = g_string_free (message, FALSE);

	e2k_global_catalog_entry_free (gc, entry);
	g_free (delegator_folder_physical_uri);

	e2k_results_free (results, nresults);
	return E2K_HTTP_OK;
}

static E2kHTTPStatus
unmangle_meeting_request_in_subscribed_inbox (MailStubExchange *mse,
					      const gchar *delegator_email,
					      gchar **body, gint *len)
{
	GString *message;
	gchar *delegator_uri, *delegator_folder_physical_uri = NULL;
	ExchangeAccount *account;
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus gcstatus;
	EFolder *folder = NULL;
	MailUtilDemangleType unmangle_type = MAIL_UTIL_DEMANGLE_MEETING_IN_SUBSCRIBED_INBOX;

	account = mse->account;
	gc = exchange_account_get_global_catalog (account);
	if (!gc) {
		g_warning ("\nNo GC: could not unmangle meeting request in subscribed folder");
		return E2K_HTTP_OK;
	}

	gcstatus = e2k_global_catalog_lookup (
		gc, NULL, /* FIXME; cancellable */
		E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
		delegator_email, E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
		&entry);
	if (gcstatus != E2K_GLOBAL_CATALOG_OK) {
		g_warning ("\nGC lookup failed: could not unmangle meeting request in subscribed folder");
		return E2K_HTTP_OK;
	}

	delegator_uri = exchange_account_get_foreign_uri (
		account, entry, E2K_PR_STD_FOLDER_CALENDAR);

	if (delegator_uri) {
		folder = exchange_account_get_folder (account, delegator_uri);
		if (folder)
			delegator_folder_physical_uri = g_strdup (e_folder_get_physical_uri (folder));
		g_free (delegator_uri);
	}

	message = g_string_new_len (*body, *len);
	mail_util_demangle_meeting_related_message (message, entry->display_name,
						entry->email,
						delegator_folder_physical_uri,
						exchange_account_get_email_id (account),
						unmangle_type);
	g_free (*body);
	*body = message->str;
	*len = message->len;
	*body = g_string_free (message, FALSE);

	e2k_global_catalog_entry_free (gc, entry);
	g_free (delegator_folder_physical_uri);

	return E2K_HTTP_OK;
}

static E2kHTTPStatus
unmangle_sender_field (MailStubExchange *mse, E2kOperation *op,
				    const gchar *uri,
				    gchar **body, gint *len)
{
	const gchar *props[] = { PR_SENT_REPRESENTING_EMAIL_ADDRESS, PR_SENDER_EMAIL_ADDRESS };
	GString *message;
	gchar *delegator_dn, *sender_dn;
	ExchangeAccount *account;
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *delegator_entry;
	E2kGlobalCatalogEntry *sender_entry;
	E2kGlobalCatalogStatus gcstatus;
	E2kHTTPStatus status;
	E2kResult *results;
	gint nresults = 0;
	MailUtilDemangleType unmangle_type = MAIL_UTIL_DEMANGLE_SENDER_FIELD;

	status = e2k_context_propfind (mse->ctx, op, uri, props, 2,
				       &results, &nresults);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		return status;
	if (!nresults)
		return E2K_HTTP_MALFORMED;

	delegator_dn = e2k_properties_get_prop (results[0].props, PR_SENT_REPRESENTING_EMAIL_ADDRESS);
	if (!delegator_dn) {
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	sender_dn = e2k_properties_get_prop (results[0].props, PR_SENDER_EMAIL_ADDRESS);
	if (!sender_dn) {
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	if (!g_ascii_strcasecmp (delegator_dn, sender_dn)) {
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	account = mse->account;
	gc = exchange_account_get_global_catalog (account);
	if (!gc) {
		g_warning ("\nNo GC: could not unmangle sender field");
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	gcstatus = e2k_global_catalog_lookup (
		gc, NULL, /* FIXME; cancellable */
		E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		delegator_dn, E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
		&delegator_entry);
	if (gcstatus != E2K_GLOBAL_CATALOG_OK) {
		g_warning ("\nGC lookup failed: for delegator_entry - could not unmangle sender field");
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	gcstatus = e2k_global_catalog_lookup (
		gc, NULL, /* FIXME; cancellable */
		E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		sender_dn, E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX,
		&sender_entry);
	if (gcstatus != E2K_GLOBAL_CATALOG_OK) {
		g_warning ("\nGC lookup failed: for sender_entry - could not unmangle sender field");
		e2k_results_free (results, nresults);
		return E2K_HTTP_OK;
	}

	message = g_string_new_len (*body, *len);
	mail_util_demangle_meeting_related_message (message, delegator_entry->display_name,
						delegator_entry->email,
						NULL,
						sender_entry->email,
						unmangle_type);
	g_free (*body);
	*body = message->str;
	*len = message->len;
	*body = g_string_free (message, FALSE);

	e2k_global_catalog_entry_free (gc, delegator_entry);
	e2k_global_catalog_entry_free (gc, sender_entry);

	e2k_results_free (results, nresults);
	return E2K_HTTP_OK;
}

static gboolean
is_foreign_folder (MailStub *stub, const gchar *folder_name, gchar **owner_email)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	EFolder *folder;
	ExchangeHierarchy *hier;
	gchar *path;

	path = g_build_filename ("/", folder_name, NULL);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder) {
		g_free (path);
		return FALSE;
	}
	g_free (path);
	g_object_ref (folder);

	hier = e_folder_exchange_get_hierarchy (folder);

	if (hier->type != EXCHANGE_HIERARCHY_FOREIGN) {
		g_object_unref (folder);
		return FALSE;
	}

	*owner_email = g_strdup (hier->owner_email);

	g_object_unref (folder);
	return TRUE;
}

static void
get_message (MailStub *stub, const gchar *folder_name, const gchar *uid)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	MailStubExchangeMessage *mmsg;
	E2kHTTPStatus status;
	gchar *body = NULL, *content_type = NULL, *owner_email = NULL;
	gint len = 0;

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

	if (mfld->type == MAIL_STUB_EXCHANGE_FOLDER_NOTES) {
		status = get_stickynote (mse->ctx, NULL, mmsg->href,
					 &body, &len);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			goto error;
		content_type = g_strdup ("message/rfc822");
	} else {
		SoupBuffer *response;

		status = e2k_context_get (mse->ctx, NULL, mmsg->href,
					  &content_type, &response);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			goto error;
		len = response->length;
		body = g_strndup (response->data, response->length);
		soup_buffer_free (response);
	}

	/* Public folders especially can contain non-email objects.
	 * In that case, we fake the headers (which in this case
	 * should include Content-Type, Content-Disposition, etc,
	 * courtesy of mp:x67200102.
	 */
	if (!content_type || g_ascii_strncasecmp (content_type, "message/", 8)) {
		status = build_message_from_document (mse->ctx, NULL,
						      mmsg->href,
						      &body, &len);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			goto error;
	}

	/* If this is a delegated meeting request, we need to know who
	 * delegated it to us.
	 */
	if (mmsg->flags & MAIL_STUB_MESSAGE_DELEGATED) {
		status = unmangle_delegated_meeting_request (mse, NULL,
							     mmsg->href,
							     &body, &len);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			goto error;
	}

	/* If the message is in a subscribed inbox,
	 * we need to modify the message appropriately.
	 */
	if (is_foreign_folder (stub, folder_name, &owner_email)) {

		status = unmangle_meeting_request_in_subscribed_inbox  (mse,
									owner_email,
									&body, &len);
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
			goto error;
	}

	/* If there is a sender field in the meeting request/response,
	 * we need to know who it is.
	 */
	status = unmangle_sender_field (mse, NULL,
					mmsg->href,
					&body, &len);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		goto error;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_BYTEARRAY, body, len,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);
	goto cleanup;

 error:
	g_warning ("get_message: %d", status);
	if (status == E2K_HTTP_NOT_FOUND) {
		/* We don't change mfld->deleted_count, because the
		 * message may actually have gone away before the last
		 * time we recorded that.
		 */
		message_removed (stub, mfld, mmsg->href);
		mail_stub_return_error (stub, _("Message has been deleted"));
	} else
		mail_stub_return_error (stub, _("Error retrieving message"));

cleanup:
	g_free (body);
	g_free (content_type);
	g_free (owner_email);
}

static void
search (MailStub *stub, const gchar *folder_name, const gchar *text)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	E2kRestriction *rn;
	const gchar *prop, *repl_uid;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	GPtrArray *matches;

	mfld = folder_from_name (mse, folder_name, 0, FALSE);
	if (!mfld)
		return;

	matches = g_ptr_array_new ();

	prop = E2K_PR_REPL_UID;
	rn = e2k_restriction_content (PR_BODY, E2K_FL_SUBSTRING, text);

	iter = e_folder_exchange_search_start (mfld->folder, NULL,
					       &prop, 1, rn, NULL, TRUE);
	e2k_restriction_unref (rn);

	while ((result = e2k_result_iter_next (iter))) {
		repl_uid = e2k_properties_get_prop (result->props,
						    E2K_PR_REPL_UID);
		if (repl_uid)
			g_ptr_array_add (matches, (gchar *)uidstrip (repl_uid));
	}
	status = e2k_result_iter_free (iter);

	if (status == E2K_HTTP_UNPROCESSABLE_ENTITY) {
		mail_stub_return_error (stub, _("Mailbox does not support full-text searching"));
	} else {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
				       CAMEL_STUB_ARG_STRINGARRAY, matches,
				       CAMEL_STUB_ARG_END);
		mail_stub_return_ok (stub);
	}

	g_ptr_array_free (matches, TRUE);
}

static void
transfer_messages (MailStub *stub, const gchar *source_name,
		   const gchar *dest_name, GPtrArray *uids,
		   gboolean delete_originals)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *source, *dest;
	MailStubExchangeMessage *mmsg;
	GPtrArray *hrefs, *new_uids;
	GHashTable *order;
	gpointer key, value;
	E2kResultIter *iter;
	E2kResult *result;
	E2kHTTPStatus status;
	const gchar *uid;
	gint i, num;

	source = folder_from_name (mse, source_name, delete_originals ? MAPI_ACCESS_DELETE : 0, FALSE);
	if (!source)
		return;
	dest = folder_from_name (mse, dest_name, MAPI_ACCESS_CREATE_CONTENTS, FALSE);
	if (!dest)
		return;

	order = g_hash_table_new (NULL, NULL);
	hrefs = g_ptr_array_new ();
	new_uids = g_ptr_array_new ();
	for (i = 0; i < uids->len; i++) {
		mmsg = find_message (source, uids->pdata[i]);
		if (!mmsg)
			continue;

		if (!mmsg->href || !strrchr (mmsg->href, '/')) {
			g_warning ("%s: Message '%s' with invalid href '%s'", G_STRFUNC, (gchar *)uids->pdata[i], mmsg->href ? mmsg->href : "NULL");
			continue;
		}

		g_hash_table_insert (order, mmsg, GINT_TO_POINTER (i));
		g_ptr_array_add (hrefs, strrchr (mmsg->href, '/') + 1);
		g_ptr_array_add (new_uids, (gpointer) "");
	}

	if (delete_originals && hrefs->len > 1) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FREEZE_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, source->name,
				       CAMEL_STUB_ARG_END);
	}

	iter = e_folder_exchange_transfer_start (source->folder, NULL,
						 dest->folder, hrefs,
						 delete_originals);

	while ((result = e2k_result_iter_next (iter))) {
		if (!e2k_properties_get_prop (result->props, E2K_PR_DAV_LOCATION))
			continue;
		uid = e2k_properties_get_prop (result->props, E2K_PR_REPL_UID);
		if (!uid)
			continue;

		if (delete_originals)
			source->deleted_count++;

		mmsg = find_message_by_href (source, result->href);
		if (!mmsg)
			continue;

		if (!g_hash_table_lookup_extended (order, mmsg, &key, &value))
			continue;
		num = GPOINTER_TO_UINT (value);
		if (num > new_uids->len)
			continue;

		new_uids->pdata[num] = (gchar *)uidstrip (uid);

		if (delete_originals)
			message_removed (stub, source, result->href);
	}
	status = e2k_result_iter_free (iter);

	if (delete_originals && hrefs->len > 1) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_THAW_FOLDER,
				       CAMEL_STUB_ARG_FOLDER, source->name,
				       CAMEL_STUB_ARG_END);
	}

	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) {
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
				       CAMEL_STUB_ARG_STRINGARRAY, new_uids,
				       CAMEL_STUB_ARG_END);
		mail_stub_return_ok (stub);
	} else {
		g_warning ("transferred_messages: %d", status);
		mail_stub_return_error (stub, _("Unable to move/copy messages"));
	}

	g_ptr_array_free (hrefs, TRUE);
	g_ptr_array_free (new_uids, TRUE);
	g_hash_table_destroy (order);
}

static void
account_new_folder (ExchangeAccount *account, EFolder *folder, gpointer user_data)
{
	MailStub *stub = user_data;
	MailStubExchange *mse = user_data;
	ExchangeHierarchy *hier;

	if (strcmp (e_folder_get_type_string (folder), "mail") != 0 &&
	    strcmp (e_folder_get_type_string (folder), "mail/public") != 0)
		return;

	if (mse->ignore_new_folder &&
	    !strcmp (e_folder_exchange_get_path (folder), mse->ignore_new_folder))
		return;

	hier = e_folder_exchange_get_hierarchy (folder);
	if (hier->type != EXCHANGE_HIERARCHY_PERSONAL &&
	    hier->type != EXCHANGE_HIERARCHY_FAVORITES &&
	    hier->type != EXCHANGE_HIERARCHY_FOREIGN)
		return;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_CREATED,
			       CAMEL_STUB_ARG_STRING, e_folder_get_name (folder),
			       CAMEL_STUB_ARG_STRING, e_folder_get_physical_uri (folder),
			       CAMEL_STUB_ARG_END);

	mail_stub_push_changes (stub);
}

static void
account_removed_folder (ExchangeAccount *account, EFolder *folder, gpointer user_data)
{
	MailStub *stub = user_data;
	MailStubExchange *mse = user_data;
	ExchangeHierarchy *hier;

	if (strcmp (e_folder_get_type_string (folder), "mail") != 0 &&
	    strcmp (e_folder_get_type_string (folder), "mail/public") != 0)
		return;

	if (mse->ignore_removed_folder &&
	    !strcmp (e_folder_exchange_get_path (folder), mse->ignore_removed_folder))
		return;

	hier = e_folder_exchange_get_hierarchy (folder);
	if (hier->type != EXCHANGE_HIERARCHY_PERSONAL &&
	    hier->type != EXCHANGE_HIERARCHY_FAVORITES &&
	    hier->type != EXCHANGE_HIERARCHY_FOREIGN)
		return;

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_DELETED,
			       CAMEL_STUB_ARG_STRING, e_folder_get_name (folder),
			       CAMEL_STUB_ARG_STRING, e_folder_get_physical_uri (folder),
			       CAMEL_STUB_ARG_END);

	mail_stub_push_changes (stub);
}

static void
get_folder_info_data (MailStub *stub, const gchar *top, guint32 store_flags,
		      GPtrArray **names, GPtrArray **uris,
		      GArray **unread, GArray **flags)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	GPtrArray *folders = NULL;
	ExchangeHierarchy *hier;
	EFolder *folder;
	const gchar *type, *name, *uri, *inbox_uri = NULL, *trash_uri = NULL, *sent_items_uri = NULL;
	gint unread_count, i, toplen = top ? strlen (top) : 0;
	guint32 folder_flags = 0;
	gboolean recursive, subscribed, subscription_list;
	gint mode = -1;
	gchar *full_path;

	recursive = (store_flags & CAMEL_STUB_STORE_FOLDER_INFO_RECURSIVE);
	subscribed = (store_flags & CAMEL_STUB_STORE_FOLDER_INFO_SUBSCRIBED);
	subscription_list = (store_flags & CAMEL_STUB_STORE_FOLDER_INFO_SUBSCRIPTION_LIST);

	exchange_account_is_offline (mse->account, &mode);
	if (!subscribed && subscription_list) {
		ExchangeAccountResult result = -1;

		d(g_print ("%s(%d):%s: NOT SUBSCRIBED top = [%s]\n", __FILE__, __LINE__, __GNUC_PRETTY_FUNCTION__, top));
		if (!toplen)
			result = exchange_account_open_folder (mse->account, "/public");
		if (result ==  EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST) {
			hier = exchange_account_get_hierarchy_by_type (mse->account, EXCHANGE_HIERARCHY_PUBLIC);
			if (hier)
				exchange_hierarchy_scan_subtree (hier, hier->toplevel, mode);
		} else {
			d(g_print ("%s(%d):%s: NOT SUBSCRIBED - open_folder returned = [%d]\n", __FILE__, __LINE__, __GNUC_PRETTY_FUNCTION__, result));
		}
	}

	/* No need to check for recursive flag, as I will always be returning a tree, instead of a single folder info object */
	if (toplen) {
		d(g_print ("%s(%d):%s: NOT RECURSIVE and toplen top = [%s]\n", __FILE__, __LINE__, __GNUC_PRETTY_FUNCTION__, top));
		full_path = g_strdup_printf ("/%s", top);
		folders = exchange_account_get_folder_tree (mse->account, full_path);
		g_free (full_path);
	} else {
		d(g_print ("%s(%d):%s calling exchange_account_get_folders \n", __FILE__, __LINE__,
					   __GNUC_PRETTY_FUNCTION__));
		folders = exchange_account_get_folders (mse->account);
	}

	*names = g_ptr_array_new ();
	*uris = g_ptr_array_new ();
	*unread = g_array_new (FALSE, FALSE, sizeof (gint));
	*flags = g_array_new (FALSE, FALSE, sizeof (gint));
	/* Can be NULL if started in offline mode */
	if (mse->inbox) {
		inbox_uri = e_folder_get_physical_uri (mse->inbox);
	}

	if (mse->deleted_items) {
		trash_uri = e_folder_get_physical_uri (mse->deleted_items);
	}

	if (mse->sent_items) {
		sent_items_uri = e_folder_get_physical_uri (mse->sent_items);
	}

	if (folders) {
		for (i = 0; i < folders->len; i++) {
			folder = folders->pdata[i];
			hier = e_folder_exchange_get_hierarchy (folder);
			folder_flags = 0;

			if (subscribed) {
				if (hier->type != EXCHANGE_HIERARCHY_PERSONAL &&
				    hier->type != EXCHANGE_HIERARCHY_FAVORITES &&
				    hier->type != EXCHANGE_HIERARCHY_FOREIGN)
					continue;
			} else if (subscription_list) {
				if (hier->type != EXCHANGE_HIERARCHY_PUBLIC)
					continue;
			}

			type = e_folder_get_type_string (folder);
			name = e_folder_get_name (folder);
			uri = e_folder_get_physical_uri (folder);
			d(g_print ("Uri: %s\n", uri));
			d(g_print ("folder type is : %s\n", type));

			if (!strcmp (type, "noselect")) {
				unread_count = 0;
				folder_flags = CAMEL_STUB_FOLDER_NOSELECT;
			}

			switch (hier->type) {
				case EXCHANGE_HIERARCHY_FAVORITES:
					/* folder_flags will be set only if the type
					   is noselect and we need to include it */
					if (strcmp (type, "mail") && !folder_flags)
						continue;
					/* selectable */
					if (!folder_flags)
						unread_count = e_folder_get_unread_count (folder);
				case EXCHANGE_HIERARCHY_PUBLIC:
					if (exchange_account_is_favorite_folder (mse->account, folder)) {
						folder_flags |= CAMEL_STUB_FOLDER_SUBSCRIBED;
						d(printf ("marked the folder as subscribed\n"));
					}
					break;
				case EXCHANGE_HIERARCHY_FOREIGN:
					if (folder_flags & CAMEL_STUB_FOLDER_NOSELECT &&
					    mse->new_folder_id == 0) {
						/* Rescan the hierarchy - as we don't rescan
						   foreign hierarchies anywhere for mailer and
						   only when we are starting up
						*/
						exchange_hierarchy_scan_subtree (hier, hier->toplevel, mode);
					}
				case EXCHANGE_HIERARCHY_PERSONAL:
					if (!strcmp (type, "mail")) {
						unread_count = e_folder_get_unread_count (folder);
					}
					else if(!folder_flags) {
						continue;
					}
					break;
				default:
					break;
			}

			if (inbox_uri && !strcmp (uri, inbox_uri))
				folder_flags |= CAMEL_STUB_FOLDER_SYSTEM|CAMEL_STUB_FOLDER_TYPE_INBOX;

			if (trash_uri && !strcmp (uri, trash_uri))
				folder_flags |= CAMEL_STUB_FOLDER_SYSTEM|CAMEL_STUB_FOLDER_TYPE_TRASH;

			if (sent_items_uri && !strcmp (uri, sent_items_uri))
				folder_flags |= CAMEL_STUB_FOLDER_SYSTEM|CAMEL_STUB_FOLDER_TYPE_SENT;

			if (!e_folder_exchange_get_has_subfolders (folder)) {
				d(printf ("%s:%d:%s - %s has no subfolders", __FILE__, __LINE__, __GNUC_PRETTY_FUNCTION__,
					  name));
				folder_flags |= CAMEL_STUB_FOLDER_NOCHILDREN;
			}

			d(g_print ("folder flags is : %d\n", folder_flags));

			g_ptr_array_add (*names, (gchar *)name);
			g_ptr_array_add (*uris, (gchar *)uri);
			g_array_append_val (*unread, unread_count);
			g_array_append_val (*flags, folder_flags);
		}

		g_ptr_array_free (folders, TRUE);
	}
}

static void
get_folder_info (MailStub *stub, const gchar *top, guint32 store_flags)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	GPtrArray *names, *uris;
	GArray *unread, *flags;

	get_folder_info_data (stub, top, store_flags, &names, &uris,
			      &unread, &flags);

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRINGARRAY, names,
			       CAMEL_STUB_ARG_STRINGARRAY, uris,
			       CAMEL_STUB_ARG_UINT32ARRAY, unread,
			       CAMEL_STUB_ARG_UINT32ARRAY, flags,
			       CAMEL_STUB_ARG_END);

	g_ptr_array_free (names, TRUE);
	g_ptr_array_free (uris, TRUE);
	g_array_free (unread, TRUE);
	g_array_free (flags, TRUE);

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

static void
send_message (MailStub *stub, const gchar *from, GPtrArray *recipients,
	      const gchar *body, gint length)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	SoupMessage *msg;
	E2kHTTPStatus status;
	gchar *timestamp, *errmsg;
	GString *data;
	gint i;

	if (!mse->mail_submission_uri) {
		mail_stub_return_error (stub, _("No mail submission URI for this mailbox"));
		return;
	}

	data = g_string_new (NULL);
	g_string_append_printf (data, "MAIL FROM:<%s>\r\n", from);
	for (i = 0; i < recipients->len; i++) {
		g_string_append_printf (data, "RCPT TO:<%s>\r\n",
					(gchar *)recipients->pdata[i]);
	}
	g_string_append (data, "\r\n");

	/* Exchange doesn't add a "Received" header to messages
	 * received via WebDAV.
	 */
	timestamp = e2k_make_timestamp_rfc822 (time (NULL));
	g_string_append_printf (data, "Received: from %s by %s; %s\r\n",
				g_get_host_name (), mse->account->exchange_server,
				timestamp);
	g_free (timestamp);

	g_string_append_len (data, body, length);

	msg = e2k_soup_message_new_full (mse->ctx, mse->mail_submission_uri,
					 SOUP_METHOD_PUT, "message/rfc821",
					 SOUP_MEMORY_TAKE,
					 data->str, data->len);
	g_string_free (data, FALSE);
	soup_message_headers_append (msg->request_headers, "Saveinsent", "f");

	status = e2k_context_send_message (mse->ctx, NULL, msg);
	if (E2K_HTTP_STATUS_IS_SUCCESSFUL (status))
		mail_stub_return_ok (stub);
	else if (status == E2K_HTTP_NOT_FOUND)
		mail_stub_return_error (stub, _("Server won't accept mail via Exchange transport"));
	else if (status == E2K_HTTP_FORBIDDEN) {
		errmsg = g_strdup_printf (_("Your account does not have permission "
					    "to use <%s>\nas a From address."),
					  from);
		mail_stub_return_error (stub, errmsg);
		g_free (errmsg);
	} else if (status == E2K_HTTP_INSUFFICIENT_SPACE_ON_RESOURCE ||
		   status == E2K_HTTP_INTERNAL_SERVER_ERROR) {
		/* (500 is what it actually returns, 507 is what it should
		 * return, so we handle that too in case the behavior
		 * changes in the future.)
		 */
		E2K_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES;
		mail_stub_return_error (stub, _("Could not send message.\n"
						"This might mean that your account is over quota."));
	} else {
		g_warning ("sent_message: %d", status);
		mail_stub_return_error (stub, _("Could not send message"));
	}
}

static void
create_folder (MailStub *stub, const gchar *parent_name, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	ExchangeAccountFolderResult result;
	EFolder *folder;
	gchar *path;

	path = g_build_filename ("/", parent_name, folder_name, NULL);
	result = exchange_account_create_folder (mse->account, path, "mail");
	folder = exchange_account_get_folder (mse->account, path);
	g_free (path);

	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
		if (folder)
			break;
		/* fall through */
	default:
		mail_stub_return_error (stub, _("Generic error"));
		return;

	case EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS:
		mail_stub_return_error (stub, _("Folder already exists"));
		return;

	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		mail_stub_return_error (stub, _("Permission denied"));
		return;
	}

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_STRING, e_folder_get_physical_uri (folder),
			       CAMEL_STUB_ARG_UINT32, e_folder_get_unread_count (folder),
			       CAMEL_STUB_ARG_UINT32, 0,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);
}

static void
delete_folder (MailStub *stub, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	ExchangeAccountFolderResult result;
	EFolder *folder;
	gchar *path;

	path = g_build_filename ("/", folder_name, NULL);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder) {
		mail_stub_return_error (stub, _("Folder doesn't exist"));
		g_free (path);
		return;
	}
	g_object_ref (folder);

	result = exchange_account_remove_folder (mse->account, path);
	g_free (path);

	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		g_hash_table_remove (mse->folders_by_name, folder_name);
		break;

	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED: /* Fall through */
	case EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION:
		mail_stub_return_error (stub, _("Permission denied"));
		g_object_unref (folder);
		return;

	default:
		mail_stub_return_error (stub, _("Generic error"));
		g_object_unref (folder);
		return;

	}

	g_object_unref (folder);
	mail_stub_return_ok (stub);
}

static void
rename_folder (MailStub *stub, const gchar *old_name, const gchar *new_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	MailStubExchangeFolder *mfld;
	ExchangeAccountFolderResult result;
	EFolder *folder;
	gchar *old_path, *new_path;
	GPtrArray *names, *uris;
	GArray *unread, *flags;
	gint i = 0, j = 0, mode;
	gchar **folder_name;
	const gchar *uri;
	gchar *new_name_mod, *old_name_remove, *uri_unescaped, *old_name_mod = NULL;

	old_path = g_build_filename ("/", old_name, NULL);
	folder = exchange_account_get_folder (mse->account, old_path);
	if (!folder) {
		mail_stub_return_error (stub, _("Folder doesn't exist"));
		g_free (old_path);
		return;
	}
	new_path = g_build_filename ("/", new_name, NULL);

	mse->ignore_removed_folder = old_path;
	mse->ignore_new_folder = new_path;
	result = exchange_account_xfer_folder (mse->account, old_path, new_path, TRUE);
	folder = exchange_account_get_folder (mse->account, new_path);
	mse->ignore_new_folder = mse->ignore_removed_folder = NULL;
	g_free (old_path);
	g_free (new_path);

	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
		mfld = g_hash_table_lookup (mse->folders_by_name, old_name);
		if (!mfld)
			break;

		g_object_unref (mfld->folder);
		mfld->folder = g_object_ref (folder);
		mfld->name = e_folder_exchange_get_path (folder) + 1;

		g_hash_table_steal (mse->folders_by_name, old_name);
		g_hash_table_insert (mse->folders_by_name, (gchar *)mfld->name, mfld);

		get_folder_info_data (stub, new_name, CAMEL_STUB_STORE_FOLDER_INFO_SUBSCRIBED,
				      &names, &uris, &unread, &flags);

		g_hash_table_remove_all (mfld->messages_by_href);

		for (i = 0; i < mfld->messages->len; i++) {
			MailStubExchangeMessage *mmsg;
			mmsg = mfld->messages->pdata[i];
			g_free (mmsg->href);
			mmsg->href = NULL;
		}

		exchange_component_is_offline (global_exchange_component, &mode);
		if (mode == ONLINE_MODE) {
			if (!get_folder_online (mfld, TRUE)) {
				return;
			}
		}

		for (i = 0; i < uris->len; i++) {
			uri = uris->pdata[i];
			if (uri == NULL)
				continue;

			uri_unescaped = g_uri_unescape_string (uri, NULL);
			new_name_mod = g_strconcat (new_name, "/", NULL);
			folder_name = g_strsplit (uri_unescaped, new_name_mod, 2);

			if (!folder_name[1]) {
				g_strfreev (folder_name);
				old_name_mod = g_strconcat (old_name, "/", NULL);
				folder_name = g_strsplit (uri_unescaped, old_name_mod, 2);
				g_free (old_name_mod);

				if (!folder_name[1]) {
					goto cont_free;
				}
			}

			old_name_remove = g_build_filename (old_name, "/", folder_name[1], NULL);

			mfld = g_hash_table_lookup (mse->folders_by_name, old_name_remove);

			/* If the lookup for the MailStubExchangeFolder doesn't succeed then do
			not modify the corresponding entry in the hash table*/
			if (!mfld) {
				g_free (old_name_remove);
				goto cont_free;
			}

			new_path = g_build_filename ("/", new_name_mod, folder_name[1], NULL);
			old_path = g_build_filename ("/", old_name_remove, NULL);

			mse->ignore_removed_folder = old_path;
			mse->ignore_new_folder = new_path;
			result = exchange_account_xfer_folder (mse->account, old_path, new_path, TRUE);
			folder = exchange_account_get_folder (mse->account, new_path);
			mse->ignore_new_folder = mse->ignore_removed_folder = NULL;

			g_object_unref (mfld->folder);
			mfld->folder = g_object_ref (folder);
			mfld->name = e_folder_exchange_get_path (folder) + 1;

			g_hash_table_steal (mse->folders_by_name, old_name_remove);
			g_hash_table_insert (mse->folders_by_name, (gchar *)mfld->name, mfld);

			g_hash_table_remove_all (mfld->messages_by_href);

			for (j = 0; j < mfld->messages->len; j++) {
				MailStubExchangeMessage *mmsg;
				mmsg = mfld->messages->pdata[j];
				g_free (mmsg->href);
				mmsg->href = NULL;
			}

			exchange_component_is_offline (global_exchange_component, &mode);
			if (mode == ONLINE_MODE) {
				if (!get_folder_online (mfld, TRUE)) {
					return;
				}
			}

			g_free (old_path);
			g_free (new_path);
cont_free:		g_free (new_name_mod);
			g_free (uri_unescaped);
			g_strfreev (folder_name);
		}

		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
				       CAMEL_STUB_ARG_STRINGARRAY, names,
				       CAMEL_STUB_ARG_STRINGARRAY, uris,
				       CAMEL_STUB_ARG_UINT32ARRAY, unread,
				       CAMEL_STUB_ARG_UINT32ARRAY, flags,
				       CAMEL_STUB_ARG_END);

		g_ptr_array_free (names, TRUE);
		g_ptr_array_free (uris, TRUE);
		g_array_free (unread, TRUE);
		g_array_free (flags, TRUE);
		break;

	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		mail_stub_return_error (stub, _("Folder doesn't exist"));
		return;

	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		mail_stub_return_error (stub, _("Permission denied"));
		return;

	default:
		mail_stub_return_error (stub, _("Generic error"));
		return;

	}

	mail_stub_return_ok (stub);
}

static void
subscribe_folder (MailStub *stub, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	ExchangeAccountFolderResult result;
	EFolder *folder;
	gchar *path;

	path = g_build_filename ("/", folder_name, NULL);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder) {
		mail_stub_return_error (stub, _("Folder doesn't exist"));
		g_free (path);
		return;
	}
	g_free (path);
	g_object_ref (folder);

	if (e_folder_exchange_get_hierarchy (folder)->type != EXCHANGE_HIERARCHY_PUBLIC) {
		g_object_unref (folder);
		mail_stub_return_ok (stub);
		return;
	}

	if (!strcmp (e_folder_get_type_string (folder), "noselect")) {
		g_object_unref (folder);
		mail_stub_return_ok (stub);
		return;
	}

	result = exchange_account_add_favorite (mse->account, folder);

	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		break;

	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		mail_stub_return_error (stub, _("Permission denied"));
		g_object_unref (folder);
		return;

	default:
		mail_stub_return_error (stub, _("Generic error"));
		g_object_unref (folder);
		return;
	}

	g_object_unref (folder);
	mail_stub_return_ok (stub);
}

static void
unsubscribe_folder (MailStub *stub, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	ExchangeAccountFolderResult result;
	EFolder *folder;
	gchar *path, *pub_name;

	d(printf ("unsubscribe folder : %s\n", folder_name));
	path = g_build_filename ("/", folder_name, NULL);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder) {
		mail_stub_return_error (stub, _("Folder doesn't exist"));
		g_free (path);
		return;
	}
	g_free (path);
	g_object_ref (folder);

	/* if (e_folder_exchange_get_hierarchy (folder)->type != EXCHANGE_HIERARCHY_FAVORITES) {
	   Should use above check, but the internal uri is the same for both
	   public and favorite hierarchies and any of them can be used for the check */
	if (!exchange_account_is_favorite_folder (mse->account, folder)) {
		g_object_unref (folder);
		mail_stub_return_ok (stub);
		return;
	}

	g_object_unref (folder);

	pub_name = strrchr (folder_name, '/');
	path = g_build_filename ("/favorites", pub_name, NULL);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder) {
		mail_stub_return_error (stub, _("Folder doesn't exist"));
		g_free (path);
		return;
	}
	g_object_ref (folder);

	result = exchange_account_remove_favorite (mse->account, folder);

	switch (result) {
	case EXCHANGE_ACCOUNT_FOLDER_OK:
	case EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST:
		g_hash_table_remove (mse->folders_by_name, path + 1);
		break;

	case EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED:
		mail_stub_return_error (stub, _("Permission denied"));
		g_object_unref (folder);
		g_free (path);
		return;

	default:
		mail_stub_return_error (stub, _("Generic error"));
		g_object_unref (folder);
		g_free (path);
		return;

	}

	g_object_unref (folder);
	g_free (path);
	mail_stub_return_ok (stub);
}

static void
is_subscribed_folder (MailStub *stub, const gchar *folder_name)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	EFolder *folder;
	gchar *path;
	guint32 is_subscribed = 0;

	path = g_build_filename ("/", folder_name, NULL);
	folder = exchange_account_get_folder (mse->account, path);
	if (!folder) {
		g_free (path);
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
				       CAMEL_STUB_ARG_UINT32, is_subscribed,
				       CAMEL_STUB_ARG_END);
		mail_stub_return_ok (stub);
		return;
	}
	g_free (path);
	g_object_ref (folder);

	if (exchange_account_is_favorite_folder (mse->account, folder))
		is_subscribed = 1;

	g_object_unref (folder);

	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_UINT32, is_subscribed,
			       CAMEL_STUB_ARG_END);
	mail_stub_return_ok (stub);
}

static void
stub_connect (MailStub *stub, gchar *pwd)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (stub);
	ExchangeAccount *account;
	ExchangeAccountResult result;
	E2kContext *ctx;
	guint32 retval = 1;
	const gchar *uri;
	gint mode;

	exchange_component_is_offline (global_exchange_component, &mode);

	account = mse->account;
	if (mode == ONLINE_MODE)
		exchange_account_set_online (account);
	else if (mode == OFFLINE_MODE)
		exchange_account_set_offline (account);

	ctx = exchange_account_get_context (account);
	if (!ctx) {
		ctx = exchange_account_connect (account, pwd, &result);
	}

	if (!ctx && mode == ONLINE_MODE) {
		retval = 0;
		goto end;
	} else if (mode == OFFLINE_MODE) {
		goto end;
	}

	mse->ctx = g_object_ref (ctx);

	mse->mail_submission_uri = exchange_account_get_standard_uri (account, "sendmsg");
	uri = exchange_account_get_standard_uri (account, "inbox");
	mse->inbox = exchange_account_get_folder (account, uri);
	uri = exchange_account_get_standard_uri (account, "deleteditems");
	mse->deleted_items = exchange_account_get_folder (account, uri);
	uri = exchange_account_get_standard_uri (account, "sentitems");
	mse->sent_items = exchange_account_get_folder (account, uri);

	/* Will be used for offline->online transition to initialize things for
	   the first time */

	g_hash_table_foreach (mse->folders_by_name,
			      (GHFunc) folder_update_linestatus,
			      GINT_TO_POINTER (mode));
end:
	mail_stub_return_data (stub, CAMEL_STUB_RETVAL_RESPONSE,
			       CAMEL_STUB_ARG_UINT32, retval,
			       CAMEL_STUB_ARG_END);

	mail_stub_return_ok (stub);
}

#if 0
static void
linestatus_listener (ExchangeComponent *component,
		     gint linestatus,
		     gpointer data)
{
	MailStubExchange *mse = MAIL_STUB_EXCHANGE (data);
	ExchangeAccount *account = mse->account;
	const gchar *uri;

	if (linestatus == ONLINE_MODE && mse->ctx == NULL) {
		mse->ctx = exchange_account_get_context (account);
		g_object_ref (mse->ctx);

		mse->mail_submission_uri = exchange_account_get_standard_uri (account, "sendmsg");
		uri = exchange_account_get_standard_uri (account, "inbox");
		mse->inbox = exchange_account_get_folder (account, uri);
		uri = exchange_account_get_standard_uri (account, "deleteditems");
		mse->deleted_items = exchange_account_get_folder (account, uri);
		g_hash_table_foreach (mse->folders_by_name,
				      (GHFunc) folder_update_linestatus,
				      GINT_TO_POINTER (linestatus));

		g_signal_handler_disconnect (G_OBJECT (component),
					     offline_listener_handler_id);
		offline_listener_handler_id = 0;
	} else if (mse->ctx != NULL) {
		g_signal_handler_disconnect (G_OBJECT (component),
					     offline_listener_handler_id);
		offline_listener_handler_id = 0;
	}
}

#endif

static void
folder_update_linestatus (gpointer key, gpointer value, gpointer data)
{
	MailStubExchangeFolder *mfld = (MailStubExchangeFolder *) value;
	MailStub *stub = MAIL_STUB (mfld->mse);
	gint linestatus = GPOINTER_TO_INT (data);
	guint32 readonly;

	if (linestatus == ONLINE_MODE) {
		get_folder_online (mfld, TRUE);
		readonly = (mfld->access & (MAPI_ACCESS_MODIFY | MAPI_ACCESS_CREATE_CONTENTS)) ? 0 : 1;
		mail_stub_return_data (stub, CAMEL_STUB_RETVAL_FOLDER_SET_READONLY,
				       CAMEL_STUB_ARG_FOLDER, mfld->name,
				       CAMEL_STUB_ARG_UINT32, readonly,
				       CAMEL_STUB_ARG_END);
	}
	else {
		/* FIXME: need any undo for offline */ ;
	}
}

/**
 * mail_stub_exchange_new:
 * @account: the #ExchangeAccount this stub is for
 * @cmd_fd: command socket file descriptor
 * @status_fd: status socket file descriptor
 *
 * Creates a new #MailStubExchange for @account, communicating over
 * @cmd_fd and @status_fd.
 *
 * Return value: the new stub
 **/
MailStub *
mail_stub_exchange_new (ExchangeAccount *account, gint cmd_fd, gint status_fd)
{
	MailStubExchange *mse;
	MailStub *stub;

	stub = g_object_new (MAIL_TYPE_STUB_EXCHANGE, NULL);
	g_object_ref (stub);
	mail_stub_construct (stub, cmd_fd, status_fd);

	mse = (MailStubExchange *)stub;
	mse->account = account;

	/* offline_listener_handler_id = g_signal_connect (G_OBJECT (global_exchange_component),
							"linestatus-changed",
							G_CALLBACK (linestatus_listener), mse); */
	return stub;
}

