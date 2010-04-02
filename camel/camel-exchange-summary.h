/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef _CAMEL_EXCHANGE_SUMMARY_H
#define _CAMEL_EXCHANGE_SUMMARY_H

#include <camel/camel.h>

#define CAMEL_EXCHANGE_SUMMARY_TYPE         (camel_exchange_summary_get_type ())
#define CAMEL_EXCHANGE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_exchange_summary_get_type (), CamelExchangeSummary)
#define CAMEL_EXCHANGE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_exchange_summary_get_type (), CamelExchangeSummaryClass)
#define CAMEL_IS_EXCHANGE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_exchange_summary_get_type ())

typedef struct _CamelExchangeSummary CamelExchangeSummary;
typedef struct _CamelExchangeSummaryClass CamelExchangeSummaryClass;

typedef struct _CamelExchangeMessageInfo {
	CamelMessageInfoBase info;

	gchar *thread_index;
	gchar *href;
} CamelExchangeMessageInfo;

struct _CamelExchangeSummary {
	CamelFolderSummary parent;

	gboolean readonly;
	guint32 high_article_num;
	guint32 version;
};

struct _CamelExchangeSummaryClass {
	CamelFolderSummaryClass parent_class;

};

CamelType           camel_exchange_summary_get_type          (void);
CamelFolderSummary *camel_exchange_summary_new               (struct _CamelFolder *folder, const gchar         *filename);

gboolean            camel_exchange_summary_get_readonly      (CamelFolderSummary *summary);
void                camel_exchange_summary_set_readonly      (CamelFolderSummary *summary,
							      gboolean            readonly);

void                camel_exchange_summary_add_offline       (CamelFolderSummary *summary,
							      const gchar         *uid,
							      CamelMimeMessage   *message,
							      CamelMessageInfo   *info);
void                camel_exchange_summary_add_offline_uncached (CamelFolderSummary *summary,
								 const gchar         *uid,
								 CamelMessageInfo   *info);

guint32             camel_exchange_summary_get_article_num      (CamelFolderSummary *summary);
void                camel_exchange_summary_set_article_num      (CamelFolderSummary *summary,
								 guint32            high_article_num);

#endif /* _CAMEL_EXCHANGE_SUMMARY_H */

