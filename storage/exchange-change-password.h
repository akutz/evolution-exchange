/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __EXCHANGE_CHANGE_PASSWORD_H__
#define __EXCHANGE_CHANGE_PASSWORD_H__

#include "exchange-types.h"
#include "exchange-account.h"
#include "e2k-autoconfig.h"

#include <stdlib.h>
#include <stdio.h>
                                                                                                   
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <shell/evolution-config-control.h>

#include <bonobo/bonobo-object.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

void exchange_change_password (char *password, E2kAutoconfig *ac, int voluntary);
                                                                                                   
struct password_data {
        GladeXML *xml;
        char *existing_password;
        GtkDialog *dialog;
        E2kAutoconfig *ac;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_CHANGE_PASSWORD_H__ */
