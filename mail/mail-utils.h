/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __MAIL_UTILS_H__
#define __MAIL_UTILS_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include "e2k-connection.h"

char    *mail_util_mapi_to_smtp_headers (E2kProperties *props);

GString *mail_util_stickynote_to_rfc822 (E2kProperties *props);

guint32  mail_util_props_to_camel_flags (E2kProperties *props,
					 gboolean       obey_read_flag);

char *   mail_util_extract_transport_headers (E2kProperties *props);

gboolean mail_util_demangle_delegated_meeting (GByteArray *body,
					       const char *delegator_cn,
					       const char *delegator_email,
					       const char *delegator_cal_uri);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MAIL_UTILS_H__ */
