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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include "e-book-backend-exchange-factory.h"
#include "e-book-backend-exchange.h"

static void
e_book_backend_exchange_factory_instance_init (EBookBackendExchangeFactory *factory)
{
}

static const char *
_get_protocol (EBookBackendFactory *factory)
{
	return "exchange";
}

static EBookBackend*
_new_backend (EBookBackendFactory *factory)
{
	return e_book_backend_exchange_new ();
}

static void
e_book_backend_exchange_factory_class_init (EBookBackendExchangeFactoryClass *klass)
{
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->new_backend = _new_backend;
}

GType
e_book_backend_exchange_factory_get_type (void)
{
	static GType  type = 0;

	if (!type) {
		GTypeInfo info = {
			sizeof (EBookBackendExchangeFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_exchange_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_exchange_factory_instance_init
		};

		type = g_type_register_static (E_TYPE_BOOK_BACKEND_FACTORY, 
					       "EBookBackendExchangeFactory", &info, 0);
	}
	return type;
}
