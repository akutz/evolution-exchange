/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_RESULT_H__
#define __E2K_RESULT_H__

#include <glib.h>
#include <libsoup/soup-message.h>
#include "e2k-properties.h"

typedef struct {
	char *href;
	int status;
	E2kProperties *props;
} E2kResult;

void       e2k_results_from_multistatus           (SoupMessage  *msg,
						   E2kResult   **results,
						   int          *nresults);

E2kResult *e2k_results_copy                       (E2kResult    *results,
						   int           nresults);

void       e2k_results_free                       (E2kResult    *results,
						   int           nresults);


GArray    *e2k_results_array_new                  (void);

void       e2k_results_array_add_from_multistatus (GArray       *results_array,
						   SoupMessage  *msg);

void       e2k_results_array_free                 (GArray       *results_array,
						   gboolean      free_results);

#endif /* __E2K_RESULT_H__ */
