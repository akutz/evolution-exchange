/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_COMPONENT_H__
#define __EXCHANGE_COMPONENT_H__

#include "exchange-types.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define XC_BACKEND_IID			"OAFIID:Ximian_Connector_Backend:" BASE_VERSION
#define EXCHANGE_CALENDAR_FACTORY_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_CalFactory:" BASE_VERSION
#define EXCHANGE_ADDRESSBOOK_FACTORY_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_BookFactory:" BASE_VERSION
#define EXCHANGE_AUTOCONFIG_WIZARD_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_Startup_Wizard:" BASE_VERSION

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_COMPONENT_H__ */
