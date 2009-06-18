/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __EXCHANGE_CHANGE_PASSWORD_H__
#define __EXCHANGE_CHANGE_PASSWORD_H__

#include "exchange-types.h"

G_BEGIN_DECLS

gchar *exchange_get_new_password (const gchar *existing_password,
				 gboolean    voluntary);

G_END_DECLS

#endif /* __EXCHANGE_CHANGE_PASSWORD_H__ */
