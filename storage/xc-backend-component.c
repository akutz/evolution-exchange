/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
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

#include "xc-backend-component.h"

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-exception.h>

#include "e2k-marshal.h"

#define d(x)

enum {
	UPGRADE_FROM_VERSION,
	REQUEST_QUIT,
	QUIT,
	INTERACTIVE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct XCBackendComponentPrivate {
	int dummy;
};

/* GObject methods */

static void
dispose (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	XCBackendComponentPrivate *priv = XC_BACKEND_COMPONENT (object)->priv;

	g_free (priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* GNOME::Evolution::Component CORBA methods. */

static CORBA_boolean
impl_upgradeFromVersion (PortableServer_Servant servant,
			 const CORBA_short major,
			 const CORBA_short minor,
			 const CORBA_short revision,
			 CORBA_Environment *ev)
{
	XCBackendComponent *component =
		XC_BACKEND_COMPONENT (bonobo_object_from_servant (servant));
	gboolean upgraded = TRUE;

	d(printf("upgradeFromVersion %d %d %d\n", major, minor, revision));

	g_signal_emit (component, signals[UPGRADE_FROM_VERSION], 0,
		       major, minor, revision,
		       &upgraded);
	return upgraded;
}

static CORBA_boolean
impl_requestQuit (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	XCBackendComponent *component =
		XC_BACKEND_COMPONENT (bonobo_object_from_servant (servant));
	gboolean can_quit = TRUE;

	d(printf("requestQuit\n"));

	g_signal_emit (component, signals[REQUEST_QUIT], 0,
		       &can_quit);
	return can_quit;
}

static CORBA_boolean
impl_quit (PortableServer_Servant servant,
	   CORBA_Environment *ev)
{
	XCBackendComponent *component =
		XC_BACKEND_COMPONENT (bonobo_object_from_servant (servant));
	gboolean can_quit = TRUE;

	d(printf("quit\n"));

	g_signal_emit (component, signals[QUIT], 0,
		       &can_quit);
	return can_quit;
}

static void
impl_interactive (PortableServer_Servant servant,
		  const CORBA_boolean now_interactive,
		  const CORBA_unsigned_long new_view_xid,
		  CORBA_Environment *ev)
{
	XCBackendComponent *component =
		XC_BACKEND_COMPONENT (bonobo_object_from_servant (servant));

	d(printf("interactive? %s, xid %lu\n", now_interactive ? "yes" : "no", new_view_xid));

	g_signal_emit (component, signals[INTERACTIVE], 0,
		       now_interactive, new_view_xid);
}

static void
xc_backend_component_class_init (XCBackendComponentClass *klass)
{
	POA_GNOME_Evolution_Component__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = dispose;
	object_class->finalize = finalize;

	epv->upgradeFromVersion = impl_upgradeFromVersion;
	epv->requestQuit        = impl_requestQuit;
	epv->quit               = impl_quit;
	epv->interactive        = impl_interactive;

	/* signals */
	signals[UPGRADE_FROM_VERSION] =
		g_signal_new ("upgrade_from_version",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (XCBackendComponentClass, upgrade_from_version),
			      NULL, NULL,
			      e2k_marshal_BOOL__INT_INT_INT,
			      G_TYPE_BOOLEAN, 3,
			      G_TYPE_INT,
			      G_TYPE_INT,
			      G_TYPE_INT);
	signals[REQUEST_QUIT] =
		g_signal_new ("request_quit",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (XCBackendComponentClass, request_quit),
			      NULL, NULL,
			      e2k_marshal_BOOL__NONE,
			      G_TYPE_BOOLEAN, 0);
	signals[QUIT] =
		g_signal_new ("quit",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (XCBackendComponentClass, quit),
			      NULL, NULL,
			      e2k_marshal_BOOL__NONE,
			      G_TYPE_BOOLEAN, 0);
	signals[INTERACTIVE] =
		g_signal_new ("interactive",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (XCBackendComponentClass, interactive),
			      NULL, NULL,
			      e2k_marshal_NONE__BOOL_ULONG,
			      G_TYPE_NONE, 2,
			      G_TYPE_BOOLEAN,
			      G_TYPE_ULONG);
}

static void
xc_backend_component_init (XCBackendComponent *component)
{
	XCBackendComponentPrivate *priv;

	priv = g_new0 (XCBackendComponentPrivate, 1);
	component->priv = priv;
}

BONOBO_TYPE_FUNC_FULL (XCBackendComponent, GNOME_Evolution_Component, PARENT_TYPE, xc_backend_component)

XCBackendComponent *
xc_backend_component_new (void)
{
	return g_object_new (XC_TYPE_BACKEND_COMPONENT, NULL);
}
