/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef CAL_BACKEND_EXCHANGE_H
#define CAL_BACKEND_EXCHANGE_H

#include <pcs/cal-backend.h>
#include <pcs/cal-backend-util.h>
#include <e2k-cache.h>
#include <e2k-connection.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define CAL_BACKEND_EXCHANGE_TYPE            (cal_backend_exchange_get_type ())
#define CAL_BACKEND_EXCHANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CAL_BACKEND_EXCHANGE_TYPE,	CalBackendExchange))
#define CAL_BACKEND_EXCHANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CAL_BACKEND_EXCHANGE_TYPE,	CalBackendExchangeClass))
#define CAL_IS_BACKEND_EXCHANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CAL_BACKEND_EXCHANGE_TYPE))
#define CAL_IS_BACKEND_EXCHANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CAL_BACKEND_EXCHANGE_TYPE))

typedef struct _CalBackendExchange        CalBackendExchange;
typedef struct _CalBackendExchangeClass   CalBackendExchangeClass;

typedef struct _CalBackendExchangePrivate CalBackendExchangePrivate;

struct _CalBackendExchange {
	CalBackend backend;
	E2kConnection *connection;

	/* Private data */
	CalBackendExchangePrivate *priv;
};

struct _CalBackendExchangeClass {
	CalBackendClass parent_class;
};

GType   cal_backend_exchange_get_type (void);

void    cal_backend_exchange_add_timezone (CalBackendExchange *cbex,
					   icalcomponent *vtzcomp);

void    cal_backend_exchange_save (CalBackendExchange *cbex);

/*
 * Utility functions
 */
char                 *calcomponentdatetime_to_string (CalComponentDateTime *dt,
						      icaltimezone *izone);
CalComponentDateTime *calcomponentdatetime_from_string (const char *timestamp,
							icaltimezone *izone);
const char           *calcomponenttransparency_to_string (CalComponentTransparency *transp);

struct icaltimetype   icaltime_from_e2k_time (const char *timestamp);
char                 *icaltime_to_e2k_time (struct icaltimetype *itt);
icaltimezone         *get_default_timezone (void);

const char *cal_backend_exchange_get_cal_address (CalBackend *backend);
const char *cal_backend_exchange_get_cal_owner (CalBackend *backend);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
