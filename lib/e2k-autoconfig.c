/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

/* e2k-autoconfig: Automatic account configuration backend code */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <resolv.h>
#include <arpa/nameser.h>

#include "e2k-autoconfig.h"
#include "e2k-encoding-utils.h"
#include "e2k-connection.h"
#include "e2k-global-catalog.h"
#include "e2k-license.h"
#include "e2k-propnames.h"
#include "e2k-uri.h"
#include "e2k-utils.h"
#include "e2k-xml-utils.h"
#include "xntlm.h"

#include <e-util/e-account.h>
#include <e-util/e-account-list.h>
#include <gconf/gconf-client.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>

static char *find_olson_timezone (const char *windows_timezone);
static void set_account_uri_string (E2kAutoconfig *ac);

/**
 * e2k_autoconfig_new:
 * @owa_uri: the OWA URI, or %NULL to (try to) use a default
 * @username: the username (or DOMAIN\\username), or %NULL to use a default
 * @password: the password, or %NULL if not yet known
 * @require_ntlm: whether or not to require ntlm
 *
 * Creates an autoconfig context, based on information stored in the
 * license file or provided as arguments.
 *
 * Return value: an autoconfig context
 **/
E2kAutoconfig *
e2k_autoconfig_new (const char *owa_uri, const char *username,
		    const char *password, gboolean require_ntlm)
{
	E2kAutoconfig *ac;

	ac = g_new0 (E2kAutoconfig, 1);

	e2k_autoconfig_set_owa_uri (ac, owa_uri);
	e2k_autoconfig_set_gc_server (ac, NULL);
	e2k_autoconfig_set_username (ac, username);
	e2k_autoconfig_set_password (ac, password);

	ac->ad_limit = g_strdup (e2k_license_lookup_option ("GAL-Limit"));
	ac->require_ntlm = require_ntlm ||
		e2k_license_lookup_option ("Disable-Plaintext");

	return ac;
}

/**
 * e2k_autoconfig_free:
 * @ac: an autoconfig context
 *
 * Frees @ac.
 **/
void
e2k_autoconfig_free (E2kAutoconfig *ac)
{
	g_free (ac->owa_uri);
	g_free (ac->gc_server);
	g_free (ac->username);
	g_free (ac->password);
	g_free (ac->display_name);
	g_free (ac->email);
	g_free (ac->account_uri);
	g_free (ac->exchange_server);
	g_free (ac->timezone);
	g_free (ac->nt_domain);
	g_free (ac->w2k_domain);
	g_free (ac->ad_limit);
	g_free (ac->home_uri);
	g_free (ac->exchange_dn);
	g_free (ac->pf_server);

	g_free (ac);
}

static void
reset_gc_derived (E2kAutoconfig *ac)
{
	if (ac->display_name) {
		g_free (ac->display_name);
		ac->display_name = NULL;
	}
	if (ac->email) {
		g_free (ac->email);
		ac->email = NULL;
	}
	if (ac->account_uri) {
		g_free (ac->account_uri);
		ac->account_uri = NULL;
	}
}

static void
reset_owa_derived (E2kAutoconfig *ac)
{
	/* Clear the information we explicitly get from OWA */
	if (ac->timezone) {
		g_free (ac->timezone);
		ac->timezone = NULL;
	}
	if (ac->exchange_dn) {
		g_free (ac->exchange_dn);
		ac->exchange_dn = NULL;
	}
	if (ac->pf_server) {
		g_free (ac->pf_server);
		ac->pf_server = NULL;
	}
	if (ac->home_uri) {
		g_free (ac->home_uri);
		ac->home_uri = NULL;
	}

	/* Reset domain info we may have implicitly got */
	ac->use_ntlm = TRUE;
	if (ac->nt_domain)
		g_free (ac->nt_domain);
	ac->nt_domain = g_strdup (e2k_license_lookup_option ("NT-Domain"));
	if (ac->w2k_domain)
		g_free (ac->w2k_domain);
	ac->w2k_domain = g_strdup (e2k_license_lookup_option ("Domain"));

	/* Reset GC-derived information since it depends on the
	 * OWA-derived information too.
	 */
	reset_gc_derived (ac);
}

/**
 * e2k_autoconfig_set_owa_uri:
 * @ac: an autoconfig context
 * @owa_uri: the new OWA URI, or %NULL
 *
 * Sets @ac's #owa_uri field to @owa_uri (or the default if @owa_uri is
 * %NULL), and resets any fields whose values had been set based on
 * the old value of #owa_uri.
 **/
void
e2k_autoconfig_set_owa_uri (E2kAutoconfig *ac, const char *owa_uri)
{
	reset_owa_derived (ac);
	if (ac->gc_server_autodetected)
		e2k_autoconfig_set_gc_server (ac, NULL);
	g_free (ac->owa_uri);

	if (owa_uri) {
		if (!strncmp (owa_uri, "http", 4))
			ac->owa_uri = g_strdup (owa_uri);
		else
			ac->owa_uri = g_strdup_printf ("http://%s", owa_uri);
	} else
		ac->owa_uri = g_strdup (e2k_license_lookup_option ("OWA-URL"));
}

/**
 * e2k_autoconfig_set_gc_server:
 * @ac: an autoconfig context
 * @owa_uri: the new GC server, or %NULL
 *
 * Sets @ac's #gc_server field to @gc_server (or the default if
 * @gc_server is %NULL), and resets any fields whose values had been
 * set based on the old value of #gc_server.
 **/
void
e2k_autoconfig_set_gc_server (E2kAutoconfig *ac, const char *gc_server)
{
	reset_gc_derived (ac);
	g_free (ac->gc_server);

	if (gc_server)
		ac->gc_server = g_strdup (gc_server);
	else
		ac->gc_server = g_strdup (e2k_license_lookup_option ("Global-Catalog"));
	ac->gc_server_autodetected = FALSE;
}

