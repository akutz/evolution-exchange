/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2000-2004 Novell, Inc.
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

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <e-util/e-url.h>
#include <e-util/e-path.h>
#include <e-util/e-xml-hash-utils.h>
#include <gal/util/e-util.h>
#include "cal-backend-exchange.h"
#include "cal-util/cal-component.h"
#include "cal-util/cal-recur.h"
#include "cal-util/cal-util.h"
#include "e2k-cal-component.h"
#include "e2k-cache.h"
#include "e2k-connection.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "exchange-component.h"
#include "exchange-account.h"
#include "exchange-hierarchy.h"
#include "e-folder-exchange.h"
#include "mapi.h"
#include <libsoup/soup-message.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

/* Private part of the CalBackendExchange structure */
struct _CalBackendExchangePrivate {
	/* The account associated with this backend */
	ExchangeAccount *account;

	/* Folder where the calendar data is stored */
	EFolder *folder;
        char *exchange_uri;
	icalcomponent_kind folder_type;
	gboolean uids_loaded;
	gboolean writable;
	E2kRestriction *private_item_restriction;

	/* list of objects */
	GHashTable *objects;
	GHashTable *missing_uids;
	GHashTable *timezones;
	icaltimezone *default_timezone;

	/* local object cache */
	char *object_cache_file;
	guint cache_save_timeout;

	/* query cache */
	E2kCache *cache;

	/* notifications from the server */
	char *last_notification;
};

static void cal_backend_exchange_class_init (CalBackendExchangeClass *klass);
static void cal_backend_exchange_init (CalBackendExchange *cbex);
static void cal_backend_exchange_dispose (GObject *object);
static void cal_backend_exchange_finalize (GObject *object);

static const char *cal_backend_exchange_get_uri (CalBackend *backend);
static gboolean cal_backend_exchange_is_read_only (CalBackend *backend);
static const char *cal_backend_exchange_get_alarm_email_address (CalBackend *backend);
static const char *cal_backend_exchange_get_ldap_attribute (CalBackend *backend);
static const char *cal_backend_exchange_get_static_capabilities (CalBackend *backend);

static CalBackendOpenStatus cal_backend_exchange_open (CalBackend *backend,
						       const char *uristr,
						       gboolean only_if_exists);
static gboolean cal_backend_exchange_is_loaded (CalBackend *backend);

static CalMode cal_backend_exchange_get_mode (CalBackend *backend);
static void cal_backend_exchange_set_mode (CalBackend *backend, CalMode mode);

static char *cal_backend_exchange_get_default_object (CalBackend *backend, CalObjType type);

static int cal_backend_exchange_get_n_objects (CalBackend *backend, CalObjType type);
static CalComponent *cal_backend_exchange_get_object_component (CalBackend *backend, const char *uid);
static char *cal_backend_exchange_get_timezone_object (CalBackend *backend, const char *tzid);
static GList *cal_backend_exchange_get_uids (CalBackend *backend, CalObjType type);
static GList *cal_backend_exchange_get_objects_in_range (CalBackend *backend, CalObjType type,
							 time_t start, time_t end);
static GList *cal_backend_exchange_get_free_busy (
	CalBackend *backend, GList *users, time_t start, time_t end);
static GNOME_Evolution_Calendar_CalObjChangeSeq *cal_backend_exchange_get_changes (
	CalBackend *backend, CalObjType type, const char *change_id);

static GNOME_Evolution_Calendar_CalComponentAlarmsSeq *
cal_backend_exchange_get_alarms_in_range (CalBackend *backend, time_t start, time_t end);

static GNOME_Evolution_Calendar_CalComponentAlarms *
cal_backend_exchange_get_alarms_for_object (CalBackend *backend, const char *uid,
					    time_t start, time_t end,
					    gboolean *object_found);

static CalBackendResult cal_backend_exchange_discard_alarm (CalBackend *backend,
							    const char *uid,
							    const char *auid);

static CalBackendResult cal_backend_exchange_update_objects (CalBackend *backend,
							     const char *calobj,
							     CalObjModType mod);
static CalBackendResult cal_backend_exchange_remove_object (CalBackend *backend,
							    const char *uid,
							    CalObjModType mod);

static CalBackendSendResult cal_backend_exchange_send_object (CalBackend *backend, 
							      const char *calobj, char **new_calobj,
							      GNOME_Evolution_Calendar_UserList **user_list,
							      char error_msg[256]);

static icaltimezone* cal_backend_exchange_get_timezone (CalBackend *backend, const char *tzid);
static icaltimezone* cal_backend_exchange_get_default_timezone (CalBackend *backend);
static gboolean cal_backend_exchange_set_default_timezone (CalBackend *backend,
							   const char *tzid);

static void add_component (CalBackendExchange *cbex,
			   E2kCalComponent *comp, gboolean notify);
static void got_uids_cb (E2kConnection *conn, SoupMessage *msg,
			 E2kResult *results, int nresults,
			 gpointer user_data);
static CalComponent *lookup_component (CalBackendExchange *cbex, const char *uid);
static gboolean remove_component (CalBackendExchange *cbex, const char *uid, gboolean notify);
static icaltimezone *resolve_tzid (const char *tzid, gpointer user_data);

static gboolean get_folder_properties (E2kConnection *conn, const char *uri,
				       icalcomponent_kind *type,
				       guint32 *access);

static gboolean save_object_cache (gpointer cbex);

#define PARENT_TYPE CAL_BACKEND_TYPE
static CalBackendClass *parent_class = NULL;

/*
 * Private functions
 */

/* adds a new component to our list */
static void
add_component (CalBackendExchange *cbex,
	       E2kCalComponent *comp,
	       gboolean notify)
{
	char *uid;
	gpointer key, val;
	GSList *categories;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex));
	g_return_if_fail (E2K_IS_CAL_COMPONENT (comp));

	cal_component_get_uid (CAL_COMPONENT (comp), (const char **) &uid);
	if (!uid || !uid[0]) {
		g_object_unref (comp);
		return;
	}

	/* See if we already know of a component with this UID */
	if (g_hash_table_lookup_extended (cbex->priv->objects, uid, &key, &val)) {
		g_hash_table_remove (cbex->priv->objects, uid);
		g_free (key);
		if (val)
			g_object_unref (val);
	}

	g_hash_table_insert (cbex->priv->objects, g_strdup (uid), comp);

	e2k_cache_clear (cbex->priv->cache);
	if (notify)
		cal_backend_notify_update (CAL_BACKEND (cbex), uid);

	/* Update the set of categories */
	cal_component_get_categories_list (CAL_COMPONENT (comp), &categories);
	cal_backend_ref_categories (CAL_BACKEND (cbex), categories);
	cal_component_free_categories_list (categories);
}

/* frees an entry in the uid<->component */
static void
free_hash_uri (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	if (value)
		g_object_unref (value);
}

/* free an entry in the timezones hash table */
static void
free_hash_tz (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	icaltimezone_free (value, TRUE);
}

/* free an entry on the missing_uids hash table */
static void
free_missing_uid (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	g_free (value);
}

/* gets the URI for a component, or builds one */
static char *
get_href_for_comp (CalBackendExchange *cbex, CalComponent *comp)
{
	const char *uid;
	const char *href;
	char *ret_href;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (CAL_IS_COMPONENT (comp), NULL);

	href = e2k_cal_component_get_href (E2K_CAL_COMPONENT (comp));
	if (href)
		ret_href = g_strdup (href);
	else {
		cal_component_get_uid (comp, &uid);
		if (!uid || !uid[0])
			return NULL;

		ret_href = g_strdup_printf ("%s/%s.EML", e_folder_exchange_get_internal_uri (cbex->priv->folder), uid);
	}

	return ret_href;
	
}

/* looks for a component in a backend */
static CalComponent *
lookup_component (CalBackendExchange *cbex, const char *uid)
{
	gpointer key, value;
	E2kCalComponent *comp = NULL;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	
	comp = g_hash_table_lookup (cbex->priv->objects, uid);
	if (CAL_IS_COMPONENT (comp))
		return CAL_COMPONENT (comp);

	/* not found, so it might be one of the not-yet-downloaded events */
       if (g_hash_table_lookup_extended (cbex->priv->missing_uids, uid, &key, &value)) {
               if (!key || !value)
                       return NULL;

               comp = e2k_cal_component_new_from_href (cbex, (const char *) value);
               if (CAL_IS_COMPONENT (comp)) {
                       g_hash_table_remove (cbex->priv->missing_uids, uid);

                       add_component (cbex, comp, FALSE);
		       g_free (key);
                       g_free (value);

                       return CAL_COMPONENT (comp);
               }
       }

	return NULL;
}

/* removes a component from our list */
static gboolean
remove_component (CalBackendExchange *cbex, const char *uid, gboolean notify)
{
	gpointer orig_key;
	gpointer value;
	GSList *categories;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	if (g_hash_table_lookup_extended (cbex->priv->objects, uid, &orig_key, &value)) {
		g_hash_table_remove (cbex->priv->objects, uid);
		g_free (orig_key);
		if (value) {
			/* Update the set of categories */
			cal_component_get_categories_list (value, &categories);
			cal_backend_unref_categories (CAL_BACKEND (cbex),
						      categories);
			cal_component_free_categories_list (categories);

			g_object_unref (value);
		}

		e2k_cache_clear (cbex->priv->cache);

		if (notify)
			cal_backend_notify_remove (CAL_BACKEND (cbex), uid);

		return TRUE;
	}

	return FALSE; /* we return FALSE when the object was not found */
}

/* function for resolving the timezone */
static icaltimezone *
resolve_tzid (const char *tzid, gpointer user_data)
{
	CalBackendExchange *cbex = (CalBackendExchange *) user_data;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	return  cal_backend_get_timezone (CAL_BACKEND (cbex), tzid);
}

/*
 * Callbacks
 */

static const char *event_properties[] = {
	E2K_PR_CALENDAR_UID,
	E2K_PR_CALENDAR_INSTANCE_TYPE,
	E2K_PR_DAV_LAST_MODIFIED,
	PR_INTERNET_CONTENT
};
static const int n_event_properties = sizeof (event_properties) / sizeof (event_properties[0]);

