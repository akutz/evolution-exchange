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

#include "e2k-cal-component.h"
#include "e2k-propnames.h"
#include "e2k-utils.h"
#include <stdlib.h>
#include <string.h>

#define PARENT_TYPE CAL_COMPONENT_TYPE

static void e2k_cal_component_class_init (GObjectClass *object_class);
static void e2k_cal_component_finalize   (GObject *object);

static CalComponentClass *parent_class = NULL;

static void
e2k_cal_component_class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	object_class->finalize = e2k_cal_component_finalize;
}

static void
e2k_cal_component_finalize (GObject *object)
{
	E2kCalComponent *comp = (E2kCalComponent *) object;

	g_return_if_fail (E2K_IS_CAL_COMPONENT (comp));

	/* free memory */
	g_free (comp->href);
	comp->href = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (e2k_cal_component, E2kCalComponent, e2k_cal_component_class_init, NULL, PARENT_TYPE)

/**
 * e2k_cal_component_new
 */
E2kCalComponent *
e2k_cal_component_new (CalBackendExchange *cbex)
{
	E2kCalComponent *comp;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);

	comp = g_object_new (E2K_CAL_COMPONENT_TYPE, NULL);
	comp->backend = cbex;

	return comp;
}

/**
 * e2k_cal_component_new_from_href
 */
E2kCalComponent *
e2k_cal_component_new_from_href (CalBackendExchange *cbex, const char *href)
{
	E2kCalComponent *comp;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (href != NULL, NULL);

	comp = e2k_cal_component_new (cbex);
	if (!e2k_cal_component_set_from_href (comp, href)) {
		g_object_unref (comp);
		return NULL;
	}

	return comp;
}

/**
 * e2k_cal_component_new_from_props
 */
E2kCalComponent *
e2k_cal_component_new_from_props (CalBackendExchange *cbex,
				  E2kResult *res,
				  CalComponentVType vtype)
{
	E2kCalComponent *comp;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (res != NULL, NULL);

	comp = e2k_cal_component_new (cbex);
	if (!e2k_cal_component_set_from_props (comp, res, vtype)) {
		g_object_unref (comp);
		return NULL;
	}

	return comp;
}

/**
 * e2k_cal_component_new_from_string
 */
E2kCalComponent *
e2k_cal_component_new_from_string (CalBackendExchange *cbex,
				   const char *href,
				   const char *body,
				   guint len)
{
	E2kCalComponent *comp;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (body != NULL, NULL);

	comp = e2k_cal_component_new (cbex);
	if (!e2k_cal_component_set_from_string (comp, body, len)) {
		g_object_unref (comp);
		return NULL;
	}

	e2k_cal_component_set_href (comp, href);

	return comp;
}

/**
 * e2k_cal_component_new_from_cache
 */
E2kCalComponent *
e2k_cal_component_new_from_cache (CalBackendExchange *cbex,
				  icalcomponent *icalcomp)
{
	E2kCalComponent *comp;
	icalproperty *icalprop;
	icalcomponent *clone;
	const char *x_name, *x_val;

	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (cbex), NULL);
	g_return_val_if_fail (icalcomp != NULL, NULL);

	clone = icalcomponent_new_clone (icalcomp);

	icalprop = icalcomponent_get_first_property (clone, ICAL_X_PROPERTY);
	while (icalprop) {
		x_name = icalproperty_get_x_name (icalprop);
		x_val = icalproperty_get_x (icalprop);

		if (!strcmp (x_name, "X-XIMIAN-CONNECTOR-HREF"))
			break;

		icalprop = icalcomponent_get_next_property (clone, ICAL_X_PROPERTY);
	}

	if (!icalprop) {
		icalcomponent_free (clone);
		return NULL;
	}


	comp = e2k_cal_component_new (cbex);
	e2k_cal_component_set_href (comp, x_val);

	icalcomponent_remove_property (clone, icalprop);
	icalproperty_free (icalprop);
	cal_component_set_icalcomponent (CAL_COMPONENT (comp), clone);

	return comp;
}


