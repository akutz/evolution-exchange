/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __E2K_KERBEROS_H__
#define __E2K_KERBEROS_H__

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E2K_PARSE_NAME_FAILURE 	-1001
#define E2K_KRB_INIT_FAILURE 	-1002
#define E2K_INIT_CRED_FAILURE 	-1003
#define E2K_CHANGE_PWD_FAILURE 	-1004
#define E2K_PASSWD_EXPIRED 		-1005
#define E2K_BAD_PASSWD			-1006
#define E2K_KRB5_NO_USER_CONF	-1007
#define E2K_SET_CONFIG_FAILURE	-1008

int    e2k_change_passwd (char *user, char *old_pwd, char *new_pwd);
int    e2k_check_expire (char *usr_name, char *passwd);
int    e2k_create_krb_config_file (char *domain, char *kdc);
int    e2k_set_config (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_FREEBUSY_H__ */
