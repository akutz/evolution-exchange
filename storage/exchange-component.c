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

#include "exchange-component.h"

#include <unistd.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>

#include <exchange-account.h>
#include <exchange-constants.h>
#include "exchange-config-listener.h"
#include "exchange-oof.h"
#include <e-folder-exchange.h>
#include <e-shell-marshal.h>

#include "mail-stub-listener.h"
#include "mail-stub-exchange.h"

#include "exchange-migrate.h" 

#define d(x)

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;
static gboolean idle_do_interactive (gpointer user_data);
static void exchange_component_update_accounts (ExchangeComponent *component,
							gboolean status);

static guint linestatus_signal_id;

struct ExchangeComponentPrivate {
	GdkNativeWindow xid;

	ExchangeConfigListener *config_listener;

	EDataCalFactory *cal_factory;
	EDataBookFactory *book_factory;

	gboolean linestatus;

	GSList *accounts;

	GSList *views;

	GNOME_Evolution_Listener evo_listener;
};

typedef struct {
	ExchangeAccount *account;
	MailStubListener *msl;
} ExchangeComponentAccount;

static void
free_account (ExchangeComponentAccount *baccount)
{
	g_object_unref (baccount->account);
	g_object_unref (baccount->msl);
	g_free (baccount);
}

static void
dispose (GObject *object)
{
	ExchangeComponentPrivate *priv = EXCHANGE_COMPONENT (object)->priv;
	GSList *p;

	if (priv->config_listener) {
		g_object_unref (priv->config_listener);
		priv->config_listener = NULL;
	}

	if (priv->accounts) {
		for (p = priv->accounts; p; p = p->next)
			free_account (p->data);
		g_slist_free (priv->accounts);
		priv->accounts = NULL;
	}

	if (priv->views) {
		for (p = priv->views; p; p = p->next)
			g_object_unref (p->data);
		g_slist_free (priv->views);
		priv->views = NULL;
	}

	if (priv->cal_factory) {
		g_object_unref (priv->cal_factory);
		priv->cal_factory = NULL;
	}

	if (priv->book_factory) {
		g_object_unref (priv->book_factory);
		priv->book_factory = NULL;
	}

	if (priv->evo_listener) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		GNOME_Evolution_Listener_complete (priv->evo_listener, &ev);
		CORBA_exception_free (&ev);
		CORBA_Object_release (priv->evo_listener, &ev);
		CORBA_exception_free (&ev);
		
		priv->evo_listener = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *sidebar_control,
		     Bonobo_Control *view_control,
		     Bonobo_Control *statusbar_control,
		     CORBA_Environment *ev)
{
	d(printf("createControls...\n"));

	*sidebar_control = *view_control = *statusbar_control = NULL;
}

static void
impl_upgradeFromVersion (PortableServer_Servant servant,
			 const CORBA_short major,
			 const CORBA_short minor,
			 const CORBA_short revision,
			 CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeAccount *account;
	const gchar *base_directory=NULL;

	d(printf("upgradeFromVersion %d %d %d\n", major, minor, revision));

	account = exchange_component_get_account_for_uri (component, NULL);
	if (account) {
		base_directory = g_build_filename (g_get_home_dir (),
						   ".evolution",
						   "exchange",
						   account->account_filename,
						   NULL);

		exchange_migrate(major, minor, revision, 
				 base_directory, account->account_filename);
	}
}

static CORBA_boolean
impl_requestQuit (PortableServer_Servant servant,
		  CORBA_Environment *ev)
{
	d(printf("requestQuit\n"));

	/* FIXME */
	return TRUE;
}

/* This returns %TRUE all the time, so if set as an idle callback it
   effectively causes each and every nested glib mainloop to be quit.  */
static gboolean
idle_quit (gpointer user_data)
{
	bonobo_main_quit ();
	return TRUE;
}

