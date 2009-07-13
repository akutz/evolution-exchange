/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

/* camel-exchange-transport.h: Exchange-based transport class */

#ifndef CAMEL_EXCHANGE_TRANSPORT_H
#define CAMEL_EXCHANGE_TRANSPORT_H 1

G_BEGIN_DECLS

#include <camel/camel-transport.h>

#define CAMEL_EXCHANGE_TRANSPORT_TYPE     (camel_exchange_transport_get_type ())
#define CAMEL_EXCHANGE_TRANSPORT(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_EXCHANGE_TRANSPORT_TYPE, CamelExchangeTransport))
#define CAMEL_EXCHANGE_TRANSPORT_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_EXCHANGE_TRANSPORT_TYPE, CamelExchangeTransportClass))
#define CAMEL_IS_EXCHANGE_TRANSPORT(o)    (CAMEL_CHECK_TYPE((o), CAMEL_EXCHANGE_TRANSPORT_TYPE))

typedef struct {
	CamelTransport parent_object;

} CamelExchangeTransport;

typedef struct {
	CamelTransportClass parent_class;

} CamelExchangeTransportClass;

/* Standard Camel function */
CamelType camel_exchange_transport_get_type (void);

G_END_DECLS

#endif /* CAMEL_EXCHANGE_TRANSPORT_H */