/**
 * e2k_cal_component_set_from_href
 */
gboolean
e2k_cal_component_set_from_href (E2kCalComponent *comp, const char *href)
{
	char *comp_data;
	int err;
	int len;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (href != NULL, FALSE);

	/* get the object from the Exchange server */
	E2K_DEBUG_HINT ('C');
	err = e2k_connection_get_sync (comp->backend->connection, href,
				       &comp_data, &len);
	if (err != SOUP_ERROR_OK || !comp_data)
		return FALSE;

	if (e2k_cal_component_set_from_string (comp, comp_data, len)) {
		e2k_cal_component_set_href (comp, href);
		return TRUE;
	}

	return FALSE;
}

/**
 * e2k_cal_component_set_from_props
 */
gboolean
e2k_cal_component_set_from_props (E2kCalComponent *comp,
				  E2kResult *res,
				  CalComponentVType vtype)
{
	char *str;
	GPtrArray *array;
	struct icaltimetype itt;
	CalComponentDateTime *dt;
	icaltimezone *default_zone;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (res != NULL, FALSE);

	e2k_cal_component_set_href (comp, res->href);
	cal_component_set_new_vtype (CAL_COMPONENT (comp), vtype);

	/* get the default timezone */
	default_zone = get_default_timezone ();

	/* check the type of the component */
	str = e2k_properties_get_prop (res->props, E2K_PR_EXCHANGE_MESSAGE_CLASS);
	if (!str)
		return FALSE;

	if (vtype == CAL_COMPONENT_TODO &&
	    (strcmp (str, "IPM.Task") && strcmp (str, "IPM.Post")))
		return FALSE;

	/* UID property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_CALENDAR_UID)))
		cal_component_set_uid (CAL_COMPONENT (comp), str);
	else if ((str = e2k_properties_get_prop (res->props, E2K_PR_DAV_UID)))
		cal_component_set_uid (CAL_COMPONENT (comp), str);
	else {
		g_warning ("Component without uid");
		return FALSE;
	}

	/* PRIORITY property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_MAILHEADER_IMPORTANCE))) {
		int priority;

		if (!strcmp (str, "high"))
			priority = 3;
		else if (!strcmp (str, "low"))
			priority = 7;
		else if (!strcmp (str, "normal"))
			priority = 5;
		else
			priority = 0;

		cal_component_set_priority (CAL_COMPONENT (comp), &priority);
	}

	/* SUMMARY property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_HTTPMAIL_SUBJECT))) {
		CalComponentText summary;

		summary.value = str;
		summary.altrep = res->href;
		cal_component_set_summary (CAL_COMPONENT (comp), &summary);
	}

	/* DTSTAMP property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_HTTPMAIL_DATE))) {
		itt = icaltime_from_e2k_time (str);
		if (!icaltime_is_null_time (itt)) {
			cal_component_set_dtstamp (CAL_COMPONENT (comp), &itt);
			cal_component_set_created (CAL_COMPONENT (comp), &itt);
		}
	}

	/* ORGANIZER property */
	if (vtype != CAL_COMPONENT_TODO &&
	    (str = e2k_properties_get_prop (res->props, E2K_PR_HTTPMAIL_FROM_EMAIL))) {
		CalComponentOrganizer org;

		org.language = NULL;
		org.value = str;
		org.sentby = str;
		if ((str = e2k_properties_get_prop (res->props, E2K_PR_HTTPMAIL_FROM_NAME)))
			org.cn = str;
		else
			org.cn = NULL;
		cal_component_set_organizer (CAL_COMPONENT (comp), &org);
	}

	/* DESCRIPTION property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_HTTPMAIL_TEXT_DESCRIPTION))) {
		GSList sl;
		CalComponentText text;

		text.value = e2k_crlf_to_lf (str);
		text.altrep = res->href;
		sl.data = &text;
		sl.next = NULL;
		cal_component_set_description_list (CAL_COMPONENT (comp), &sl);

		g_free ((char *)text.value);
	}

	/* DUE property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_MAPI_COMMON_END))) {
		dt = calcomponentdatetime_from_string (str, icaltimezone_get_utc_timezone ());
		if (!icaltime_is_null_time (*dt->value)) {
			icaltimezone_convert_time (
				dt->value,
				icaltimezone_get_utc_timezone (),
				default_zone);
			dt->tzid = g_strdup (icaltimezone_get_tzid (default_zone));

			cal_component_set_due (CAL_COMPONENT (comp), dt);
		}
		cal_component_free_datetime (dt);
	}

	/* DTSTART property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_MAPI_COMMON_START))) {
		dt = calcomponentdatetime_from_string (str, icaltimezone_get_utc_timezone ());
		if (!icaltime_is_null_time (*dt->value)) {
			icaltimezone_convert_time (
				dt->value,
				icaltimezone_get_utc_timezone (),
				default_zone);
			dt->tzid = g_strdup (icaltimezone_get_tzid (default_zone));

			cal_component_set_dtstart (CAL_COMPONENT (comp), dt);
		}
		cal_component_free_datetime (dt);
	}

	/* CLASSIFICATION property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_MAPI_SENSITIVITY))) {
		if (!strcmp (str, "0"))
			cal_component_set_classification (CAL_COMPONENT (comp),
							  CAL_COMPONENT_CLASS_PUBLIC);
		else if (!strcmp (str, "1"))
			cal_component_set_classification (CAL_COMPONENT (comp),
							  CAL_COMPONENT_CLASS_CONFIDENTIAL);
		else if (!strcmp (str, "2"))
			cal_component_set_classification (CAL_COMPONENT (comp),
							  CAL_COMPONENT_CLASS_PRIVATE);
	}

	/* % COMPLETED property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_OUTLOOK_TASK_PERCENT))) {
		float f_percent;
		int percent;

		f_percent = atof (str);
		percent = (int) (f_percent * 100);
		cal_component_set_percent (CAL_COMPONENT (comp), &percent);
	}

	/* STATUS property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_OUTLOOK_TASK_STATUS))) {
		if (!strcmp (str, "0")) {
			/* Not Started */
			cal_component_set_status (CAL_COMPONENT (comp),
						  ICAL_STATUS_NEEDSACTION);
		} else if (!strcmp (str, "1")) {
			/* In Progress */
			cal_component_set_status (CAL_COMPONENT (comp),
						  ICAL_STATUS_INPROCESS);
		} else if (!strcmp (str, "2")) {
			/* Completed */
			cal_component_set_status (CAL_COMPONENT (comp),
						  ICAL_STATUS_COMPLETED);
		} else if (!strcmp (str, "3")) {
			/* Waiting on someone else */
			cal_component_set_status (CAL_COMPONENT (comp),
						  ICAL_STATUS_INPROCESS);
		} else if (!strcmp (str, "4")) {
			/* Deferred */
			cal_component_set_status (CAL_COMPONENT (comp),
						  ICAL_STATUS_CANCELLED);
		}
	}

	/* DATE COMPLETED property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_OUTLOOK_TASK_DONE_DT))) {
		struct icaltimetype itt;

		itt = icaltime_from_e2k_time (str);
		if (!icaltime_is_null_time (itt))
			cal_component_set_completed (CAL_COMPONENT (comp), &itt);
	}

	/* LAST MODIFIED property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_CALENDAR_LAST_MODIFIED))) {
		itt = icaltime_from_e2k_time (str);
		if (!icaltime_is_null_time (itt))
			cal_component_set_last_modified (CAL_COMPONENT (comp), &itt);
	}

	/* CATEGORIES */
	if ((array = e2k_properties_get_prop (res->props, E2K_PR_EXCHANGE_KEYWORDS))) {
		GSList *list = NULL;
		int i;

		for (i = 0; i < array->len; i++)
			list = g_slist_prepend (list, array->pdata[i]);
		cal_component_set_categories_list (CAL_COMPONENT (comp), list);
		g_slist_free (list);
	}

	/* URL property */
	if ((str = e2k_properties_get_prop (res->props, E2K_PR_CALENDAR_URL))) {
		cal_component_set_url (CAL_COMPONENT (comp), str);
	}
	
	/* FIXME: needed for some components */
	cal_component_commit_sequence (CAL_COMPONENT (comp));

	return TRUE;
}