static guint
get_changed_events_sync (CalBackendExchange *cbex, GHashTable *cache_objects)
{
	const char *prop;
	GPtrArray *hrefs;
	E2kResult *results = NULL;
	E2kRestriction *rn;
	int count = 0;
	SoupMessage msg;
	int status;
	E2kCalComponent *comp;
	int i;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), SOUP_ERROR_CANCELLED);

	prop = E2K_PR_CALENDAR_UID;
	rn = e2k_restriction_andv (
		e2k_restriction_prop_date (E2K_PR_DAV_LAST_MODIFIED,
					   E2K_RELOP_GT,
					   cbex->priv->last_notification),
		e2k_restriction_prop_string (E2K_PR_DAV_CONTENT_CLASS,
					     E2K_RELOP_EQ,
					     "urn:content-classes:appointment"),
		e2k_restriction_orv (
			e2k_restriction_prop_int (E2K_PR_CALENDAR_INSTANCE_TYPE,
						  E2K_RELOP_EQ, 0),
			e2k_restriction_prop_int (E2K_PR_CALENDAR_INSTANCE_TYPE,
						  E2K_RELOP_EQ, 1),
			NULL),
		NULL);
	if (cbex->priv->private_item_restriction) {
		e2k_restriction_ref (cbex->priv->private_item_restriction);
		rn = e2k_restriction_andv (rn, cbex->priv->private_item_restriction, NULL);
	}

	E2K_DEBUG_HINT ('C');
	status = e2k_cache_search (cbex->priv->cache,
				   &prop, 1, rn,
				   &results, &count);
	e2k_restriction_unref (rn);

	if (status != SOUP_ERROR_DAV_MULTISTATUS || !results)
		return SOUP_ERROR_OK;
	if (count <= 0) {
		e2k_results_free (results, count);
		return SOUP_ERROR_OK;
	}

	hrefs = g_ptr_array_new ();
	for (i = 0; i < count; i++) {
		if (cache_objects) {
			prop = e2k_properties_get_prop (results[i].props,
							E2K_PR_CALENDAR_UID);
			if (!prop)
				continue;
			comp = g_hash_table_lookup (cache_objects, prop);
			if (comp) {
				g_hash_table_remove (cache_objects, prop);
				add_component (cbex, comp, cbex->priv->uids_loaded);
				continue;
			}
		}

		g_ptr_array_add (hrefs, g_strdup (results[i].href));
	}
	e2k_results_free (results, count);

	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		return SOUP_ERROR_OK;
	}

	E2K_DEBUG_HINT ('C');
	msg.errorcode = e_folder_exchange_bpropfind_sync (
		cbex->priv->folder,
		(const char **)hrefs->pdata, hrefs->len, "0",
		event_properties, n_event_properties,
		&results, &count);

	got_uids_cb (cbex->connection, &msg, results, count, cbex);
	
	/* free memory */
	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_free (hrefs, TRUE);
	if (SOUP_ERROR_IS_SUCCESSFUL (msg.errorcode))
		e2k_results_free (results, count);

	return msg.errorcode;
}

static const char *task_props[] = {
	E2K_PR_EXCHANGE_MESSAGE_CLASS,
	E2K_PR_DAV_UID,
	E2K_PR_CALENDAR_UID,
	E2K_PR_DAV_LAST_MODIFIED,
	E2K_PR_HTTPMAIL_SUBJECT,
	E2K_PR_HTTPMAIL_TEXT_DESCRIPTION,
	E2K_PR_HTTPMAIL_DATE,
	E2K_PR_CALENDAR_LAST_MODIFIED,
	E2K_PR_HTTPMAIL_FROM_EMAIL,
	E2K_PR_HTTPMAIL_FROM_NAME,
	E2K_PR_MAILHEADER_IMPORTANCE,
	E2K_PR_MAPI_SENSITIVITY,
	E2K_PR_MAPI_COMMON_START,
	E2K_PR_MAPI_COMMON_END,
	E2K_PR_OUTLOOK_TASK_STATUS,
	E2K_PR_OUTLOOK_TASK_PERCENT,
	E2K_PR_OUTLOOK_TASK_DONE_DT,
	E2K_PR_EXCHANGE_KEYWORDS,
	E2K_PR_CALENDAR_URL
};
static const int n_task_props = sizeof (task_props) / sizeof (task_props[0]);

static guint
get_changed_tasks_sync (CalBackendExchange *cbex)
{
	E2kRestriction *rn;
	E2kResult *results;
	int count = 0;
	SoupMessage msg;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), SOUP_ERROR_CANCELLED);

	rn = e2k_restriction_prop_date (E2K_PR_DAV_LAST_MODIFIED,
					E2K_RELOP_GT,
					cbex->priv->last_notification);
	if (cbex->priv->private_item_restriction) {
		e2k_restriction_ref (cbex->priv->private_item_restriction);
		rn = e2k_restriction_andv (rn, cbex->priv->private_item_restriction, NULL);
	}

	E2K_DEBUG_HINT ('T');
	msg.errorcode = e_folder_exchange_search_sync (cbex->priv->folder,
						       task_props,
						       n_task_props,
						       FALSE, rn, NULL,
						       &results, &count);
	got_uids_cb (cbex->connection, &msg, results, count, cbex);

	/* free memory */
	e2k_restriction_unref (rn);
	if (SOUP_ERROR_IS_SUCCESSFUL (msg.errorcode))
		e2k_results_free (results, count);

	return msg.errorcode;
}

typedef enum {
	CAL_BACKEND_EXCHANGE_BOOKING_OK,
	CAL_BACKEND_EXCHANGE_BOOKING_NO_SUCH_USER,
	CAL_BACKEND_EXCHANGE_BOOKING_BUSY,
	CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED,
	CAL_BACKEND_EXCHANGE_BOOKING_ERROR
} CalBackendExchangeBookingResult;

/* start_time and end_time are in e2k_timestamp format. */
static CalBackendExchangeBookingResult
book_resource (CalBackendExchange *cbex,
	       const char *resource_email,
	       E2kCalComponent *e2k_comp,
	       icalproperty_method method)
{
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;
	E2kResult *results;
	CalComponentDateTime dt;
	icaltimezone *izone;
	guint32 access;
	time_t tt;
	const char *uid, *prop;
	gboolean bookable;
	char *top_uri = NULL, *cal_uri = NULL;
	char *startz, *endz, *href = NULL;
	E2kRestriction *rn;
	int nresults;
	icalcomponent_kind type;
	CalBackendExchangeBookingResult retval = CAL_BACKEND_EXCHANGE_BOOKING_ERROR;
	const char *localfreebusy_path = "NON_IPM_SUBTREE/Freebusy%20Data/LocalFreebusy.EML";

	
	g_object_ref (e2k_comp);
	
	/* Look up the resource's mailbox */
	gc = exchange_account_get_global_catalog (cbex->priv->account);
	if (!gc)
		goto cleanup;
	
	status = e2k_global_catalog_lookup (
		gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL, resource_email,
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX, &entry);
	switch (status) {
	case E2K_GLOBAL_CATALOG_OK:
		break;

	case E2K_GLOBAL_CATALOG_NO_SUCH_USER:
		retval = CAL_BACKEND_EXCHANGE_BOOKING_NO_SUCH_USER;
		goto cleanup;
		
	default:
		retval = CAL_BACKEND_EXCHANGE_BOOKING_ERROR;
		goto cleanup;
	}

	top_uri = exchange_account_get_foreign_uri (cbex->priv->account,
						    entry, NULL);
	cal_uri = exchange_account_get_foreign_uri (cbex->priv->account, entry,
						    E2K_PR_STD_FOLDER_CALENDAR);
	e2k_global_catalog_entry_free (gc, entry);
	if (!top_uri || !cal_uri) {
		retval = CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}

	if (!get_folder_properties (cbex->connection, cal_uri, &type, &access)) {
		retval = CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}
	if (!(access & MAPI_ACCESS_CREATE_CONTENTS)) {
		retval = CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}

	prop = PR_PROCESS_MEETING_REQUESTS;
	status = e2k_connection_bpropfind_sync (cbex->connection, top_uri,
						&localfreebusy_path, 1, "0",
						&prop, 1, &results, &nresults);
	if (!SOUP_ERROR_IS_SUCCESSFUL (status) || nresults == 0) {
		retval = CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}
	prop = e2k_properties_get_prop (results[0].props, PR_PROCESS_MEETING_REQUESTS);
	bookable = prop && atoi (prop);
	e2k_results_free (results, nresults);
	if (!bookable) {
		retval = CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
		goto cleanup;
	}

	cal_component_get_uid (CAL_COMPONENT (e2k_comp), &uid);
	href = g_strdup_printf ("%s/%s.EML", cal_uri, uid);

	if (method == ICAL_METHOD_CANCEL) {
		/* Mark the cancellation properly in the resource's calendar */
		CalComponentText old_text, new_text;

		g_object_unref (e2k_comp);		
		e2k_comp = e2k_cal_component_new_from_href (cbex, href);

		/* If there is nothing to cancel, we're good */
		if (e2k_comp == NULL) {
			retval = CAL_BACKEND_EXCHANGE_BOOKING_OK;
			goto cleanup;
		}

		/* Mark the item as cancelled */
		cal_component_get_summary (CAL_COMPONENT (e2k_comp), &old_text);
		if (old_text.value)
			new_text.value = g_strdup_printf ("Cancelled: %s", old_text.value);
		else
			new_text.value = g_strdup_printf ("Cancelled");
		new_text.altrep = NULL;
		cal_component_set_summary (CAL_COMPONENT (e2k_comp), &new_text);

		cal_component_set_transparency (CAL_COMPONENT (e2k_comp), CAL_COMPONENT_TRANSP_TRANSPARENT);
	} else {
		/* Check that the new appointment doesn't conflict with any
		 * existing appointment.
		 */

		cal_component_get_dtstart (CAL_COMPONENT (e2k_comp), &dt);
		izone = cal_backend_get_timezone (CAL_BACKEND (cbex), dt.tzid);
		tt = icaltime_as_timet_with_zone (*dt.value, izone);
		cal_component_free_datetime (&dt);
		startz = e2k_make_timestamp (tt);
		
		cal_component_get_dtend (CAL_COMPONENT (e2k_comp), &dt);
		izone = cal_backend_get_timezone (CAL_BACKEND (cbex), dt.tzid);
		tt = icaltime_as_timet_with_zone (*dt.value, izone);
		cal_component_free_datetime (&dt);
		endz = e2k_make_timestamp (tt);

		prop = E2K_PR_CALENDAR_UID;
		rn = e2k_restriction_andv (
			e2k_restriction_prop_bool (
				E2K_PR_DAV_IS_COLLECTION, E2K_RELOP_EQ, FALSE),
			e2k_restriction_prop_string (
				E2K_PR_DAV_CONTENT_CLASS, E2K_RELOP_EQ,
				"urn:content-classes:appointment"),
			e2k_restriction_prop_string (
				E2K_PR_CALENDAR_UID, E2K_RELOP_NE, uid),
			e2k_restriction_prop_date (
				E2K_PR_CALENDAR_DTEND, E2K_RELOP_GT, startz),
			e2k_restriction_prop_date (
				E2K_PR_CALENDAR_DTSTART, E2K_RELOP_LT, endz),
			e2k_restriction_prop_string (
				E2K_PR_CALENDAR_BUSY_STATUS, E2K_RELOP_NE, "FREE"),
			NULL);

		status = e2k_connection_search_sync (cbex->connection, cal_uri,
						     &prop, 1, FALSE, rn, NULL,
						     &results, &nresults);
		g_free (startz);
		g_free (endz);
		e2k_restriction_unref (rn);
		if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
			if (status == SOUP_ERROR_UNAUTHORIZED) {
				retval = CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED;
				goto cleanup;
			} else {
				retval = CAL_BACKEND_EXCHANGE_BOOKING_ERROR;
				goto cleanup;
			}
		}

		if (nresults != 0) {
			e2k_results_free (results, nresults);
			retval = CAL_BACKEND_EXCHANGE_BOOKING_BUSY;
			goto cleanup;
		}
	}
	
	/* We're good. Book it. */
	e2k_cal_component_set_href (e2k_comp, href);

	status = e2k_cal_component_update (e2k_comp, method, FALSE /* FIXME? */);
	if (SOUP_ERROR_IS_SUCCESSFUL (status))
		retval = CAL_BACKEND_EXCHANGE_BOOKING_OK;
	else
		retval = CAL_BACKEND_EXCHANGE_BOOKING_ERROR;

 cleanup:
	g_object_unref (e2k_comp);
	if (href)
		g_free (href);
	if (cal_uri)
		g_free (cal_uri);
	if (top_uri)
		g_free (top_uri);

	return retval;
}

