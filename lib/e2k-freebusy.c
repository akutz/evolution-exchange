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

/* e2k-freebusy.c: routines for manipulating Exchange free/busy data */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e2k-freebusy.h"
#include "e2k-propnames.h"
#include "e2k-restriction.h"
#include "e2k-uri.h"
#include "e2k-utils.h"

#include <stdlib.h>
#include <string.h>

#include <e-util/e-time-utils.h>

/**
 * e2k_freebusy_destroy:
 * @fb: the #E2kFreebusy
 *
 * Frees @fb and all associated data.
 **/
void
e2k_freebusy_destroy (E2kFreebusy *fb)
{
	int i;

	g_object_unref (fb->conn);
	for (i = 0; i < E2K_BUSYSTATUS_MAX; i++)
		g_array_free (fb->events[i], TRUE);
	g_free (fb->uri);
	g_free (fb->dn);
	g_free (fb);
}

static char *
fb_uri_for_dn (const char *public_uri, const char *dn)
{
	char *uri, *div, *org;
	GString *str;

	for (div = strchr (dn, '/'); div; div = strchr (div + 1, '/')) {
		if (!g_ascii_strncasecmp (div, "/cn=", 4))
			break;
	}
	g_return_val_if_fail (div, NULL);

	org = g_strndup (dn, div - dn);

	str = g_string_new (public_uri);
	g_string_append (str, "/NON_IPM_SUBTREE/SCHEDULE%2B%20FREE%20BUSY/EX:");
	e2k_uri_append_encoded (str, org, NULL);
	g_string_append (str, "/USER-");
	e2k_uri_append_encoded (str, div, NULL);
	g_string_append (str, ".EML");

	uri = str->str;
	g_string_free (str, FALSE);
	g_free (org);

	return uri;
}

static void
merge_events (GArray *events)
{
	E2kFreebusyEvent evt, evt2;
	int i;

	if (events->len < 2)
		return;

	evt = g_array_index (events, E2kFreebusyEvent, 0);
	for (i = 1; i < events->len; i++) {
		evt2 = g_array_index (events, E2kFreebusyEvent, i);
		if (evt.end >= evt2.start) {
			if (evt2.end > evt.end)
				evt.end = evt2.end;
			g_array_remove_index (events, i--);
		} else
			evt = evt2;
	}
}

static void
add_data_for_status (E2kFreebusy *fb, GPtrArray *monthyears, GPtrArray *fbdatas, GArray *events)
{
	E2kFreebusyEvent evt;
	int i, monthyear;
	GByteArray *fbdata;
	unsigned char *p;
	struct tm tm;

	if (!monthyears || !fbdatas)
		return;

	memset (&tm, 0, sizeof (tm));
	for (i = 0; i < monthyears->len && i < fbdatas->len; i++) {
		monthyear = atoi (monthyears->pdata[i]);
		fbdata = fbdatas->pdata[i];

		tm.tm_year = (monthyear >> 4) - 1900;
		tm.tm_mon = (monthyear & 0xF) - 1;

		for (p = fbdata->data; p + 3 < fbdata->data + fbdata->len; p += 4) {
			tm.tm_mday = 1;
			tm.tm_hour = 0;
			tm.tm_min = p[0] + p[1] * 256;
			evt.start = e_mktime_utc (&tm);

			tm.tm_mday = 1;
			tm.tm_hour = 0;
			tm.tm_min = p[2] + p[3] * 256;
			evt.end = e_mktime_utc (&tm);

			g_array_append_val (events, evt);
		}
	}
	merge_events (events);
}

static const char *public_freebusy_props[] = {
	PR_FREEBUSY_START_RANGE,
	PR_FREEBUSY_END_RANGE,
	PR_FREEBUSY_ALL_MONTHS,
	PR_FREEBUSY_ALL_EVENTS,
	PR_FREEBUSY_TENTATIVE_MONTHS,
	PR_FREEBUSY_TENTATIVE_EVENTS,
	PR_FREEBUSY_BUSY_MONTHS,
	PR_FREEBUSY_BUSY_EVENTS,
	PR_FREEBUSY_OOF_MONTHS,
	PR_FREEBUSY_OOF_EVENTS
};
static const int n_public_freebusy_props = sizeof (public_freebusy_props) / sizeof (public_freebusy_props[0]);

/**
 * e2k_freebusy_new:
 * @conn: an #E2kConnection
 * @public_uri: the URI of the MAPI public folder tree
 * @dn: the legacy Exchange DN of a user
 *
 * Creates a new #E2kFreebusy, filled in with information from the
 * indicated user's published free/busy information. This uses the
 * public free/busy folder; the caller does not need permission to
 * access the @dn's Calendar.
 *
 * Return value: the freebusy information
 **/