/**
 * e2k_cal_component_set_from_string
 */
gboolean
e2k_cal_component_set_from_string (E2kCalComponent *e2k_comp,
				   const char *body, guint len)
{
	CalComponent *comp;
	char *start, *end, *str;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;
	icalproperty *icalprop;
	CalComponentClassification classif;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (e2k_comp), FALSE);
	g_return_val_if_fail (body != NULL, FALSE);

	comp = CAL_COMPONENT (e2k_comp);

	start = g_strstr_len (body, len, "\nBEGIN:VCALENDAR");
	if (!start)
		return FALSE;
	start++;
	end = g_strstr_len (start, len - (start - body), "\nEND:VCALENDAR");
	if (!end)
		return FALSE;
	end += sizeof ("\nEND:VCALENDAR");

	str = g_strndup (start, end - start);
	icalcomp = icalparser_parse_string (str);
	g_free (str);
	if (!icalcomp)
		return FALSE;

	kind = icalcomponent_isa (icalcomp);
	switch (kind) {
	case ICAL_VTODO_COMPONENT:
#ifdef JOURNAL_SUPPORT
	case ICAL_VJOURNAL_COMPONENT:
#endif
	case ICAL_VEVENT_COMPONENT:
		if (!cal_component_set_icalcomponent (comp, icalcomp)) {
			icalcomponent_free (icalcomp);
			return FALSE;
		}
		break;
	case ICAL_VCALENDAR_COMPONENT: {
		icalcomponent_kind child_kind;
		icalcomponent *vcal_comp;
		icalcomponent *subcomp;
		icalcomponent *real_comp = NULL;
		
		vcal_comp = icalcomp;
		subcomp = icalcomponent_get_first_component (
			vcal_comp, ICAL_ANY_COMPONENT);
		while (subcomp) {
			child_kind = icalcomponent_isa (subcomp);
			switch (child_kind) {
			case ICAL_VEVENT_COMPONENT:
#ifdef JOURNAL_SUPPORT
			case ICAL_VJOURNAL_COMPONENT:
#endif
			case ICAL_VTODO_COMPONENT:
				if (!real_comp)
					real_comp = icalcomponent_new_clone (subcomp);
				break;
			case ICAL_VTIMEZONE_COMPONENT:
				cal_backend_exchange_add_timezone (e2k_comp->backend, subcomp);
				break;
			default:
				break;
			}
			
			subcomp = icalcomponent_get_next_component (
				vcal_comp, ICAL_ANY_COMPONENT);
		}
		
		if (real_comp != NULL) {
			icalcomponent_free (icalcomp);
			if (!cal_component_set_icalcomponent (comp, real_comp)) {
				icalcomponent_free (real_comp);
				return FALSE;
			}
		} else {
			icalcomponent_free (icalcomp);
			return FALSE;
		}
		break;
	}
	default:
		icalcomponent_free (icalcomp);
		return FALSE;
	}

	/* check all X-MICROSOFT-CDO properties to fix any needed stuff */
	icalcomp = cal_component_get_icalcomponent (comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		const char *x_name, *x_val;
		struct icaltimetype itt;

		x_name = icalproperty_get_x_name (icalprop);
		x_val = icalproperty_get_x (icalprop);

		if (!strcmp (x_name, "X-MICROSOFT-CDO-ALLDAYEVENT")) {
			/* all-day events */
			if (!strcmp (x_val, "TRUE")) {
				itt = icalcomponent_get_dtstart (icalcomp);
				itt.is_date = TRUE;
				itt.hour = itt.minute = itt.second = 0;
				icalcomponent_set_dtstart (icalcomp, itt);

				itt = icalcomponent_get_dtend (icalcomp);
				itt.is_date = TRUE;
				itt.hour = itt.minute = itt.second = 0;
				icalcomponent_set_dtend (icalcomp, itt);
			}
		}
		else if (!strcmp (x_name, "X-MICROSOFT-CDO-BUSYSTATUS")) {
			/* it seems OWA sometimes don't set the iCal
			   transparency property, so use this one to
			   be sure */
			if (!strcmp (x_val, "BUSY")) {
				cal_component_set_transparency (
					comp, CAL_COMPONENT_TRANSP_OPAQUE);
			}
			else if (!strcmp (x_val, "FREE")) {
				cal_component_set_transparency (
					comp, CAL_COMPONENT_TRANSP_TRANSPARENT);
			}
		}

		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	/* OWA seems to be broken, and sets the component class to
	   "CLASS:", which defaults to PUBLIC. Evolution treats this
	   as PRIVATE, so we have to work around. */
	cal_component_get_classification (comp, &classif);
	switch (classif) {
	case CAL_COMPONENT_CLASS_PUBLIC :
	case CAL_COMPONENT_CLASS_PRIVATE :
	case CAL_COMPONENT_CLASS_CONFIDENTIAL :
		/* do nothing, it is correct */
		break;
	default :
		/* set it to PUBLIC, which is the default for Exchange */
		cal_component_set_classification (comp, CAL_COMPONENT_CLASS_PUBLIC);
		break;
	}
	
	/* Exchange sets an ORGANIZER on all events. RFC2445 says:
	 *
	 *   This property MUST NOT be specified in an iCalendar
	 *   object that specifies only a time zone definition or
	 *   that defines calendar entities that are not group
	 *   scheduled entities, but are entities only on a single
	 *   user's calendar.
	 */
	if (cal_component_has_organizer (comp) && !cal_component_has_attendees (comp))
		cal_component_set_organizer (comp, NULL);

	return TRUE;
}