/**
 * e2k_autoconfig_set_username:
 * @ac: an autoconfig context
 * @username: the new username (or DOMAIN\username), or %NULL
 *
 * Sets @ac's #username field to @username (or the default if
 * @username is %NULL), and resets any fields whose values had been
 * set based on the old value of #username.
 **/
void
e2k_autoconfig_set_username (E2kAutoconfig *ac, const char *username)
{
	int dlen;

	reset_owa_derived (ac);
	g_free (ac->username);

	if (username) {
		/* If the username includes a domain name, split it out */
		dlen = strcspn (username, "/\\");
		if (username[dlen]) {
			g_free (ac->nt_domain);
			ac->nt_domain = g_strndup (username, dlen);
			ac->username = g_strdup (username + dlen + 1);
		} else
			ac->username = g_strdup (username);
	} else
		ac->username = g_strdup (g_get_user_name ());
}

/**
 * e2k_autoconfig_set_password:
 * @ac: an autoconfig context
 * @password: the new password, or %NULL to clear
 *
 * Sets or clears @ac's #password field.
 **/
void
e2k_autoconfig_set_password (E2kAutoconfig *ac, const char *password)
{
	g_free (ac->password);
	ac->password = g_strdup (password);
}


static void
get_conn_auth_handler (SoupMessage *msg, gpointer user_data)
{
	E2kAutoconfig *ac = user_data;
	const GSList *headers;
	const char *challenge_hdr;
	GByteArray *challenge;

	headers = soup_message_get_header_list (msg->response_headers,
						"WWW-Authenticate");
	while (headers) {
		challenge_hdr = headers->data;

		if (!strcmp (challenge_hdr, "NTLM"))
			ac->saw_ntlm = TRUE;
		else if (!strncmp (challenge_hdr, "Basic ", 6))
			ac->saw_basic = TRUE;

		if (!strncmp (challenge_hdr, "NTLM ", 5) && !ac->w2k_domain) {
			challenge = e2k_base64_decode (challenge_hdr + 5);
			xntlm_parse_challenge (challenge->data, challenge->len,
					       NULL, NULL, &ac->w2k_domain);
			g_byte_array_free (challenge, TRUE);
			return;
		}

		headers = headers->next;
	}
}

/**
 * e2k_autoconfig_get_connection:
 * @ac: an autoconfig context
 * @result: on output, a result code
 *
 * Checks if @ac's URI and authentication parameters work, and if so
 * returns an #E2kConnection using them. On return, *@result (which
 * may not be %NULL) will contain a result code as follows:
 *
 *   %E2K_AUTOCONFIG_OK: success
 *   %E2K_AUTOCONFIG_REDIRECT: The server issued a valid-looking
 *     redirect. @ac->owa_uri has been updated and the caller
 *     should try again.
 *   %E2K_AUTOCONFIG_TRY_SSL: The server requires SSL.
 *     @ac->owa_uri has been updated and the caller should try
 *     again.
 *   %E2K_AUTOCONFIG_AUTH_ERROR: Generic authentication failure.
 *     Probably password incorrect
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN: Authentication failed.
 *     Including an NT domain with the username (or using NTLM)
 *     may fix the problem.
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC: Caller requested NTLM
 *     auth, but only Basic was available.
 *   %E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM: Caller requested Basic
 *     auth, but only NTLM was available.
 *   %E2K_AUTOCONFIG_EXCHANGE_5_5: Server appears to be Exchange 5.5.
 *   %E2K_AUTOCONFIG_NOT_EXCHANGE: Server does not appear to be
 *     any version of Exchange
 *   %E2K_AUTOCONFIG_NO_OWA: Server may be Exchange 2000, but OWA
 *     is not present at the given URL.
 *   %E2K_AUTOCONFIG_NO_MAILBOX: OWA claims the user has no mailbox.
 *   %E2K_AUTOCONFIG_NETWORK_ERROR: A network error occurred.
 *     (usually could not connect or could not resolve hostname).
 *   %E2K_AUTOCONFIG_FAILED: Other error.
 *
 * Return value: the new connection, or %NULL
 **/
