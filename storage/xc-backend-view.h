/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __XC_BACKEND_VIEW_H__
#define __XC_BACKEND_VIEW_H__

#include "exchange-types.h"

#include <bonobo/bonobo-control.h>
#include <bonobo/Bonobo.h>
#include "e-storage-set-view.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define XC_TYPE_BACKEND_VIEW               (xc_backend_view_get_type ())
#define XC_BACKEND_VIEW(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), XC_TYPE_BACKEND_VIEW, XCBackendView))
#define XC_BACKEND_VIEW_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), XC_TYPE_BACKEND_VIEW, XCBackendViewClass))
#define XC_IS_BACKEND_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XC_TYPE_BACKEND_VIEW))
#define XC_IS_BACKEND_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), XC_TYPE_BACKEND_VIEW))

struct XCBackendView {
	BonoboControl parent;

	XCBackendViewPrivate *priv;
};

struct XCBackendViewClass {
	BonoboControlClass parent_class;

};

GType            xc_backend_view_get_type             (void);

BonoboControl   *xc_backend_view_new                  (EStorageSet   *storage_set);

EStorageSetView *xc_backend_view_get_storage_set_view (XCBackendView *view);
EFolder         *xc_backend_view_get_selected_folder  (XCBackendView *view);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __XC_BACKEND_VIEW_H__ */
