/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef __PAS_BACKEND_EXCHANGE_H__
#define __PAS_BACKEND_EXCHANGE_H__

#include "pas/pas-backend.h"

typedef struct _PASBackendExchangePrivate PASBackendExchangePrivate;

typedef struct {
	PASBackend             parent_object;
	PASBackendExchangePrivate *priv;
} PASBackendExchange;

typedef struct {
	PASBackendClass parent_class;
} PASBackendExchangeClass;

#define PAS_BACKEND_EXCHANGE_TYPE        (pas_backend_exchange_get_type ())
#define PAS_BACKEND_EXCHANGE(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), PAS_BACKEND_EXCHANGE_TYPE, PASBackendExchange))
#define PAS_BACKEND_EXCHANGE_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), PAS_BACKEND_TYPE, PASBackendExchangeClass))
#define PAS_IS_BACKEND_EXCHANGE(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), PAS_BACKEND_EXCHANGE_TYPE))
#define PAS_IS_BACKEND_EXCHANGE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), PAS_BACKEND_EXCHANGE_TYPE))

PASBackend *pas_backend_exchange_new      (void);
GType       pas_backend_exchange_get_type (void);

#endif /* ! __PAS_BACKEND_EXCHANGE_H__ */