E2kConnection *
e2k_autoconfig_get_connection (E2kAutoconfig *ac, E2kAutoconfigResult *result)
{
	E2kConnection *conn;
	gboolean use_ntlm;
	SoupMessage *msg;
	int status;
	const char *ms_webstorage;
	xmlDoc *doc;
	xmlNode *node;
	xmlChar *equiv, *content, *href;

	use_ntlm = ac->require_ntlm || ac->use_ntlm;
	conn = e2k_connection_new (ac->owa_uri);
	if (!conn) {
		*result = E2K_AUTOCONFIG_FAILED;
		return NULL;
	}
	e2k_connection_set_auth (conn, ac->username, ac->nt_domain,
				 use_ntlm ? "NTLM" : "Basic", ac->password);

	msg = e2k_soup_message_new (conn, ac->owa_uri, SOUP_METHOD_GET);
	soup_message_add_header (msg->request_headers, "Accept-Language",
				 e2k_get_accept_language ());
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	soup_message_add_error_code_handler (msg, SOUP_ERROR_UNAUTHORIZED,
					     SOUP_HANDLER_PRE_BODY,
					     get_conn_auth_handler, ac);

 try_again:
	e2k_soup_message_send (msg);
	/* (The soup response doesn't end in '\0', but we know that
	 * truncating the document won't lose any actual data, and
	 * this lets us use strstr below.)
	 */
	if (msg->response.length > 0)
		msg->response.body[msg->response.length - 1] = '\0';

	status = msg->errorcode;

	/* Check for an authentication failure. This could be because
	 * the password is incorrect, or because we used Basic auth
	 * without specifying a domain and the server doesn't have a
	 * default domain, or because we tried to use an auth type the
	 * server doesn't allow.
	 */
	if (status == SOUP_ERROR_CANT_AUTHENTICATE) {
		if (!use_ntlm && !ac->nt_domain)
			*result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN;
		else if (use_ntlm && !ac->saw_ntlm)
			*result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC;
		else if (!use_ntlm && !ac->saw_basic)
			*result = E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM;
		else
			*result = E2K_AUTOCONFIG_AUTH_ERROR;
		goto done;
	}

	/* Check for transport error. (We do this after auth errors
	 * because soup calls "can't authenticate" a transport error.)
	 */
	if (SOUP_ERROR_IS_TRANSPORT (status)) {
		*result = E2K_AUTOCONFIG_NETWORK_ERROR;
		goto done;
	}

	/* A redirection to "logon.asp" means this is Exchange 5.5
	 * OWA. A redirection to "owalogon.asp" means this is Exchange
	 * 2003 forms-based authentication. Other redirections most
	 * likely indicate that the user's mailbox has been moved to a
	 * new server.
	 */
	if (SOUP_ERROR_IS_REDIRECTION (status)) {
		const char *location;
		char *new_uri;

		location = soup_message_get_header (msg->response_headers,
						   "Location");
		if (!location) {
			*result = E2K_AUTOCONFIG_FAILED;
			goto done;
		}

		if (strstr (location, "/logon.asp")) {
			*result = E2K_AUTOCONFIG_EXCHANGE_5_5;
			goto done;
		} else if (strstr (location, "/owalogon.asp")) {
			if (e2k_connection_fba (conn, msg))
				goto try_again;
			*result = E2K_AUTOCONFIG_AUTH_ERROR;
			goto done;
		}

		new_uri = e2k_strdup_with_trailing_slash (location);
		e2k_autoconfig_set_owa_uri (ac, new_uri);
		g_free (new_uri);
		*result = E2K_AUTOCONFIG_REDIRECT;
		goto done;
	}

	/* If the server requires SSL, it will send back 403 Forbidden
	 * with a body explaining that.
	 */
	if (status == SOUP_ERROR_FORBIDDEN &&
	    !strncmp (ac->owa_uri, "http:", 5) &&
	    msg->response.length && strstr (msg->response.body, "SSL")) {
		char *new_uri = g_strconcat ("https:", ac->owa_uri + 5, NULL);
		e2k_autoconfig_set_owa_uri (ac, new_uri);
		g_free (new_uri);
		*result = E2K_AUTOCONFIG_TRY_SSL;
		goto done;
	}

	/* Figure out some stuff about the server */
	ms_webstorage = soup_message_get_header (msg->response_headers,
						 "MS-WebStorage");
	if (ms_webstorage) {
		if (!strncmp (ms_webstorage, "6.0.", 4))
			ac->version = E2K_EXCHANGE_2000;
		else if (!strncmp (ms_webstorage, "6.5.", 4))
			ac->version = E2K_EXCHANGE_2003;
		else
			ac->version = E2K_EXCHANGE_FUTURE;
	} else {
		const char *server = soup_message_get_header (msg->response_headers, "Server");

		/* If the server explicitly claims to be something
		 * other than IIS, then return the "not windows"
		 * error.
		 */
		if (server && !strstr (server, "IIS")) {
			*result = E2K_AUTOCONFIG_NOT_EXCHANGE;
			goto done;
		}

		ac->version = E2K_EXCHANGE_UNKNOWN;
	}

	/* If we're talking to OWA, then 404 Not Found means you don't
	 * have a mailbox. Otherwise, it means you're not talking to
	 * Exchange (even 5.5).
	 */
	if (status == SOUP_ERROR_NOT_FOUND) {
		if (ms_webstorage)
			*result = E2K_AUTOCONFIG_NO_MAILBOX;
		else
			*result = E2K_AUTOCONFIG_NOT_EXCHANGE;
		goto done;
	}

	/* Any other error else gets generic failure */
	if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
		*result = E2K_AUTOCONFIG_FAILED;
		goto done;
	}

	/* Parse the returned HTML. */
	doc = e2k_parse_html (msg->response.body);
	if (!doc) {
		/* Not HTML? */
		*result = ac->version == E2K_EXCHANGE_UNKNOWN ?
			E2K_AUTOCONFIG_NO_OWA :
			E2K_AUTOCONFIG_FAILED;
		goto done;
	}

	/* Make sure we didn't just get a page that wants to
	 * redirect us to Exchange 5.5
	 */
	for (node = doc->children; node; node = e2k_xml_find (node, "meta")) {
		gboolean ex55 = FALSE;

		equiv = xmlGetProp (node, "http-equiv");
		content = xmlGetProp (node, "content");
		if (equiv && content &&
		    !g_ascii_strcasecmp (equiv, "REFRESH") &&
		    strstr (content, "/logon.asp"))
			ex55 = TRUE;
		if (equiv)
			xmlFree (equiv);
		if (content)
			xmlFree (content);

		if (ex55) {
			*result = E2K_AUTOCONFIG_EXCHANGE_5_5;
			goto done;
		}
	}

	/* Don't go any further if it's not really Exchange 2000 */
	if (ac->version == E2K_EXCHANGE_UNKNOWN) {
		if (strstr (ac->owa_uri, "/logon.asp"))
			*result = E2K_AUTOCONFIG_EXCHANGE_5_5;
		else
			*result = E2K_AUTOCONFIG_NO_OWA;
		goto done;
	}

	/* Try to find the base URI */
	node = e2k_xml_find (doc->children, "base");
	if (node) {
		/* We won */
		*result = E2K_AUTOCONFIG_OK;
		href = xmlGetProp (node, "href");
		ac->home_uri = g_strdup (href);
		xmlFree (href);
	} else
		*result = E2K_AUTOCONFIG_FAILED;
	xmlFreeDoc (doc);

 done:
	soup_message_free (msg);

	if (*result != E2K_AUTOCONFIG_OK) {
		g_object_unref (conn);
		conn = NULL;
	}
	return conn;
}

