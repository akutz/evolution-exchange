/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include "e-book-backend-gal-factory.h"
#include "e-book-backend-gal.h"

static void
e_book_backend_gal_factory_instance_init (EBookBackendGalFactory *factory)
{
}

static const char *
_get_protocol (EBookBackendFactory *factory)
{
	return "gal";
}

static EBookBackend*
_new_backend (EBookBackendFactory *factory)
{
	return e_book_backend_gal_new ();
}

static void
e_book_backend_gal_factory_class_init (EBookBackendGalFactoryClass *klass)
{
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->new_backend = _new_backend;
}

GType
e_book_backend_gal_factory_get_type (void)
{
	GType type;

	GTypeInfo info = {
		sizeof (EBookBackendGalFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  e_book_backend_gal_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (EBookBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_book_backend_gal_factory_instance_init
	};

	type = g_type_register_static (E_TYPE_BOOK_BACKEND_FACTORY, 
				       "EBookBackendGalFactory", &info, 0);
	return type;
}
