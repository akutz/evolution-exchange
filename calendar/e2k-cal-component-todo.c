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
#include <fcntl.h>
#include <unistd.h>

#include "e2k-cal-component.h"
#include "e2k-propnames.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"

static void
set_categories (E2kProperties *props, E2kCalComponent *comp)
{
	GSList *categories;
	GSList *sl;
	GPtrArray *array;

	cal_component_get_categories_list (CAL_COMPONENT (comp), &categories);
	if (!categories) {
		e2k_properties_remove (props, E2K_PR_EXCHANGE_KEYWORDS);
		return;
	}

	array = g_ptr_array_new ();
	for (sl = categories; sl != NULL; sl = sl->next) {
		char *cat = (char *) sl->data;

		if (cat)
			g_ptr_array_add (array, g_strdup (cat));
	}
	cal_component_free_categories_list (categories);

	e2k_properties_set_string_array (props, E2K_PR_EXCHANGE_KEYWORDS, array);
}

static void
set_sensitivity (E2kProperties *props, E2kCalComponent *comp)
{
	CalComponentClassification classif;
	int sensitivity;

	cal_component_get_classification (CAL_COMPONENT (comp), &classif);
	switch (classif) {
	case CAL_COMPONENT_CLASS_PRIVATE:
		sensitivity = 2;
		break;
	case CAL_COMPONENT_CLASS_CONFIDENTIAL:
		sensitivity = 1;
		break;
	default:
		sensitivity = 0;
		break;
	}

	e2k_properties_set_int (props, E2K_PR_MAPI_SENSITIVITY, sensitivity);
}

static void
set_date_completed (E2kProperties *props, E2kCalComponent *comp)
{
	struct icaltimetype *itt;
	char *tstr;

	cal_component_get_completed (CAL_COMPONENT (comp), &itt);
	if (!itt || icaltime_is_null_time (*itt)) {
		e2k_properties_remove (props, E2K_PR_OUTLOOK_TASK_DONE_DT);
		return;
	}

	icaltimezone_convert_time (itt,
				   icaltimezone_get_builtin_timezone (itt->zone),
				   icaltimezone_get_utc_timezone ());
	tstr = icaltime_to_e2k_time (itt);
	cal_component_free_icaltimetype (itt);

	e2k_properties_set_date (props, E2K_PR_OUTLOOK_TASK_DONE_DT, tstr);
}

static char *
convert_to_utc (CalComponentDateTime *dt)
{
	icaltimezone *from_zone;
	icaltimezone *utc_zone;

	from_zone = icaltimezone_get_builtin_timezone_from_tzid (dt->tzid);
	utc_zone = icaltimezone_get_utc_timezone ();
	if (!from_zone)
		from_zone = get_default_timezone ();
	dt->value->is_date = 0;
	icaltimezone_convert_time (dt->value, from_zone, utc_zone);

	return calcomponentdatetime_to_string (dt, utc_zone);
}

static void
set_dtstart (E2kProperties *props, E2kCalComponent *comp)
{
	CalComponentDateTime dt;
	char *dtstart_str;

	cal_component_get_dtstart (CAL_COMPONENT (comp), &dt);
	if (!dt.value || icaltime_is_null_time (*dt.value)) {
		cal_component_free_datetime (&dt);
		e2k_properties_remove (props, E2K_PR_MAPI_COMMON_START);
		return;
	}

	dtstart_str = convert_to_utc (&dt);
	cal_component_free_datetime (&dt);
	e2k_properties_set_date (props, E2K_PR_MAPI_COMMON_START, dtstart_str);
}

static void
set_due_date (E2kProperties *props, E2kCalComponent *comp)
{
	CalComponentDateTime dt;
	char *due_str;

	cal_component_get_due (CAL_COMPONENT (comp), &dt);
	if (!dt.value || icaltime_is_null_time (*dt.value)) {
		cal_component_free_datetime (&dt);
		e2k_properties_remove (props, E2K_PR_MAPI_COMMON_END);
		return;
	}

	due_str = convert_to_utc (&dt);
	cal_component_free_datetime (&dt);
	e2k_properties_set_date (props, E2K_PR_MAPI_COMMON_END, due_str);
}