static CORBA_boolean
impl_quit (PortableServer_Servant servant,
	   CORBA_Environment *ev)
{
	d(printf("quit\n"));

	g_timeout_add (500, idle_quit, NULL);
	return TRUE;
}

static gboolean
idle_do_interactive (gpointer user_data)
{
	ExchangeComponent *component = user_data;
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;
		if (exchange_component_is_interactive (component))
			exchange_oof_init (baccount->account, priv->xid);
	}
	return FALSE;
}

static void
impl_interactive (PortableServer_Servant servant,
		  const CORBA_boolean now_interactive,
		  const CORBA_unsigned_long new_view_xid,
		  CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("interactive? %s, xid %lu\n", now_interactive ? "yes" : "no", new_view_xid));

	if (now_interactive) {
		priv->xid = new_view_xid;
		g_idle_add (idle_do_interactive, component);
	} else
		priv->xid = 0;
}

static void
impl_setLineStatus (PortableServer_Servant servant,
		    const CORBA_boolean status,
		    const GNOME_Evolution_Listener listener,
		    CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	priv->linestatus = status;
	
	if (priv->cal_factory) {
		e_data_cal_factory_set_backend_mode (priv->cal_factory,
						     status ? ONLINE_MODE : OFFLINE_MODE);
	}

	if (priv->book_factory) {
		e_data_book_factory_set_backend_mode (priv->book_factory,
						      status ? ONLINE_MODE : OFFLINE_MODE);
	}

	if (priv->evo_listener == NULL) {
		priv->evo_listener = CORBA_Object_duplicate (listener, ev);
		if (ev->_major == CORBA_NO_EXCEPTION) {
			exchange_component_update_accounts (component, status);
			g_signal_emit (component, linestatus_signal_id, 0,
				       status ? ONLINE_MODE : OFFLINE_MODE);
			return;
		} else {
			CORBA_exception_free (ev);
		}
	}

	GNOME_Evolution_Listener_complete (listener, ev);
}

static void
exchange_component_update_accounts (ExchangeComponent *component,
					gboolean status)
{
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;	
		if (status)
			exchange_account_set_online (baccount->account);
		else
			exchange_account_set_offline (baccount->account);
	}
}

static void
new_connection (MailStubListener *listener, int cmd_fd, int status_fd,
		ExchangeComponentAccount *baccount)
{
	MailStub *mse;
	ExchangeAccount *account = baccount->account;
	ExchangeAccountResult result;
	int mode;

	g_object_ref (account);

	exchange_account_is_offline (account, &mode);
	if (mode != ONLINE_MODE) {
		mse = mail_stub_exchange_new (account, cmd_fd, status_fd);
		goto end;
	}

	if (exchange_account_connect (account, NULL, &result))
		mse = mail_stub_exchange_new (account, cmd_fd, status_fd);
	else {
		close (cmd_fd);
		close (status_fd);
	}
end:
	g_object_unref (account);
}

static void
config_listener_account_created (ExchangeConfigListener *config_listener,
				 ExchangeAccount *account,
				 gpointer user_data)
{
	ExchangeComponent *component = user_data;
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	char *path;

	baccount = g_new0 (ExchangeComponentAccount, 1);
	baccount->account = g_object_ref (account);

	path = g_strdup_printf ("/tmp/.exchange-%s/%s",
				g_get_user_name (),
				account->account_filename);
	baccount->msl = mail_stub_listener_new (path);
	g_signal_connect (baccount->msl, "new_connection",
			  G_CALLBACK (new_connection), baccount);
	g_free (path);

	priv->accounts = g_slist_prepend (priv->accounts, baccount);

	if (priv->xid)
		exchange_oof_init (account, priv->xid);
}

static void
config_listener_account_removed (ExchangeConfigListener *config_listener,
				 ExchangeAccount *account,
				 gpointer user_data)
{
	ExchangeComponent *component = user_data;
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;
		if (baccount->account == account) {
			priv->accounts = g_slist_remove (priv->accounts, baccount);
			free_account (baccount);
			return;
		}
	}
}

