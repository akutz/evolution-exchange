/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */


#ifndef CAMEL_EXCHANGE_STORE_H
#define CAMEL_EXCHANGE_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <camel/camel-disco-store.h>
#include "camel-stub.h"

#define CAMEL_EXCHANGE_STORE_TYPE     (camel_exchange_store_get_type ())
#define CAMEL_EXCHANGE_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_EXCHANGE_STORE_TYPE, CamelExchangeStore))
#define CAMEL_EXCHANGE_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_EXCHANGE_STORE_TYPE, CamelExchangeStoreClass))
#define CAMEL_IS_EXCHANGE_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_EXCHANGE_STORE_TYPE))

typedef struct {
	CamelDiscoStore parent_object;

	CamelStub *stub;
	char *storage_path, *base_url;
	char *trash_name;
	GHashTable *folders;

	guint note_idle;
} CamelExchangeStore;

typedef struct {
	CamelDiscoStoreClass parent_class;

} CamelExchangeStoreClass;


/* Standard Camel function */
CamelType camel_exchange_store_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_EXCHANGE_STORE_H */
