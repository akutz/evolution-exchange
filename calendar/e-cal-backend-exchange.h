/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef E_CAL_BACKEND_EXCHANGE_H
#define E_CAL_BACKEND_EXCHANGE_H

#include <libedata-cal/e-cal-backend-sync.h>
#include <libedataserver/e-xml-hash-utils.h>

#include "exchange-types.h"
#include "e-folder.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_EXCHANGE            (e_cal_backend_exchange_get_type ())
#define E_CAL_BACKEND_EXCHANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_EXCHANGE, ECalBackendExchange))
#define E_CAL_BACKEND_EXCHANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_EXCHANGE, ECalBackendExchangeClass))
#define E_IS_CAL_BACKEND_EXCHANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_EXCHANGE))
#define E_IS_CAL_BACKEND_EXCHANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_EXCHANGE))
#define E_CAL_BACKEND_EXCHANGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CAL_BACKEND_EXCHANGE, ECalBackendExchangeClass))

typedef struct ECalBackendExchange ECalBackendExchange;
typedef struct ECalBackendExchangeClass ECalBackendExchangeClass;

typedef struct ECalBackendExchangeComponent ECalBackendExchangeComponent;

typedef struct ECalBackendExchangePrivate ECalBackendExchangePrivate;

struct ECalBackendExchange {
	ECalBackendSync parent;

	ECalBackendExchangePrivate *priv;

	ExchangeAccount *account;
	EFolder *folder;
	E2kRestriction *private_item_restriction;

};

struct ECalBackendExchangeClass {
	ECalBackendSyncClass parent_class;

};

struct ECalBackendExchangeComponent {
	char *uid, *href, *lastmod;
	icalcomponent *comp;
	GList *instances;
};

GType     e_cal_backend_exchange_get_type         (void);

void      e_cal_backend_exchange_cache_sync_start (ECalBackendExchange *cbex);
gboolean  e_cal_backend_exchange_in_cache         (ECalBackendExchange *cbex,
						   const char          *uid,
						   const char          *lastmod);
void      e_cal_backend_exchange_cache_sync_end   (ECalBackendExchange *cbex);


gboolean  e_cal_backend_exchange_add_object       (ECalBackendExchange *cbex,
						   const char          *href,
						   const char          *lastmod,
						   icalcomponent       *comp);
gboolean  e_cal_backend_exchange_modify_object    (ECalBackendExchange *cbex,
						   icalcomponent       *comp,
						   CalObjModType mod);
gboolean  e_cal_backend_exchange_remove_object    (ECalBackendExchange *cbex,
						   const char          *uid);

gboolean e_cal_backend_exchange_receive_objects (ECalBackendExchange *cbex, 
						 EDataCal *cal,
						 const char *calobj);

gboolean  e_cal_backend_exchange_add_timezone     (ECalBackendExchange *cbex,
						   icalcomponent       *vtzcomp);
						   
icaltimezone * e_cal_backend_exchange_get_default_time_zone (ECalBackendSync *backend);

char *	  e_cal_backend_exchange_lf_to_crlf 	(const char *in);
char *	  e_cal_backend_exchange_make_timestamp_rfc822 	(time_t when);
char *	  e_cal_backend_exchange_get_from_string (ECalBackendSync *backend, 
							ECalComponent *comp);
ECalBackendSyncStatus	get_timezone 	(ECalBackendSync *backend, 
							EDataCal *cal, const char *tzid, char **object);

ECalBackendExchangeComponent * get_exchange_comp (ECalBackendExchange *cbex, 
												const char *uid);

/* Utility functions */
                                                                                
const char *e_cal_backend_exchange_get_cal_address (ECalBackendSync *backend);
const char *e_cal_backend_exchange_get_cal_owner (ECalBackendSync *backend);

G_END_DECLS

#endif
