/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __XC_COMMANDS_H__
#define __XC_COMMANDS_H__

#include "exchange-types.h"
#include "e-storage-set-view.h"
#include <bonobo/bonobo-control.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

void xc_commands_activate     (XCBackendView   *view);
void xc_commands_deactivate   (XCBackendView   *view);

void xc_commands_context_menu (EStorageSetView *view,
			       EFolder         *folder,
			       GdkEvent        *event);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __XC_COMMANDS_H__ */
