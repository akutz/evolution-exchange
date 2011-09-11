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

#include <config.h>

#include <libedata-cal/e-cal-backend-factory.h>
#include "e-cal-backend-exchange-calendar.h"
#include "e-cal-backend-exchange-tasks.h"

#define FACTORY_NAME "exchange"

typedef ECalBackendFactory ECalBackendExchangeEventsFactory;
typedef ECalBackendFactoryClass ECalBackendExchangeEventsFactoryClass;

typedef ECalBackendFactory ECalBackendExchangeTodosFactory;
typedef ECalBackendFactoryClass ECalBackendExchangeTodosFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_cal_backend_exchange_events_factory_get_type (void);
GType e_cal_backend_exchange_todos_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendExchangeEventsFactory,
	e_cal_backend_exchange_events_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	ECalBackendExchangeTodosFactory,
	e_cal_backend_exchange_todos_factory,
	E_TYPE_CAL_BACKEND_FACTORY)

static void
e_cal_backend_exchange_events_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VEVENT_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_EXCHANGE_CALENDAR;
}

static void
e_cal_backend_exchange_events_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_exchange_events_factory_init (ECalBackendFactory *factory)
{
}

static void
e_cal_backend_exchange_todos_factory_class_init (ECalBackendFactoryClass *class)
{
	class->factory_name = FACTORY_NAME;
	class->component_kind = ICAL_VTODO_COMPONENT;
	class->backend_type = E_TYPE_CAL_BACKEND_EXCHANGE_TASKS;
}

static void
e_cal_backend_exchange_todos_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_exchange_todos_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_cal_backend_exchange_events_factory_register_type (type_module);
	e_cal_backend_exchange_todos_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