E2kFreebusy *
e2k_freebusy_new (E2kConnection *conn, const char *public_uri, const char *dn)
{
	E2kFreebusy *fb;
	char *uri, *time;
	GPtrArray *monthyears, *fbdatas;
	int status, nresults, i;
	E2kResult *results;

	uri = fb_uri_for_dn (public_uri, dn);
	g_return_val_if_fail (uri, NULL);

	status = e2k_connection_propfind_sync (conn, uri, "0",
					       public_freebusy_props,
					       n_public_freebusy_props,
					       &results, &nresults);
	if (!SOUP_ERROR_IS_SUCCESSFUL (status) || nresults == 0) {
		/* FIXME: create it */
		g_free (uri);
		return NULL;
	}

	fb = g_new0 (E2kFreebusy, 1);
	fb->uri = uri;
	fb->dn = g_strdup (dn);
	fb->conn = conn;
	g_object_ref (conn);

	for (i = 0; i < E2K_BUSYSTATUS_MAX; i++)
		fb->events[i] = g_array_new (FALSE, FALSE, sizeof (E2kFreebusyEvent));

	time = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_START_RANGE);
	fb->start = time ? e2k_systime_to_time_t (strtol (time, NULL, 10)) : 0;
	time = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_END_RANGE);
	fb->end = time ? e2k_systime_to_time_t (strtol (time, NULL, 10)) : 0;

	monthyears = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_ALL_MONTHS);
	fbdatas = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_ALL_EVENTS);
	add_data_for_status (fb, monthyears, fbdatas, fb->events[E2K_BUSYSTATUS_ALL]);

	monthyears = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_TENTATIVE_MONTHS);
	fbdatas = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_TENTATIVE_EVENTS);
	add_data_for_status (fb, monthyears, fbdatas, fb->events[E2K_BUSYSTATUS_TENTATIVE]);

	monthyears = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_BUSY_MONTHS);
	fbdatas = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_BUSY_EVENTS);
	add_data_for_status (fb, monthyears, fbdatas, fb->events[E2K_BUSYSTATUS_BUSY]);

	monthyears = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_OOF_MONTHS);
	fbdatas = e2k_properties_get_prop (
		results[0].props, PR_FREEBUSY_OOF_EVENTS);
	add_data_for_status (fb, monthyears, fbdatas, fb->events[E2K_BUSYSTATUS_OOF]);

	e2k_results_free (results, nresults);
	return fb;
}

/**
 * e2k_freebusy_reset:
 * @fb: an #E2kFreebusy
 * @nmonths: the number of months of info @fb will store
 *
 * Clears all existing data in @fb and resets the start and end times
 * to a span of @nmonths around the current date.
 **/
void
e2k_freebusy_reset (E2kFreebusy *fb, int nmonths)
{
	time_t now;
	struct tm tm;
	int i;

	/* Remove all existing events */
	for (i = 0; i < E2K_BUSYSTATUS_MAX; i++)
		g_array_set_size (fb->events[i], 0);

	/* Set the start and end times appropriately: from the beginning
	 * of the current month until nmonths later.
	 * FIXME: Use default timezone, not local time.
	 */
	now = time (NULL);
	tm = *gmtime (&now);
	tm.tm_mday = 1;
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;

	tm.tm_isdst = -1;
	fb->start = mktime (&tm);

	tm.tm_mon += nmonths;
	tm.tm_isdst = -1;
	fb->end = mktime (&tm);
}

/**
 * e2k_freebusy_add_interval:
 * @fb: an #E2kFreebusy
 * @busystatus: the busy status of the interval
 * @start: the start of the interval
 * @end: the end of the interval
 *
 * This adds an interval of type @busystatus to @fb.
 **/
void
e2k_freebusy_add_interval (E2kFreebusy *fb, E2kBusyStatus busystatus,
			   time_t start, time_t end)
{
	E2kFreebusyEvent evt, *events;
	int i;

	if (busystatus == E2K_BUSYSTATUS_FREE)
		return;

	/* Clip to the fb's range */
	if (start < fb->start)
		start = fb->start;
	if (end > fb->end)
		end = fb->end;
	if (end <= start)
		return;

	events = (E2kFreebusyEvent *)(fb->events[busystatus]->data);

	for (i = 0; i < fb->events[busystatus]->len; i++) {
		if (events[i].end >= start)
			break;
	}

	evt.start = start;
	evt.end = end;

	if (i == fb->events[busystatus]->len)
		g_array_append_val (fb->events[busystatus], evt);
	else {
		/* events[i] is the first event that is not completely
		 * before evt, meaning it is either completely after it,
		 * or they overlap/abut.
		 */
		if (events[i].start > end) {
			/* No overlap. Insert evt before events[i]. */
			g_array_insert_val (fb->events[busystatus], i, evt);
		} else {
			/* They overlap or abut. Merge them. */
			events[i].start = MIN (events[i].start, start);
			events[i].end   = MAX (events[i].end, end);
		}
	}
}

