/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <krb5.h>

#include "e2k-kerberos.h"


static krb5_error_code
e2k_init_cred (krb5_context ctx, const char *usr_name, char *passwd, char *in_tkt_service, krb5_creds *cred)
{
	krb5_principal principal;
	krb5_get_init_creds_opt opt;
	krb5_error_code result;
	
	result = krb5_parse_name (ctx, usr_name, &principal);	
	if (result) {
		return E2K_PARSE_NAME_FAILURE;
	}

	krb5_get_init_creds_opt_init (&opt);	
	krb5_get_init_creds_opt_set_tkt_life (&opt, 5*60);
	krb5_get_init_creds_opt_set_renew_life (&opt, 0);
	krb5_get_init_creds_opt_set_forwardable (&opt, 0);
	krb5_get_init_creds_opt_set_proxiable (&opt, 0);

	result = krb5_get_init_creds_password (ctx, cred, principal, passwd, NULL, NULL, 0, in_tkt_service, &opt);
	
	switch(result){
	case KRB5KRB_AP_ERR_BAD_INTEGRITY:
	case KRB5KDC_ERR_PREAUTH_FAILED:
		/*bad password given*/
		result = E2K_BAD_PASSWD;
		break;
	default:
		break;
	}
		

	return result;
}

/**
 * e2k_change_passwd
 * @usr_name: user name - only name not principal
 * @old_pw: currrent password
 * @new_pw: password to be changed to
 *
 * Changes the password for the given user name
 *
 * Return value: int error/success code. 0 for success.
 **/
int
e2k_change_passwd (char *usr_name, char *old_pw, char *new_pw)
{
	krb5_creds cred;
	krb5_context ctx;
	krb5_data res_code_string, res_string;
	krb5_error_code result;
	
	int res_code;

	result = e2k_set_config ();
	if (result && result != E2K_KRB5_NO_USER_CONF)
		return E2K_SET_CONFIG_FAILURE;
	
	result = krb5_init_context (&ctx);
	if (result) {
		return E2K_KRB_INIT_FAILURE;
	}
	
	result = e2k_init_cred (ctx, usr_name, old_pw, "kadmin/changepw", &cred);

	/* check for key expiry here? should the user be allowed to change pwd, if already expired?
	 * May not be required as the user is informed about the expiry details on login.
	 */
	
	if (result) {
		goto cleanup;
	}
	
	result = krb5_change_password (ctx, &cred, new_pw, &res_code, &res_code_string, &res_string);
	
	if (result) {
		result = E2K_CHANGE_PWD_FAILURE;
		goto cleanup;
	}
	
	if (res_code_string.data != NULL)
		free (res_code_string.data);

	if (res_string.data != NULL)
		free (res_string.data);	
	
cleanup:
	krb5_free_context (ctx);
	return result;
}

/**
 * e2k_check_expire
 * @usr_name: user name - only name not principal
 * @passwd: currrent password
 *
 * Checks if the password is expired or going to be expired??
 *
 * Return value: int error/success code. 0 for success.
 **/
int
e2k_check_expire (char *usr_name, char *passwd)
{
	krb5_creds cred;
	krb5_context ctx;
	krb5_error_code result;
	
	result = e2k_set_config ();
	if (result && result != E2K_KRB5_NO_USER_CONF)
		return E2K_SET_CONFIG_FAILURE;
	
	/* going ahead with krb5_init_context even if user conf file was not existing.
	 * /etc/krb5.conf may be valid. if not valid krb5_init_context will error out.
	 */
	
	result = krb5_init_context (&ctx);
	if (result) {
		return E2K_KRB_INIT_FAILURE;
	}

	result = e2k_init_cred (ctx, usr_name, passwd, NULL, &cred);
	
	/* expire waring check may have to go here... or else
	 * possibility of letting the user to change the password when found expired.
	 * caller needs to check for KRB5KDC_ERR_KEY_EXP, if yes, get the new password 
	 * and change?
	 */
	
	krb5_free_context (ctx);	
	return result;

}

/**
 * e2k_create_krb_config_file
 * @domain domain name for the user
 * @kdc kdc -->global catalog server
 * Creates the krb5.conf file in ~/evolution/
 * setup code should call this while configuring connector
 **/
int
e2k_create_krb_config_file (char *domain, char *kdc)
{
	FILE *fp;
	char *path, *tmp, chr;
	
	path = g_strdup_printf ("%s/.evolution/krb5.conf", g_get_home_dir ());
	
	fp = fopen (path, "w");
	g_free (path);
	if (fp == NULL) {
		return -1; /*error code*/
	}
	
	/* make realm name from domain by converting into upper case.
	 * Assumption is, Windows kerberos insists that the realm name will be same
	 * as the domain name in caps. @novell.com => NOVELL.COM as realm
	 * If this assumption goes wrong, then we may need a configuration item
	 * to get the realm name.
	 */
	tmp = domain;
	while ((chr = *tmp)) {
		*tmp = toupper (chr);
		tmp++;
	}
	
	/*only the minimum fields are written into krb5.conf. rest of the fields may not be required?*/	
	fprintf (fp, "[libdefaults]\n default_realm = %s\n[realms]\n %s = {\n\tkdc = %s:88\n\tadmin_server = %s:749\n}",
		domain, domain, kdc, kdc);
	fclose(fp);
	
	return 0; /*success code*/
}

/**
 * e2k_set_config
 *
 * Sets the env var KRB_CONFIG to ~/.evolution/krb5.conf
 **/
int
e2k_set_config ()
{
	char *path;
	FILE *fp;
	int res;
	
	path = g_strdup_printf ("%s/.evolution/krb5.conf", g_get_home_dir ());
	/* check if ~/.evolution/krb5.conf exists, if exists, set KRB5_CONFIG to that
	 */
	fp = fopen (path, "r");

	if (fp == NULL){
	/* error code... -to indicate nonexistent krb5.conf... 
	 * caller may need to invoke e2k_create_krb_config_file
	 */	
		res = E2K_KRB5_NO_USER_CONF; 
		goto cleanup;
	}
	else
		fclose (fp);
	
	res = setenv ("KRB5_CONFIG", path, 1);
	
cleanup: 	
	g_free (path);
	return	res; 
}