static void
default_linestatus_notify_handler (ExchangeComponent *component, uint status)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	GNOME_Evolution_Listener_complete (component->priv->evo_listener, &ev);
	CORBA_exception_free (&ev);
	CORBA_Object_release (component->priv->evo_listener, &ev);
	CORBA_exception_free (&ev);

	component->priv->evo_listener = NULL;
}

static void
exchange_component_class_init (ExchangeComponentClass *klass)
{
	POA_GNOME_Evolution_Component__epv *epv = &klass->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = dispose;
	object_class->finalize = finalize;

	epv->createControls     = impl_createControls;
	epv->upgradeFromVersion = impl_upgradeFromVersion;
	epv->requestQuit        = impl_requestQuit;
	epv->quit               = impl_quit;
	epv->interactive        = impl_interactive;
	epv->setLineStatus	= impl_setLineStatus;

	klass->linestatus_notify = default_linestatus_notify_handler;

	linestatus_signal_id = 
		g_signal_new ("linestatus-changed",
			       G_TYPE_FROM_CLASS (klass),
			       G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
			       G_STRUCT_OFFSET (ExchangeComponentClass, linestatus_notify),
			       NULL,
			       NULL,
			       e_shell_marshal_VOID__INT,
			       G_TYPE_NONE,
			       1,
			       G_TYPE_INT);
}

static void
exchange_component_init (ExchangeComponent *component)
{
	ExchangeComponentPrivate *priv;

	priv = component->priv = g_new0 (ExchangeComponentPrivate, 1);

       	priv->config_listener = exchange_config_listener_new ();
	g_signal_connect (priv->config_listener, "exchange_account_created",
			  G_CALLBACK (config_listener_account_created),
			  component);
	g_signal_connect (priv->config_listener, "exchange_account_removed",
			  G_CALLBACK (config_listener_account_removed),
			  component);
}

BONOBO_TYPE_FUNC_FULL (ExchangeComponent, GNOME_Evolution_Component, PARENT_TYPE, exchange_component)

ExchangeComponent *
exchange_component_new (void)
{
	return EXCHANGE_COMPONENT (g_object_new (EXCHANGE_TYPE_COMPONENT, NULL));
}

ExchangeAccount *
exchange_component_get_account_for_uri (ExchangeComponent *component,
					const char *uri)
{
	ExchangeComponentPrivate *priv = component->priv;
	ExchangeComponentAccount *baccount;
	GSList *acc;

	for (acc = priv->accounts; acc; acc = acc->next) {
		baccount = acc->data;

		/* Kludge for while we don't support multiple accounts */
		if (!uri)
			return baccount->account;

		if (exchange_account_get_folder (baccount->account, uri)) {
			return baccount->account;
		} else {
			exchange_account_rescan_tree (baccount->account);
			if (exchange_account_get_folder (baccount->account, uri))
				return baccount->account;
		}
		/* FIXME : Handle multiple accounts */
	}
	return NULL;
}

void
exchange_component_is_offline (ExchangeComponent *component, int *state)
{
	g_return_if_fail (EXCHANGE_IS_COMPONENT (component));

	*state = component->priv->linestatus ? ONLINE_MODE : OFFLINE_MODE;
}

gboolean
exchange_component_is_interactive (ExchangeComponent *component)
{
	return component->priv->xid != 0;
}

void
exchange_component_set_factories (ExchangeComponent *component,
				  EDataCalFactory *cal_factory,
				  EDataBookFactory *book_factory)
{
	g_return_if_fail (EXCHANGE_IS_COMPONENT (component));
	g_return_if_fail (E_IS_DATA_CAL_FACTORY (cal_factory));
	g_return_if_fail (E_IS_DATA_BOOK_FACTORY (book_factory));

	component->priv->cal_factory = g_object_ref (cal_factory);
	component->priv->book_factory = g_object_ref (book_factory);
}