/**
 * e2k_freebusy_clear_interval:
 * @fb: an #E2kFreebusy
 * @start: the start of the interval
 * @end: the end of the interval
 *
 * This removes any events between @start and @end in @fb.
 **/
void
e2k_freebusy_clear_interval (E2kFreebusy *fb, time_t start, time_t end)
{
	E2kFreebusyEvent *evt;
	int busystatus, i;

	for (busystatus = 0; busystatus < E2K_BUSYSTATUS_MAX; busystatus++) {
		for (i = 0; i < fb->events[busystatus]->len; i++) {
			evt = &g_array_index (fb->events[busystatus], E2kFreebusyEvent, i);
			if (evt->end < start || evt->start > end)
				continue;

			/* evt overlaps the interval. Truncate or
			 * remove it.
			 */

			if (evt->start > start /* && evt->start <= end */)
				evt->start = end;
			if (evt->end < end /* && evt->end >= start */)
				evt->end = start;

			if (evt->start >= evt->end)
				g_array_remove_index (fb->events[busystatus], i--);
		}
	}
}

static const char *freebusy_props[] = {
	E2K_PR_CALENDAR_DTSTART,
	E2K_PR_CALENDAR_DTEND,
	E2K_PR_CALENDAR_BUSY_STATUS
};
static const int n_freebusy_props = sizeof (freebusy_props) / sizeof (freebusy_props[0]);

/**
 * e2k_freebusy_add_from_calendar_uri:
 * @fb: an #E2kFreebusy
 * @uri: the URI of a calendar folder
 * @start_tt: start of the range to add
 * @end_tt: end of the range to add
 *
 * This queries the server for events between @start_tt and @end_tt in
 * the calendar at @uri (which the caller must have permission to
 * read) and adds them @fb. Any previously-existing events during that
 * range are removed.
 *
 * Return value: a soup status code.
 **/
int
e2k_freebusy_add_from_calendar_uri (E2kFreebusy *fb, const char *uri,
				    time_t start_tt, time_t end_tt)
{
	char *start, *end, *busystatus;
	E2kBusyStatus busy;
	int status, nresults, i;
	E2kResult *results;
	E2kRestriction *rn;

	e2k_freebusy_clear_interval (fb, start_tt, end_tt);

	start = e2k_make_timestamp (start_tt);
	end = e2k_make_timestamp (end_tt);

	rn = e2k_restriction_andv (
		e2k_restriction_prop_string (E2K_PR_DAV_CONTENT_CLASS,
					     E2K_RELOP_EQ,
					     "urn:content-classes:appointment"),
		e2k_restriction_prop_date (E2K_PR_CALENDAR_DTEND,
					   E2K_RELOP_GT, start),
		e2k_restriction_prop_date (E2K_PR_CALENDAR_DTSTART,
					   E2K_RELOP_LT, end),
		e2k_restriction_prop_string (E2K_PR_CALENDAR_BUSY_STATUS,
					     E2K_RELOP_NE, "FREE"),
		NULL);

	status = e2k_connection_search_sync (fb->conn, uri,
					     freebusy_props, n_freebusy_props,
					     FALSE, rn, NULL,
					     &results, &nresults);
	e2k_restriction_unref (rn);
	g_free (start);
	g_free (end);

	if (!SOUP_ERROR_IS_SUCCESSFUL (status))
		return status;

	for (i = 0; i < nresults; i++) {
		start = e2k_properties_get_prop (results[i].props,
						 E2K_PR_CALENDAR_DTSTART);
		end = e2k_properties_get_prop (results[i].props,
					       E2K_PR_CALENDAR_DTEND);
		busystatus = e2k_properties_get_prop (results[i].props,
						      E2K_PR_CALENDAR_BUSY_STATUS);
		if (!start || !end || !busystatus)
			continue;

		if (!strcmp (busystatus, "TENTATIVE"))
			busy = E2K_BUSYSTATUS_TENTATIVE;
		else if (!strcmp (busystatus, "OUTOFOFFICE"))
			busy = E2K_BUSYSTATUS_OOF;
		else
			busy = E2K_BUSYSTATUS_BUSY;

		e2k_freebusy_add_interval (fb, busy,
					   e2k_parse_timestamp (start),
					   e2k_parse_timestamp (end));
			      
	}

	e2k_results_free (results, nresults);
	return status;
}