/* subscription notify callback */
static void
event_notification_cb (E2kConnection *conn, const char *uri,
		       E2kConnectionChangeType type, gpointer user_data)
{
	CalBackendExchange *cbex = (CalBackendExchange *) user_data;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex));
	g_return_if_fail (uri != NULL);

	e2k_cache_clear (cbex->priv->cache);

	switch (cbex->priv->folder_type) {
	case ICAL_VEVENT_COMPONENT :
		get_changed_events_sync (cbex, NULL);
		break;

	case ICAL_VTODO_COMPONENT :
		get_changed_tasks_sync (cbex);
		break;

	default :
		break;
	}
}

/*
 * CalBackendExchange class implementation
 */
E2K_MAKE_TYPE (cal_backend_exchange, CalBackendExchange, cal_backend_exchange_class_init, cal_backend_exchange_init, PARENT_TYPE)

/* Class initialization function for the Exchange backend */
static void
cal_backend_exchange_class_init (CalBackendExchangeClass *klass)
{
	GObjectClass *object_class;
	CalBackendClass *backend_class;

	object_class = G_OBJECT_CLASS (klass);
	backend_class = CAL_BACKEND_CLASS (klass);

	parent_class = g_type_class_ref (CAL_BACKEND_TYPE);

	object_class->dispose = cal_backend_exchange_dispose;
	object_class->finalize = cal_backend_exchange_finalize;

	backend_class->get_uri = cal_backend_exchange_get_uri;
	backend_class->is_read_only = cal_backend_exchange_is_read_only;
	backend_class->get_cal_address = cal_backend_exchange_get_cal_address;
	backend_class->get_alarm_email_address = cal_backend_exchange_get_alarm_email_address;
	backend_class->get_ldap_attribute = cal_backend_exchange_get_ldap_attribute;
	backend_class->get_static_capabilities = cal_backend_exchange_get_static_capabilities;
	backend_class->open = cal_backend_exchange_open;
	backend_class->is_loaded = cal_backend_exchange_is_loaded;
	backend_class->get_mode = cal_backend_exchange_get_mode;
	backend_class->set_mode = cal_backend_exchange_set_mode;
	backend_class->get_default_object = cal_backend_exchange_get_default_object;
	backend_class->get_n_objects = cal_backend_exchange_get_n_objects;
	backend_class->get_object_component = cal_backend_exchange_get_object_component;
	backend_class->get_timezone_object = cal_backend_exchange_get_timezone_object;
	backend_class->get_uids = cal_backend_exchange_get_uids;
	backend_class->get_objects_in_range = cal_backend_exchange_get_objects_in_range;
	backend_class->get_free_busy = cal_backend_exchange_get_free_busy;
	backend_class->get_changes = cal_backend_exchange_get_changes;
	backend_class->get_alarms_in_range = cal_backend_exchange_get_alarms_in_range;
	backend_class->get_alarms_for_object = cal_backend_exchange_get_alarms_for_object;
	backend_class->discard_alarm = cal_backend_exchange_discard_alarm;
	backend_class->update_objects = cal_backend_exchange_update_objects;
	backend_class->remove_object = cal_backend_exchange_remove_object;
	backend_class->send_object = cal_backend_exchange_send_object;
	backend_class->get_timezone = cal_backend_exchange_get_timezone;
	backend_class->get_default_timezone = cal_backend_exchange_get_default_timezone;
	backend_class->set_default_timezone = cal_backend_exchange_set_default_timezone;
}

/* Object initialization function for the Exchange backend */
static void
cal_backend_exchange_init (CalBackendExchange *cbex)
{
	cbex->priv = g_new0 (CalBackendExchangePrivate, 1);

	cbex->priv->objects = g_hash_table_new (g_str_hash, g_str_equal);
	cbex->priv->missing_uids = g_hash_table_new (g_str_hash, g_str_equal);
	cbex->priv->timezones = g_hash_table_new (g_str_hash, g_str_equal);
	cbex->priv->last_notification = e2k_make_timestamp (0);
}

/* dispose handler for the Exchange backend */
static void
cal_backend_exchange_dispose (GObject *object)
{
	CalBackendExchange *cbex;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (object));

	cbex = CAL_BACKEND_EXCHANGE (object);

	if (cbex->priv->cache_save_timeout) {
		g_source_remove (cbex->priv->cache_save_timeout);
		save_object_cache (cbex);
	}

	if (cbex->priv->folder) {
		e_folder_exchange_unsubscribe (cbex->priv->folder);
		g_object_unref (cbex->priv->folder);
		cbex->priv->folder = NULL;
	}

	if (cbex->priv->cache != NULL) {
		g_object_unref (cbex->priv->cache);
		cbex->priv->cache = NULL;
	}

	if (cbex->priv->account != NULL) {
		g_object_unref (cbex->priv->account);
		cbex->priv->account = NULL;
		cbex->connection = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* finalize handler for the Exchange backend */
static void
cal_backend_exchange_finalize (GObject *object)
{
	CalBackendExchange *cbex;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (object));

	cbex = CAL_BACKEND_EXCHANGE (object);

	if (cbex->priv->exchange_uri)
		g_free (cbex->priv->exchange_uri);

	if (cbex->priv->private_item_restriction)
		e2k_restriction_unref (cbex->priv->private_item_restriction);

	g_hash_table_foreach (cbex->priv->objects, free_hash_uri, NULL);
	g_hash_table_destroy (cbex->priv->objects);

	g_hash_table_foreach (cbex->priv->missing_uids, free_missing_uid, NULL);
	g_hash_table_destroy (cbex->priv->missing_uids);

	g_hash_table_foreach (cbex->priv->timezones, free_hash_tz, NULL);
	g_hash_table_destroy (cbex->priv->timezones);

	g_free (cbex->priv->last_notification);

	g_free (cbex->priv->object_cache_file);

	g_free (cbex->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* get_uri handler for the Exchange backend */
static const char *
cal_backend_exchange_get_uri (CalBackend *backend)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	return cbex->priv->exchange_uri;
}

/* is_read_only handler for the Exchange backend */
static gboolean
cal_backend_exchange_is_read_only (CalBackend *backend)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), FALSE);

	return !cbex->priv->writable;
}

/* get_cal_address handler for the Exchange backend */
const char *
cal_backend_exchange_get_cal_address (CalBackend *backend)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	ExchangeHierarchy *hier;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	hier = e_folder_exchange_get_hierarchy (cbex->priv->folder);
	return hier->owner_email;
}

const char *
cal_backend_exchange_get_cal_owner (CalBackend *backend)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	ExchangeHierarchy *hier;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	hier = e_folder_exchange_get_hierarchy (cbex->priv->folder);
	return hier->owner_name;
}

/* get_alarm_email_address handler for the Exchange backend */
static const char *
cal_backend_exchange_get_alarm_email_address (CalBackend *backend)
{
	/* We don't support email alarms */
	return NULL;
}

static const char *
cal_backend_exchange_get_ldap_attribute (CalBackend *backend)
{
	return NULL;
}

static const char *
cal_backend_exchange_get_static_capabilities (CalBackend *backend)
{
	return CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS "," \
		CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT "," \
		CAL_STATIC_CAPABILITY_REMOVE_ALARMS;
}

static void
save_timezone (gpointer key, gpointer tz, gpointer vcalcomp)
{
	icalcomponent *tzcomp;

	tzcomp = icalcomponent_new_clone (icaltimezone_get_component (tz));
	icalcomponent_add_component (vcalcomp, tzcomp);
}

