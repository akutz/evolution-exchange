/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_CONNECTION_H__
#define __E2K_CONNECTION_H__

#include <libsoup/soup-message.h>
#include <sys/time.h>

#include <glib-object.h>

#include "e2k-types.h"
#include "e2k-result.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E2K_TYPE_CONNECTION            (e2k_connection_get_type ())
#define E2K_CONNECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_CONNECTION, E2kConnection))
#define E2K_CONNECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_CONNECTION, E2kConnectionClass))
#define E2K_IS_CONNECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_CONNECTION))
#define E2K_IS_CONNECTION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_CONNECTION))

struct _E2kConnection {
	GObject parent;

	E2kConnectionPrivate *priv;
};

struct _E2kConnectionClass {
	GObjectClass parent_class;

	/* signals */
	void (*redirect) (E2kConnection *conn, int status,
			  const char *old_uri, const char *new_uri);
};

GType          e2k_connection_get_type      (void);
gboolean       e2k_connection_construct     (E2kConnection *connection,
					     const char *uri);

E2kConnection *e2k_connection_new           (const char *uri);
void           e2k_connection_set_auth      (E2kConnection *conn,
					     const char *username,
					     const char *domain,
					     const char *authmech,
					     const char *password);
gboolean       e2k_connection_fba           (E2kConnection *conn,
					     SoupMessage *failed_msg);

typedef void (*E2kGetLocalAddressCallback)      (E2kConnection *conn,
						 const char *local_ipaddr,
						 gpointer user_data);
void           e2k_connection_get_local_address (E2kConnection *conn,
						 E2kGetLocalAddressCallback callback,
						 gpointer user_data);

time_t        e2k_connection_get_last_timestamp (E2kConnection *conn);

typedef void (*E2kSimpleCallback)    (E2kConnection *, SoupMessage *,
				      gpointer user_data);
typedef void (*E2kResultsCallback)   (E2kConnection *, SoupMessage *, 
				      E2kResult *results, int nresults,
				      gpointer user_data);
typedef int  (*E2kProgressCallback)  (E2kConnection *, SoupMessage *, 
				      E2kResult *results, int nresults,
				      int first, int total,
				      gpointer user_data);

void          e2k_connection_get             (E2kConnection *conn,
					      const char *uri,
					      E2kSimpleCallback callback,
					      gpointer user_data);
int           e2k_connection_get_sync        (E2kConnection *conn,
					      const char *uri,
					      char **data, int *len);
void          e2k_connection_get_owa         (E2kConnection *conn,
					      const char *uri,
					      gboolean claim_ie,
					      E2kSimpleCallback callback,
					      gpointer user_data);
int           e2k_connection_get_owa_sync    (E2kConnection *conn,
					      const char *uri,
					      gboolean claim_ie,
					      char **data, int *len);

void          e2k_connection_put             (E2kConnection *conn,
					      const char *uri,
					      const char *content_type,
					      const char *body, int length,
					      E2kSimpleCallback callback,
					      gpointer user_data);
int           e2k_connection_put_sync        (E2kConnection *conn,
					      const char *uri,
					      const char *content_type,
					      const char *body, int length);
void          e2k_connection_append          (E2kConnection *conn,
					      const char *folder_uri,
					      const char *object_name,
					      const char *content_type,
					      const char *body, int length,
					      E2kSimpleCallback callback,
					      gpointer user_data);
void          e2k_connection_post            (E2kConnection *conn,
					      const char *uri,
					      const char *content_type,
					      const char *body, int length,
					      E2kSimpleCallback callback,
					      gpointer user_data);
int           e2k_connection_post_sync       (E2kConnection *conn,
					      const char *uri,
					      const char *content_type,
					      const char *body, int length);

void          e2k_connection_proppatch       (E2kConnection *conn,
					      const char *uri,
					      E2kProperties *props,
					      gboolean create,
					      E2kSimpleCallback callback,
					      gpointer user_data);
int           e2k_connection_proppatch_sync  (E2kConnection *conn,
					      const char *uri,
					      E2kProperties *props,
					      gboolean create);
void          e2k_connection_bproppatch      (E2kConnection *conn,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      E2kProperties *props,
					      gboolean create,
					      E2kResultsCallback callback,
					      gpointer user_data);
int           e2k_connection_bproppatch_sync (E2kConnection *conn,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      E2kProperties *props,
					      gboolean create,
					      E2kResult **results,
					      int *nresults);