static const char *home_properties[] = {
	PR_STORE_ENTRYID,
	E2K_PR_EXCHANGE_TIMEZONE
};
static const int n_home_properties = sizeof (home_properties) / sizeof (home_properties[0]);

/**
 * e2k_autoconfig_check_exchange:
 * @ac: an autoconfiguration context
 *
 * Tries to connect to the the Exchange server using the OWA URL,
 * username, and password in @ac. Attempts to determine the domain
 * name and home_uri, and then given the home_uri, looks up the
 * user's mailbox entryid (used to find his Exchange 5.5 DN) and
 * default timezone.
 *
 * The returned codes are the same as for e2k_autoconfig_get_connection()
 * with the following changes/additions/removals:
 *
 *   %E2K_AUTOCONFIG_REDIRECT: URL returned in first redirect returned
 *     another redirect, which was not followed.
 *   %E2K_AUTOCONFIG_CANT_BPROPFIND: The server does not allow
 *     BPROPFIND due to IIS Lockdown configuration
 *   %E2K_AUTOCONFIG_TRY_SSL: Not used; always handled internally by
 *     e2k_autoconfig_check_exchange()
 *
 * Return value: an #E2kAutoconfigResult
 **/
E2kAutoconfigResult
e2k_autoconfig_check_exchange (E2kAutoconfig *ac)
{
	xmlDoc *doc;
	xmlNode *node;
	int status, nresults;
	char *new_uri, *pf_uri;
	E2kConnection *conn;
	E2kAutoconfigResult result;
	gboolean redirected = FALSE;
	E2kResult *results;
	GByteArray *entryid;
	const char *exchange_dn, *timezone, *prop, *hrefs[] = { "" };
	char *body;
	int len;
	E2kUri *euri;

	g_return_val_if_fail (ac->owa_uri != NULL, E2K_AUTOCONFIG_FAILED);
	g_return_val_if_fail (ac->username != NULL, E2K_AUTOCONFIG_FAILED);
	g_return_val_if_fail (ac->password != NULL, E2K_AUTOCONFIG_FAILED);

 try_again:
	conn = e2k_autoconfig_get_connection (ac, &result);

	switch (result) {
	case E2K_AUTOCONFIG_OK:
		break;

	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC:
		if (ac->use_ntlm && !ac->require_ntlm) {
			ac->use_ntlm = FALSE;
			goto try_again;
		} else
			return E2K_AUTOCONFIG_AUTH_ERROR;

	case E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM:
		return E2K_AUTOCONFIG_AUTH_ERROR;

	case E2K_AUTOCONFIG_REDIRECT:
		if (!redirected) {
			redirected = TRUE;
			goto try_again;
		} else
			return result;

	case E2K_AUTOCONFIG_TRY_SSL:
		goto try_again;

	case E2K_AUTOCONFIG_NO_OWA:
		/* If the provided OWA URI had no path, try appending
		 * /exchange.
		 */
		euri = e2k_uri_new (ac->owa_uri);
		g_return_val_if_fail (euri != NULL, result);
		if (!euri->path || !strcmp (euri->path, "/")) {
			e2k_uri_free (euri);
			new_uri = e2k_uri_concat (ac->owa_uri, "exchange/");
			e2k_autoconfig_set_owa_uri (ac, new_uri);
			g_free (new_uri);
			goto try_again;
		}
		e2k_uri_free (euri);
		return result;

	default:
		return result;
	}

	/* Find the link to the public folders */
	if (ac->version < E2K_EXCHANGE_2003)
		pf_uri = g_strdup_printf ("%s/?Cmd=contents", ac->owa_uri);
	else
		pf_uri = g_strdup_printf ("%s/?Cmd=navbar", ac->owa_uri);

	status = e2k_connection_get_owa_sync (conn, pf_uri, FALSE, &body, &len);
	g_free (pf_uri);
	if (SOUP_ERROR_IS_SUCCESSFUL (status) && len > 0) {
		body[len - 1] = '\0';
		doc = e2k_parse_html (body);
		g_free (body);
	} else
		doc = NULL;

	if (doc) {
		for (node = e2k_xml_find (doc->children, "img"); node; node = e2k_xml_find (node, "img")) {
			prop = xmlGetProp (node, "src");
			if (prop && strstr (prop, "public") && node->parent) {
				node = node->parent;
				prop = xmlGetProp (node, "href");
				if (prop) {
					euri = e2k_uri_new (prop);
					ac->pf_server = g_strdup (euri->host);
					e2k_uri_free (euri);
				}
				break;
			}
		}
		xmlFreeDoc (doc);
	} else
		g_warning ("Could not parse pf page");

	/* Now find the store entryid and default timezone. We
	 * gratuitously use BPROPFIND in order to test if they
	 * have the IIS Lockdown problem.
	 */
	status = e2k_connection_bpropfind_sync (conn, ac->home_uri,
						hrefs, 1, "0",
						home_properties,
						n_home_properties,
						&results, &nresults);
	if (!SOUP_ERROR_IS_SUCCESSFUL (status)) {
		g_object_unref (conn);

		if (status == SOUP_ERROR_UNAUTHORIZED ||
		    status == SOUP_ERROR_CANT_AUTHENTICATE) {
			if (ac->use_ntlm && !ac->require_ntlm) {
				ac->use_ntlm = FALSE;
				goto try_again;
			} else
				return E2K_AUTOCONFIG_AUTH_ERROR;
		} else if (status == SOUP_ERROR_NOT_FOUND)
			return E2K_AUTOCONFIG_CANT_BPROPFIND;
		else
			return E2K_AUTOCONFIG_FAILED;
	}

	timezone = e2k_properties_get_prop (results[0].props,
					    E2K_PR_EXCHANGE_TIMEZONE);
	if (timezone)
		ac->timezone = find_olson_timezone (timezone);

	entryid = e2k_properties_get_prop (results[0].props, PR_STORE_ENTRYID);
	if (entryid) {
		exchange_dn = e2k_entryid_to_dn (entryid);
		if (exchange_dn)
			ac->exchange_dn = g_strdup (exchange_dn);
	}
	e2k_results_free (results, nresults);

	g_object_unref (conn);
	return ac->exchange_dn ? E2K_AUTOCONFIG_OK : E2K_AUTOCONFIG_FAILED;
}


