/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __XC_BACKEND_COMPONENT_H__
#define __XC_BACKEND_COMPONENT_H__

#include "exchange-types.h"
#include <bonobo/bonobo-object.h>
#include <shell/Evolution.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define XC_TYPE_BACKEND_COMPONENT               (xc_backend_component_get_type ())
#define XC_BACKEND_COMPONENT(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), XC_TYPE_BACKEND_COMPONENT, XCBackendComponent))
#define XC_BACKEND_COMPONENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), XC_TYPE_BACKEND_COMPONENT, XCBackendComponentClass))
#define XC_IS_BACKEND_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XC_TYPE_BACKEND_COMPONENT))
#define XC_IS_BACKEND_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), XC_TYPE_BACKEND_COMPONENT))

struct XCBackendComponent {
	BonoboObject parent;

	XCBackendComponentPrivate *priv;
};

struct XCBackendComponentClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Component__epv epv;

	/* signals */
	gboolean (*upgrade_from_version) (XCBackendComponent *component,
					  int major, int minor, int revision);
	gboolean (*request_quit)         (XCBackendComponent *component);
	gboolean (*quit)                 (XCBackendComponent *component);
	void     (*interactive)          (XCBackendComponent *component,
					  gboolean now_interactive,
					  gulong new_view_xid);
};

GType               xc_backend_component_get_type (void);
XCBackendComponent *xc_backend_component_new      (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __XC_BACKEND_COMPONENT_H__ */
