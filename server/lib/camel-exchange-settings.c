/*
 * camel-exchange-settings.c
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

#include "camel-exchange-settings.h"

#include "e2k-enumtypes.h"

#define CAMEL_EXCHANGE_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_EXCHANGE_SETTINGS, CamelExchangeSettingsPrivate))

#define GC_RESULTS_LIMIT_MIN 1
#define GC_RESULTS_LIMIT_MAX 10000

#define PASSWD_EXP_WARN_PERIOD_MIN 1
#define PASSWD_EXP_WARN_PERIOD_MAX 90

struct _CamelExchangeSettingsPrivate {
	gchar *mailbox;
	gchar *owa_path;
	gchar *owa_url;

	gboolean check_all;
	gboolean filter_junk;
	gboolean filter_junk_inbox;
	gboolean use_passwd_exp_warn_period;

	guint passwd_exp_warn_period;

	/* Global Catalog settings */
	gboolean gc_allow_browse;
	E2kAutoconfigGalAuthPref gc_auth_method;
	gboolean gc_expand_groups;
	guint gc_results_limit;
	gchar *gc_server_name;
	gboolean use_gc_results_limit;
};

enum {
	PROP_0,
	PROP_AUTH_MECHANISM,
	PROP_CHECK_ALL,
	PROP_FILTER_JUNK,
	PROP_FILTER_JUNK_INBOX,
	PROP_GC_ALLOW_BROWSE,
	PROP_GC_AUTH_METHOD,
	PROP_GC_EXPAND_GROUPS,
	PROP_GC_RESULTS_LIMIT,
	PROP_GC_SERVER_NAME,
	PROP_HOST,
	PROP_MAILBOX,
	PROP_OWA_PATH,
	PROP_OWA_URL,
	PROP_PASSWD_EXP_WARN_PERIOD,
	PROP_PORT,
	PROP_SECURITY_METHOD,
	PROP_USER,
	PROP_USE_GC_RESULTS_LIMIT,
	PROP_USE_PASSWD_EXP_WARN_PERIOD
};

G_DEFINE_TYPE_WITH_CODE (
	CamelExchangeSettings,
	camel_exchange_settings,
	CAMEL_TYPE_OFFLINE_SETTINGS,
	G_IMPLEMENT_INTERFACE (
		CAMEL_TYPE_NETWORK_SETTINGS, NULL))

