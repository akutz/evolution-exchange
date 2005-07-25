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

#ifndef _EXCHANGE_OFFLINE_LISTNER_H_
#define _EXCHANGE_OFFLINE_LISTNER_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>
#include <libedata-book/e-data-book-factory.h>
#include <libedata-cal/e-data-cal-factory.h>

G_BEGIN_DECLS

#define EXCHANGE_OFFLINE_TYPE_LISTENER		        (exchange_offline_listener_get_type ())
#define EXCHANGE_OFFLINE_LISTENER(obj)		        ((G_TYPE_CHECK_INSTANCE_CAST((obj), EXCHANGE_OFFLINE_TYPE_LISTENER, ExchangeOfflineListener)))
#define EXCHANGE_OFFLINE_LISTENER_CLASS(klass)	        (G_TYPE_CHECK_CLASS_CAST((klass), EXCHANGE_OFFLINE_TYPE_LISTENER, ExchangeOfflineListenerClass))
#define EXCHANGE_IS_OFFLINE_LISTENER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_OFFLINE_TYPE_LISTENER))
#define EXCHANGE_IS_OFFLINE_LISTENER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_OFFLINE_TYPE_LISTENER)

#if 0
enum {
	UNSUPPORTED_MODE = 0,
        OFFLINE_MODE,
        ONLINE_MODE
};
#endif

typedef struct _ExchangeOfflineListener        ExchangeOfflineListener;
typedef struct _ExchangeOfflineListenerPrivate  ExchangeOfflineListenerPrivate;
typedef struct _ExchangeOfflineListenerClass   ExchangeOfflineListenerClass;

struct _ExchangeOfflineListener {
	GObject parent;
	ExchangeOfflineListenerPrivate *priv;
};

struct _ExchangeOfflineListenerClass {
	GObjectClass  parent_class;

	/* signal default handlers */
	void (*linestatus_notify) (ExchangeOfflineListener *listener, guint status);
};


GType exchange_offline_listener_get_type  (void);

ExchangeOfflineListener  *exchange_offline_listener_new (EDataBookFactory *book_factory, EDataCalFactory *cal_factory);

void exchange_is_offline (ExchangeOfflineListener *offline_listener, int *state);

G_END_DECLS

#endif /* _EXCHANGE_OFFLINE_LISTNER_H_ */
