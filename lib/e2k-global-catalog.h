/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_GLOBAL_CATALOG_H__
#define __E2K_GLOBAL_CATALOG_H__

#include <glib-object.h>
#include "e2k-types.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E2K_TYPE_GLOBAL_CATALOG            (e2k_global_catalog_get_type ())
#define E2K_GLOBAL_CATALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_GLOBAL_CATALOG, E2kGlobalCatalog))
#define E2K_GLOBAL_CATALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_GLOBAL_CATALOG, E2kGlobalCatalogClass))
#define E2K_IS_GLOBAL_CATALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_GLOBAL_CATALOG))
#define E2K_IS_GLOBAL_CATALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_GLOBAL_CATALOG))

struct _E2kGlobalCatalog {
	GObject parent;

	int response_limit;

	E2kGlobalCatalogPrivate *priv;
};

struct _E2kGlobalCatalogClass {
	GObjectClass parent_class;

};

GType             e2k_global_catalog_get_type        (void);
E2kGlobalCatalog *e2k_global_catalog_new             (const char *server,
						      int response_limit,
						      const char *user,
						      const char *domain,
						      const char *password);

/* This returns an LDAP *, but we don't want to #include <ldap.h> here. */
gpointer          e2k_global_catalog_get_ldap        (E2kGlobalCatalog *gc);

typedef enum {
	E2K_GLOBAL_CATALOG_OK,
	E2K_GLOBAL_CATALOG_NO_SUCH_USER,
	E2K_GLOBAL_CATALOG_NO_DATA,
	E2K_GLOBAL_CATALOG_BAD_DATA,
	E2K_GLOBAL_CATALOG_EXISTS,
	E2K_GLOBAL_CATALOG_ERROR
} E2kGlobalCatalogStatus;

typedef enum {
	E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
	E2K_GLOBAL_CATALOG_LOOKUP_BY_DN,
	E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN
} E2kGlobalCatalogLookupType;

#define E2K_GLOBAL_CATALOG_LOOKUP_SID                (1 << 0)
#define E2K_GLOBAL_CATALOG_LOOKUP_EMAIL              (1 << 1)
#define E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX            (1 << 2)
#define E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN (1 << 3)
#define E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES          (1 << 4)
#define E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS         (1 << 5)

typedef struct {
	char *dn, *display_name;
	E2kSid *sid;
	char *email, *exchange_server, *mailbox, *legacy_exchange_dn;
	GPtrArray *delegates, *delegators;
} E2kGlobalCatalogEntry;

typedef gpointer E2kGlobalCatalogLookupId;

E2kGlobalCatalogStatus e2k_global_catalog_lookup       (E2kGlobalCatalog *gc,
							E2kGlobalCatalogLookupType type,
							const char *key,
							guint32 lookup_flags,
							E2kGlobalCatalogEntry **entry);

#define e2k_global_catalog_entry_free(gc, entry)

typedef void         (*E2kGlobalCatalogCallback)       (E2kGlobalCatalog *,
							E2kGlobalCatalogStatus,
							E2kGlobalCatalogEntry *,
							gpointer user_data);

E2kGlobalCatalogLookupId e2k_global_catalog_async_lookup  (E2kGlobalCatalog *gc,
							   E2kGlobalCatalogLookupType type,
							   const char *key,
							   guint32 lookup_flags,
							   E2kGlobalCatalogCallback,
							   gpointer user_data);

void                     e2k_global_catalog_cancel_lookup (E2kGlobalCatalog *gc,
							   gpointer lookup_id);

E2kGlobalCatalogStatus e2k_global_catalog_add_delegate    (E2kGlobalCatalog *gc,
							   const char *self_dn,
							   const char *delegate_dn);
E2kGlobalCatalogStatus e2k_global_catalog_remove_delegate (E2kGlobalCatalog *gc,
							   const char *self_dn,
							   const char *delegate_dn);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_GLOBAL_CATALOG_H__ */
