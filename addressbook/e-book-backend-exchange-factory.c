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

#include <libebackend/e-data-server-module.h>
#include "e-book-backend-exchange-factory.h"
#include "e-book-backend-exchange.h"

static GType exchange_type;

static const gchar *
book_backend_exchange_factory_get_protocol (EBookBackendFactory *factory)
{
	return "exchange";
}

static EBookBackend*
book_backend_exchange_factory_new_backend (EBookBackendFactory *factory)
{
	return e_book_backend_exchange_new ();
}

static void
book_backend_exchange_factory_class_init (EBookBackendExchangeFactoryClass *class)
{
	EBookBackendFactoryClass *factory_class;

	factory_class = E_BOOK_BACKEND_FACTORY_CLASS (class);
	factory_class->get_protocol = book_backend_exchange_factory_get_protocol;
	factory_class->new_backend = book_backend_exchange_factory_new_backend;
}

GType
e_book_backend_exchange_factory_get_type (void)
{
	return exchange_type;
}

void
e_book_backend_exchange_factory_register_type (GTypeModule *type_module)
{
	static const GTypeInfo type_info = {
		sizeof (EBookBackendExchangeFactoryClass),
		(GBaseInitFunc) NULL,
		(GBaseFinalizeFunc) NULL,
		(GClassInitFunc)  book_backend_exchange_factory_class_init,
		(GClassFinalizeFunc) NULL,
		NULL,  /* class_data */
		sizeof (EBookBackend),
		0,     /* n_preallocs */
		(GInstanceInitFunc) NULL,
		NULL   /* value_table */
	};

	exchange_type = g_type_module_register_type (
		type_module, E_TYPE_BOOK_BACKEND_FACTORY,
		"EBookBackendExchangeFactory", &type_info, 0);
}

void
eds_module_initialize (GTypeModule *type_module)
{
	e_book_backend_exchange_factory_register_type (type_module);
}

void
eds_module_shutdown (void)
{
}

void
eds_module_list_types (const GType **types, gint *num_types)
{
	static GType module_types[1];

	module_types[0] = E_TYPE_BOOK_BACKEND_EXCHANGE_FACTORY;

	*types = module_types;
	*num_types = G_N_ELEMENTS (module_types);
}
