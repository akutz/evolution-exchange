/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef __E_BOOK_BACKEND_GAL_H__
#define __E_BOOK_BACKEND_GAL_H__

#include "libedata-book/e-book-backend.h"
#include "exchange-component.h"

typedef struct _EBookBackendGALPrivate EBookBackendGALPrivate;

typedef struct {
	EBookBackend             parent_object;
	EBookBackendGALPrivate *priv;
} EBookBackendGAL;

typedef struct {
	EBookBackendClass parent_class;
} EBookBackendGALClass;

EBookBackend *e_book_backend_gal_new      (void);
GType       e_book_backend_gal_get_type (void);

#define E_TYPE_BOOK_BACKEND_GAL        (e_book_backend_gal_get_type ())
#define E_BOOK_BACKEND_GAL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_BOOK_BACKEND_GAL, EBookBackendGAL))
#define E_BOOK_BACKEND_GAL_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), E_TYPE_BOOK_BACKEND_GAL, EBookBackendGALClass))
#define E_IS_BOOK_BACKEND_GAL(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_BOOK_BACKEND_GAL))
#define E_IS_BOOK_BACKEND_GAL_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), E_TYPE_BOOK_BACKEND_GAL))

#endif /* ! __E_BOOK_BACKEND_GAL_H__ */