static void
add_events (GArray *events_array, E2kProperties *props,
	    const char *month_list_prop, const char *data_list_prop)
{
	E2kFreebusyEvent *events = (E2kFreebusyEvent *)events_array->data;
	int i, evt_start, evt_end, monthyear;
	struct tm start_tm, end_tm;
	time_t start, end;
	GPtrArray *monthyears, *datas;
	GByteArray *data;
	char startend[4];

	if (!events_array->len) {
		e2k_properties_remove (props, month_list_prop);
		e2k_properties_remove (props, data_list_prop);
		return;
	}

	monthyears = g_ptr_array_new ();
	start_tm = *gmtime (&events[0].start);
	end_tm = *gmtime (&events[events_array->len - 1].end);
	while (start_tm.tm_year <= end_tm.tm_year ||
	       start_tm.tm_mon <= end_tm.tm_mon) {
		monthyear = ((start_tm.tm_year + 1900) * 16) +
			(start_tm.tm_mon + 1);
		g_ptr_array_add (monthyears, g_strdup_printf ("%d", monthyear));

		start_tm.tm_mon++;
		if (start_tm.tm_mon == 12) {
			start_tm.tm_year++;
			start_tm.tm_mon = 0;
		}
	}	     
	e2k_properties_set_int_array (props, month_list_prop, monthyears);

	datas = g_ptr_array_new ();
	start = events[0].start;
	i = 0;
	while (i < events_array->len) {
		start_tm = *gmtime (&start);
		start_tm.tm_mon++;
		end = e_mktime_utc (&start_tm);

		data = g_byte_array_new ();
		while (i << events_array->len &&
		       events[i].end > start && events[i].start < end) {
			if (events[i].start < start)
				evt_start = 0;
			else
				evt_start = (events[i].start - start) / 60;
			if (events[i].end > end)
				evt_end = (end - start) / 60;
			else
				evt_end = (events[i].end - start) / 60;

			startend[0] = evt_start & 0xFF;
			startend[1] = evt_start >> 8;
			startend[2] = evt_end & 0xFF;
			startend[3] = evt_end >> 8;
			g_byte_array_append (data, startend, 4);
			i++;
		}

		g_ptr_array_add (datas, data);
		start = end;
	}
	e2k_properties_set_binary_array (props, data_list_prop, datas);
}

int
e2k_freebusy_save (E2kFreebusy *fb)
{
	E2kProperties *props;
	char *timestamp;
	int status;

	props = e2k_properties_new ();
	e2k_properties_set_string (props, E2K_PR_EXCHANGE_MESSAGE_CLASS,
				   g_strdup ("IPM.Post"));
	e2k_properties_set_int (props, PR_FREEBUSY_START_RANGE, fb->start);
	e2k_properties_set_int (props, PR_FREEBUSY_END_RANGE, fb->end);
	e2k_properties_set_string (props, PR_FREEBUSY_EMAIL_ADDRESS,
				   g_strdup (fb->dn));

	add_events (fb->events[E2K_BUSYSTATUS_ALL], props,
		    PR_FREEBUSY_ALL_MONTHS, PR_FREEBUSY_ALL_EVENTS);
	add_events (fb->events[E2K_BUSYSTATUS_TENTATIVE], props,
		    PR_FREEBUSY_TENTATIVE_MONTHS, PR_FREEBUSY_TENTATIVE_EVENTS);
	add_events (fb->events[E2K_BUSYSTATUS_BUSY], props,
		    PR_FREEBUSY_BUSY_MONTHS, PR_FREEBUSY_BUSY_EVENTS);
	add_events (fb->events[E2K_BUSYSTATUS_OOF], props,
		    PR_FREEBUSY_OOF_MONTHS, PR_FREEBUSY_OOF_EVENTS);

	timestamp = e2k_make_timestamp (e2k_connection_get_last_timestamp (fb->conn));
	e2k_properties_set_date (props, PR_FREEBUSY_LAST_MODIFIED, timestamp);

	status = e2k_connection_proppatch_sync (fb->conn, fb->uri,
						props, TRUE);
	e2k_properties_free (props);

	return status;
}
