/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_TYPES_H__
#define __E2K_TYPES_H__

#include <glib.h>
#include <glib/gi18n.h>

typedef struct _E2kAction                     E2kAction;
typedef struct _E2kAddrEntry                  E2kAddrEntry;
typedef struct _E2kAddrList                   E2kAddrList;

typedef struct _E2kContext                    E2kContext;
typedef struct _E2kContextPrivate             E2kContextPrivate;
typedef struct _E2kContextClass               E2kContextClass;

typedef struct _E2kGlobalCatalog              E2kGlobalCatalog;
typedef struct _E2kGlobalCatalogPrivate       E2kGlobalCatalogPrivate;
typedef struct _E2kGlobalCatalogClass         E2kGlobalCatalogClass;

typedef struct _E2kOperation                  E2kOperation;

typedef struct _E2kRestriction                E2kRestriction;

typedef struct _E2kSecurityDescriptor         E2kSecurityDescriptor;
typedef struct _E2kSecurityDescriptorPrivate  E2kSecurityDescriptorPrivate;
typedef struct _E2kSecurityDescriptorClass    E2kSecurityDescriptorClass;

typedef struct _E2kSid                        E2kSid;
typedef struct _E2kSidPrivate                 E2kSidPrivate;
typedef struct _E2kSidClass                   E2kSidClass;

/* Put "E2K_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES;" on a line to
 * separate a _() from a comment that doesn't go with it.
 */
#define E2K_KEEP_PRECEDING_COMMENT_OUT_OF_PO_FILES

#endif
