/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* camel-exchange-provider.c: exchange provider registration code */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <camel/camel-i18n.h>
#include <camel/camel-provider.h>
#include <camel/camel-session.h>
#include <camel/camel-url.h>
#include "camel-exchange-store.h"
#include "camel-exchange-transport.h"

#include "lib/e2k-validate.h"

static guint exchange_url_hash (gconstpointer key);
static gint exchange_url_equal (gconstpointer a, gconstpointer b);

CamelProviderConfEntry exchange_conf_entries[] = {
	/* override the labels/defaults of the standard settings */
	{ CAMEL_PROVIDER_CONF_LABEL, "username", NULL,
	  /* i18n: the '_' should appear before the same letter it
	     does in the evolution:mail-config.glade "User_name"
	     translation (or not at all) */
	  N_("Windows User_name:") },

	/* extra Exchange configuration settings */
	{ CAMEL_PROVIDER_CONF_SECTION_START, "activedirectory", NULL,
	  /* i18n: GAL is an Outlookism, AD is a Windowsism */
	  N_("Global Address List / Active Directory") },
	{ CAMEL_PROVIDER_CONF_ENTRY, "ad_server", NULL,
	  /* i18n: "Global Catalog" is a Windowsism, but it's a
	     technical term and may not have translations? */
	  N_("Global Catalog server name:") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "ad_limit", NULL,
	  N_("Limit number of GAL responses: %s"), "y:1:500:10000" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_SECTION_START, "generals", NULL,
	  N_("Options") },
	{ CAMEL_PROVIDER_CONF_CHECKSPIN, "passwd_exp_warn_period", NULL,
	  N_("Password Expiry Warning period: %s"), "y:1:7:90" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "offline_sync", NULL,
	  N_("Automatically synchronize account locally"), "0" },
	{ CAMEL_PROVIDER_CONF_SECTION_END },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter", NULL,
	  /* i18n: copy from evolution:camel-imap-provider.c */
	  N_("Apply filters to new messages in Inbox on this server"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk", NULL,
	  N_("Check new messages for Junk contents"), "0" },
	{ CAMEL_PROVIDER_CONF_CHECKBOX, "filter_junk_inbox", "filter_junk",
	  N_("Only check for Junk messages in the Inbox folder"), "0" },
	{ CAMEL_PROVIDER_CONF_END }
};

typedef gboolean (CamelProviderValidateUserFunc) (CamelURL *camel_url, const char *url, gboolean *remember_password, CamelException *ex);

typedef struct {
        CamelProviderValidateUserFunc *validate_user;
}CamelProviderValidate;

static gboolean exchange_validate_user_cb (CamelURL *camel_url, const char *owa_url, gboolean *remember_password, CamelException *ex);

CamelProviderValidate validate_struct = { exchange_validate_user_cb };

static CamelProvider exchange_provider = {
	"exchange",
	N_("Microsoft Exchange"),

	N_("For handling mail (and other data) on Microsoft Exchange servers"),

	"mail",

	CAMEL_PROVIDER_IS_REMOTE | CAMEL_PROVIDER_IS_SOURCE |
	CAMEL_PROVIDER_IS_STORAGE | CAMEL_PROVIDER_IS_EXTERNAL,

	CAMEL_URL_NEED_USER, 

	exchange_conf_entries 

	/* ... */
};

CamelServiceAuthType camel_exchange_ntlm_authtype = {
	/* i18n: "Secure Password Authentication" is an Outlookism */
	N_("Secure Password"),

	/* i18n: "NTLM" probably doesn't translate */
	N_("This option will connect to the Exchange server using "
	   "secure password (NTLM) authentication."),

	"",
	TRUE
};

CamelServiceAuthType camel_exchange_password_authtype = {
	N_("Plaintext Password"),

	N_("This option will connect to the Exchange server using "
	   "standard plaintext password authentication."),

	"Basic",
	TRUE
};

static int
exchange_auto_detect_cb (CamelURL *url, GHashTable **auto_detected,
			 CamelException *ex)
{
	*auto_detected = g_hash_table_new (g_str_hash, g_str_equal);

	g_hash_table_insert (*auto_detected, g_strdup ("mailbox"),
			     g_strdup (url->user));
	g_hash_table_insert (*auto_detected, g_strdup ("pf_server"),
			     g_strdup (url->host));

	return 0;
}

static gboolean
exchange_validate_user_cb (CamelURL *camel_url, const char *owa_url, 
			   gboolean *remember_password, CamelException *ex)
{
	gboolean valid;

	valid = e2k_validate_user (owa_url, camel_url->user, 
				   &camel_url->host, remember_password);
	return valid;
}

void
camel_provider_module_init (void)
{
	exchange_provider.object_types[CAMEL_PROVIDER_STORE] = camel_exchange_store_get_type ();
	exchange_provider.object_types[CAMEL_PROVIDER_TRANSPORT] = camel_exchange_transport_get_type ();
	exchange_provider.authtypes = g_list_prepend (g_list_prepend (NULL, &camel_exchange_password_authtype), &camel_exchange_ntlm_authtype);
	exchange_provider.url_hash = exchange_url_hash;
	exchange_provider.url_equal = exchange_url_equal;
	exchange_provider.auto_detect = exchange_auto_detect_cb;

	bindtextdomain (GETTEXT_PACKAGE, CONNECTOR_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	exchange_provider.translation_domain = GETTEXT_PACKAGE;
	exchange_provider.translation_domain = GETTEXT_PACKAGE;
	exchange_provider.priv = (void *)&validate_struct;

	camel_provider_register (&exchange_provider);
}

static const char *
exchange_username (const char *user)
{
	const char *p;

	if (user) {
		p = strpbrk (user, "\\/");
		if (p)
			return p + 1;
	}

	return user;
}

static guint
exchange_url_hash (gconstpointer key)
{
	const CamelURL *u = (CamelURL *)key;
	guint hash = 0;

	if (u->user)
		hash ^= g_str_hash (exchange_username (u->user));
	if (u->host)
		hash ^= g_str_hash (u->host);

	return hash;
}

static gboolean
check_equal (const char *s1, const char *s2)
{
	if (!s1)
		return s2 == NULL;
	else if (!s2)
		return FALSE;
	else
		return strcmp (s1, s2) == 0;
}

static gint
exchange_url_equal (gconstpointer a, gconstpointer b)
{
	const CamelURL *u1 = a, *u2 = b;

	return  check_equal (u1->protocol, u2->protocol) &&
		check_equal (exchange_username (u1->user),
			     exchange_username (u2->user)) &&
		check_equal (u1->host, u2->host);
}
