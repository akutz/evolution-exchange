/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __MAIL_STUB_EXCHANGE_H__
#define __MAIL_STUB_EXCHANGE_H__

#include "mail-stub.h"
#include <exchange-account.h>

G_BEGIN_DECLS

#define MAIL_TYPE_STUB_EXCHANGE            (mail_stub_exchange_get_type ())
#define MAIL_STUB_EXCHANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MAIL_TYPE_STUB_EXCHANGE, MailStubExchange))
#define MAIL_STUB_EXCHANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MAIL_TYPE_STUB_EXCHANGE, MailStubExchangeClass))
#define MAIL_STUB_IS_EXCHANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAIL_TYPE_STUB_EXCHANGE))
#define MAIL_STUB_IS_EXCHANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MAIL_TYPE_STUB_EXCHANGE))

typedef struct _MailStubExchange        MailStubExchange;
typedef struct _MailStubExchangeClass   MailStubExchangeClass;

struct _MailStubExchange {
	MailStub parent;

	ExchangeAccount *account;
	E2kContext *ctx;
	GHashTable *folders_by_name;
	const gchar *mail_submission_uri;
	EFolder *inbox, *deleted_items, *sent_items;

	guint new_folder_id, removed_folder_id;
	const gchar *ignore_new_folder, *ignore_removed_folder;
};

struct _MailStubExchangeClass {
	MailStubClass parent_class;

};

GType             mail_stub_exchange_get_type   (void);
gboolean          mail_stub_exchange_construct  (MailStubExchange *exchange);

MailStub         *mail_stub_exchange_new        (ExchangeAccount *account,
						 gint cmd_fd, gint status_fd);

G_END_DECLS

#endif /* __MAIL_STUB_EXCHANGE_H__ */
