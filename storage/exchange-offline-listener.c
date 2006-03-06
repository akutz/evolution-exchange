/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* server-interface-check.h
 *
 * Copyright (C) 2004  Novell, Inc.
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
 *
 * Author: Sivaiah Nallagatla <snallagatla@novell.com>
 * Author: Sarfraaz Ahmed <asarfraaz@novell.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "exchange-offline-listener.h"
#include <exchange-constants.h>
#include <libedata-book/e-data-book-factory.h>
#include <libedata-cal/e-data-cal-factory.h>
#include <gconf/gconf-client.h>
#include <e-shell-marshal.h>

static GObjectClass *parent_class = NULL;

static guint linestatus_signal_id;

struct _ExchangeOfflineListenerPrivate 
{
	GConfClient *default_client;
	guint gconf_cnx;
	EDataCalFactory *cal_factory;
	EDataBookFactory *book_factory;
	gboolean offline;
};

static void 
set_online_status (ExchangeOfflineListener *ex_offline_listener, gboolean is_offline)
{
	ExchangeOfflineListenerPrivate *priv;
	
	priv = ex_offline_listener->priv;
	priv->offline = is_offline;

	if (is_offline) {
		e_data_cal_factory_set_backend_mode (priv->cal_factory, OFFLINE_MODE);
		e_data_book_factory_set_backend_mode (priv->book_factory, OFFLINE_MODE);

	} else {
		e_data_cal_factory_set_backend_mode (priv->cal_factory, ONLINE_MODE);
		e_data_book_factory_set_backend_mode (priv->book_factory, ONLINE_MODE);
	}
}

static void 
online_status_changed (GConfClient *client, int cnxn_id, GConfEntry *entry, gpointer data)
{
	GConfValue *value;
	gboolean offline = FALSE;
        ExchangeOfflineListener *ex_offline_listener;
	ExchangeOfflineListenerPrivate *priv;
	
	ex_offline_listener = EXCHANGE_OFFLINE_LISTENER(data);
	priv = ex_offline_listener->priv;
	value = gconf_entry_get_value (entry);
	if (value)
		offline = gconf_value_get_bool (value);
	if (priv->offline != offline)
		set_online_status (ex_offline_listener ,offline);

	g_signal_emit (ex_offline_listener, linestatus_signal_id,
		       0, offline ? OFFLINE_MODE : ONLINE_MODE);
}

static void 
setup_offline_listener (ExchangeOfflineListener *ex_offline_listener)
{
	ExchangeOfflineListenerPrivate *priv = ex_offline_listener->priv;
	GConfValue *value;
	gboolean offline = FALSE;
	
	priv->default_client = gconf_client_get_default ();
	gconf_client_add_dir (priv->default_client, "/apps/evolution/shell", 
				GCONF_CLIENT_PRELOAD_RECURSIVE,NULL);

	priv->gconf_cnx = gconf_client_notify_add (priv->default_client, 
				"/apps/evolution/shell/start_offline", 
				(GConfClientNotifyFunc)online_status_changed, 
				(gpointer)ex_offline_listener, NULL, NULL);

	value = gconf_client_get (priv->default_client, 
				"/apps/evolution/shell/start_offline", NULL);
	if (value)
		offline = gconf_value_get_bool (value);

	set_online_status (ex_offline_listener, offline); 
	gconf_value_free (value);
}

ExchangeOfflineListener*
exchange_offline_listener_new (EDataBookFactory *book_factory, EDataCalFactory *cal_factory)
{
	ExchangeOfflineListener *ex_offline_listener = g_object_new (EXCHANGE_OFFLINE_TYPE_LISTENER, NULL);
	ExchangeOfflineListenerPrivate *priv = ex_offline_listener->priv;
	
	g_return_val_if_fail (book_factory != NULL, NULL);
	g_return_val_if_fail (cal_factory != NULL, NULL);

	priv->book_factory = book_factory;
	priv->cal_factory = cal_factory;
	setup_offline_listener (ex_offline_listener);
	return ex_offline_listener;
}

/* This returns TRUE if exchange is set offline */
void
exchange_is_offline (ExchangeOfflineListener *ex_offline_listener, int *state)
{
	ExchangeOfflineListenerPrivate * priv;

	g_return_if_fail (EXCHANGE_IS_OFFLINE_LISTENER (ex_offline_listener));

	priv = ex_offline_listener->priv;
	
	if (priv->offline)
		*state = OFFLINE_MODE;
	else
		*state = ONLINE_MODE;
}

static void
exchange_offline_listener_dispose (GObject *object)
{
	ExchangeOfflineListener *ex_offline_listener = EXCHANGE_OFFLINE_LISTENER (object);
	gconf_client_notify_remove (ex_offline_listener->priv->default_client, 
				ex_offline_listener->priv->gconf_cnx);
	if (ex_offline_listener->priv->default_client) {
		g_object_unref (ex_offline_listener->priv->default_client);
		ex_offline_listener->priv->default_client = NULL;
	}
	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
exchange_offline_listener_finalize (GObject *object)
{
	ExchangeOfflineListener *ex_offline_listener;
	ExchangeOfflineListenerPrivate *priv;

	ex_offline_listener = EXCHANGE_OFFLINE_LISTENER (object);
	priv = ex_offline_listener->priv;
	
	g_free (priv);
	ex_offline_listener->priv = NULL;
	
	parent_class->finalize (object);
}

static void
exchange_offline_listener_init (ExchangeOfflineListener *listener)
{
	ExchangeOfflineListenerPrivate *priv;
	
	priv =g_new0 (ExchangeOfflineListenerPrivate, 1);
	listener->priv = priv;
}

static void
exchange_offline_listener_class_init (ExchangeOfflineListenerClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = exchange_offline_listener_dispose;
	object_class->finalize = exchange_offline_listener_finalize;

	linestatus_signal_id = 
		g_signal_new ("linestatus-changed",
			       G_TYPE_FROM_CLASS (klass),
			       G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
			       G_STRUCT_OFFSET (ExchangeOfflineListenerClass, linestatus_notify),
			       NULL,
			       NULL,
			       e_shell_marshal_VOID__INT,
			       G_TYPE_NONE,
			       1,
			       G_TYPE_INT);
}

GType
exchange_offline_listener_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (ExchangeOfflineListenerClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) exchange_offline_listener_class_init,
                        NULL, NULL,
                        sizeof (ExchangeOfflineListener),
                        0,
                        (GInstanceInitFunc) exchange_offline_listener_init,
                };
		type = g_type_register_static (G_TYPE_OBJECT, "ExchangeOfflineListener", &info, 0);
	}

	return type;
}
