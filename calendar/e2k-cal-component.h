/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef E2K_CAL_COMPONENT_H
#define E2K_CAL_COMPONENT_H

#include <cal-util/cal-component.h>
#include <cal-backend-exchange.h>
#include "e2k-connection.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E2K_CAL_COMPONENT_TYPE            (e2k_cal_component_get_type ())
#define E2K_CAL_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_CAL_COMPONENT_TYPE, E2kCalComponent))
#define E2K_CAL_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_CAL_COMPONENT_TYPE, E2kCalComponentClass))
#define E2K_IS_CAL_COMPONENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_CAL_COMPONENT_TYPE))
#define E2K_IS_CAL_COMPONENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E2K_CAL_COMPONENT_TYPE))

#define CAL_IS_COMPONENT(obj) IS_CAL_COMPONENT(obj)

typedef struct _E2kCalComponent      E2kCalComponent;
typedef struct _E2kCalComponentClass E2kCalComponentClass;

struct _E2kCalComponent {
	CalComponent component;

	CalBackendExchange *backend;
	char *href;
};

struct _E2kCalComponentClass {
	CalComponentClass parent_class;
};

GType            e2k_cal_component_get_type (void);
E2kCalComponent *e2k_cal_component_new (CalBackendExchange *cbex);
E2kCalComponent *e2k_cal_component_new_from_href (CalBackendExchange *cbex, const char *href);
E2kCalComponent *e2k_cal_component_new_from_props (CalBackendExchange *cbex,
						   E2kResult *result,
						   CalComponentVType vtype);
E2kCalComponent *e2k_cal_component_new_from_string (CalBackendExchange *cbex,
						    const char *href,
						    const char *body,
						    guint len);
E2kCalComponent *e2k_cal_component_new_from_cache (CalBackendExchange *cbex,
						   icalcomponent *icalcomp);
gboolean         e2k_cal_component_set_from_href (E2kCalComponent *comp, const char *href);
gboolean         e2k_cal_component_set_from_props (E2kCalComponent *comp,
						   E2kResult *result,
						   CalComponentVType vtype);
gboolean         e2k_cal_component_set_from_string (E2kCalComponent *comp,
						    const char *body,
						    guint len);

const char      *e2k_cal_component_get_href (E2kCalComponent *comp);
void             e2k_cal_component_set_href (E2kCalComponent *comp, const char *href);

int              e2k_cal_component_update (E2kCalComponent *comp,
					   icalproperty_method method,
					   gboolean new);
int              e2k_cal_component_remove (E2kCalComponent *comp);

int              e2k_cal_component_todo_update (E2kCalComponent *comp,
						gboolean new);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