static void
save_object (gpointer key, gpointer comp, gpointer vcalcomp)
{
	icalcomponent *icalcomp;
	icalproperty *icalprop;

	icalcomp = icalcomponent_new_clone (cal_component_get_icalcomponent (comp));
	icalprop = icalproperty_new_x (e2k_cal_component_get_href (comp));
	icalproperty_set_x_name (icalprop, "X-XIMIAN-CONNECTOR-HREF");
	icalcomponent_add_property (icalcomp, icalprop);

	icalcomponent_add_component (vcalcomp, icalcomp);
}

static gboolean
save_object_cache (gpointer user_data)
{
	CalBackendExchange *cbex = user_data;
	icalcomponent *vcalcomp;
	char *data, *tmpfile;
	size_t len, nwrote;
	FILE *f;

	cbex->priv->cache_save_timeout = 0;

	vcalcomp = cal_util_new_top_level ();
	g_hash_table_foreach (cbex->priv->timezones, save_timezone, vcalcomp);
	g_hash_table_foreach (cbex->priv->objects, save_object, vcalcomp);
	data = icalcomponent_as_ical_string (vcalcomp);
	icalcomponent_free (vcalcomp);

	tmpfile = g_strdup_printf ("%s~", cbex->priv->object_cache_file);
	f = fopen (tmpfile, "w");
	if (!f) {
		g_free (tmpfile);
		return FALSE;
	}

	len = strlen (data);
	nwrote = fwrite (data, 1, len, f);
	if (fclose (f) != 0 || nwrote != len) {
		g_free (tmpfile);
		return FALSE;
	}

	if (rename (tmpfile, cbex->priv->object_cache_file) != 0)
		unlink (tmpfile);
	g_free (tmpfile);
	return FALSE;
}

void
cal_backend_exchange_save (CalBackendExchange *cbex)
{
	/* This is just a cache, so if we crash with unsaved changes,
	 * it's not a big deal. So we use a reasonably large timeout.
	 */
	if (cbex->priv->cache_save_timeout)
		g_source_remove (cbex->priv->cache_save_timeout);
	cbex->priv->cache_save_timeout = g_timeout_add (6 * 1000,
							save_object_cache,
							cbex);
}

static GHashTable *
load_object_cache (CalBackendExchange *cbex)
{
	GHashTable *cache_objects;
	icalcomponent *vcalcomp, *icalcomp;
	icalcomponent_kind kind;
	E2kCalComponent *comp;
	icalcompiter iter;
	const char *uid;

	vcalcomp = cal_util_parse_ics_file (cbex->priv->object_cache_file);
	if (!vcalcomp)
		return NULL;

	if (icalcomponent_isa (vcalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (vcalcomp);
		return NULL;
	}

	cache_objects = g_hash_table_new (g_str_hash, g_str_equal);

	for (iter = icalcomponent_begin_component (vcalcomp, ICAL_ANY_COMPONENT); icalcompiter_deref (&iter) != NULL; icalcompiter_next (&iter)) {
		icalcomp = icalcompiter_deref (&iter);
		kind = icalcomponent_isa (icalcomp);

		if (kind == ICAL_VEVENT_COMPONENT ||
		    kind == ICAL_VTODO_COMPONENT) {
			comp = e2k_cal_component_new_from_cache (cbex, icalcomp);
			if (!comp)
				continue;

			cal_component_get_uid (CAL_COMPONENT (comp), &uid);
			g_hash_table_insert (cache_objects, (char *)uid, comp);
		} else if (kind == ICAL_VTIMEZONE_COMPONENT)
			cal_backend_exchange_add_timezone (cbex, icalcomp);
	}

	icalcomponent_free (vcalcomp);
	return cache_objects;
}

static void
free_cache_object (gpointer key, gpointer comp, gpointer data)
{
	g_object_unref (comp);
}

static void
free_cache_objects (GHashTable *cache_objects)
{
	g_hash_table_foreach (cache_objects, free_cache_object, NULL);
	g_hash_table_destroy (cache_objects);
}

static void
got_uids_cb (E2kConnection *conn, SoupMessage *msg,
	     E2kResult *results, int nresults,
	     gpointer user_data)
{
	int i;
	CalBackendExchange *cbex = (CalBackendExchange *) user_data;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex));

	if (msg->errorcode != SOUP_ERROR_DAV_MULTISTATUS) {
		cbex->priv->uids_loaded = TRUE;
		return;
	}

	/* process the results */
	for (i = 0; i < nresults; i++) {
		char *uid;
		char *lastmodified;

		uid = e2k_properties_get_prop (results[i].props,
					       E2K_PR_CALENDAR_UID);
		if (!uid) {
			/* tasks may have only "DAV:uid" */
			uid = e2k_properties_get_prop (results[i].props,
						       E2K_PR_DAV_UID);
			if (!uid)
				continue;
		}

		lastmodified = e2k_properties_get_prop (results[i].props,
							E2K_PR_DAV_LAST_MODIFIED);
		if (lastmodified) {
			if (strcmp (cbex->priv->last_notification, lastmodified) < 0) {
				g_free (cbex->priv->last_notification);
				cbex->priv->last_notification = g_strdup (lastmodified);
			}
		}

		/* update our internal lists */
		if (cbex->priv->folder_type == ICAL_VEVENT_COMPONENT) {
			E2kCalComponent *comp;
			GByteArray *ical_data;

			ical_data = e2k_properties_get_prop (results[i].props, PR_INTERNET_CONTENT);
			if (!ical_data) {
				/* we didn't get the body, so postponing */
				g_hash_table_insert (cbex->priv->missing_uids,
						     g_strdup (uid),
						     g_strdup (results[i].href));
				continue;
                        }

			comp = e2k_cal_component_new_from_string (cbex,
								  results[i].href,
								  ical_data->data,
								  ical_data->len);
			if (E2K_IS_CAL_COMPONENT (comp)) {
				add_component (cbex, comp,
					       cbex->priv->uids_loaded);
			}
		}
		else if (cbex->priv->folder_type == ICAL_VTODO_COMPONENT) {
			E2kCalComponent *comp;

			comp = e2k_cal_component_new_from_props (
				cbex, &results[i], CAL_COMPONENT_TODO);
			if (E2K_IS_CAL_COMPONENT (comp)) {
				add_component (cbex, comp,
					       cbex->priv->uids_loaded);
			}
		}
	}

	cbex->priv->uids_loaded = TRUE;
}

static const char *folder_props[] = {
	PR_ACCESS,
	E2K_PR_DAV_CONTENT_CLASS
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static gboolean
get_folder_properties (E2kConnection *conn, const char *uri,
		       icalcomponent_kind *type, guint32 *access)
{
	const char *prop;
	E2kResult *results = NULL;
	int count, status;

	E2K_DEBUG_HINT ('C');
	status = e2k_connection_propfind_sync (conn, uri, "0",
					       folder_props, n_folder_props,
					       &results, &count);

	if (status != SOUP_ERROR_DAV_MULTISTATUS || count == 0)
		return FALSE;

	prop = e2k_properties_get_prop (results[0].props,
					E2K_PR_DAV_CONTENT_CLASS);
	if (prop) {
		if (!strcmp (prop, "urn:content-classes:calendarfolder"))
			*type = ICAL_VEVENT_COMPONENT;
		else if (!strcmp (prop, "urn:content-classes:taskfolder"))
			*type = ICAL_VTODO_COMPONENT;
		else {
			e2k_results_free (results, count);
			return FALSE;
		}
	}

	prop = e2k_properties_get_prop (results[0].props, PR_ACCESS);
	if (prop)
		*access = atoi (prop);
	else {
		e2k_results_free (results, count);
		return FALSE;
	}

	e2k_results_free (results, count);
	return TRUE;
}

/* open handler for the Exchange backend */
static CalBackendOpenStatus
cal_backend_exchange_open (CalBackend *backend, const char *uristr, gboolean only_if_exists)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	ExchangeHierarchy *hier;
	int status;
	guint32 access;
	GHashTable *cache_objects;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), CAL_BACKEND_OPEN_ERROR);
	g_return_val_if_fail (uristr != NULL, CAL_BACKEND_OPEN_ERROR);

	/* open the connection */
	cbex->priv->account = exchange_component_get_account_for_uri (uristr);
	if (!cbex->priv->account)
		return CAL_BACKEND_OPEN_ERROR;
	cbex->connection = exchange_account_get_connection (cbex->priv->account);
	if (!cbex->connection)
		return CAL_BACKEND_OPEN_ERROR;

	cbex->priv->folder = exchange_account_get_folder (cbex->priv->account, uristr);
	if (!cbex->priv->folder)
		return CAL_BACKEND_OPEN_ERROR;
	g_object_ref (cbex->priv->folder);

	cbex->priv->cache = e2k_cache_new (cbex->priv->folder);

	/* FIXME */
	if (!get_folder_properties (cbex->connection,
				    e_folder_exchange_get_internal_uri (cbex->priv->folder),
				    &cbex->priv->folder_type, &access))
		return CAL_BACKEND_OPEN_ERROR;

	if (!(access & MAPI_ACCESS_READ))
		return CAL_BACKEND_OPEN_PERMISSION_DENIED;

	cbex->priv->writable = ((access & MAPI_ACCESS_CREATE_CONTENTS) != 0);

	hier = e_folder_exchange_get_hierarchy (cbex->priv->folder);
	if (hier->hide_private_items) {
		cbex->priv->private_item_restriction =
			e2k_restriction_prop_int (
				E2K_PR_MAPI_SENSITIVITY, E2K_RELOP_NE, 2);
	} else
		cbex->priv->private_item_restriction = NULL;

	/* subscribe to the notification event */
	e_folder_exchange_subscribe (cbex->priv->folder,
				     E2K_CONNECTION_OBJECT_CHANGED, 30,
				     event_notification_cb, cbex);

	cbex->priv->exchange_uri = g_strdup (uristr);

	/* load all calendar objects (last_timestamp will be
	 * e2k_make_timestamp(0), so everything will be loaded)
	 */
	cbex->priv->uids_loaded = FALSE;
	switch (cbex->priv->folder_type) {
	case ICAL_VEVENT_COMPONENT:
		/* first load cached objects */
		cbex->priv->object_cache_file =
			e_folder_exchange_get_storage_file (cbex->priv->folder,
							    "cache.ics");
		cache_objects = load_object_cache (cbex);

		/* now scan for events, using the cache when possible */
		status = get_changed_events_sync (cbex, cache_objects);

		if (g_hash_table_size (cache_objects) > 0)
			cal_backend_exchange_save (cbex);
		free_cache_objects (cache_objects);
		break;

	case ICAL_VTODO_COMPONENT:
		status = get_changed_tasks_sync (cbex);
		break;

	default:
		/* Shouldn't happen */
		status = SOUP_ERROR_CANCELLED;
		break;
	}
	cbex->priv->uids_loaded = TRUE;

	if (!SOUP_ERROR_IS_SUCCESSFUL (status))
		return CAL_BACKEND_OPEN_ERROR;

	return CAL_BACKEND_OPEN_SUCCESS;
}

