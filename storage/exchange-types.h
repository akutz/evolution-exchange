/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#include "e2k-types.h"
#include <glib-object.h>

#ifndef __EXCHANGE_TYPES_H__
#define __EXCHANGE_TYPES_H__

typedef struct _ExchangeAccount                  ExchangeAccount;
typedef struct _ExchangeAccountPrivate           ExchangeAccountPrivate;
typedef struct _ExchangeAccountClass             ExchangeAccountClass;
typedef struct _ExchangeConfigListener           ExchangeConfigListener;
typedef struct _ExchangeConfigListenerPrivate    ExchangeConfigListenerPrivate;
typedef struct _ExchangeConfigListenerClass      ExchangeConfigListenerClass;
typedef struct _ExchangeDelegatesControl         ExchangeDelegatesControl;
typedef struct _ExchangeDelegatesControlPrivate  ExchangeDelegatesControlPrivate;
typedef struct _ExchangeDelegatesControlClass    ExchangeDelegatesControlClass;
typedef struct _ExchangeHierarchy                ExchangeHierarchy;
typedef struct _ExchangeHierarchyPrivate         ExchangeHierarchyPrivate;
typedef struct _ExchangeHierarchyClass           ExchangeHierarchyClass;
typedef struct _ExchangeHierarchyForeign         ExchangeHierarchyForeign;
typedef struct _ExchangeHierarchyForeignPrivate  ExchangeHierarchyForeignPrivate;
typedef struct _ExchangeHierarchyForeignClass    ExchangeHierarchyForeignClass;
typedef struct _ExchangeHierarchyGAL             ExchangeHierarchyGAL;
typedef struct _ExchangeHierarchyGALPrivate      ExchangeHierarchyGALPrivate;
typedef struct _ExchangeHierarchyGALClass        ExchangeHierarchyGALClass;
typedef struct _ExchangeHierarchyWebDAV          ExchangeHierarchyWebDAV;
typedef struct _ExchangeHierarchyWebDAVPrivate   ExchangeHierarchyWebDAVPrivate;
typedef struct _ExchangeHierarchyWebDAVClass     ExchangeHierarchyWebDAVClass;
typedef struct _ExchangeOfflineHandler           ExchangeOfflineHandler;
typedef struct _ExchangeOfflineHandlerClass      ExchangeOfflineHandlerClass;
typedef struct _ExchangeOOFControl               ExchangeOOFControl;
typedef struct _ExchangeOOFControlPrivate        ExchangeOOFControlPrivate;
typedef struct _ExchangeOOFControlClass          ExchangeOOFControlClass;
typedef struct _ExchangePermissionsDialog        ExchangePermissionsDialog;
typedef struct _ExchangePermissionsDialogPrivate ExchangePermissionsDialogPrivate;
typedef struct _ExchangePermissionsDialogClass   ExchangePermissionsDialogClass;
typedef struct _ExchangeStorage                  ExchangeStorage;
typedef struct _ExchangeStoragePrivate           ExchangeStoragePrivate;
typedef struct _ExchangeStorageClass             ExchangeStorageClass;

typedef struct _EFolderExchange                  EFolderExchange;
typedef struct _EFolderExchangePrivate           EFolderExchangePrivate;
typedef struct _EFolderExchangeClass             EFolderExchangeClass;

struct _EvolutionShellClient;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_TYPES_H__ */