static void
find_global_catalog (E2kAutoconfig *ac)
{
	int count, len;
	unsigned char answer[1024], namebuf[1024], *end, *p;
	guint16 type, qclass, rdlength, priority, weight, port;
	guint32 ttl;
	HEADER *header;

	if (!ac->w2k_domain)
		return;

	len = res_querydomain ("_gc._tcp", ac->w2k_domain, C_IN, T_SRV,
			       answer, sizeof (answer));
	if (len == -1)
		return;

	header = (HEADER *)answer;
	p = answer + sizeof (HEADER);
	end = answer + len;

	/* See RFCs 1035 and 2782 for details of the parsing */

	/* Skip query */
	count = ntohs (header->qdcount);
	while (count-- && p < end) {
		p += dn_expand (answer, end, p, namebuf, sizeof (namebuf));
		p += 4;
	}

	/* Read answers */
	while (count-- && p < end) {
		p += dn_expand (answer, end, p, namebuf, sizeof (namebuf));
		GETSHORT (type, p);
		GETSHORT (qclass, p);
		GETLONG (ttl, p);
		GETSHORT (rdlength, p);

		if (type != T_SRV || qclass != C_IN) {
			p += rdlength;
			continue;
		}

		GETSHORT (priority, p);
		GETSHORT (weight, p);
		GETSHORT (port, p);
		p += dn_expand (answer, end, p, namebuf, sizeof (namebuf));

		/* FIXME: obey priority and weight */
		ac->gc_server = g_strdup (namebuf);
		ac->gc_server_autodetected = TRUE;
		return;
	}

	return;
}

/**
 * e2k_autoconfig_check_global_catalog
 * @ac: an autoconfig context
 *
 * Tries to connect to the global catalog associated with @ac
 * (trying to figure it out from the domain name if the server
 * name is not yet known). On success it will look up the user's
 * full name and email address (based on his Exchange DN).
 *
 * Possible return values are:
 *
 *   %E2K_AUTOCONFIG_OK: Success
 *   %E2K_AUTOCONFIG_NETWORK_ERROR: Could not determine GC server
 *   %E2K_AUTOCONFIG_NO_MAILBOX: Could not find information for
 *     the user
 *   %E2K_AUTOCONFIG_FAILED: Other error.
 *
 * Return value: an #E2kAutoconfigResult.
 */
E2kAutoconfigResult
e2k_autoconfig_check_global_catalog (E2kAutoconfig *ac)
{
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogEntry *entry;
	E2kGlobalCatalogStatus status;
	E2kAutoconfigResult result;

	g_return_val_if_fail (ac->exchange_dn != NULL, E2K_AUTOCONFIG_FAILED);

	find_global_catalog (ac);
	if (!ac->gc_server)
		return E2K_AUTOCONFIG_NETWORK_ERROR;

	set_account_uri_string (ac);

	gc = e2k_global_catalog_new (ac->gc_server, -1,
				     ac->username, ac->nt_domain,
				     ac->password);

	status = e2k_global_catalog_lookup (
		gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		ac->exchange_dn, E2K_GLOBAL_CATALOG_LOOKUP_EMAIL |
		E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX, &entry);

	if (status == E2K_GLOBAL_CATALOG_OK) {
		ac->display_name = g_strdup (entry->display_name);
		ac->email = g_strdup (entry->email);
		result = E2K_AUTOCONFIG_OK;
	} else if (status == E2K_GLOBAL_CATALOG_ERROR)
		result = E2K_AUTOCONFIG_FAILED;
	else
		result = E2K_AUTOCONFIG_NO_MAILBOX;

	g_object_unref (gc);
	return result;
}

