/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_COMPONENT_H__
#define __EXCHANGE_COMPONENT_H__

#include "exchange-types.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

ExchangeAccount *exchange_component_get_account_for_uri (const char *uri);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_COMPONENT_H__ */
