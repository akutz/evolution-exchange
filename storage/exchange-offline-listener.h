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

#ifndef _OFFLINE_LISTNER_H_
#define _OFFLINE_LISTNER_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>
#include <libedata-book/e-data-book-factory.h>
#include <libedata-cal/e-data-cal-factory.h>

G_BEGIN_DECLS

#define OFFLINE_TYPE_LISTENER		        (offline_listener_get_type ())
#define OFFLINE_LISTENER(obj)		        ((G_TYPE_CHECK_INSTANCE_CAST((obj), OFFLINE_TYPE_LISTENER, ExchangeOfflineListener)))
#define OFFLINE_LISTENER_CLASS(klass)	        (G_TYPE_CHECK_CLASS_CAST((klass), OFFLINE_TYPE_LISTENER, ExchangeOfflineListenerClass))
#define OFFLINE_IS_LISTENER(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), OFFLINE_TYPE_LISTENER))
#define OFFLINE_IS_LISTENER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), OFFLINE_TYPE_LISTENER))


typedef struct _ExchangeOfflineListener        ExchangeOfflineListener;
typedef struct _ExchangeOfflineListenerPrivate  ExchangeOfflineListenerPrivate;
typedef struct _ExchangeOfflineListenerClass   ExchangeOfflineListenerClass;

struct _ExchangeOfflineListener {
	GObject parent;
	ExchangeOfflineListenerPrivate *priv;
};

struct _ExchangeOfflineListenerClass {
	GObjectClass  parent_class;
};


GType offline_listener_get_type  (void);
ExchangeOfflineListener  *offline_listener_new (EDataBookFactory *book_factory, EDataCalFactory *cal_factory);

gboolean exchange_is_offline (ExchangeOfflineListener *offline_listener);

G_END_DECLS

#endif /* _OFFLINE_LISTNER_H_ */
