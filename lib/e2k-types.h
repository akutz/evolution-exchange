/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_TYPES_H__
#define __E2K_TYPES_H__

#include <glib/gtypes.h>
#include <bonobo/bonobo-i18n.h>

typedef struct _E2kConnection                 E2kConnection;
typedef struct _E2kConnectionPrivate          E2kConnectionPrivate;
typedef struct _E2kConnectionClass            E2kConnectionClass;

typedef struct _E2kGlobalCatalog              E2kGlobalCatalog;
typedef struct _E2kGlobalCatalogPrivate       E2kGlobalCatalogPrivate;
typedef struct _E2kGlobalCatalogClass         E2kGlobalCatalogClass;

typedef struct _E2kRestriction                E2kRestriction;

typedef struct _E2kSecurityDescriptor         E2kSecurityDescriptor;
typedef struct _E2kSecurityDescriptorPrivate  E2kSecurityDescriptorPrivate;
typedef struct _E2kSecurityDescriptorClass    E2kSecurityDescriptorClass;

typedef struct _E2kSid                        E2kSid;
typedef struct _E2kSidPrivate                 E2kSidPrivate;
typedef struct _E2kSidClass                   E2kSidClass;

#define E2K_MAKE_TYPE(l,t,ci,i,parent) \
GType l##_get_type(void)\
{\
	static GType type = 0;				\
	if (!type){					\
		static GTypeInfo const object_info = {	\
			sizeof (t##Class),		\
							\
			(GBaseInitFunc) NULL,		\
			(GBaseFinalizeFunc) NULL,	\
							\
			(GClassInitFunc) ci,		\
			(GClassFinalizeFunc) NULL,	\
			NULL,	/* class_data */	\
							\
			sizeof (t),			\
			0,	/* n_preallocs */	\
			(GInstanceInitFunc) i,		\
		};					\
		type = g_type_register_static (parent, #t, &object_info, 0); \
	}						\
	return type;					\
}

/* Put "E2K_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES;" on a line to
 * separate a _() from a comment that doesn't go with it.
 */
#define E2K_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES

#endif
