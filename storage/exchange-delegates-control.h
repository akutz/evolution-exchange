/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __EXCHANGE_DELEGATES_CONTROL_H__
#define __EXCHANGE_DELEGATES_CONTROL_H__

#include "exchange-types.h"
#include "exchange-delegates-user.h"
#include "e2k-security-descriptor.h"

#include <shell/evolution-config-control.h>

#include <bonobo/bonobo-object.h>
#include <e-util/e-account.h>
#include <glade/glade-xml.h>
#include <gtk/gtkliststore.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_DELEGATES_CONTROL            (exchange_delegates_control_get_type ())
#define EXCHANGE_DELEGATES_CONTROL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_DELEGATES_CONTROL, ExchangeDelegatesControl))
#define EXCHANGE_DELEGATES_CONTROL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_DELEGATES_CONTROL, ExchangeDelegatesControlClass))
#define EXCHANGE_IS_DELEGATES_CONTROL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_DELEGATES_CONTROL))
#define EXCHANGE_IS_DELEGATES_CONTROL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_DELEGATES_CONTROL))

typedef struct {
	const char *uri;
	E2kSecurityDescriptor *sd;
	gboolean changed;
} ExchangeDelegatesFolder;

struct _ExchangeDelegatesControl {
	EvolutionConfigControl parent;

	ExchangeAccount *account;
	char *self_dn;

	GladeXML *xml;


	/* Delegates page */
	GtkListStore *delegates_model;
	GtkWidget *delegates_table;

	GByteArray *creator_entryid;
	GPtrArray *users, *added_users, *removed_users;
	gboolean loaded_folders;
	ExchangeDelegatesFolder folder[EXCHANGE_DELEGATES_LAST];
	ExchangeDelegatesFolder freebusy_folder;


	/* Delegators page */
	GtkListStore *delegators_model;
	GtkWidget *delegators_table;

	GPtrArray *delegators;
	EAccount *my_account;
};

struct _ExchangeDelegatesControlClass {
	EvolutionConfigControlClass parent_class;

};


GType         exchange_delegates_control_get_type    (void);

BonoboObject *exchange_delegates_control_new         (void);

void          exchange_delegates_control_get_self_dn (ExchangeDelegatesControl *control);

/* From exchange-delegates-delegates.c */
void exchange_delegates_delegates_construct  (ExchangeDelegatesControl *);
void exchange_delegates_delegates_apply      (ExchangeDelegatesControl *);
void exchange_delegates_delegates_destroy    (ExchangeDelegatesControl *);

/* From exchange-delegates-delegators.c */
void exchange_delegates_delegators_construct (ExchangeDelegatesControl *);
void exchange_delegates_delegators_apply     (ExchangeDelegatesControl *);
void exchange_delegates_delegators_destroy   (ExchangeDelegatesControl *);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_DELEGATES_CONTROL_H__ */