/**
 * e2k_cal_component_get_href
 */
const char *
e2k_cal_component_get_href (E2kCalComponent *comp)
{
	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), NULL);

	return (const char *) comp->href;
}

/**
 * e2k_cal_component_set_href
 */
void
e2k_cal_component_set_href (E2kCalComponent *comp, const char *href)
{
	g_return_if_fail (E2K_IS_CAL_COMPONENT (comp));
	g_return_if_fail (href != NULL);

	if (comp->href)
		g_free (comp->href);
	comp->href = g_strdup (href);
}

/* Private functions for e2k_cal_component_update */
struct _cb_data {
	CalBackendExchange *cbex;
	icalcomponent *vcal_comp;
};

static void
add_timezone_cb (icalparameter *param, void *data)
{
	const char *tzid;
	struct _cb_data *cbdata = (struct _cb_data *) data;

	g_return_if_fail (cbdata != NULL);

	tzid = icalparameter_get_tzid (param);
	if (tzid != NULL) {
		icaltimezone *izone;

		izone = cal_backend_get_timezone (CAL_BACKEND (cbdata->cbex), tzid);
		if (izone != NULL) {
			icalcomponent *vtzcomp;
			icalcomponent *icalcomp;

			icalcomp = icaltimezone_get_component (izone);
			if (icalcomp) {
				vtzcomp = icalcomponent_new_clone (icalcomp);
				icalcomponent_add_component (cbdata->vcal_comp, vtzcomp);
			}
		}
	}
}

