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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <e-util/e-config-listener.h>
#include "e2k-utils.h"
#include "e2k-connection.h"
#include "ical.h"
#include "cal-util/cal-component.h"
#include "cal-backend-exchange.h"

char *
calcomponentdatetime_to_string (CalComponentDateTime *dt,
				icaltimezone *izone)
{
	time_t tt;

	g_return_val_if_fail (dt != NULL, NULL);
	g_return_val_if_fail (dt->value != NULL, NULL);

	if (izone != NULL)
		tt = icaltime_as_timet_with_zone (*dt->value, izone);
	else
		tt = icaltime_as_timet (*dt->value);

	return e2k_make_timestamp (tt);
}

CalComponentDateTime *
calcomponentdatetime_from_string (const char *timestamp, icaltimezone *izone)
{
	CalComponentDateTime *dt;

	g_return_val_if_fail (timestamp != NULL, NULL);

	dt = g_new0 (CalComponentDateTime, 1);
	dt->value = g_new0 (struct icaltimetype, 1);
	*dt->value = icaltime_from_e2k_time (timestamp);
	if (izone != NULL)
		dt->tzid = g_strdup (icaltimezone_get_tzid (izone));
	else {
		izone = icaltimezone_get_utc_timezone ();
		dt->tzid = g_strdup (icaltimezone_get_tzid (izone));
	}

	return dt;
}

const char *
calcomponenttransparency_to_string (CalComponentTransparency *transp)
{
	g_return_val_if_fail (transp != NULL, "");

	switch (*transp) {
	case CAL_COMPONENT_TRANSP_NONE :
		return "NONE";
	case CAL_COMPONENT_TRANSP_TRANSPARENT :
		return "TRANSPARENT";
	case CAL_COMPONENT_TRANSP_OPAQUE :
		return "OPAQUE";
	default :
		return "";
	}
}

struct icaltimetype
icaltime_from_e2k_time (const char *timestamp)
{
	static struct icaltimetype itt;
	time_t tt;

	tt = e2k_parse_timestamp ((char *) timestamp);
	itt = icaltime_from_timet_with_zone (tt, 0, icaltimezone_get_utc_timezone ());

	return itt;
}

char *
icaltime_to_e2k_time (struct icaltimetype *itt)
{
	time_t tt;

	g_return_val_if_fail (itt != NULL, NULL);

	tt = icaltime_as_timet_with_zone (*itt, icaltimezone_get_utc_timezone ());
	return e2k_make_timestamp (tt);
}

icaltimezone *
get_default_timezone (void)
{
	static EConfigListener *cl = NULL;
	gchar *location;
	icaltimezone *local_timezone;

	if (!cl)
		cl = e_config_listener_new ();

	location = e_config_listener_get_string_with_default (cl, "/apps/evolution/calendar/display/timezone",
							      "UTC", NULL);
	if (location && location[0]) {
		local_timezone = icaltimezone_get_builtin_timezone (location);
	} else {
		local_timezone = icaltimezone_get_utc_timezone ();
	}

	g_free (location);

	return local_timezone;
}