static void
set_account_uri_string (E2kAutoconfig *ac)
{
	E2kUri *owa_uri, *home_uri;
	char *path, *mailbox;
	GString *uri;

	owa_uri = e2k_uri_new (ac->owa_uri);
	home_uri = e2k_uri_new (ac->home_uri);

	uri = g_string_new ("exchange://");
	if (ac->nt_domain && (!ac->use_ntlm || !ac->nt_domain_defaulted)) {
		e2k_uri_append_encoded (uri, ac->nt_domain, "\\;:@/");
		g_string_append_c (uri, '\\');
	}
	e2k_uri_append_encoded (uri, ac->username, ";:@/");

	if (!ac->use_ntlm)
		g_string_append (uri, ";auth=Basic");

	g_string_append_c (uri, '@');
	e2k_uri_append_encoded (uri, owa_uri->host, ":/");
	if (owa_uri->port)
		g_string_append_printf (uri, ":%d", owa_uri->port);
	g_string_append_c (uri, '/');

	if (!strcmp (owa_uri->protocol, "https"))
		g_string_append (uri, ";use_ssl=always");
	g_string_append (uri, ";ad_server=");
	e2k_uri_append_encoded (uri, ac->gc_server, ";?");
	if (ac->ad_limit) {
		g_string_append_printf (uri, ";ad_limit=%d",
					atoi (ac->ad_limit));
	}

	path = g_strdup (home_uri->path + 1);
	mailbox = strrchr (path, '/');
	if (mailbox && !mailbox[1]) {
		*mailbox = '\0';
		mailbox = strrchr (path, '/');
	}
	if (mailbox) {
		*mailbox++ = '\0';
		g_string_append (uri, ";mailbox=");
		e2k_uri_append_encoded (uri, mailbox, ";?");
	}
	g_string_append (uri, ";owa_path=/");
	e2k_uri_append_encoded (uri, path, ";?");
	g_free (path);

	g_string_append (uri, ";pf_server=");
	e2k_uri_append_encoded (uri, ac->pf_server ? ac->pf_server : home_uri->host, ";?");

	ac->account_uri = uri->str;
	ac->exchange_server = g_strdup (home_uri->host);
	g_string_free (uri, FALSE);
	e2k_uri_free (home_uri);
	e2k_uri_free (owa_uri);
}


/* Approximate mapping from Exchange timezones to Olson ones. Exchange
 * is less specific, so we factor in the language/country info from
 * the locale in our guess.
 *
 * We strip " Standard Time" / " Daylight Time" from the Windows
 * timezone names. (Actually, we just strip the last two words.)
 */