static void
update_x_properties (CalBackendExchange *cbex, CalComponent *comp)
{
	icalcomponent *icalcomp;
	icalproperty *icalprop;
	const char *x_name, *x_val;
	CalComponentTransparency transp;
	CalComponentDateTime dtstart;
	int *priority;
	char *busystatus, *insttype, *allday, *importance;

	cal_component_get_transparency (comp, &transp);
	if (transp == CAL_COMPONENT_TRANSP_TRANSPARENT)
		busystatus = "FREE";
	else
		busystatus = "BUSY";

	if (cal_component_has_recurrences (comp))
		insttype = "1";
	else
		insttype = "0";

	cal_component_get_dtstart (comp, &dtstart);
	if (dtstart.value->is_date)
		allday = "TRUE";
	else
		allday = "FALSE";
	cal_component_free_datetime (&dtstart);

	cal_component_get_priority (comp, &priority);
	if (priority) {
		importance = *priority < 5 ? "2" : *priority > 5 ? "0" : "1";
		cal_component_free_priority (priority);
	} else
		importance = "1";

	/* Go through the existing X-MICROSOFT-CDO- properties first */
       	icalcomp = cal_component_get_icalcomponent (comp);
	icalprop = icalcomponent_get_first_property (icalcomp, ICAL_X_PROPERTY);
	while (icalprop) {
		x_name = icalproperty_get_x_name (icalprop);
		x_val = icalproperty_get_x (icalprop);

		if (!strcmp (x_name, "X-MICROSOFT-CDO-BUSYSTATUS")) {
			/* If TRANSP was TRANSPARENT, BUSYSTATUS must
			 * be FREE. But if it was OPAQUE, it can
			 * be BUSY, TENTATIVE, or OOF, so only change
			 * it if it was FREE.
			 */
			if (busystatus && strcmp (busystatus, "FREE") == 0)
				icalproperty_set_x (icalprop, "FREE");
			else if (strcmp (x_val, "FREE") == 0)
				icalproperty_set_x (icalprop, "BUSY");
			busystatus = NULL;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-INSTTYPE")) {
			icalproperty_set_x (icalprop, insttype);
			insttype = NULL;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-ALLDAYEVENT")) {
			icalproperty_set_x (icalprop, allday);
			allday = NULL;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-IMPORTANCE")) {
			icalproperty_set_x (icalprop, importance);
			importance = NULL;
		} else if (!strcmp (x_name, "X-MICROSOFT-CDO-MODPROPS"))
			icalcomponent_remove_property (icalcomp, icalprop);

		icalprop = icalcomponent_get_next_property (icalcomp, ICAL_X_PROPERTY);
	}

	/* Now set the ones that weren't set. */
	if (busystatus) {
		icalprop = icalproperty_new_x (busystatus);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-BUSYSTATUS");
		icalcomponent_add_property (icalcomp, icalprop);
	}

	if (insttype) {
		icalprop = icalproperty_new_x (insttype);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-INSTTYPE");
		icalcomponent_add_property (icalcomp, icalprop);
	}

	if (allday) {
		icalprop = icalproperty_new_x (allday);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-ALLDAYEVENT");
		icalcomponent_add_property (icalcomp, icalprop);
	}

	if (importance) {
		icalprop = icalproperty_new_x (importance);
		icalproperty_set_x_name (icalprop, "X-MICROSOFT-CDO-IMPORTANCE");
		icalcomponent_add_property (icalcomp, icalprop);
	}
}

