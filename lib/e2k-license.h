/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_LICENSE_H__
#define __E2K_LICENSE_H__

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

void        e2k_license_validate      (void);

const char *e2k_license_lookup_option (const char *option);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_LICENSE_H__ */
