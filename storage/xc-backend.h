/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __XC_BACKEND_H__
#define __XC_BACKEND_H__

#include <bonobo/bonobo-object.h>
#include <shell/Evolution.h>

#include "exchange-account.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define XC_TYPE_BACKEND               (xc_backend_get_type ())
#define XC_BACKEND(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), XC_TYPE_BACKEND, XCBackend))
#define XC_BACKEND_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), XC_TYPE_BACKEND, XCBackendClass))
#define XC_IS_BACKEND(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XC_TYPE_BACKEND))
#define XC_IS_BACKEND_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), XC_TYPE_BACKEND))

struct XCBackend {
	BonoboObject parent;

	XCBackendPrivate *priv;
};

struct XCBackendClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Component__epv epv;
};

extern XCBackend *global_backend;

GType            xc_backend_get_type            (void);

XCBackend       *xc_backend_new                 (void);

ExchangeAccount *xc_backend_get_account_for_uri (XCBackend  *backend,
						 const char *uri);
gboolean         xc_backend_is_interactive      (XCBackend  *backend);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __XC_BACKEND_H__ */