/* is_loaded handler for the Exchange backend */
static gboolean
cal_backend_exchange_is_loaded (CalBackend *backend)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), FALSE);

	return (cbex->connection != NULL);
}

/* get_mode handler for the Exchange backend */
static CalMode
cal_backend_exchange_get_mode (CalBackend *backend)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), CAL_MODE_INVALID);

	return CAL_MODE_LOCAL;
}

/* set_mode handler for the Exchange backend */
static void
cal_backend_exchange_set_mode (CalBackend *backend, CalMode mode)
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex));

	if (mode == CAL_MODE_REMOTE)
		cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_Listener_MODE_NOT_SUPPORTED, CAL_MODE_LOCAL);
	else
		cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_Listener_MODE_SET, CAL_MODE_LOCAL);
}

/* get_default_object handler for the Exchange backend */
static char *
cal_backend_exchange_get_default_object (CalBackend *backend, CalObjType type)
{
	CalComponent *comp;
	char *calobj;
	
	comp = cal_component_new ();
	
	switch (type) {
	case CALOBJ_TYPE_EVENT:
		cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);
		break;
	case CALOBJ_TYPE_TODO:
		cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);
		break;
	default:
		g_object_unref (comp);
		return NULL;
	}

	calobj = cal_component_get_as_string (comp);
	g_object_unref (comp);

	return calobj;
}

/* get_n_objects handler for the Exchange backend */
static int
cal_backend_exchange_get_n_objects (CalBackend *backend, CalObjType type)
{
	GList *uids;
	int len;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), -1);

	if (!(type & cbex->priv->folder_type))
		return 0;

	uids = cal_backend_exchange_get_uids (backend, type);
	len = g_list_length (uids);

	g_list_foreach (uids, (GFunc) g_free, NULL);
	g_list_free (uids);

	return len;
}

/* get_object_component handler for the Exchange backend */
static CalComponent *
cal_backend_exchange_get_object_component (CalBackend *backend, const char *uid)
{
	CalComponent *comp;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	comp = lookup_component (cbex, uid);

	return comp;
}

/* get_timezone_object handler for the Exchange backend */
static char *
cal_backend_exchange_get_timezone_object (CalBackend *backend, const char *tzid)
{
	icaltimezone *izone;
	icalcomponent *icalcomp;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	izone = cal_backend_get_timezone (CAL_BACKEND (cbex), tzid);

	icalcomp = icaltimezone_get_component (izone);
	if (icalcomp != NULL)
		return g_strdup (icalcomponent_as_ical_string (icalcomp));

	return g_strdup ("");
}

/* adds uids from a GList of to another list */
static void
append_to_list (gpointer key, gpointer value, gpointer user_data)
{
	char *uid = (char *) key;
	GList **list = (GList **) user_data;

	if (key)
		*list = g_list_append (*list, g_strdup (uid));
}

/* get_uids handler for the Exchange backend */
static GList *
cal_backend_exchange_get_uids (CalBackend *backend, CalObjType type)
{
	GList *list = NULL;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	g_hash_table_foreach (cbex->priv->objects, (GHFunc) append_to_list, &list);
	g_hash_table_foreach (cbex->priv->missing_uids, (GHFunc) append_to_list, &list);

	return list;
}

/* get_objects_in_range handler for the Exchange backend */
static GList *
cal_backend_exchange_get_objects_in_range (CalBackend *backend,
					   CalObjType type,
					   time_t start,
					   time_t end)
{
	char *dtstart;
	char *dtend;
	int status;
	int count;
	int i;
	GList *uidlist = NULL;
	GHashTable *uidhash;
	E2kResult *results = NULL;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	const char *prop;
	E2kRestriction *rn;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (cbex->connection != NULL, NULL);
	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	if (cbex->priv->folder_type == ICAL_VTODO_COMPONENT) {
		/* FIXME: for tasks, we are returning ALL of them */
		return cal_backend_get_uids (CAL_BACKEND (cbex), CALOBJ_TYPE_TODO);
	}

	dtstart = e2k_make_timestamp (start);
	dtend = e2k_make_timestamp (end);
	prop = E2K_PR_CALENDAR_UID;
	rn = e2k_restriction_andv (
		e2k_restriction_prop_bool (
			E2K_PR_DAV_IS_COLLECTION, E2K_RELOP_EQ, FALSE),
		e2k_restriction_prop_string (
			E2K_PR_DAV_CONTENT_CLASS, E2K_RELOP_EQ,
			"urn:content-classes:appointment"),
		e2k_restriction_prop_date (
			E2K_PR_CALENDAR_DTEND, E2K_RELOP_GT, dtstart),
		e2k_restriction_prop_date (
			E2K_PR_CALENDAR_DTSTART, E2K_RELOP_LT, dtend),
		NULL);
	g_free (dtstart);
	g_free (dtend);

	if (cbex->priv->private_item_restriction) {
		e2k_restriction_ref (cbex->priv->private_item_restriction);
		rn = e2k_restriction_andv (rn, cbex->priv->private_item_restriction, NULL);
	}

	/* execute query on server */
	E2K_DEBUG_HINT ('C');
	status = e2k_cache_search (cbex->priv->cache,
				   &prop, 1, rn,
				   &results, &count);
	e2k_restriction_unref (rn);
	if (status != SOUP_ERROR_DAV_MULTISTATUS || !results)
		return NULL;

	uidhash = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < count; i++) {
		char *uid;

		uid = e2k_properties_get_prop (results[i].props,
					       E2K_PR_CALENDAR_UID);
		if (!uid)
			continue;

		/* avoid duplicated uids (recurrences) */
		if (!g_hash_table_lookup (uidhash, uid))
			g_hash_table_insert (uidhash, uid, uid);
	}

	g_hash_table_foreach (uidhash, (GHFunc) append_to_list, &uidlist);
	g_hash_table_destroy (uidhash);
	e2k_results_free (results, count);

	return uidlist;
}

#define THIRTY_MINUTES (30 * 60)

static gboolean
create_freebusy_prop (icalcomponent *vfb, time_t fb_start, time_t fb_end, char old_ch)
{
	icalproperty *prop;
	icalparameter *param = NULL;
	struct icalperiodtype ipt;
	icaltimezone *utc = icaltimezone_get_utc_timezone ();

	switch (old_ch) {
	case '0' :
		param = icalparameter_new_fbtype (ICAL_FBTYPE_FREE);
		break;
	case '1' :
		param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSYTENTATIVE);
		break;
	case '2' :
		param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
		break;
	case '3' :
		param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSYUNAVAILABLE);
		break;
	default :
		return FALSE;
	}

	prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
	ipt.start = icaltime_from_timet_with_zone (fb_start, 0, utc);
	ipt.end = icaltime_from_timet_with_zone (fb_end, 0, utc);
	icalproperty_set_freebusy (prop, ipt);
	icalproperty_add_parameter (prop, param);
	icalcomponent_add_property (vfb, prop);

	return TRUE;
}

static gboolean
set_freebusy_info (icalcomponent *vfb, char *s, time_t start, time_t end)
{
	char old_ch = 0;
	char *s_ptr = s;
	time_t fb_start;
	time_t fb_end;
	gboolean there_are_fb_props = FALSE;

	g_return_val_if_fail (vfb != NULL, FALSE);
	g_return_val_if_fail (s != NULL, FALSE);

	fb_start = start;
	fb_end = start;

	while (*s_ptr && *s_ptr != '\0') {
		if (old_ch == '4')
			fb_start = fb_end;
		else if (old_ch != *s_ptr && old_ch != 0) {
			if (create_freebusy_prop (vfb, fb_start, fb_end, old_ch))
				there_are_fb_props = TRUE;
			fb_start = fb_end;
		}

		fb_end += THIRTY_MINUTES;
		old_ch = *s_ptr;
		s_ptr++;
	}

	/* create the last F/B property */
	if (old_ch != '4')
		there_are_fb_props = create_freebusy_prop (vfb, fb_start, fb_end, old_ch);

	return there_are_fb_props;
}

