/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_CACHE_H__
#define __E2K_CACHE_H__

#include "e-folder-exchange.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E2K_TYPE_CACHE            (e2k_cache_get_type ())
#define E2K_CACHE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_CACHE, E2kCache))
#define E2K_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_CACHE, E2kCacheClass))
#define E2K_IS_CACHE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_CACHE))
#define E2K_IS_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_CACHE))

typedef struct _E2kCache      E2kCache;
typedef struct _E2kCacheClass E2kCacheClass;

struct _E2kCache {
	GObject parent;

	EFolder *folder;
	GHashTable *entries;
};

struct _E2kCacheClass {
	GObjectClass parent_class;
};

GType     e2k_cache_get_type (void);
E2kCache *e2k_cache_new      (EFolder *folder);

int       e2k_cache_search   (E2kCache *cache,
			      const char **props, int nprops,
			      E2kRestriction *rn, 
			      E2kResult **results, int *nresults);
void      e2k_cache_clear    (E2kCache *cache);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