static struct {
	const char *windows_name, *lang, *country, *olson_name;
} zonemap[] = {
	/* (GMT-12:00) Eniwetok, Kwajalein */
	{ "Dateline", NULL, NULL, "Pacific/Kwajalein" },

	/* (GMT-11:00) Midway Island, Samoa */
	{ "Samoa", NULL, NULL, "Pacific/Midway" },

	/* (GMT-10:00) Hawaii */
	{ "Hawaiian", NULL, NULL, "Pacific/Honolulu" },

	/* (GMT-09:00) Alaska */
	{ "Alaskan", NULL, NULL, "America/Juneau" },

	/* (GMT-08:00) Pacific Time (US & Canada); Tijuana */
	{ "Pacific", NULL, "CA", "America/Vancouver" },
	{ "Pacific", "es", "MX", "America/Tijuana" },
	{ "Pacific", NULL, NULL, "America/Los_Angeles" },

	/* (GMT-07:00) Arizona */
	{ "US Mountain", NULL, NULL, "America/Phoenix" },

	/* (GMT-07:00) Mountain Time (US & Canada) */
	{ "Mountain", NULL, "CA", "America/Edmonton" },
	{ "Mountain", NULL, NULL, "America/Denver" },

	/* (GMT-06:00) Central America */
	{ "Central America", NULL, "BZ", "America/Belize" },
	{ "Central America", NULL, "CR", "America/Costa_Rica" },
	{ "Central America", NULL, "GT", "America/Guatemala" },
	{ "Central America", NULL, "HN", "America/Tegucigalpa" },
	{ "Central America", NULL, "NI", "America/Managua" },
	{ "Central America", NULL, "SV", "America/El_Salvador" },

	/* (GMT-06:00) Central Time (US & Canada) */
	{ "Central", NULL, NULL, "America/Chicago" },

	/* (GMT-06:00) Mexico City */
	{ "Mexico", NULL, NULL, "America/Mexico_City" },

	/* (GMT-06:00) Saskatchewan */
	{ "Canada Central", NULL, NULL, "America/Regina" },

	/* (GMT-05:00) Bogota, Lima, Quito */
	{ "SA Pacific", NULL, "BO", "America/Bogota" },
	{ "SA Pacific", NULL, "EC", "America/Guayaquil" },
	{ "SA Pacific", NULL, "PA", "America/Panama" },
	{ "SA Pacific", NULL, "PE", "America/Lima" },

	/* (GMT-05:00) Eastern Time (US & Canada) */
	{ "Eastern", "fr", "CA", "America/Montreal" },
	{ "Eastern", NULL, NULL, "America/New_York" },

	/* (GMT-05:00) Indiana (East) */
	{ "US Eastern", NULL, NULL, "America/Indiana/Indianapolis" },

	/* (GMT-04:00) Atlantic Time (Canada) */
	{ "Atlantic", "es", "US", "America/Puerto_Rico" },
	{ "Atlantic", NULL, "VI", "America/St_Thomas" },
	{ "Atlantic", NULL, "CA", "America/Halifax" },

	/* (GMT-04:00) Caracas, La Paz */
	{ "SA Western", NULL, "BO", "America/La_Paz" },
	{ "SA Western", NULL, "VE", "America/Caracas" },

	/* (GMT-04:00) Santiago */
	{ "Pacific SA", NULL, NULL, "America/Santiago" },

	/* (GMT-03:30) Newfoundland */
	{ "Newfoundland", NULL, NULL, "America/St_Johns" },

	/* (GMT-03:00) Brasilia */
	{ "E. South America", NULL, NULL, "America/Sao_Paulo" },

	/* (GMT-03:00) Greenland */
	{ "Greenland", NULL, NULL, "America/Godthab" },

	/* (GMT-03:00) Buenos Aires, Georgetown */
	{ "SA Eastern", NULL, NULL, "America/Buenos_Aires" },

	/* (GMT-02:00) Mid-Atlantic */
	{ "Mid-Atlantic", NULL, NULL, "America/Noronha" },

	/* (GMT-01:00) Azores */
	{ "Azores", NULL, NULL, "Atlantic/Azores" },

	/* (GMT-01:00) Cape Verde Is. */
	{ "Cape Verde", NULL, NULL, "Atlantic/Cape_Verde" },

	/* (GMT) Casablanca, Monrovia */
	{ "Greenwich", NULL, "LR", "Africa/Monrovia" },
	{ "Greenwich", NULL, "MA", "Africa/Casablanca" },

	/* (GMT) Greenwich Mean Time : Dublin, Edinburgh, Lisbon, London */
	{ "GMT", "ga", "IE", "Europe/Dublin" },
	{ "GMT", "pt", "PT", "Europe/Lisbon" },
	{ "GMT", NULL, NULL, "Europe/London" },

	/* (GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna */
	{ "W. Europe", "nl", "NL", "Europe/Amsterdam" },
	{ "W. Europe", "it", "IT", "Europe/Rome" },
	{ "W. Europe", "sv", "SE", "Europe/Stockholm" },
	{ "W. Europe", NULL, "CH", "Europe/Zurich" },
	{ "W. Europe", NULL, "AT", "Europe/Vienna" },
	{ "W. Europe", "de", "DE", "Europe/Berlin" },

	/* (GMT+01:00) Belgrade, Bratislava, Budapest, Ljubljana, Prague */
	{ "Central Europe", "sr", "YU", "Europe/Belgrade" },
	{ "Central Europe", "sk", "SK", "Europe/Bratislava" },
	{ "Central Europe", "hu", "HU", "Europe/Budapest" },
	{ "Central Europe", "sl", "SI", "Europe/Ljubljana" },
	{ "Central Europe", "cz", "CZ", "Europe/Prague" },

	/* (GMT+01:00) Brussels, Copenhagen, Madrid, Paris */
	{ "Romance", NULL, "BE", "Europe/Brussels" },
	{ "Romance", "da", "DK", "Europe/Copenhagen" },
	{ "Romance", "es", "ES", "Europe/Madrid" },
	{ "Romance", "fr", "FR", "Europe/Paris" },

	/* (GMT+01:00) Sarajevo, Skopje, Sofija, Vilnius, Warsaw, Zagreb */
	{ "Central European", "bs", "BA", "Europe/Sarajevo" },
	{ "Central European", "mk", "MK", "Europe/Skopje" },
	{ "Central European", "bg", "BG", "Europe/Sofia" },
	{ "Central European", "lt", "LT", "Europe/Vilnius" },
	{ "Central European", "pl", "PL", "Europe/Warsaw" },
	{ "Central European", "hr", "HR", "Europe/Zagreb" },

	/* (GMT+01:00) West Central Africa */
	{ "W. Central Africa", NULL, NULL, "Africa/Kinshasa" },

	/* (GMT+02:00) Athens, Istanbul, Minsk */
	{ "GTB", "el", "GR", "Europe/Athens" },
	{ "GTB", "tr", "TR", "Europe/Istanbul" },
	{ "GTB", "be", "BY", "Europe/Minsk" },

	/* (GMT+02:00) Bucharest */
	{ "E. Europe", NULL, NULL, "Europe/Bucharest" },

	/* (GMT+02:00) Cairo */
	{ "Egypt", NULL, NULL, "Africa/Cairo" },

	/* (GMT+02:00) Harare, Pretoria */
	{ "South Africa", NULL, NULL, "Africa/Johannesburg" },

	/* (GMT+02:00) Helsinki, Riga, Tallinn */
	{ "FLE", "lv", "LV", "Europe/Riga" },
	{ "FLE", "et", "EE", "Europe/Tallinn" },
	{ "FLE", "fi", "FI", "Europe/Helsinki" },

	/* (GMT+02:00) Jerusalem */
	{ "Israel", NULL, NULL, "Asia/Jerusalem" },

	/* (GMT+03:00) Baghdad */
	{ "Arabic", NULL, NULL, "Asia/Baghdad" },

	/* (GMT+03:00) Kuwait, Riyadh */
	{ "Arab", NULL, "KW", "Asia/Kuwait" },
	{ "Arab", NULL, "SA", "Asia/Riyadh" },

	/* (GMT+03:00) Moscow, St. Petersburg, Volgograd */
	{ "Russian", NULL, NULL, "Europe/Moscow" },

	/* (GMT+03:00) Nairobi */
	{ "E. Africa", NULL, NULL, "Africa/Nairobi" },

	/* (GMT+03:30) Tehran */
	{ "Iran", NULL, NULL, "Asia/Tehran" },

	/* (GMT+04:00) Abu Dhabi, Muscat */
	{ "Arabian", NULL, NULL, "Asia/Muscat" },

	/* (GMT+04:00) Baku, Tbilisi, Yerevan */
	{ "Caucasus", NULL, NULL, "Asia/Baku" },

	/* (GMT+04:30) Kabul */
	{ "Afghanistan", NULL, NULL, "Asia/Kabul" },

	/* (GMT+05:00) Ekaterinburg */
	{ "Ekaterinburg", NULL, NULL, "Asia/Yekaterinburg" },

	/* (GMT+05:00) Islamabad, Karachi, Tashkent */
	{ "West Asia", NULL, NULL, "Asia/Karachi" },

	/* (GMT+05:30) Kolkata, Chennai, Mumbai, New Delhi */
	{ "India", NULL, NULL, "Asia/Calcutta" },

	/* (GMT+05:45) Kathmandu */
	{ "Nepal", NULL, NULL, "Asia/Katmandu" },

	/* (GMT+06:00) Almaty, Novosibirsk */
	{ "N. Central Asia", NULL, NULL, "Asia/Almaty" },

	/* (GMT+06:00) Astana, Dhaka */
	{ "Central Asia", NULL, NULL, "Asia/Dhaka" },

	/* (GMT+06:00) Sri Jayawardenepura */
	{ "Sri Lanka", NULL, NULL, "Asia/Colombo" },

	/* (GMT+06:30) Rangoon */
	{ "Myanmar", NULL, NULL, "Asia/Rangoon" },

	/* (GMT+07:00) Bangkok, Hanoi, Jakarta */
	{ "SE Asia", "th", "TH", "Asia/Bangkok" },
	{ "SE Asia", "vi", "VN", "Asia/Saigon" },
	{ "SE Asia", "id", "ID", "Asia/Jakarta" },

	/* (GMT+07:00) Krasnoyarsk */
	{ "North Asia", NULL, NULL, "Asia/Krasnoyarsk" },

	/* (GMT+08:00) Beijing, Chongqing, Hong Kong, Urumqi */
	{ "China", NULL, "HK", "Asia/Hong_Kong" },
	{ "China", NULL, NULL, "Asia/Shanghai" },

	/* (GMT+08:00) Irkutsk, Ulaan Bataar */
	{ "North Asia East", NULL, NULL, "Asia/Irkutsk" },

	/* (GMT+08:00) Perth */
	{ "W. Australia", NULL, NULL, "Australia/Perth" },

	/* (GMT+08:00) Kuala Lumpur, Singapore */
	{ "Singapore", NULL, NULL, "Asia/Kuala_Lumpur" },

	/* (GMT+08:00) Taipei */
	{ "Taipei", NULL, NULL, "Asia/Taipei" },

	/* (GMT+09:00) Osaka, Sapporo, Tokyo */
	{ "Tokyo", NULL, NULL, "Asia/Tokyo" },

	/* (GMT+09:00) Seoul */
	{ "Korea", NULL, "KP", "Asia/Pyongyang" },
	{ "Korea", NULL, "KR", "Asia/Seoul" },

	/* (GMT+09:00) Yakutsk */
	{ "Yakutsk", NULL, NULL, "Asia/Yakutsk" },

	/* (GMT+09:30) Adelaide */
	{ "Cen. Australia", NULL, NULL, "Australia/Adelaide" },

	/* (GMT+09:30) Darwin */
	{ "AUS Central", NULL, NULL, "Australia/Darwin" },

	/* (GMT+10:00) Brisbane */
	{ "E. Australia", NULL, NULL, "Australia/Brisbane" },

	/* (GMT+10:00) Canberra, Melbourne, Sydney */
	{ "AUS Eastern", NULL, NULL, "Australia/Sydney" },

	/* (GMT+10:00) Guam, Port Moresby */
	{ "West Pacific", NULL, NULL, "Pacific/Guam" },

	/* (GMT+10:00) Hobart */
	{ "Tasmania", NULL, NULL, "Australia/Hobart" },

	/* (GMT+10:00) Vladivostok */
	{ "Vladivostok", NULL, NULL, "Asia/Vladivostok" },

	/* (GMT+11:00) Magadan, Solomon Is., New Caledonia */
	{ "Central Pacific", NULL, NULL, "Pacific/Midway" },

	/* (GMT+12:00) Auckland, Wellington */
	{ "New Zealand", NULL, NULL, "Pacific/Auckland" },

	/* (GMT+12:00) Fiji, Kamchatka, Marshall Is. */
	{ "Fiji", "ru", "RU", "Asia/Kamchatka" },
	{ "Fiji", NULL, NULL, "Pacific/Fiji" },

	/* (GMT+13:00) Nuku'alofa */
	{ "Tonga", NULL, NULL, "Pacific/Tongatapu" }
};
static const int n_zone_mappings = sizeof (zonemap) / sizeof (zonemap[0]);

