/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef __PAS_BACKEND_AD_H__
#define __PAS_BACKEND_AD_H__

#include "pas/pas-backend.h"

typedef struct _PASBackendADPrivate PASBackendADPrivate;

typedef struct {
	PASBackend             parent_object;
	PASBackendADPrivate *priv;
} PASBackendAD;

typedef struct {
	PASBackendClass parent_class;
} PASBackendADClass;

PASBackend *pas_backend_ad_new      (void);
GType       pas_backend_ad_get_type (void);

#define PAS_BACKEND_AD_TYPE        (pas_backend_ad_get_type ())
#define PAS_BACKEND_AD(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), PAS_BACKEND_AD_TYPE, PASBackendAD))
#define PAS_BACKEND_AD_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), PAS_BACKEND_TYPE, PASBackendADClass))
#define PAS_IS_BACKEND_AD(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), PAS_BACKEND_AD_TYPE))
#define PAS_IS_BACKEND_AD_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), PAS_BACKEND_AD_TYPE))

#endif /* ! __PAS_BACKEND_AD_H__ */