static void
set_percent (E2kProperties *props, E2kCalComponent *comp)
{
	int *percent;
	float res;

	cal_component_get_percent (CAL_COMPONENT (comp), &percent);
	if (percent) {
		res = (float) *percent / 100.0;
		cal_component_free_percent (percent);
	} else
		res = 0.;

	e2k_properties_set_float (props, E2K_PR_OUTLOOK_TASK_PERCENT, res);
}

static void
set_priority (E2kProperties *props, E2kCalComponent *comp)
{
	int *priority, value = 0;

	cal_component_get_priority (CAL_COMPONENT (comp), &priority);
	if (priority) {
		if (*priority == 0)
			value = 0;
		else if (*priority <= 4)
			value = 1;
		else if (*priority == 5)
			value = 0;
		else
			value = 2;
		cal_component_free_priority (priority);
	}

	e2k_properties_set_int (props, E2K_PR_MAPI_PRIORITY, value);
}

static void
set_status (E2kProperties *props, E2kCalComponent *comp)
{
	icalproperty_status ical_status;
	int status;

	cal_component_get_status (CAL_COMPONENT (comp), &ical_status);
	switch (ical_status) {
	case ICAL_STATUS_NONE :
	case ICAL_STATUS_NEEDSACTION :
		/* Not Started */
		status = 0;
		break;
	case ICAL_STATUS_INPROCESS :
		/* In Progress */
		status = 1;
		break;
	case ICAL_STATUS_COMPLETED :
		/* Completed */
		status = 2;
		break;
	case ICAL_STATUS_CANCELLED :
		/* Deferred */
		status = 4;
		break;
	default :
		status = 0;
	}

	e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_STATUS, status);
	e2k_properties_set_bool (props, E2K_PR_OUTLOOK_TASK_IS_DONE, status == 2);
}

static void
set_url (E2kProperties *props, E2kCalComponent *comp)
{
	const char *url;

	cal_component_get_url (CAL_COMPONENT (comp), &url);
	if (url)
		e2k_properties_set_string (props, E2K_PR_CALENDAR_URL, g_strdup (url));
	else
		e2k_properties_remove (props, E2K_PR_CALENDAR_URL);
}

static void
set_summary (E2kProperties *props, E2kCalComponent *comp)
{
	static CalComponentText summary;

	cal_component_get_summary (CAL_COMPONENT (comp), &summary);
	if (summary.value) {
		e2k_properties_set_string (props, E2K_PR_HTTPMAIL_THREAD_TOPIC,
					   g_strdup (summary.value));
	} else
		e2k_properties_remove (props, E2K_PR_HTTPMAIL_THREAD_TOPIC);
}

static void
set_uid (E2kProperties *props, E2kCalComponent *comp)
{
	const char *uid;

	cal_component_get_uid (CAL_COMPONENT (comp), &uid);
	e2k_properties_set_string (props, E2K_PR_CALENDAR_UID, g_strdup (uid));
}

static const char *
get_summary (E2kCalComponent *comp)
{
	static CalComponentText summary;

	cal_component_get_summary (CAL_COMPONENT (comp), &summary);
	return summary.value;
}

static const char *
get_uid (E2kCalComponent *comp)
{
	const char *uid;

	cal_component_get_uid (CAL_COMPONENT (comp), &uid);
	return uid;
}

static const char *
get_priority (E2kCalComponent *comp)
{
	int *priority;
	const char *res;

	cal_component_get_priority (CAL_COMPONENT (comp), &priority);
	if (!priority)
		return "normal";

	if (*priority == 0)
		res = "normal";
	else if (*priority <= 4)
		res = "high";
	else if (*priority == 5)
		res = "normal";
	else
		res = "low";

	cal_component_free_priority (priority);

	return res;
}

static void
get_from (E2kCalComponent *comp, const char **from_name, const char **from_addr)
{
	CalComponentOrganizer org;

	cal_component_get_organizer (CAL_COMPONENT (comp), &org);

	if (org.cn && org.cn[0] && org.value && org.value[0]) {
		*from_name = org.cn;
		if (!g_ascii_strncasecmp (org.value, "mailto:", 7))
			*from_addr = org.value + 7;
		else
			*from_addr = org.value;
	} else {
		*from_name = cal_backend_exchange_get_cal_owner (CAL_BACKEND (comp->backend));
		*from_addr = cal_backend_exchange_get_cal_address (CAL_BACKEND (comp->backend));
	}
}

