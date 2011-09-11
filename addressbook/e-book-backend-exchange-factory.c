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

#include <config.h>

#include <libedata-book/e-book-backend-factory.h>
#include "e-book-backend-exchange.h"
#include "e-book-backend-gal.h"

typedef EBookBackendFactory EBookBackendExchangeFactory;
typedef EBookBackendFactoryClass EBookBackendExchangeFactoryClass;

typedef EBookBackendFactory EBookBackendGalFactory;
typedef EBookBackendFactoryClass EBookBackendGalFactoryClass;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_exchange_factory_get_type (void);
GType e_book_backend_gal_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendExchangeFactory,
	e_book_backend_exchange_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendGalFactory,
	e_book_backend_gal_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_exchange_factory_class_init (EBookBackendFactoryClass *class)
{
	class->factory_name = "exchange";
	class->backend_type = E_TYPE_BOOK_BACKEND_EXCHANGE;
}

static void
e_book_backend_exchange_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_exchange_factory_init (EBookBackendFactory *factory)
{
}

static void
e_book_backend_gal_factory_class_init (EBookBackendFactoryClass *class)
{
	class->factory_name = "gal";
	class->backend_type = E_TYPE_BOOK_BACKEND_GAL;
}

static void
e_book_backend_gal_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_gal_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_book_backend_exchange_factory_register_type (type_module);
	e_book_backend_gal_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

