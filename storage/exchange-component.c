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

#include <libedataserver/e-data-server-util.h>

#include "exchange-component.h"
#include "shell/e-component-view.h"

#include <unistd.h>

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>

#include <exchange-account.h>
#include <exchange-constants.h>
#include "exchange-config-listener.h"
#include <e-folder-exchange.h>

#include "mail-stub-listener.h"
#include "mail-stub-exchange.h"

#include "exchange-migrate.h"

#define d(x)

#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;
static void exchange_component_update_accounts (ExchangeComponent *component,
							gboolean status);
static void ex_migrate_esources (ExchangeComponent *component,
				  const CORBA_short major,
				  const CORBA_short minor,
				  const CORBA_short revision);

static guint linestatus_signal_id;

struct ExchangeComponentPrivate {
	ExchangeConfigListener *config_listener;

	EDataCalFactory *cal_factory;
	EDataBookFactory *book_factory;

	gboolean linestatus;

	GSList *accounts;

	GSList *views;

	GMutex *comp_lock;

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

static GNOME_Evolution_ComponentView
impl_createView (PortableServer_Servant servant,
		 GNOME_Evolution_ShellView parent,
                 CORBA_boolean select_item,
		 CORBA_Environment *ev)
{
	EComponentView *component_view = e_component_view_new_controls (parent, "exchange",
                                                        		NULL,
									NULL,
                                                        		NULL);
	d(printf("createView...\n"));

        return BONOBO_OBJREF(component_view);
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
	gchar *base_directory=NULL;
	char *account_filename;

	d(printf("upgradeFromVersion %d %d %d\n", major, minor, revision));

	account = exchange_component_get_account_for_uri (component, NULL);
	if (account) {
		/*
		base_directory = g_build_filename (g_get_home_dir (),
						   ".evolution",
						   "exchange",
						   account->account_filename,
						   NULL);
		*/
		base_directory = g_strdup (account->storage_dir);
		e_filename_make_safe (base_directory);
		account_filename = strrchr (base_directory, '/') + 1;

		exchange_migrate(major, minor, revision,
				 base_directory, account_filename);

		ex_migrate_esources (component, major, minor, revision);
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

/* This interface is not being used anymore */
static void
impl_interactive (PortableServer_Servant servant,
		  const CORBA_boolean now_interactive,
		  const CORBA_unsigned_long new_view_xid,
		  CORBA_Environment *ev)
{
/*
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	d(printf("interactive? %s, xid %lu\n", now_interactive ? "yes" : "no", new_view_xid));

	if (now_interactive) {
		priv->xid = new_view_xid;
	} else
		priv->xid = 0;
*/
}

static void
impl_setLineStatus (PortableServer_Servant servant,
		    const GNOME_Evolution_ShellState status,
		    const GNOME_Evolution_Listener listener,
		    CORBA_Environment *ev)
{
	ExchangeComponent *component = EXCHANGE_COMPONENT (bonobo_object_from_servant (servant));
	ExchangeComponentPrivate *priv = component->priv;

	switch (status) {
		case GNOME_Evolution_USER_OFFLINE:
		case GNOME_Evolution_FORCED_OFFLINE:
			priv->linestatus = FALSE;
			break;

		case GNOME_Evolution_USER_ONLINE:
			priv->linestatus = TRUE;
			break;
		default:
			break;
	}

	if (priv->cal_factory) {
		e_data_cal_factory_set_backend_mode (priv->cal_factory,
						     priv->linestatus ? ONLINE_MODE : OFFLINE_MODE);
	}

	if (priv->book_factory) {
		e_data_book_factory_set_backend_mode (priv->book_factory,
						      priv->linestatus ? ONLINE_MODE : OFFLINE_MODE);
	}

	if (priv->evo_listener == NULL) {
		priv->evo_listener = CORBA_Object_duplicate (listener, ev);
		if (ev->_major == CORBA_NO_EXCEPTION) {
			exchange_component_update_accounts (component, status);
			g_signal_emit (component, linestatus_signal_id, 0,
				       priv->linestatus ? ONLINE_MODE : OFFLINE_MODE);
			return;
		} else {
			CORBA_exception_free (ev);
		}
	}

	GNOME_Evolution_Listener_complete (listener, ev);
}

static void
ex_migrate_esources (ExchangeComponent *component,
		  const CORBA_short major,
		  const CORBA_short minor,
		  const CORBA_short revision)
{

	if ((major == 1) || ((major == 2) && (minor <= 3)))
	{
		exchange_config_listener_migrate_esources (component->priv->config_listener);
	}
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
	MailStub *stub;
	MailStubExchange *mse, *mse_prev;
	ExchangeAccount *account = baccount->account;
	int mode;

	g_object_ref (account);

	exchange_account_is_offline (account, &mode);
	if (mode != ONLINE_MODE) {
		mse = MAIL_STUB_EXCHANGE (mail_stub_exchange_new (account, cmd_fd, status_fd));
		goto end;
	}

	stub = mail_stub_exchange_new (account, cmd_fd, status_fd);
	mse = (MailStubExchange *) stub;
	mse_prev = (MailStubExchange *) listener->stub;
	if (mse_prev) {
		g_hash_table_destroy (mse->folders_by_name);
		mse->folders_by_name = mse_prev->folders_by_name;
		mse_prev->folders_by_name = NULL;
	}

	if (listener->stub)
		g_object_unref (listener->stub);
	listener->stub = mse;
	/* FIXME : We need to close these sockets */
/*
	if (exchange_account_connect (account, NULL, &result))
		mse = mail_stub_exchange_new (account, cmd_fd, status_fd);
	else {
		close (cmd_fd);
		close (status_fd);
	}
*/
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
	char *path, *dot_exchange_username, *account_filename;

	baccount = g_new0 (ExchangeComponentAccount, 1);
	baccount->account = g_object_ref (account);

	dot_exchange_username = g_strdup_printf (".exchange-%s", g_get_user_name ());

	account_filename = strrchr (account->storage_dir, '/') + 1;
	e_filename_make_safe (account_filename);

	path = g_build_filename (g_get_tmp_dir (),
				 dot_exchange_username,
				 account_filename,
				 NULL);
	g_free (dot_exchange_username);
	baccount->msl = mail_stub_listener_new (path);
	g_signal_connect (baccount->msl, "new_connection",
			  G_CALLBACK (new_connection), baccount);
	g_free (path);

	priv->accounts = g_slist_prepend (priv->accounts, baccount);
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
default_linestatus_notify_handler (ExchangeComponent *component, guint status)
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

	epv->createView     	= impl_createView;
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
			       g_cclosure_marshal_VOID__INT,
			       G_TYPE_NONE,
			       1,
			       G_TYPE_INT);
}

static void
exchange_component_init (ExchangeComponent *component)
{
	ExchangeComponentPrivate *priv;
	GConfClient *client;
	GConfValue *value;

	priv = component->priv = g_new0 (ExchangeComponentPrivate, 1);

       	priv->config_listener = exchange_config_listener_new ();
	priv->comp_lock = g_mutex_new ();

	g_signal_connect (priv->config_listener, "exchange_account_created",
			  G_CALLBACK (config_listener_account_created),
			  component);
	g_signal_connect (priv->config_listener, "exchange_account_removed",
			  G_CALLBACK (config_listener_account_removed),
			  component);

	client = gconf_client_get_default ();
	value = gconf_client_get (client, "/apps/evolution/shell/start_offline", NULL);

	priv->linestatus = !(value && gconf_value_get_bool (value));

	gconf_value_free (value);
	g_object_unref (client);
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
			g_mutex_lock (priv->comp_lock);
			exchange_account_rescan_tree (baccount->account);
			g_mutex_unlock (priv->comp_lock);
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

/*
gboolean
exchange_component_is_interactive (ExchangeComponent *component)
{
	return component->priv->xid != 0;
}
*/

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