static char *
find_olson_timezone (const char *windows_timezone)
{
	int i, tzlen;
	const char *locale, *p;
	char lang[3] = { 0 }, country[3] = { 0 };

	/* Strip " Standard Time" / " Daylight Time" from name */
	p = windows_timezone + strlen (windows_timezone) - 1;
	while (p > windows_timezone && *p-- != ' ')
		;
	while (p > windows_timezone && *p-- != ' ')
		;
	tzlen = p - windows_timezone + 1;

	/* Find the first entry in zonemap with a matching name */
	for (i = 0; i < n_zone_mappings; i++) {
		if (!g_ascii_strncasecmp (windows_timezone,
					  zonemap[i].windows_name,
					  tzlen))
			break;
	}
	if (i == n_zone_mappings)
		return NULL; /* Shouldn't happen... */

	/* If there's only one choice, go with it */
	if (!zonemap[i].lang && !zonemap[i].country)
		return g_strdup (zonemap[i].olson_name);

	/* Find our language/country (hopefully). */
	locale = getenv ("LANG");
	if (locale) {
		strncpy (lang, locale, 2);
		locale = strchr (locale, '_');
		if (locale++)
			strncpy (country, locale, 2);
	}

	/* Look for an entry where either the country or the
	 * language matches.
	 */
	do {
		if ((zonemap[i].lang && !strcmp (zonemap[i].lang, lang)) ||
		    (zonemap[i].country && !strcmp (zonemap[i].country, country)))
			return g_strdup (zonemap[i].olson_name);
	} while (++i < n_zone_mappings &&
		 !g_ascii_strncasecmp (windows_timezone,
				       zonemap[i].windows_name,
				       tzlen));

	/* None of the hints matched, so (semi-arbitrarily) return the
	 * last of the entries with the right Windows timezone name.
	 */
	return g_strdup (zonemap[i - 1].olson_name);
}
