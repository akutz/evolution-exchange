/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

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

#include "exchange-offline-handler.h"

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

/* GNOME::Evolution::Offline methods.  */

static CORBA_boolean
impl__get_isOffline (PortableServer_Servant servant,
		     CORBA_Environment *ev)
{
	return FALSE;
}

static void
impl_prepareForOffline (PortableServer_Servant servant,
			GNOME_Evolution_ConnectionList **active_connection_list,
			CORBA_Environment *ev)
{
	*active_connection_list = GNOME_Evolution_ConnectionList__alloc ();
	(*active_connection_list)->_length = 0;
	(*active_connection_list)->_maximum = 0;
	(*active_connection_list)->_buffer = CORBA_sequence_GNOME_Evolution_Connection_allocbuf ((*active_connection_list)->_maximum);
}

static void
impl_goOffline (PortableServer_Servant servant,
		const GNOME_Evolution_OfflineProgressListener progress_listener,
		CORBA_Environment *ev)
{
	;
}

static void
impl_goOnline (PortableServer_Servant servant,
	       CORBA_Environment *ev)
{
	;
}

/* GTK+ type initialization.  */

static void
exchange_offline_handler_class_init (ExchangeOfflineHandlerClass *klass)
{
	POA_GNOME_Evolution_Offline__epv *epv;

	epv = &klass->epv;
	epv->_get_isOffline    = impl__get_isOffline;
	epv->prepareForOffline = impl_prepareForOffline;
	epv->goOffline         = impl_goOffline;
	epv->goOnline          = impl_goOnline;

	parent_class = g_type_class_ref (PARENT_TYPE);
}

static void
exchange_offline_handler_init (ExchangeOfflineHandler *offline_handler)
{
	;
}

ExchangeOfflineHandler *
exchange_offline_handler_new (void)
{
	ExchangeOfflineHandler *new;

	new = g_object_new (exchange_offline_handler_get_type (), NULL);

	return new;
}

BONOBO_TYPE_FUNC_FULL (ExchangeOfflineHandler,
		       GNOME_Evolution_Offline,
		       PARENT_TYPE,
		       exchange_offline_handler);
