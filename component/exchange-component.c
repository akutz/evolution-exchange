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

#include <string.h>

#include "exchange-component.h"
#include "Ximian-Connector.h"

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-exception.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>

#include <gtk/gtkdrawingarea.h>

#include <shell/evolution-shell-component-utils.h>

#define XC_BACKEND_IID "OAFIID:Ximian_Connector_Backend:" BASE_VERSION

#define d(x)

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct ExchangeComponentPrivate {
	Ximian_Connector_Backend backend;
	GNOME_Evolution_Component backend_component;
};

/* GObject methods */

static void
dispose (GObject *object)
{
	ExchangeComponentPrivate *priv = EXCHANGE_COMPONENT (object)->priv;

	if (priv->backend) {
		bonobo_object_release_unref (priv->backend, NULL);
		priv->backend = CORBA_OBJECT_NIL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	ExchangeComponentPrivate *priv = EXCHANGE_COMPONENT (object)->priv;

	g_free (priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Evolution::Component CORBA methods. */

static CORBA_boolean
impl_upgradeFromVersion (PortableServer_Servant servant,
			 const CORBA_short major,
			 const CORBA_short minor,
			 const CORBA_short revision,
			 CORBA_Environment *ev)
{
	ExchangeComponent *component =
		EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("upgradeFromVersion %d %d %d\n", major, minor, revision));

	return GNOME_Evolution_Component_upgradeFromVersion (priv->backend_component,
							     major, minor,
							     revision, ev);
}

static CORBA_boolean
impl_requestQuit (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	ExchangeComponent *component =
		EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("requestQuit\n"));

	return GNOME_Evolution_Component_requestQuit (priv->backend_component, ev);
}

static CORBA_boolean
impl_quit (PortableServer_Servant servant,
	   CORBA_Environment *ev)
{
	ExchangeComponent *component =
		EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("quit\n"));

	return GNOME_Evolution_Component_quit (priv->backend_component, ev);
}

static void
impl_interactive (PortableServer_Servant servant,
		  const CORBA_boolean now_interactive,
		  const CORBA_unsigned_long new_view_xid,
		  CORBA_Environment *ev)
{
	ExchangeComponent *component =
		EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("interactive? %s, xid %lu\n", now_interactive ? "yes" : "no", new_view_xid));

	GNOME_Evolution_Component_interactive (priv->backend_component,
					       now_interactive,
					       new_view_xid,
					       ev);
}

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *sidebar_control,
		     Bonobo_Control *view_control,
		     Bonobo_Control *statusbar_control,
		     CORBA_Environment *ev)
{
	ExchangeComponent *component =
		EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;
	GtkWidget *blank;
	BonoboControl *control;

	d(printf("createControls...\n"));

	/* Get the sidebar from the backend */
	*sidebar_control =
		Ximian_Connector_Backend_createSideBar (priv->backend, ev);
	if (BONOBO_EX (ev)) {
		d(printf("  createSideBar failed: %s\n", bonobo_exception_get_text (ev)));
		return;
	}

	d(printf("  created side bar\n"));

	blank = gtk_drawing_area_new ();
	gtk_widget_show (blank);
	control = bonobo_control_new (blank);
	*statusbar_control = CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);

	blank = gtk_drawing_area_new ();
	gtk_widget_show (blank);
	control = bonobo_control_new (blank);
	*view_control = CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);
}

ExchangeComponent *
exchange_component_peek (void)
{
	static ExchangeComponent *component = NULL;

	if (component == NULL) {
		component = g_object_new (exchange_component_get_type (), NULL);
	}

	return component;
}

static void
exchange_component_class_init (ExchangeComponentClass *klass)
{
	POA_GNOME_Evolution_Component__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = dispose;
	object_class->finalize = finalize;

	epv->upgradeFromVersion = impl_upgradeFromVersion;
	epv->createControls = impl_createControls;
	epv->requestQuit = impl_requestQuit;
	epv->quit = impl_quit;
	epv->interactive = impl_interactive;
}

static void
exchange_component_init (ExchangeComponent *component)
{
	ExchangeComponentPrivate *priv;
	CORBA_Environment ev;

	priv = g_new0 (ExchangeComponentPrivate, 1);
	component->priv = priv;

	CORBA_exception_init (&ev);
	priv->backend = bonobo_activation_activate_from_id (XC_BACKEND_IID,
							    0, NULL, &ev);
	if (BONOBO_EX (&ev) || priv->backend == CORBA_OBJECT_NIL) {
		char *error_message = e_get_activation_failure_msg (&ev);
		g_warning (_("Cannot activate Ximian Connector backend:\n"
			     "The error from the activation system is:\n"
			     "%s"), error_message);
		g_free (error_message);
		CORBA_exception_free (&ev);
		exit (1);
	}
	priv->backend_component = Bonobo_Unknown_queryInterface (priv->backend,
								 "IDL:GNOME/Evolution/Component:1.0",
								 &ev);
	if (BONOBO_EX (&ev) || priv->backend_component == CORBA_OBJECT_NIL) {
		char *error_message = bonobo_exception_get_text (&ev);
		g_warning (_("Could not get backend component interface:\n"
			     "%s"), error_message);
		g_free (error_message);
		CORBA_exception_free (&ev);
		bonobo_object_release_unref (priv->backend, NULL);
		exit (1);
	}
}

BONOBO_TYPE_FUNC_FULL (ExchangeComponent, GNOME_Evolution_Component, PARENT_TYPE, exchange_component)