static int
put_body (E2kCalComponent *comp, const char *from_name, const char *from_addr)
{
        GSList *desc_list;
	GString *desc;
	char *desc_crlf;
	char *body, *date;
	int status;

	/* get the description */
        cal_component_get_description_list (CAL_COMPONENT (comp), &desc_list);
	desc = g_string_new ("");
	if (desc_list != NULL) {
		GSList *sl;

		for (sl = desc_list; sl; sl = sl->next) {
			CalComponentText *text = (CalComponentText *) sl->data;

			if (text)
				desc = g_string_append (desc, text->value);
		}
	}

	/* PUT the component on the server */
	desc_crlf = e2k_lf_to_crlf ((const char *) desc->str);
	date = e2k_make_timestamp_rfc822 (time (NULL));
	body = g_strdup_printf ("content-class: urn:content-classes:task\r\n"
				"Subject: %s\r\n"
				"Date: %s\r\n"
				"Message-ID: <%s>\r\n"
				"MIME-Version: 1.0\r\n"
				"Content-Type: text/plain;\r\n"
				"\tcharset=\"utf-8\"\r\n"
				"Content-Transfer-Encoding: 8bit\r\n"
				"Thread-Topic: %s\r\n"
				"Priority: %s\r\n"
				"Importance: %s\r\n"
				"From: \"%s\" <%s>\r\n"
				"\r\n%s",
				get_summary (comp),
				date,
				get_uid (comp),
				get_summary (comp),
				get_priority (comp),
				get_priority (comp),
				from_name ? from_name : "Evolution",
				from_addr ? from_addr : "",
				desc_crlf);

	E2K_DEBUG_HINT ('T');
	status = e2k_connection_put_sync (comp->backend->connection, comp->href,
					  "message/rfc822", body, strlen (body));

	/* free memory */
	g_free (body);
	g_free (desc_crlf);
	g_free (date);
        cal_component_free_text_list (desc_list);
	g_string_free (desc, TRUE);

	return status;
}

int
e2k_cal_component_todo_update (E2kCalComponent *comp, gboolean new)
{
	E2kProperties *props;
	int status;
	const char *from_name, *from_addr;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (comp->href != NULL, SOUP_ERROR_MALFORMED);

	get_from (comp, &from_name, &from_addr);

	props = e2k_properties_new ();

	if (new) {
		e2k_properties_set_string (
			props, E2K_PR_EXCHANGE_MESSAGE_CLASS,
			g_strdup ("IPM.Task"));

		/* Magic number to make the context menu in Outlook work */
		e2k_properties_set_int (props, E2K_PR_MAPI_SIDE_EFFECTS, 272);

		/* I don't remember what happens if you don't set this. */
		e2k_properties_set_int (props, PR_ACTION, 1280);

		/* Various fields we don't support but should initialize
		 * so evo-created tasks look the same as Outlook-created
		 * ones.
		 */
		e2k_properties_set_bool (props, E2K_PR_MAPI_NO_AUTOARCHIVE, FALSE);
		e2k_properties_set_bool (props, E2K_PR_OUTLOOK_TASK_TEAM_TASK, FALSE);
		e2k_properties_set_bool (props, E2K_PR_OUTLOOK_TASK_RECURRING, FALSE);
		e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_ACTUAL_WORK, 0);
		e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_TOTAL_WORK, 0);
		e2k_properties_set_int (props, E2K_PR_OUTLOOK_TASK_ASSIGNMENT, 0);
		e2k_properties_set_string (props, E2K_PR_OUTLOOK_TASK_OWNER,
					   g_strdup (from_name));
	}

	set_uid (props, comp);
	set_summary (props, comp);
	set_priority (props, comp);
	set_sensitivity (props, comp);

	set_dtstart (props, comp);
	set_due_date (props, comp);
	set_date_completed (props, comp);

	set_status (props, comp);
	set_percent (props, comp);

	set_categories (props, comp);
	set_url (props, comp);

	E2K_DEBUG_HINT ('T');
	status = e2k_connection_proppatch_sync (comp->backend->connection,
						comp->href, props, TRUE);
	if (status != SOUP_ERROR_DAV_MULTISTATUS)
		return status;

	/* put the body of the component into the server */
	return put_body (comp, from_name, from_addr);
}