/* get_free_busy handler for the Exchange backend */
static GList *
cal_backend_exchange_get_free_busy (CalBackend *backend, GList *users, time_t start, time_t end)
{
	char *uri;
	char *start_str;
	char *end_str;
	char *users_str = NULL;
	GList *obj_list = NULL;
	char *body;
	int len;
	xmlDoc *xmldoc;
	xmlNode *xmlrec, *xmlitem;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	icaltimezone *utc = icaltimezone_get_utc_timezone ();

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	if (users) {
		GString *str = g_string_new (NULL);
		GList *l;

		for (l = users; l; l = l->next) {
			str = g_string_append (str, "&u=SMTP:");
			str = g_string_append (str, (gchar *) l->data);
		}

		users_str = str->str;
		g_string_free (str, FALSE);
	} else
		users_str = g_strdup ("");

	/* The calendar component (currently) sets start to "exactly
	 * 24 hours ago". But since we're going to get the information
	 * in 30-minute intervals starting from "start", we want to
	 * round off to the nearest half hour.
	 */

	start = THIRTY_MINUTES * (start / THIRTY_MINUTES);
	start_str = e2k_make_timestamp (start);
	end_str = e2k_make_timestamp (end);
	uri = g_strdup_printf (
		"%s/?Cmd=freebusy&start=%s&end=%s&interval=30%s",
		cbex->priv->account->public_uri,
		start_str, end_str, users_str);
	g_free (users_str);
	g_free (start_str);
	g_free (end_str);

	E2K_DEBUG_HINT ('F');
	if (e2k_connection_get_owa_sync (cbex->connection, uri, TRUE, &body, &len)
	    != SOUP_ERROR_OK) {
		g_free (uri);
		return NULL;
	}
	g_free (uri);

	/* parse the XML returned by the Exchange server */
	xmldoc = e2k_parse_xml (body, len);
	g_free (body);
	if (!xmldoc)
		return NULL;

	xmlrec = e2k_xml_find (xmldoc->children, "recipients");
	if (!xmlrec) {
		xmlFreeDoc (xmldoc);
		return NULL;
	}

	for (xmlitem = xmlrec->children; xmlitem; xmlitem = xmlitem->next) {
		icalcomponent *vfb;			
		icalproperty *prop = NULL;
		icalparameter *param = NULL;
		char *s;
		xmlNodePtr n;
		char *calobj;
		gboolean has_fb_props = FALSE;
		gboolean has_email = FALSE;

		if (xmlitem->type != XML_ELEMENT_NODE ||
		    strcmp (xmlitem->name, "item") != 0)
			continue;

		vfb = icalcomponent_new_vfreebusy ();
		icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, 0, utc));
		icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, 0, utc));

		/* get properties for this user */
		for (n = xmlitem->xmlChildrenNode; n; n = n->next) {
			s = xmlNodeListGetString (xmldoc, n->xmlChildrenNode, 1);
			if (!s)
				continue;

			if (!strcmp (n->name, "displayname")) {
				param = icalparameter_new_cn ((const char *) s);
				if (param)
					icalproperty_add_parameter (prop, param);
			}
			else if (!strcmp (n->name, "email")) {
				char *org = g_strdup_printf ("MAILTO:%s", s);

				prop = icalproperty_new_organizer ((const char *) org);
				icalcomponent_add_property (vfb, prop);
				if (param) {
					icalproperty_add_parameter (prop, param);
					has_email = TRUE;
				}
				g_free (org);
			}
			else if (!strcmp (n->name, "fbdata")) {
				if (set_freebusy_info (vfb, s, start, end))
					has_fb_props = TRUE;
			}

			xmlFree (s);
		}

		if (has_email && has_fb_props) {
			calobj = icalcomponent_as_ical_string (vfb);
			obj_list = g_list_append (obj_list, g_strdup (calobj));
		}

		icalcomponent_free (vfb);
	}

	/* free memory */
	xmlFreeDoc (xmldoc);

	return obj_list;
}

typedef struct {
	CalBackendExchange *cbex;
	CalObjType type;
	GList *changes;
	GList *change_ids;
} _get_changes_data_t;

static void
compute_changes_foreach_key (const char *key, gpointer user_data)
{
	_get_changes_data_t *cdata = user_data;
	CalComponent *comp;

	g_return_if_fail (cdata != NULL);

	comp = lookup_component (cdata->cbex, key);
	if (comp == NULL) {
		GNOME_Evolution_Calendar_CalObjChange *coc;
		char *calobj;

		comp = cal_component_new ();

		if (cdata->type == GNOME_Evolution_Calendar_TYPE_EVENT)
			cal_component_set_new_vtype (comp, CAL_COMPONENT_EVENT);
		else if (cdata->type == GNOME_Evolution_Calendar_TYPE_TODO)
			cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);
		else
			return;

		cal_component_set_uid (comp, key);
		calobj = cal_component_get_as_string (comp);

		coc = GNOME_Evolution_Calendar_CalObjChange__alloc ();
		coc->calobj = CORBA_string_dup (calobj);
		coc->type = GNOME_Evolution_Calendar_DELETED;
		cdata->changes = g_list_prepend (cdata->changes, coc);
		cdata->change_ids = g_list_prepend (cdata->change_ids, g_strdup (key));

		g_free (calobj);
		g_object_unref (comp);
	}
}

/* get_changes handler for the Exchange backend */
static GNOME_Evolution_Calendar_CalObjChangeSeq *
cal_backend_exchange_get_changes (CalBackend *backend, CalObjType type, const char *change_id)
{
	GNOME_Evolution_Calendar_CalObjChangeSeq *seq;
	_get_changes_data_t cdata;
	char *dirname, *filename;
	EXmlHash *xmlhash;
	GList *uids;
	GList *changes = NULL;
	GList *change_ids = NULL;
	GList *l;
	GList *m;
	int n;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	/* open the changes file */
	dirname = g_strdup_printf ("%s/Calendar", cbex->priv->account->storage_dir);
	if (e_mkdir_hier (dirname, S_IRWXU) != 0) {
		g_message ("cal_backend_exchange_get_changes: Cannot create directory %s", dirname);
		return NULL;
	}

	filename = g_strdup_printf ("%s/%s.changes", dirname, change_id);
	xmlhash = e_xmlhash_new (filename);
	g_free (filename);
	g_free (dirname);

	/* calculate additions and modifications */
	uids = cal_backend_get_uids (CAL_BACKEND (cbex), CALOBJ_TYPE_ANY);
	for (l = g_list_first (uids); l != NULL; l = l->next) {
		GNOME_Evolution_Calendar_CalObjChange *coc;
		char *uid = (char *) l->data;
		char *calobj;
		CalComponent *comp;

		comp = lookup_component (cbex, uid);
		g_assert (comp != NULL);

		calobj = cal_component_get_as_string (comp);

		/* check type of change that has occurred */
		switch (e_xmlhash_compare (xmlhash, uid, calobj)) {
		case E_XMLHASH_STATUS_SAME :
			break;
		case E_XMLHASH_STATUS_NOT_FOUND :
			coc = GNOME_Evolution_Calendar_CalObjChange__alloc ();
			coc->calobj = CORBA_string_dup (calobj);
			coc->type = GNOME_Evolution_Calendar_ADDED;
			changes = g_list_prepend (changes, coc);
			change_ids = g_list_prepend (change_ids, g_strdup (uid));
			break;
		case E_XMLHASH_STATUS_DIFFERENT :
			coc = GNOME_Evolution_Calendar_CalObjChange__alloc ();
			coc->calobj = CORBA_string_dup (calobj);
			coc->type = GNOME_Evolution_Calendar_MODIFIED;
			changes = g_list_prepend (changes, coc);
			change_ids = g_list_prepend (change_ids, g_strdup (uid));
			break;
		}

		g_free (calobj);
	}

	cal_obj_uid_list_free (uids);

	/* calculate deletions */
	cdata.cbex = cbex;
	cdata.type = type;
	cdata.changes = changes;
	cdata.change_ids = change_ids;

	e_xmlhash_foreach_key (xmlhash,
			       (EXmlHashFunc) compute_changes_foreach_key,
			       &cdata);
	changes = cdata.changes;
	change_ids = cdata.change_ids;

	/* build the CORBA sequence and update the changes file */
	n = g_list_length (changes);

	seq = GNOME_Evolution_Calendar_CalObjChangeSeq__alloc ();
	seq->_length = n;
	seq->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalObjChange_allocbuf (n);
	CORBA_sequence_set_release (seq, TRUE);

	for (l = changes, m = change_ids, n = 0; l != NULL; l = l->next, m = m->next, n++) {
		GNOME_Evolution_Calendar_CalObjChange *coc = l->data;
		GNOME_Evolution_Calendar_CalObjChange *seq_coc;
		char *uid = (char *) m->data;

		seq_coc = &seq->_buffer[n];
		seq_coc->calobj = CORBA_string_dup (coc->calobj);
		seq_coc->type = coc->type;

		/* update the changes file */
		if (coc->type == GNOME_Evolution_Calendar_ADDED ||
		    coc->type == GNOME_Evolution_Calendar_MODIFIED) {
			icalcomponent *icalcomp;

			/* parse the given data */
			icalcomp = icalparser_parse_string (coc->calobj);
			if (icalcomp) {
				icalcomponent_kind kind = icalcomponent_isa (icalcomp);

				if ((kind == cbex->priv->folder_type))
					e_xmlhash_add (xmlhash, uid, coc->calobj);

				icalcomponent_free (icalcomp);
			}
			else
				g_warning ("Invalid iCal object");
		}
		else
			e_xmlhash_remove (xmlhash, uid);

		CORBA_free (coc);
		g_free (uid);
	}

	e_xmlhash_write (xmlhash);

	e_xmlhash_destroy (xmlhash);
	g_list_free (change_ids);
	g_list_free (changes);

	return seq;
}

typedef struct {
	GList *list;
	CalBackendExchange *cbex;
} GetAlarmsData;

static void
add_object_to_list (gpointer key, gpointer value, gpointer user_data)
{
	CalComponent *comp;
	const char *uid = (const char *) key;
	GetAlarmsData *data = (GetAlarmsData *) user_data;

	comp = lookup_component (data->cbex, uid);
	if (CAL_IS_COMPONENT (comp))
		data->list = g_list_append (data->list, comp);
}

