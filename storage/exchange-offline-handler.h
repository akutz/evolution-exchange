/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef _EXCHANGE_OFFLINE_HANDLER_H_
#define _EXCHANGE_OFFLINE_HANDLER_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <bonobo/bonobo-object.h>
#include <shell/Evolution.h>
#include "exchange-types.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_OFFLINE_HANDLER			(exchange_offline_handler_get_type ())
#define EXCHANGE_OFFLINE_HANDLER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_OFFLINE_HANDLER, ExchangeOfflineHandler))
#define EXCHANGE_OFFLINE_HANDLER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_OFFLINE_HANDLER, ExchangeOfflineHandlerClass))
#define EXCHANGE_IS_OFFLINE_HANDLER(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_OFFLINE_HANDLER))
#define EXCHANGE_IS_OFFLINE_HANDLER_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_OFFLINE_HANDLER))

struct _ExchangeOfflineHandler {
	BonoboObject parent;

};

struct _ExchangeOfflineHandlerClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Offline__epv epv;
};

GType                   exchange_offline_handler_get_type (void);
ExchangeOfflineHandler *exchange_offline_handler_new      (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _EXCHANGE_OFFLINE_HANDLER_H_ */