static char *
get_from_string (E2kCalComponent *comp)
{
	CalComponentOrganizer org;
	const char *name, *addr;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), NULL);

	cal_component_get_organizer (CAL_COMPONENT (comp), &org);
	if (org.cn) {
		name = org.cn;
		addr = org.value;
	} else {
		name = cal_backend_exchange_get_cal_owner (CAL_BACKEND (comp->backend));
		addr = cal_backend_exchange_get_cal_address (CAL_BACKEND (comp->backend));
	}

	return g_strdup_printf ("\"%s\" <%s>", name, addr);
}

/**
 * e2k_cal_component_update
 */
int
e2k_cal_component_update (E2kCalComponent *comp, icalproperty_method method,
			  gboolean new)
{
	int err;
	char *comp_str;
	char *body, *body_crlf, *msg;
	char *from, *date;
	const char *summary;
	icalcomponent *icalcomp;
	icalcomponent *real_icalcomp;
	struct _cb_data *cbdata;
	icalproperty *icalprop;
	CalComponentVType vtype;
	CalComponentDateTime dt;
	CalComponent *tmp_comp;
	struct icaltimetype last_modified;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (comp->href != NULL, SOUP_ERROR_MALFORMED);

	/* set LAST-MODIFIED time on all components we modify */
	last_modified = icaltime_from_timet (time (NULL), 0);
	cal_component_set_last_modified (CAL_COMPONENT (comp), &last_modified);

	vtype = cal_component_get_vtype (CAL_COMPONENT (comp));
	if (vtype == CAL_COMPONENT_TODO)
		return e2k_cal_component_todo_update (comp, new);
	else if (vtype != CAL_COMPONENT_EVENT)
		return SOUP_ERROR_MALFORMED;

	/* Update X-MICROSOFT-CDO- properties */
	update_x_properties (comp->backend, CAL_COMPONENT (comp));

	comp_str = cal_component_get_as_string (CAL_COMPONENT (comp));
	icalcomp = icalparser_parse_string (comp_str);
	g_free (comp_str);
	if (!icalcomp)
		return SOUP_ERROR_MALFORMED;

	tmp_comp = cal_component_new ();
	if (!cal_component_set_icalcomponent (tmp_comp, icalcomp)) {
		icalcomponent_free (icalcomp);
		return SOUP_ERROR_MALFORMED;
	}

	cbdata = g_new0 (struct _cb_data, 1);
	cbdata->cbex = comp->backend;
	cbdata->vcal_comp = cal_util_new_top_level ();

	icalprop = icalproperty_new (ICAL_METHOD_PROPERTY);
	icalproperty_set_method (icalprop, method);
	icalcomponent_add_property (cbdata->vcal_comp, icalprop);

	/* fix all day events */
	cal_component_get_dtstart (tmp_comp, &dt);
	if (dt.value->is_date) {
		icaltimezone *zone;

		zone = cal_backend_get_default_timezone (CAL_BACKEND (comp->backend));
		if (!zone)
			zone = icaltimezone_get_utc_timezone ();

		dt.value->is_date = FALSE;
		dt.value->is_utc = FALSE;
		dt.value->hour = dt.value->minute = dt.value->second = 0;
		dt.value->zone = icaltimezone_get_tzid (zone);
		dt.tzid = icaltimezone_get_tzid (zone);
		cal_component_set_dtstart (tmp_comp, &dt);

		cal_component_get_dtend (tmp_comp, &dt);
		dt.value->is_date = FALSE;
		dt.value->is_utc = FALSE;
		dt.value->hour = dt.value->minute = dt.value->second = 0;
		dt.value->zone = icaltimezone_get_tzid (zone);
		dt.tzid = icaltimezone_get_tzid (zone);
		cal_component_set_dtend (tmp_comp, &dt);
	}

	/* Fix UNTIL date in a simple recurrence */
	if (cal_component_has_recurrences (tmp_comp)
	    && cal_component_has_simple_recurrence (tmp_comp)) {
		GSList *rrule_list;
		struct icalrecurrencetype *r;
		
		cal_component_get_rrule_list (tmp_comp, &rrule_list);
		r = rrule_list->data;

		if (!icaltime_is_null_time (r->until) && r->until.is_date) {
			icaltimezone *from_zone, *to_zone;
			
			cal_component_get_dtstart (tmp_comp, &dt);

			if (dt.tzid == NULL)
				from_zone = icaltimezone_get_utc_timezone ();
			else
				from_zone = cal_backend_get_timezone (CAL_BACKEND (comp->backend), dt.tzid);
			to_zone = icaltimezone_get_utc_timezone ();

			r->until.hour = dt.value->hour;
			r->until.minute = dt.value->minute;
			r->until.second = dt.value->second;
			r->until.is_date = FALSE;
			
			icaltimezone_convert_time (&r->until, from_zone, to_zone);
			r->until.is_utc = TRUE;
			
			cal_component_set_rrule_list (tmp_comp, rrule_list);			
		}

		cal_component_free_recur_list (rrule_list);
	}
	
	/* Remove X parameters from properties */
	/* This is specifically for X-EVOLUTION-END-DATE, 
	   but removing anything else is probably ok too */
	for (icalprop = icalcomponent_get_first_property (icalcomp, ICAL_ANY_PROPERTY);
	     icalprop != NULL;
	     icalprop = icalcomponent_get_next_property (icalcomp, ICAL_ANY_PROPERTY))
	{
		icalproperty_remove_parameter (icalprop, ICAL_X_PARAMETER);
	}
	
	/* add the timezones information and the component itself
	   to the VCALENDAR object */
	cal_component_commit_sequence (tmp_comp);
	comp_str = cal_component_get_as_string (tmp_comp);
	if (!comp_str) {
		g_object_unref (tmp_comp);
		icalcomponent_free (cbdata->vcal_comp);
		g_free (cbdata);
		return SOUP_ERROR_MALFORMED;
	}
	real_icalcomp = icalparser_parse_string (comp_str);
	g_free (comp_str);

	icalcomponent_foreach_tzid (real_icalcomp, add_timezone_cb, cbdata);
	icalcomponent_add_component (cbdata->vcal_comp, real_icalcomp);

	body = icalcomponent_as_ical_string (cbdata->vcal_comp);
	body_crlf = e2k_lf_to_crlf (body);
	date = e2k_make_timestamp_rfc822 (time (NULL));
	from = get_from_string (comp);
	summary = icalcomponent_get_summary (real_icalcomp);
	if (!summary)
		summary = "";

	msg = g_strdup_printf ("Subject: %s\r\n"
			       "Date: %s\r\n"
			       "MIME-Version: 1.0\r\n"
			       "Content-Type: text/calendar;\r\n"
			       "\tmethod=REQUEST;\r\n"
			       "\tcharset=\"utf-8\"\r\n"
			       "Content-Transfer-Encoding: 8bit\r\n"
			       "content-class: urn:content-classes:appointment\r\n"
			       "Importance: normal\r\n"
			       "Priority: normal\r\n"
			       "From: %s\r\n"
			       "\r\n%s", summary, date,
			       from ? from : "Evolution",
			       body_crlf);

	g_free (date);
	g_free (from);
	g_free (body_crlf);

	/* PUT the iCal object in the Exchange server */
	E2K_DEBUG_HINT ('C');
	err = e2k_connection_put_sync (comp->backend->connection, comp->href,
				       "message/rfc822", msg, strlen (msg));
	if (SOUP_ERROR_IS_SUCCESSFUL (err))
		cal_backend_exchange_save (comp->backend);

	g_free (msg);
	g_object_unref (tmp_comp);
	icalcomponent_free (cbdata->vcal_comp);
	g_free (cbdata);

	return err;
}

/**
 * e2k_cal_component_remove
 */
int
e2k_cal_component_remove (E2kCalComponent *comp)
{
	int err;

	g_return_val_if_fail (E2K_IS_CAL_COMPONENT (comp), SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (comp->href != NULL, SOUP_ERROR_MALFORMED);
	g_return_val_if_fail (CAL_IS_BACKEND_EXCHANGE (comp->backend), SOUP_ERROR_MALFORMED);

	E2K_DEBUG_HINT ('C');
	err = e2k_connection_delete_sync (comp->backend->connection, comp->href);
	if (SOUP_ERROR_IS_SUCCESSFUL (err))
		cal_backend_exchange_save (comp->backend);
	return err;
}
