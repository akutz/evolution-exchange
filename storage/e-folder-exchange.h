/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E_FOLDER_EXCHANGE_H__
#define __E_FOLDER_EXCHANGE_H__

#include <shell/e-folder.h>
#include "exchange-types.h"
#include "e2k-connection.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_FOLDER_EXCHANGE            (e_folder_exchange_get_type ())
#define E_FOLDER_EXCHANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_FOLDER_EXCHANGE, EFolderExchange))
#define E_FOLDER_EXCHANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_FOLDER_EXCHANGE, EFolderExchangeClass))
#define E_IS_FOLDER_EXCHANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_FOLDER_EXCHANGE))
#define E_IS_FOLDER_EXCHANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_FOLDER_EXCHANGE))

struct _EFolderExchange {
	EFolder parent;

	EFolderExchangePrivate *priv;
};

struct _EFolderExchangeClass {
	EFolderClass parent_class;

};

GType       e_folder_exchange_get_type      (void);

EFolder    *e_folder_exchange_new           (ExchangeHierarchy     *hier,
					     const char            *name,
					     const char            *type,
					     const char            *outlook_class,
					     const char            *phys_uri,
					     const char            *int_uri);

EFolder    *e_folder_exchange_new_from_file (ExchangeHierarchy     *hier,
					     const char            *filename);
gboolean    e_folder_exchange_save_to_file  (EFolder               *folder,
					     const char            *filename);


const char *e_folder_exchange_get_internal_uri     (EFolder    *folder);
void        e_folder_exchange_set_internal_uri     (EFolder    *folder,
						    const char *internal_uri);

const char *e_folder_exchange_get_path             (EFolder    *folder);

gboolean    e_folder_exchange_get_has_subfolders   (EFolder    *folder);
void        e_folder_exchange_set_has_subfolders   (EFolder    *folder,
						    gboolean   has_subfolders);

const char *e_folder_exchange_get_outlook_class    (EFolder    *folder);

char       *e_folder_exchange_get_storage_file     (EFolder    *folder,
						    const char *filename);

ExchangeHierarchy *e_folder_exchange_get_hierarchy (EFolder    *folder);


/* E2kConnection wrappers */
void        e_folder_exchange_propfind             (EFolder *folder,
						    const char *depth,
						    const char **props,
						    int nprops,
						    E2kResultsCallback,
						    gpointer user_data);
int         e_folder_exchange_propfind_sync        (EFolder *folder,
						    const char *depth,
						    const char **props,
						    int nprops,
						    E2kResult **results,
						    int *nresults);
void        e_folder_exchange_bpropfind            (EFolder *folder,
						    const char **hrefs,
						    int nhrefs,
						    const char *depth,
						    const char **props,
						    int nprops,
						    E2kResultsCallback,
						    gpointer user_data);
int         e_folder_exchange_bpropfind_sync       (EFolder *folder,
						    const char **hrefs,
						    int nhrefs,
						    const char *depth,
						    const char **props,
						    int nprops,
						    E2kResult **results,
						    int *nresults);

void        e_folder_exchange_search               (EFolder *folder,
						    const char **props,
						    int nprops,
						    gboolean folders_only,
						    E2kRestriction *rn,
						    const char *orderby,
						    E2kResultsCallback,
						    gpointer user_data);
int         e_folder_exchange_search_sync          (EFolder *folder,
						    const char **props,
						    int nprops,
						    gboolean folders_only,
						    E2kRestriction *rn,
						    const char *orderby,
						    E2kResult **results,
						    int *nresults);
void        e_folder_exchange_search_with_progress (EFolder *folder,
						    const char **props,
						    int nprops,
						    E2kRestriction *rn,
						    const char *orderby,
						    int increment_size,
						    gboolean ascending,
						    E2kProgressCallback progress_callback,
						    E2kSimpleCallback done_callback,
						    gpointer user_data);

void        e_folder_exchange_subscribe            (EFolder *folder,
						    E2kConnectionChangeType,
						    int min_interval,
						    E2kConnectionChangeCallback,
						    gpointer user_data);
void        e_folder_exchange_unsubscribe          (EFolder *folder);


void        e_folder_exchange_transfer             (EFolder *source,
						    EFolder *dest,
						    const char *source_hrefs,
						    gboolean delete_original,
						    E2kResultsCallback,
						    gpointer user_data);

void        e_folder_exchange_append               (EFolder *folder,
						    const char *object_name,
						    const char *content_type,
						    const char *body,
						    int length,
						    E2kSimpleCallback,
						    gpointer user_data);

void        e_folder_exchange_bproppatch           (EFolder *folder,
						    const char **hrefs,
						    int nhrefs,
						    E2kProperties *props,
						    gboolean create,
						    E2kResultsCallback,
						    gpointer user_data);

void        e_folder_exchange_bdelete              (EFolder *folder,
						    const char **hrefs,
						    int nhrefs,
						    E2kProgressCallback,
						    E2kSimpleCallback,
						    gpointer user_data);

void        e_folder_exchange_mkcol                (EFolder *folder,
						    E2kProperties *props,
						    E2kSimpleCallback callback,
						    gpointer user_data);
void        e_folder_exchange_delete               (EFolder *folder,
						    E2kSimpleCallback callback,
						    gpointer user_data);
void        e_folder_exchange_transfer_dir         (EFolder *source,
						    EFolder *dest,
						    gboolean delete_original,
						    E2kSimpleCallback callback,
						    gpointer user_data);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E_FOLDER_EXCHANGE_H__ */
