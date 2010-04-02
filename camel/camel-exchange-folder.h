/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

/* camel-exchange-folder.h: class for a exchange folder */

#ifndef CAMEL_EXCHANGE_FOLDER_H
#define CAMEL_EXCHANGE_FOLDER_H 1

#include <camel/camel.h>

#define CAMEL_EXCHANGE_FOLDER_TYPE     (camel_exchange_folder_get_type ())
#define CAMEL_EXCHANGE_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_EXCHANGE_FOLDER_TYPE, CamelExchangeFolder))
#define CAMEL_EXCHANGE_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_EXCHANGE_FOLDER_TYPE, CamelExchangeFolderClass))
#define CAMEL_IS_EXCHANGE_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_EXCHANGE_FOLDER_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelOfflineFolder parent_object;

	CamelDataCache *cache;
	CamelOfflineJournal *journal;
	gchar *source;

	GHashTable *thread_index_to_message_id;
} CamelExchangeFolder;

typedef struct {
	CamelOfflineFolderClass parent_class;

} CamelExchangeFolderClass;

/* Standard Camel function */
CamelType camel_exchange_folder_get_type (void);

gboolean camel_exchange_folder_construct            (CamelFolder *folder,
						     CamelStore *parent,
						     const gchar *name,
						     guint32 camel_flags,
						     const gchar *folder_dir,
						     gint offline_state,
						     CamelException *ex);

void     camel_exchange_folder_add_message          (CamelExchangeFolder *exch,
						     const gchar *uid,
						     guint32 flags,
						     guint32 size,
						     const gchar *headers,
						     const gchar *href);

void     camel_exchange_folder_remove_message       (CamelExchangeFolder *exch,
						     const gchar *uid);

void     camel_exchange_folder_uncache_message      (CamelExchangeFolder *exch,
						     const gchar *uid);

void     camel_exchange_folder_update_message_flags (CamelExchangeFolder *exch,
						     const gchar *uid,
						     guint32 flags);

void     camel_exchange_folder_update_message_flags_ex (CamelExchangeFolder *exch,
							const gchar *uid,
							guint32 flags,
							guint32 mask);

void     camel_exchange_folder_update_message_tag   (CamelExchangeFolder *exch,
						     const gchar *uid,
						     const gchar *name,
						     const gchar *value);

G_END_DECLS

#endif /* CAMEL_EXCHANGE_FOLDER_H */

