/*
 * camel-exchange-settings.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef CAMEL_EXCHANGE_SETTINGS_H
#define CAMEL_EXCHANGE_SETTINGS_H

#include <camel/camel.h>
#include "e2k-enums.h"

/* Standard GObject macros */
#define CAMEL_TYPE_EXCHANGE_SETTINGS \
	(camel_exchange_settings_get_type ())
#define CAMEL_EXCHANGE_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_EXCHANGE_SETTINGS, CamelExchangeSettings))
#define CAMEL_EXCHANGE_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_EXCHANGE_SETTINGS, CamelExchangeSettingsClass))
#define CAMEL_IS_EXCHANGE_SETTINGS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_EXCHANGE_SETTINGS))
#define CAMEL_IS_EXCHANGE_SETTINGS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_EXCHANGE_SETTINGS))
#define CAMEL_EXCHANGE_SETTINGS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_EXCHANGE_SETTINGS, CamelExchangeSettingsClass))

G_BEGIN_DECLS

typedef struct _CamelExchangeSettings CamelExchangeSettings;
typedef struct _CamelExchangeSettingsClass CamelExchangeSettingsClass;
typedef struct _CamelExchangeSettingsPrivate CamelExchangeSettingsPrivate;

struct _CamelExchangeSettings {
	CamelOfflineSettings parent;
	CamelExchangeSettingsPrivate *priv;
};

struct _CamelExchangeSettingsClass {
	CamelOfflineSettingsClass parent_class;
};

GType		camel_exchange_settings_get_type
				(void) G_GNUC_CONST;
gboolean	camel_exchange_settings_get_check_all
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_check_all
					(CamelExchangeSettings *settings,
					 gboolean check_all);
gboolean	camel_exchange_settings_get_filter_junk
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_filter_junk
					(CamelExchangeSettings *settings,
					 gboolean filter_junk);
gboolean	camel_exchange_settings_get_filter_junk_inbox
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_filter_junk_inbox
					(CamelExchangeSettings *settings,
					 gboolean filter_junk_inbox);
gboolean	camel_exchange_settings_get_gc_allow_browse
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_gc_allow_browse
					(CamelExchangeSettings *settings,
					 gboolean gc_allow_browse);
E2kAutoconfigGalAuthPref
		camel_exchange_settings_get_gc_auth_method
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_gc_auth_method
					(CamelExchangeSettings *settings,
					 E2kAutoconfigGalAuthPref gc_auth_method);
gboolean	camel_exchange_settings_get_gc_expand_groups
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_gc_expand_groups
					(CamelExchangeSettings *settings,
					 gboolean gc_expand_groups);
guint		camel_exchange_settings_get_gc_results_limit
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_gc_results_limit
					(CamelExchangeSettings *settings,
					 guint gc_results_limit);
const gchar *	camel_exchange_settings_get_gc_server_name
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_gc_server_name
					(CamelExchangeSettings *settings,
					 const gchar *gc_server_name);
const gchar *	camel_exchange_settings_get_mailbox
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_mailbox
					(CamelExchangeSettings *settings,
					 const gchar *mailbox);
const gchar *	camel_exchange_settings_get_owa_path
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_owa_path
					(CamelExchangeSettings *settings,
					 const gchar *owa_path);
const gchar *	camel_exchange_settings_get_owa_url
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_owa_url
					(CamelExchangeSettings *settings,
					 const gchar *owa_url);
guint		camel_exchange_settings_get_passwd_exp_warn_period
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_passwd_exp_warn_period
					(CamelExchangeSettings *settings,
					 guint passwd_exp_warn_period);
gboolean	camel_exchange_settings_get_use_gc_results_limit
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_use_gc_results_limit
					(CamelExchangeSettings *settings,
					 gboolean use_gc_results_limit);
gboolean	camel_exchange_settings_get_use_passwd_exp_warn_period
					(CamelExchangeSettings *settings);
void		camel_exchange_settings_set_use_passwd_exp_warn_period
					(CamelExchangeSettings *settings,
					 gboolean use_passwd_exp_warn_period);

G_END_DECLS

#endif /* CAMEL_EXCHANGE_SETTINGS_H */