static void
exchange_settings_set_property (GObject *object,
                                guint property_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTH_MECHANISM:
			camel_network_settings_set_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_CHECK_ALL:
			camel_exchange_settings_set_check_all (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK:
			camel_exchange_settings_set_filter_junk (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_FILTER_JUNK_INBOX:
			camel_exchange_settings_set_filter_junk_inbox (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_GC_ALLOW_BROWSE:
			camel_exchange_settings_set_gc_allow_browse (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_GC_AUTH_METHOD:
			camel_exchange_settings_set_gc_auth_method (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_GC_EXPAND_GROUPS:
			camel_exchange_settings_set_gc_expand_groups (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_GC_RESULTS_LIMIT:
			camel_exchange_settings_set_gc_results_limit (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_GC_SERVER_NAME:
			camel_exchange_settings_set_gc_server_name (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_HOST:
			camel_network_settings_set_host (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_MAILBOX:
			camel_exchange_settings_set_mailbox (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OWA_PATH:
			camel_exchange_settings_set_owa_path (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_OWA_URL:
			camel_exchange_settings_set_owa_url (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_PASSWD_EXP_WARN_PERIOD:
			camel_exchange_settings_set_passwd_exp_warn_period (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_PORT:
			camel_network_settings_set_port (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_uint (value));
			return;

		case PROP_SECURITY_METHOD:
			camel_network_settings_set_security_method (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_enum (value));
			return;

		case PROP_USER:
			camel_network_settings_set_user (
				CAMEL_NETWORK_SETTINGS (object),
				g_value_get_string (value));
			return;

		case PROP_USE_GC_RESULTS_LIMIT:
			camel_exchange_settings_set_use_gc_results_limit (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;

		case PROP_USE_PASSWD_EXP_WARN_PERIOD:
			camel_exchange_settings_set_use_passwd_exp_warn_period (
				CAMEL_EXCHANGE_SETTINGS (object),
				g_value_get_boolean (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
exchange_settings_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AUTH_MECHANISM:
			g_value_set_string (
				value,
				camel_network_settings_get_auth_mechanism (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_CHECK_ALL:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_check_all (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_filter_junk (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_FILTER_JUNK_INBOX:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_filter_junk_inbox (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_GC_ALLOW_BROWSE:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_gc_allow_browse (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_GC_AUTH_METHOD:
			g_value_set_enum (
				value,
				camel_exchange_settings_get_gc_auth_method (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_GC_EXPAND_GROUPS:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_gc_expand_groups (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_GC_RESULTS_LIMIT:
			g_value_set_uint (
				value,
				camel_exchange_settings_get_gc_results_limit (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_GC_SERVER_NAME:
			g_value_set_string (
				value,
				camel_exchange_settings_get_gc_server_name (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_HOST:
			g_value_set_string (
				value,
				camel_network_settings_get_host (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_MAILBOX:
			g_value_set_string (
				value,
				camel_exchange_settings_get_mailbox (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_OWA_PATH:
			g_value_set_string (
				value,
				camel_exchange_settings_get_owa_path (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_OWA_URL:
			g_value_set_string (
				value,
				camel_exchange_settings_get_owa_url (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_PASSWD_EXP_WARN_PERIOD:
			g_value_set_uint (
				value,
				camel_exchange_settings_get_passwd_exp_warn_period (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_PORT:
			g_value_set_uint (
				value,
				camel_network_settings_get_port (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_SECURITY_METHOD:
			g_value_set_enum (
				value,
				camel_network_settings_get_security_method (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USER:
			g_value_set_string (
				value,
				camel_network_settings_get_user (
				CAMEL_NETWORK_SETTINGS (object)));
			return;

		case PROP_USE_GC_RESULTS_LIMIT:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_use_gc_results_limit (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;

		case PROP_USE_PASSWD_EXP_WARN_PERIOD:
			g_value_set_boolean (
				value,
				camel_exchange_settings_get_use_passwd_exp_warn_period (
				CAMEL_EXCHANGE_SETTINGS (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
exchange_settings_finalize (GObject *object)
{
	CamelExchangeSettingsPrivate *priv;

	priv = CAMEL_EXCHANGE_SETTINGS_GET_PRIVATE (object);

	g_free (priv->gc_server_name);
	g_free (priv->mailbox);
	g_free (priv->owa_path);
	g_free (priv->owa_url);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_exchange_settings_parent_class)->finalize (object);
}

static void
camel_exchange_settings_class_init (CamelExchangeSettingsClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelExchangeSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = exchange_settings_set_property;
	object_class->get_property = exchange_settings_get_property;
	object_class->finalize = exchange_settings_finalize;

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_AUTH_MECHANISM,
		"auth-mechanism");

	g_object_class_install_property (
		object_class,
		PROP_CHECK_ALL,
		g_param_spec_boolean (
			"check-all",
			"Check All",
			"Check all folders for new messages",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK,
		g_param_spec_boolean (
			"filter-junk",
			"Filter Junk",
			"Whether to filter junk from all folders",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_FILTER_JUNK_INBOX,
		g_param_spec_boolean (
			"filter-junk-inbox",
			"Filter Junk Inbox",
			"Whether to filter junk from Inbox only",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_GC_ALLOW_BROWSE,
		g_param_spec_boolean (
			"gc-allow-browse",
			"Global Catalog Allow Browse",
			"Allow browsing until results limit is reached",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_GC_AUTH_METHOD,
		g_param_spec_enum (
			"gc-auth-method",
			"Global Catalog Auth Method",
			"Global Catalog authentication method",
			E_TYPE_2K_AUTOCONFIG_GAL_AUTH_PREF,
			E2K_AUTOCONFIG_USE_GAL_DEFAULT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_GC_EXPAND_GROUPS,
		g_param_spec_boolean (
			"gc-expand-groups",
			"Global Catalog Expand Groups",
			"Expand groups of contacts in GC to contact lists",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_GC_RESULTS_LIMIT,
		g_param_spec_uint (
			"gc-results-limit",
			"Global Catalog Results Limit",
			"Limit for Global Catalog query results",
			GC_RESULTS_LIMIT_MIN,
			GC_RESULTS_LIMIT_MAX,
			500,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_GC_SERVER_NAME,
		g_param_spec_string (
			"gc-server-name",
			"Global Catalog Server Name",
			"Global Catalog server name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_HOST,
		"host");

	g_object_class_install_property (
		object_class,
		PROP_MAILBOX,
		g_param_spec_string (
			"mailbox",
			"Mailbox",
			"Exchange mailbox name",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OWA_PATH,
		g_param_spec_string (
			"owa-path",
			"OWA Path",
			"URL path to Outlook Web Access",
			"/exchange",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_OWA_URL,
		g_param_spec_string (
			"owa-url",
			"OWA URL",
			"URL to Outlook Web Access",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_PASSWD_EXP_WARN_PERIOD,
		g_param_spec_uint (
			"passwd-exp-warn-period",
			"Password Exp Warn Period",
			"Password expiry warning period",
			PASSWD_EXP_WARN_PERIOD_MIN,
			PASSWD_EXP_WARN_PERIOD_MAX,
			7,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_PORT,
		"port");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_SECURITY_METHOD,
		"security-method");

	/* Inherited from CamelNetworkSettings. */
	g_object_class_override_property (
		object_class,
		PROP_USER,
		"user");

	g_object_class_install_property (
		object_class,
		PROP_USE_GC_RESULTS_LIMIT,
		g_param_spec_boolean (
			"use-gc-results-limit",
			"Use Global Catalog Results Limit",
			"Whether to impose a limit on GC query results",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USE_PASSWD_EXP_WARN_PERIOD,
		g_param_spec_boolean (
			"use-passwd-exp-warn-period",
			"Use Password Exp Warn Period",
			"Whether to warn in advance of expiring passwords",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_exchange_settings_init (CamelExchangeSettings *settings)
{
	settings->priv = CAMEL_EXCHANGE_SETTINGS_GET_PRIVATE (settings);
}

gboolean
camel_exchange_settings_get_check_all (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->check_all;
}

void
camel_exchange_settings_set_check_all (CamelExchangeSettings *settings,
                                       gboolean check_all)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->check_all = check_all;

	g_object_notify (G_OBJECT (settings), "check-all");
}

gboolean
camel_exchange_settings_get_filter_junk (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk;
}

void
camel_exchange_settings_set_filter_junk (CamelExchangeSettings *settings,
                                         gboolean filter_junk)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->filter_junk = filter_junk;

	g_object_notify (G_OBJECT (settings), "filter-junk");
}

gboolean
camel_exchange_settings_get_filter_junk_inbox (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->filter_junk_inbox;
}

void
camel_exchange_settings_set_filter_junk_inbox (CamelExchangeSettings *settings,
                                               gboolean filter_junk_inbox)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->filter_junk_inbox = filter_junk_inbox;

	g_object_notify (G_OBJECT (settings), "filter-junk-inbox");
}

gboolean
camel_exchange_settings_get_gc_allow_browse (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->gc_allow_browse;
}

void
camel_exchange_settings_set_gc_allow_browse (CamelExchangeSettings *settings,
                                             gboolean gc_allow_browse)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->gc_allow_browse = gc_allow_browse;

	g_object_notify (G_OBJECT (settings), "gc-allow-browse");
}

E2kAutoconfigGalAuthPref
camel_exchange_settings_get_gc_auth_method (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (
		CAMEL_IS_EXCHANGE_SETTINGS (settings),
		E2K_AUTOCONFIG_USE_GAL_DEFAULT);

	return settings->priv->gc_auth_method;
}

void
camel_exchange_settings_set_gc_auth_method (CamelExchangeSettings *settings,
                                            E2kAutoconfigGalAuthPref gc_auth_method)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->gc_auth_method = gc_auth_method;

	g_object_notify (G_OBJECT (settings), "gc-auth-method");
}

gboolean
camel_exchange_settings_get_gc_expand_groups (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->gc_expand_groups;
}

void
camel_exchange_settings_set_gc_expand_groups (CamelExchangeSettings *settings,
                                              gboolean gc_expand_groups)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->gc_expand_groups = gc_expand_groups;

	g_object_notify (G_OBJECT (settings), "gc-expand-groups");
}

guint
camel_exchange_settings_get_gc_results_limit (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), 0);

	return settings->priv->gc_results_limit;
}

void
camel_exchange_settings_set_gc_results_limit (CamelExchangeSettings *settings,
                                              guint gc_results_limit)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->gc_results_limit = CLAMP (
		gc_results_limit,
		GC_RESULTS_LIMIT_MIN,
		GC_RESULTS_LIMIT_MAX);

	g_object_notify (G_OBJECT (settings), "gc-results-limit");
}

const gchar *
camel_exchange_settings_get_gc_server_name (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), NULL);

	return settings->priv->gc_server_name;
}

void
camel_exchange_settings_set_gc_server_name (CamelExchangeSettings *settings,
                                            const gchar *gc_server_name)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	/* The value should never be NULL. */
	if (gc_server_name == NULL)
		gc_server_name = "";

	g_free (settings->priv->gc_server_name);
	settings->priv->gc_server_name = g_strdup (gc_server_name);

	g_object_notify (G_OBJECT (settings), "gc-server-name");
}

const gchar *
camel_exchange_settings_get_mailbox (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), NULL);

	return settings->priv->mailbox;
}

void
camel_exchange_settings_set_mailbox (CamelExchangeSettings *settings,
                                     const gchar *mailbox)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	/* The value should never be NULL. */
	if (mailbox == NULL)
		mailbox = "";

	g_free (settings->priv->mailbox);
	settings->priv->mailbox = g_strdup (mailbox);

	g_object_notify (G_OBJECT (settings), "mailbox");
}

const gchar *
camel_exchange_settings_get_owa_path (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), NULL);

	return settings->priv->owa_path;
}

void
camel_exchange_settings_set_owa_path (CamelExchangeSettings *settings,
                                      const gchar *owa_path)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	/* The value should never be NULL. */
	if (owa_path == NULL)
		owa_path = "";

	g_free (settings->priv->owa_path);
	settings->priv->owa_path = g_strdup (owa_path);

	g_object_notify (G_OBJECT (settings), "owa-path");
}

const gchar *
camel_exchange_settings_get_owa_url (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), NULL);

	return settings->priv->owa_url;
}

void
camel_exchange_settings_set_owa_url (CamelExchangeSettings *settings,
                                     const gchar *owa_url)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	/* The value should never be NULL. */
	if (owa_url == NULL)
		owa_url = "";

	g_free (settings->priv->owa_url);
	settings->priv->owa_url = g_strdup (owa_url);

	g_object_notify (G_OBJECT (settings), "owa-url");
}

guint
camel_exchange_settings_get_passwd_exp_warn_period (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), 0);

	return settings->priv->passwd_exp_warn_period;
}

void
camel_exchange_settings_set_passwd_exp_warn_period (CamelExchangeSettings *settings,
                                                    guint passwd_exp_warn_period)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->passwd_exp_warn_period = CLAMP (
		passwd_exp_warn_period,
		PASSWD_EXP_WARN_PERIOD_MIN,
		PASSWD_EXP_WARN_PERIOD_MAX);

	g_object_notify (G_OBJECT (settings), "passwd-exp-warn-period");
}

gboolean
camel_exchange_settings_get_use_gc_results_limit (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->use_gc_results_limit;
}

void
camel_exchange_settings_set_use_gc_results_limit (CamelExchangeSettings *settings,
                                                  gboolean use_gc_results_limit)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->use_gc_results_limit = use_gc_results_limit;

	g_object_notify (G_OBJECT (settings), "use-gc-results-limit");
}

gboolean
camel_exchange_settings_get_use_passwd_exp_warn_period (CamelExchangeSettings *settings)
{
	g_return_val_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings), FALSE);

	return settings->priv->use_passwd_exp_warn_period;
}

void
camel_exchange_settings_set_use_passwd_exp_warn_period (CamelExchangeSettings *settings,
                                                        gboolean use_passwd_exp_warn_period)
{
	g_return_if_fail (CAMEL_IS_EXCHANGE_SETTINGS (settings));

	settings->priv->use_passwd_exp_warn_period = use_passwd_exp_warn_period;

	g_object_notify (G_OBJECT (settings), "use-passwd-exp-warn-period");
}
