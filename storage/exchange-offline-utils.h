#ifndef _EXCHANGE_OFFLINE_UTILS_H_
#define _EXCHANGE_OFFLINE_UTILS_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib-object.h>
//#include <ctype.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
#include "e2k-uri.h"


G_BEGIN_DECLS

char * exchange_offline_build_object_cache_file (E2kUri *e2kuri, const char *filename, gboolean has_relative_uri);

G_END_DECLS
#endif