/* get_alarms_in_range handler for the Exchange backend */
static GNOME_Evolution_Calendar_CalComponentAlarmsSeq *
cal_backend_exchange_get_alarms_in_range (CalBackend *backend, time_t start, time_t end)
{
	int alarm_count;
	int i;
	GSList *comp_alarms, *l;
	CalComponentAlarms *alarms;
	CalAlarmAction omit[] = { -1 };
	GetAlarmsData data;
	GNOME_Evolution_Calendar_CalComponentAlarmsSeq *seq;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	ExchangeHierarchy *hier;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (start != -1 && end != -1, NULL);
	g_return_val_if_fail (start <= end, NULL);

	/* we return no alarms for public or other user's folders */
	hier = e_folder_exchange_get_hierarchy (cbex->priv->folder);
	if (hier->type != EXCHANGE_HIERARCHY_PERSONAL) {
		seq = GNOME_Evolution_Calendar_CalComponentAlarmsSeq__alloc ();
		CORBA_sequence_set_release (seq, TRUE);
		seq->_length = 0;
		seq->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalComponentAlarms_allocbuf (0);

		return seq;
	}

	/* generate list of alarms */
	comp_alarms = NULL;
	alarm_count = 0;
	data.list = NULL;
	data.cbex = cbex;

	g_hash_table_foreach (cbex->priv->objects, (GHFunc) add_object_to_list, &data);

	alarm_count = cal_util_generate_alarms_for_list (data.list, start, end, omit,
							 &comp_alarms, resolve_tzid,
							 cbex, cbex->priv->default_timezone);

	/* create the CORBA sequence */
	seq = GNOME_Evolution_Calendar_CalComponentAlarmsSeq__alloc ();
	CORBA_sequence_set_release (seq, TRUE);
	seq->_length = alarm_count;
	seq->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_CalComponentAlarms_allocbuf (alarm_count);

	for (l = comp_alarms, i = 0; l != NULL; l = l->next, i++) {
		char *comp_str;

		alarms = (CalComponentAlarms *) l->data;

		comp_str = cal_component_get_as_string (alarms->comp);
		seq->_buffer[i].calobj = CORBA_string_dup (comp_str);
		g_free (comp_str);

		cal_backend_util_fill_alarm_instances_seq (&seq->_buffer[i].alarms, alarms->alarms);
		cal_component_alarms_free (alarms);
	}

	g_slist_free (comp_alarms);
	g_list_free (data.list);

	return seq;
}

/* get_alarms_for_object handler for the Exchange backend */
static GNOME_Evolution_Calendar_CalComponentAlarms *
cal_backend_exchange_get_alarms_for_object (CalBackend *backend,
					    const char *uid,
					    time_t start,
					    time_t end,
					    gboolean *object_found)
{
	GNOME_Evolution_Calendar_CalComponentAlarms *corba_alarms;
	CalComponentAlarms *alarms;
	CalAlarmAction omit[] = { -1 };
	CalComponent *comp;
	char *comp_str;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	comp = lookup_component (cbex, uid);
	if (!comp) {
		*object_found = FALSE;
		return NULL;
	}
	*object_found = TRUE;

	/* fill the CORBA sequence */
	comp_str = cal_component_get_as_string (comp);
	corba_alarms = GNOME_Evolution_Calendar_CalComponentAlarms__alloc ();
	corba_alarms->calobj = CORBA_string_dup (comp_str);
	g_free (comp_str);

	alarms = cal_util_generate_alarms_for_comp (comp, start, end, omit,
						    resolve_tzid, cbex,
						    cbex->priv->default_timezone);
        if (alarms) {
                cal_backend_util_fill_alarm_instances_seq (&corba_alarms->alarms, alarms->alarms);
                cal_component_alarms_free (alarms);
        }
	else
                cal_backend_util_fill_alarm_instances_seq (&corba_alarms->alarms, NULL);

        return corba_alarms;
}

/* discard_alarm handler for the Exchange backend */
static CalBackendResult
cal_backend_exchange_discard_alarm (CalBackend *backend, const char *uid, const char *auid)
{
	CalComponent *comp;
	CalBackendResult result = CAL_BACKEND_RESULT_SUCCESS;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), CAL_BACKEND_RESULT_NOT_FOUND);
	g_return_val_if_fail (uid != NULL, CAL_BACKEND_RESULT_NOT_FOUND);
	g_return_val_if_fail (auid != NULL, CAL_BACKEND_RESULT_NOT_FOUND);

	if (!cbex->priv->writable) {
		g_warning ("Folder is not writable");
		return CAL_BACKEND_RESULT_PERMISSION_DENIED;
	}

	comp = lookup_component (cbex, uid);
	if (!comp)
		return CAL_BACKEND_RESULT_NOT_FOUND;

	if (cal_component_has_recurrences (comp)) {
	} else {
		char *calobj;

		cal_component_remove_alarm (comp, auid);
		calobj = cal_component_get_as_string (comp);
		result = cal_backend_exchange_update_objects ((CalBackend *) cbex, calobj, CALOBJ_MOD_ALL);
		g_free (calobj);
	}

	return result;
}

static CalBackendResult
update_component (CalBackendExchange *cbex, icalcomponent *icalcomp,
		  icalproperty_method method)
{
	CalComponent *updated_comp, *cur_comp;
	const char *uid;
	char *href, *new_uid;
	gboolean is_new_comp;
	CalComponentRange recur_id;
	int status;
	GSList *categories;

	updated_comp = CAL_COMPONENT (e2k_cal_component_new (cbex));
	if (!cal_component_set_icalcomponent (updated_comp, icalcomp)) {
		g_object_unref (updated_comp);
		return CAL_BACKEND_RESULT_INVALID_OBJECT;
	}

	cal_component_get_uid (updated_comp, &uid);
	cur_comp = lookup_component (cbex, uid);

	cal_component_get_recurid (updated_comp, &recur_id);
	if (cur_comp && recur_id.datetime.value) {
		GSList *exdates;

		/* We are either cancelling or editing a single
		 * instance. Either way, we need to EXDATE out
		 * this instance from the master appointment.
		 */

		cal_component_get_exdate_list (cur_comp, &exdates);
		exdates = g_slist_prepend (exdates, &recur_id.datetime);
		cal_component_set_exdate_list (cur_comp, exdates);
		cal_component_commit_sequence (cur_comp);
		cal_component_free_exdate_list (exdates->next);

		cal_component_free_datetime (&recur_id.datetime);
		g_slist_free_1 (exdates);

		status = e2k_cal_component_update (E2K_CAL_COMPONENT (cur_comp),
						   ICAL_METHOD_REQUEST, FALSE);
		if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
			g_object_unref (updated_comp);
			return CAL_BACKEND_RESULT_INVALID_OBJECT;
		}

		if (method == ICAL_METHOD_CANCEL) {
			/* There's nothing else interesting in the new
			 * comp, so we're done.
			 */
			g_object_unref (updated_comp);
			return CAL_BACKEND_RESULT_SUCCESS;
		}

		/* Evo 1.4 doesn't handle having two events with the
		 * same UID but different RECURRENCE-ID, so we have to
		 * split this off from the master event.
		 */
		cal_component_set_recurid (updated_comp, NULL);
		new_uid = cal_component_gen_uid ();
		cal_component_set_uid (updated_comp, new_uid);
		g_free (new_uid);
	}

	if (/* no recurid && */ method == ICAL_METHOD_CANCEL) {
		/* Remove the existing component */
		g_object_unref (updated_comp);
		status = e2k_cal_component_remove (E2K_CAL_COMPONENT (cur_comp));
		if (SOUP_ERROR_IS_SUCCESSFUL (status) ||
		    status == SOUP_ERROR_NOT_FOUND) {
			remove_component (cbex, uid, TRUE);
			return CAL_BACKEND_RESULT_SUCCESS;
		} else
			return CAL_BACKEND_RESULT_NOT_FOUND;
	}

	if (cur_comp) {
		href = g_strdup (e2k_cal_component_get_href (E2K_CAL_COMPONENT (cur_comp)));
		is_new_comp = FALSE;

		/* Unref the old set of categories */
		cal_component_get_categories_list (cur_comp, &categories);
		cal_backend_unref_categories (CAL_BACKEND (cbex), categories);
		cal_component_free_categories_list (categories);
	} else {
		href = get_href_for_comp (cbex, updated_comp);
		is_new_comp = TRUE;
	}
	e2k_cal_component_set_href (E2K_CAL_COMPONENT (updated_comp), href);
	status = e2k_cal_component_update (E2K_CAL_COMPONENT (updated_comp),
					   method, is_new_comp);
	if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
		g_object_unref (updated_comp);
		g_free (href);
		return CAL_BACKEND_RESULT_INVALID_OBJECT;
	}

	/* Ref the new set of categories */
	cal_component_get_categories_list (updated_comp, &categories);
	cal_backend_unref_categories (CAL_BACKEND (cbex), categories);
	cal_component_free_categories_list (categories);

       	g_free (href);
	g_object_unref (updated_comp);
	return CAL_BACKEND_RESULT_SUCCESS;
}

/* update_object handler for the Exchange backend */
static CalBackendResult
cal_backend_exchange_update_objects (CalBackend *backend, const char *calobj,
				     CalObjModType mod)
{
	icalcomponent *icalcomp;
	icalcomponent_kind kind;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	int tz_found = 0;
	int obj_found = 0;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), CAL_BACKEND_RESULT_INVALID_OBJECT);
	g_return_val_if_fail (calobj != NULL, CAL_BACKEND_RESULT_INVALID_OBJECT);

	if (!cbex->priv->writable) {
		g_warning ("Folder is not writable");
		return CAL_BACKEND_RESULT_PERMISSION_DENIED;
	}

	/* check the component in the string */
	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return CAL_BACKEND_RESULT_INVALID_OBJECT;

	kind = icalcomponent_isa (icalcomp);
	if (kind == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;
		icalcomponent *vcalendar_comp;
		icalproperty *method_prop;
		icalproperty_method method_value;

		vcalendar_comp = icalcomp;
		method_prop = icalcomponent_get_first_property (vcalendar_comp,
								ICAL_METHOD_PROPERTY);
		if (method_prop)
			method_value = icalproperty_get_method (method_prop);
		else
			method_value = ICAL_METHOD_REQUEST;

		/* traverse all timezones to add them to the backend */
		subcomp = icalcomponent_get_first_component (vcalendar_comp,
							     ICAL_VTIMEZONE_COMPONENT);
		while (subcomp) {
			tz_found++;
			cal_backend_exchange_add_timezone (cbex, subcomp);
			subcomp = icalcomponent_get_next_component (vcalendar_comp,
                                                                    ICAL_VTIMEZONE_COMPONENT);
		}

		/* traverse all sub-components */
		subcomp = icalcomponent_get_first_component (vcalendar_comp, cbex->priv->folder_type);
		while (subcomp) {
			if (update_component (cbex, icalcomponent_new_clone (subcomp), method_value) !=
			    CAL_BACKEND_RESULT_SUCCESS)
				break;

			obj_found++;

			subcomp = icalcomponent_get_next_component (vcalendar_comp, cbex->priv->folder_type);
		}

		icalcomponent_free (icalcomp);
	} else if (kind == cbex->priv->folder_type) {
		if (update_component (cbex, icalcomp, ICAL_METHOD_REQUEST) ==
		    CAL_BACKEND_RESULT_SUCCESS)
			obj_found++;
	} else
		icalcomponent_free (icalcomp);

	if (obj_found > 0) {
		e2k_cache_clear (cbex->priv->cache);
		if (cbex->priv->folder_type == ICAL_VEVENT_COMPONENT)
			get_changed_events_sync (cbex, NULL);
		else if (cbex->priv->folder_type == ICAL_VTODO_COMPONENT)
			get_changed_tasks_sync (cbex);
	}

	if (tz_found + obj_found > 0)
		return CAL_BACKEND_RESULT_SUCCESS;
	else
		return CAL_BACKEND_RESULT_INVALID_OBJECT;
}

