/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef E2K_UTILS_H
#define E2K_UTILS_H

#include <glib.h>
#include <time.h>
#include "e2k-connection.h"

time_t e2k_parse_timestamp       (const char *timestamp);
char  *e2k_make_timestamp        (time_t when);
char  *e2k_make_timestamp_rfc822 (time_t when);

time_t e2k_systime_to_time_t     (long systime);
long   e2k_systime_from_time_t   (time_t tt);

time_t e2k_parse_http_date       (const char *date);

char *e2k_lf_to_crlf (const char *in);
char *e2k_crlf_to_lf (const char *in);

char *e2k_strdup_with_trailing_slash (const char *path);

const char *e2k_entryid_to_dn (GByteArray *entryid);

gint  e2k_ascii_strcase_equal (gconstpointer v,
			       gconstpointer v2);
guint e2k_ascii_strcase_hash  (gconstpointer v);

const char *e2k_get_accept_language (void);

#endif /* E2K_UTILS_H */
