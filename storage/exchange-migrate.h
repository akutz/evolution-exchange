/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2004 Novell, Inc. */

#ifndef __EXCHANGE_MIGRATE_H__
#define __EXCHANGE_MIGRATE_H__

#include "xc-backend.h"

gboolean exchange_migrate (const CORBA_short major, const CORBA_short minor, const CORBA_short revision, const gchar *base_diri, char *account_filename);

#endif /* _EXCHANGE_MIGRATE_H_ */