/* remove_object handler for the Exchange backend */
static CalBackendResult
cal_backend_exchange_remove_object (CalBackend *backend, const char *uid,
				    CalObjModType mod)
{
	CalComponent *comp;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	int status;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), CAL_BACKEND_RESULT_NOT_FOUND);

	if (!cbex->priv->writable) {
		g_warning ("Folder is not writable");
		return CAL_BACKEND_RESULT_PERMISSION_DENIED;
	}

	comp = lookup_component (cbex, uid);
	if (comp != NULL) {
		status = e2k_cal_component_remove (E2K_CAL_COMPONENT (comp));
		if (SOUP_ERROR_IS_SUCCESSFUL (status)) {
			remove_component (cbex, uid, TRUE);
			return CAL_BACKEND_RESULT_SUCCESS;
		} else if (status == SOUP_ERROR_CANT_AUTHENTICATE)
			return CAL_BACKEND_RESULT_PERMISSION_DENIED;

		/* we fire the notification even if the removal failed
		   (only if the object exists in our internal lists),
		   since this is the case for removed objects, for which
		   we haven't get a notification */
		remove_component (cbex, uid, TRUE);
	}

	return CAL_BACKEND_RESULT_NOT_FOUND;
}

static CalBackendSendResult
cal_backend_exchange_send_object (CalBackend *backend, const char *calobj, char **new_calobj,
				  GNOME_Evolution_Calendar_UserList **user_list, char error_msg[256])
{
	CalBackendExchange *cbex = (CalBackendExchange *) backend;
	CalBackendSendResult retval;
	CalBackendExchangeBookingResult result;
	E2kCalComponent *e2k_comp;
	icalcomponent *top_level, *icalcomp, *tzcomp;
	icalproperty *prop;
	icalproperty_method method;
	int count;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), CAL_BACKEND_SEND_INVALID_OBJECT);

	top_level = icalparser_parse_string (calobj);
	icalcomp = icalcomponent_get_inner (top_level);

	e2k_comp = e2k_cal_component_new (cbex);
	cal_component_set_icalcomponent (CAL_COMPONENT (e2k_comp), 
					 icalcomponent_new_clone (icalcomp));
	
	count = icalcomponent_count_properties (icalcomp, ICAL_ATTENDEE_PROPERTY);
	*user_list = GNOME_Evolution_Calendar_UserList__alloc ();
	(*user_list)->_maximum = count;
	(*user_list)->_length = 0;
	(*user_list)->_buffer = CORBA_sequence_GNOME_Evolution_Calendar_User_allocbuf (count);

	method = icalcomponent_get_method (top_level);
	if (icalcomponent_isa (icalcomp) != ICAL_VEVENT_COMPONENT 
	    || (method != ICAL_METHOD_REQUEST && method != ICAL_METHOD_CANCEL)) {
		*new_calobj = g_strdup (calobj);
		retval = CAL_BACKEND_SEND_SUCCESS;
		goto cleanup;
	}
	
	/* traverse all timezones to add them to the backend */
	tzcomp = icalcomponent_get_first_component (top_level,
						    ICAL_VTIMEZONE_COMPONENT);
	while (tzcomp) {
		cal_backend_exchange_add_timezone (cbex, tzcomp);
		tzcomp = icalcomponent_get_next_component (top_level,
							   ICAL_VTIMEZONE_COMPONENT);
	}

	for (prop = icalcomponent_get_first_property (icalcomp, ICAL_ATTENDEE_PROPERTY);
	     prop != NULL;
	     prop = icalcomponent_get_next_property (icalcomp, ICAL_ATTENDEE_PROPERTY))
	{
		icalvalue *value;
		icalparameter *param;
		const char *attendee;

		param = icalproperty_get_first_parameter (prop, ICAL_CUTYPE_PARAMETER);
		if (!param)
			continue;
		if (icalparameter_get_cutype (param) != ICAL_CUTYPE_RESOURCE)
			continue;
		
		value = icalproperty_get_value (prop);
		if (!value)
			continue;
		
		attendee = icalvalue_get_string (value);
		if (g_ascii_strncasecmp ("mailto:", attendee, 7))
			continue;

		/* See if it recurs */
		if (icalcomponent_get_first_property (icalcomp, ICAL_RRULE_PROPERTY)
		    || icalcomponent_get_first_property (icalcomp, ICAL_RDATE_PROPERTY)) {
			g_snprintf (error_msg, 256, 
				    _("Unable to schedule resource '%s' for recurring meetings.\n"
				      "You must book each meeting separately."),
				    attendee + 7);

			retval = CAL_BACKEND_SEND_BUSY;
			goto cleanup;
		}
		
		result = book_resource (cbex, attendee + 7, e2k_comp, method);
		switch (result) {
		case CAL_BACKEND_EXCHANGE_BOOKING_OK:
			param = icalproperty_get_first_parameter (prop, ICAL_PARTSTAT_PARAMETER);
			icalparameter_set_partstat (param, ICAL_PARTSTAT_ACCEPTED);

			(*user_list)->_buffer[(*user_list)->_length] = CORBA_string_dup (attendee);
			(*user_list)->_length++;
			break;
		case CAL_BACKEND_EXCHANGE_BOOKING_BUSY:
			g_snprintf (error_msg, 256, 
				    _("The resource '%s' is busy during the selected time period."),
				    attendee + 7);

			retval = CAL_BACKEND_SEND_BUSY;
			goto cleanup;

		case CAL_BACKEND_EXCHANGE_BOOKING_PERMISSION_DENIED:
		case CAL_BACKEND_EXCHANGE_BOOKING_NO_SUCH_USER:
			/* Do nothing, we fallback to iMip */
			break;
		case CAL_BACKEND_EXCHANGE_BOOKING_ERROR:
			/* What should we do here? */
			retval = CAL_BACKEND_SEND_PERMISSION_DENIED;
			goto cleanup;
		}
	}

	retval = CAL_BACKEND_SEND_SUCCESS;
	*new_calobj = g_strdup (icalcomponent_as_ical_string (top_level));

 cleanup:	
	icalcomponent_free (top_level);
	g_object_unref (e2k_comp);
	
	return retval;
}

/* get_timezone handler for the Exchange backend */
static icaltimezone *
cal_backend_exchange_get_timezone (CalBackend *backend, const char *tzid)
{
	icaltimezone *izone;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	izone = g_hash_table_lookup (cbex->priv->timezones, tzid);
	if (!izone)
		izone = icaltimezone_get_builtin_timezone_from_tzid (tzid);

	return izone;
}

/* get_default_timezone handler for the Exchange backend */
static icaltimezone *
cal_backend_exchange_get_default_timezone (CalBackend *backend)
{
	int status;
	E2kResult *results = NULL;
	int count;
	static char *tzid = NULL;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	if (cbex->priv->default_timezone)
		return cbex->priv->default_timezone;

	if (!tzid) {
		/* get the default zone from the server */
		const char *prop = E2K_PR_EXCHANGE_TIMEZONE;

		E2K_DEBUG_HINT ('C');
		status = e2k_connection_propfind_sync (
			cbex->connection,
			cbex->priv->account->home_uri,
			"0", &prop, 1,
			&results, &count);

		if (status != SOUP_ERROR_DAV_MULTISTATUS)
			return NULL;
		if (count == 0) {
			e2k_results_free (results, count);
			return NULL;
		}

		/* resolve the timezone ID */
		tzid = g_strdup (e2k_properties_get_prop (
					 results[0].props,
					 E2K_PR_EXCHANGE_TIMEZONE));

		e2k_results_free (results, count);
	}

	if (tzid) {
		cbex->priv->default_timezone = cal_backend_get_timezone (CAL_BACKEND (cbex),
									 tzid);
	}

	return cbex->priv->default_timezone;
}

/* set_default_timezone handler for the Exchange backend */
static gboolean
cal_backend_exchange_set_default_timezone (CalBackend *backend, const char *id)
{
	icaltimezone *zone;
	CalBackendExchange *cbex = (CalBackendExchange *) backend;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), FALSE);

	zone = cal_backend_exchange_get_timezone (backend, id);
	if (!zone)
		return FALSE;

	cbex->priv->default_timezone = zone;

	return TRUE;
}

void
cal_backend_exchange_add_timezone (CalBackendExchange *cbex,
				   icalcomponent *vtzcomp)
{
	icaltimezone *tz;
	icalcomponent *new_vtzcomp;
	char *tzid;

	g_return_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex));
	g_return_if_fail (vtzcomp != NULL);

	tz = icaltimezone_new ();
	new_vtzcomp = icalcomponent_new_clone (vtzcomp);
	if (!icaltimezone_set_component (tz, new_vtzcomp)) {
		icaltimezone_free (tz, TRUE);
		icalcomponent_free (new_vtzcomp);
		return;
	}

	tzid = icaltimezone_get_tzid (tz);
	if (!g_hash_table_lookup (cbex->priv->timezones, tzid))
		g_hash_table_insert (cbex->priv->timezones, g_strdup (tzid), tz);
	else
		icaltimezone_free (tz, TRUE);
}
