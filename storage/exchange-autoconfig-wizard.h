/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __EXCHANGE_AUTOCONFIG_WIZARD_H__
#define __EXCHANGE_AUTOCONFIG_WIZARD_H__

#include "exchange-types.h"

#include <bonobo/bonobo-object.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

BonoboObject *exchange_autoconfig_wizard_new (void);

void          exchange_autoconfig_druid_run  (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_AUTOCONFIG_WIZARD_H__ */
