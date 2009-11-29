/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

/* camel-exchange-search.h: exchange folder search */

#ifndef _CAMEL_EXCHANGE_SEARCH_H
#define _CAMEL_EXCHANGE_SEARCH_H

#include <camel/camel.h>

#define CAMEL_EXCHANGE_SEARCH_TYPE         (camel_exchange_search_get_type ())
#define CAMEL_EXCHANGE_SEARCH(obj)         CAMEL_CHECK_CAST (obj, camel_exchange_search_get_type (), CamelExchangeSearch)
#define CAMEL_EXCHANGE_SEARCH_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_exchange_search_get_type (), CamelExchangeSearchClass)
#define CAMEL_IS_EXCHANGE_SEARCH(obj)      CAMEL_CHECK_TYPE (obj, camel_exchange_search_get_type ())

typedef struct _CamelExchangeSearchClass CamelExchangeSearchClass;
typedef struct _CamelExchangeSearch CamelExchangeSearch;

struct _CamelExchangeSearch {
	CamelFolderSearch parent;

};

struct _CamelExchangeSearchClass {
	CamelFolderSearchClass parent_class;

};

CamelType          camel_exchange_search_get_type (void);
CamelFolderSearch *camel_exchange_search_new      (void);

#endif /* ! _CAMEL_EXCHANGE_SEARCH_H */
