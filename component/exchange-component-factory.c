/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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
#include "config.h"
#endif

#include <string.h>
#include "exchange-component.h"

#include <bonobo/bonobo-shlib-factory.h>


#define FACTORY_ID         "OAFIID:GNOME_Evolution_Exchange_Component_Factory:" BASE_VERSION
#define COMPONENT_ID       "OAFIID:GNOME_Evolution_Exchange_Control:" BASE_VERSION

static BonoboObject *
factory (BonoboGenericFactory *factory,
	 const char *component_id,
	 void *closure)
{
	if (!strcmp (component_id, COMPONENT_ID))
		return BONOBO_OBJECT (exchange_component_peek ());

	g_warning (FACTORY_ID ": Dont't know how to activate %s", component_id);
	
	return NULL;
}

BONOBO_ACTIVATION_SHLIB_FACTORY (FACTORY_ID, "Evolution Exchange component factory", factory, NULL)
