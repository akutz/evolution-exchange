/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_CONFIG_LISTENER_H__
#define __EXCHANGE_CONFIG_LISTENER_H__

#include "exchange-types.h"
#include <e-util/e-account-list.h>
#include <libedataserver/e-source-list.h>
#include <libedataserver/e-source-group.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_CONFIG_LISTENER            (exchange_config_listener_get_type ())
#define EXCHANGE_CONFIG_LISTENER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_CONFIG_LISTENER, ExchangeConfigListener))
#define EXCHANGE_CONFIG_LISTENER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_CONFIG_LISTENER, ExchangeConfigListenerClass))
#define EXCHANGE_IS_CONFIG_LISTENER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_CONFIG_LISTENER))
#define EXCHANGE_IS_CONFIG_LISTENER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_CONFIG_LISTENER))

struct _ExchangeConfigListener {
	EAccountList parent;

	ExchangeConfigListenerPrivate *priv;
};

struct _ExchangeConfigListenerClass {
	EAccountListClass parent_class;

	/* signals */
	void (*exchange_account_created) (ExchangeConfigListener *,
					  ExchangeAccount *);
	void (*exchange_account_removed) (ExchangeConfigListener *,
					  ExchangeAccount *);
};

GType                   exchange_config_listener_get_type (void);
ExchangeConfigListener *exchange_config_listener_new      (void);

GSList                 *exchange_config_listener_get_accounts (ExchangeConfigListener *config_listener);

void 			add_esource (ExchangeAccount *account, char *conf_key, const char *folder_name, const char *physical_uri, ESourceList **source_list, gboolean is_contact_folder);
void 			remove_esource (ExchangeAccount *account, char *conf_key, const char *physical_uri, ESourceList **source_list, gboolean is_account, gboolean is_contact_folder);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_CONFIG_LISTENER_H__ */
