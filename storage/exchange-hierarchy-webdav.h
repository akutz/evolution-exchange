/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_WEBDAV_H__
#define __EXCHANGE_HIERARCHY_WEBDAV_H__

#include "exchange-hierarchy.h"
#include "e2k-connection.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_HIERARCHY_WEBDAV            (exchange_hierarchy_webdav_get_type ())
#define EXCHANGE_HIERARCHY_WEBDAV(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY_WEBDAV, ExchangeHierarchyWebDAV))
#define EXCHANGE_HIERARCHY_WEBDAV_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY_WEBDAV, ExchangeHierarchyWebDAVClass))
#define EXCHANGE_IS_HIERARCHY_WEBDAV(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_WEBDAV))
#define EXCHANGE_IS_HIERARCHY_WEBDAV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_WEBDAV))

struct _ExchangeHierarchyWebDAV {
	ExchangeHierarchy parent;

	ExchangeHierarchyWebDAVPrivate *priv;
};

struct _ExchangeHierarchyWebDAVClass {
	ExchangeHierarchyClass parent_class;

};

GType              exchange_hierarchy_webdav_get_type (void);

ExchangeHierarchy *exchange_hierarchy_webdav_new (ExchangeAccount *account,
						  ExchangeHierarchyType type,
						  const char *hierarchy_name,
						  const char *physical_uri_prefix,
						  const char *internal_uri_prefix,
						  const char *owner_name,
						  const char *owner_email,
						  const char *source_uri,
						  gboolean deep_searchable,
						  const char *toplevel_icon,
						  int sorting_priority);

/* for subclasses */
ExchangeAccountFolderResult exchange_hierarchy_webdav_parse_folders (
						  ExchangeHierarchyWebDAV *hwd,
						  SoupMessage *msg,
						  EFolder *parent,
						  E2kResult *results,
						  int nresults,
						  GPtrArray **folders_p);

void exchange_hierarchy_webdav_construct   (ExchangeHierarchyWebDAV *hwd,
					    ExchangeAccount         *account,
					    ExchangeHierarchyType    type,
					    const char              *hierarchy_name,
					    const char              *physical_uri_prefix,
					    const char              *internal_uri_prefix,
					    const char              *owner_name,
					    const char              *owner_email,
					    const char              *source_uri,
					    gboolean                 deep_searchable,
					    const char              *toplevel_icon,
					    int                      sorting_priority);

typedef void (*ExchangeHierarchyWebDAVScanCallback)    (ExchangeHierarchy                   *hier,
							EFolder                             *folder,
							gpointer                             user_data);
void    exchange_hierarchy_webdav_offline_scan_subtree (ExchangeHierarchy                   *hier,
							ExchangeHierarchyWebDAVScanCallback  cb,
							gpointer                             user_data);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_HIERARCHY_WEBDAV_H__ */