void          e2k_connection_propfind        (E2kConnection *conn,
					      const char *uri,
					      const char *depth,
					      const char **props,
					      int nprops,
					      E2kResultsCallback callback,
					      gpointer user_data);
int           e2k_connection_propfind_sync   (E2kConnection *conn,
					      const char *uri,
					      const char *depth,
					      const char **props,
					      int nprops,
					      E2kResult **results,
					      int *nresults);
void          e2k_connection_bpropfind       (E2kConnection *conn,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      const char *depth,
					      const char **props,
					      int nprops,
					      E2kResultsCallback callback,
					      gpointer user_data);
int           e2k_connection_bpropfind_sync  (E2kConnection *conn,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      const char *depth,
					      const char **props,
					      int nprops,
					      E2kResult **results,
					      int *nresults);

void          e2k_connection_search          (E2kConnection *conn,
					      const char *uri,
					      const char **props,
					      int nprops,
					      gboolean folders_only,
					      E2kRestriction *rn,
					      const char *orderby,
					      E2kResultsCallback callback,
					      gpointer user_data);
int           e2k_connection_search_sync     (E2kConnection *conn,
					      const char *uri,
					      const char **props,
					      int nprops,
					      gboolean folders_only,
					      E2kRestriction *rn,
					      const char *orderby,
					      E2kResult **results,
					      int *nresults);
void          e2k_connection_search_with_progress (E2kConnection *conn,
						   const char *uri,
						   const char **props,
						   int nprops,
						   E2kRestriction *rn,
						   const char *orderby,
						   int increment_size,
						   gboolean ascending,
						   E2kProgressCallback progress_callback,
						   E2kSimpleCallback done_callback,
						   gpointer user_data);

void          e2k_connection_delete          (E2kConnection *conn,
					      const char *uri,
					      E2kSimpleCallback callback,
					      gpointer user_data);
int           e2k_connection_delete_sync     (E2kConnection *conn,
					      const char *uri);

void          e2k_connection_bdelete         (E2kConnection *conn,
					      const char *uri,
					      const char **hrefs,
					      int nhrefs,
					      E2kProgressCallback progress_callback,
					      E2kSimpleCallback done_callback,
					      gpointer user_data);

void          e2k_connection_mkcol           (E2kConnection *conn,
					      const char *uri,
					      E2kProperties *props,
					      E2kSimpleCallback callback,
					      gpointer user_data);

void          e2k_connection_transfer        (E2kConnection *conn,
					      const char *source_folder,
					      const char *dest_folder,
					      const char *source_hrefs,
					      gboolean delete_original,
					      E2kResultsCallback callback,
					      gpointer user_data);
void          e2k_connection_transfer_dir    (E2kConnection *conn,
					      const char *source_href,
					      const char *dest_href,
					      gboolean delete_original,
					      E2kSimpleCallback callback,
					      gpointer user_data);

/* Subscriptions */
typedef enum {
	E2K_CONNECTION_OBJECT_CHANGED,
	E2K_CONNECTION_OBJECT_ADDED,
	E2K_CONNECTION_OBJECT_REMOVED,
	E2K_CONNECTION_OBJECT_MOVED
} E2kConnectionChangeType;

typedef void (*E2kConnectionChangeCallback)  (E2kConnection *conn,
					      const char *uri,
					      E2kConnectionChangeType type,
					      gpointer user_data);

void          e2k_connection_subscribe       (E2kConnection *conn,
					      const char *uri,
					      E2kConnectionChangeType type,
					      int min_interval,
					      E2kConnectionChangeCallback callback,
					      gpointer user_data);
void          e2k_connection_unsubscribe     (E2kConnection *conn,
					      const char *uri);


/*
 * Utility functions
 */
SoupMessage   *e2k_soup_message_new      (E2kConnection *conn,
					  const char *uri,
					  const char *method);
SoupMessage   *e2k_soup_message_new_full (E2kConnection *conn,
					  const char *uri,
					  const char *method,
					  const char *content_type,
					  SoupOwnership owner,
					  const char *body,
					  gulong length);
void           e2k_soup_message_queue    (SoupMessage *msg, 
					  SoupCallbackFn callback, 
					  gpointer user_data);
SoupErrorClass e2k_soup_message_send     (SoupMessage *msg);


#ifdef E2K_DEBUG
void    E2K_DEBUG_HINT(char hint);
#else
#define E2K_DEBUG_HINT(hint)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_CONNECTION_H__ */
