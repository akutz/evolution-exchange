/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-book-backend-gal-factory.h
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
 * Author: Chris Toshok <toshok@ximian.com>
 */

#ifndef _E_BOOK_BACKEND_GAL_FACTORY_H_
#define _E_BOOK_BACKEND_GAL_FACTORY_H_

#include <glib-object.h>
#include <libedata-book/e-book-backend-factory.h>

G_BEGIN_DECLS

#define E_TYPE_BOOK_BACKEND_GAL_FACTORY        (e_book_backend_gal_factory_get_type ())
#define E_BOOK_BACKEND_GAL_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_GAL_FACTORY, EBookBackendGalFactory))
#define E_BOOK_BACKEND_GAL_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), E_TYPE_BOOK_BACKEND_GAL_FACTORY, EBookBackendGalFactoryClass))
#define E_IS_BOOK_BACKEND_GAL_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_GAL_FACTORY))
#define E_IS_BOOK_BACKEND_GAL_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_GAL_FACTORY))
#define E_BOOK_BACKEND_GAL_FACTORY_GET_CLASS(k) (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_BOOK_BACKEND_GAL_FACTORY, EBookBackendGalFactoryClass))

typedef struct {
	EBookBackendFactory            parent_object;
} EBookBackendGalFactory;

typedef struct {
	EBookBackendFactoryClass parent_class;
} EBookBackendGalFactoryClass;

GType	e_book_backend_gal_factory_get_type (void)

G_END_DECLS

#endif /* _E_BOOK_BACKEND_GAL_FACTORY_H_ */
