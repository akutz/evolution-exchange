/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __EXCHANGE_OOF_H__
#define __EXCHANGE_OOF_H__

#include "exchange-types.h"

#include <shell/evolution-config-control.h>

#include <bonobo/bonobo-object.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_OOF_CONTROL            (exchange_oof_control_get_type ())
#define EXCHANGE_OOF_CONTROL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_OOF_CONTROL, ExchangeOOFControl))
#define EXCHANGE_OOF_CONTROL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_OOF_CONTROL, ExchangeOOFControlClass))
#define EXCHANGE_IS_OOF_CONTROL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_OOF_CONTROL))
#define EXCHANGE_IS_OOF_CONTROL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_OOF_CONTROL))

struct _ExchangeOOFControl {
	EvolutionConfigControl parent;

	ExchangeOOFControlPrivate *priv;
};

struct _ExchangeOOFControlClass {
	EvolutionConfigControlClass parent_class;

};


GType         exchange_oof_control_get_type (void);

BonoboObject *exchange_oof_control_new      (void);

void          exchange_oof_init             (ExchangeAccount  *account,
					     GdkNativeWindow   shell_view_xid);
gboolean      exchange_oof_get              (ExchangeAccount  *account,
					     gboolean         *oof,
					     char            **mmsg);
gboolean      exchange_oof_set              (ExchangeAccount  *account,
					     gboolean          oof,
					     const char       *msg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_OOF_H__ */
