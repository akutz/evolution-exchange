/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __MAIL_STUB_H__
#define __MAIL_STUB_H__

#include <stdio.h>
#include <glib-object.h>
#include "camel-stub-constants.h"
#include "camel-stub-marshal.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define MAIL_TYPE_STUB            (mail_stub_get_type ())
#define MAIL_STUB(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MAIL_TYPE_STUB, MailStub))
#define MAIL_STUB_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MAIL_TYPE_STUB, MailStubClass))
#define MAIL_STUB_IS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAIL_TYPE_STUB))
#define MAIL_STUB_IS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MAIL_TYPE_STUB))

typedef struct _MailStub        MailStub;
typedef struct _MailStubClass   MailStubClass;

struct _MailStub {
	GObject parent;

	GIOChannel *channel;
	CamelStubMarshal *cmd, *status;
};

struct _MailStubClass {
	GObjectClass parent_class;

	/* methods */
	void (*connect)            (MailStub *);
	void (*get_folder)         (MailStub *, const char *name,
				    GPtrArray *uids, GByteArray *flags);
	void (*get_trash_name)     (MailStub *);
	void (*sync_folder)        (MailStub *, const char *folder_name);
	void (*refresh_folder)     (MailStub *, const char *folder_name);
	void (*expunge_uids)       (MailStub *, const char *folder_name,
				    GPtrArray *uids);
	void (*append_message)     (MailStub *, const char *folder_name,
				    guint32 flags, const char *subject,
				    const char *data, int length);
	void (*set_message_flags)  (MailStub *, const char *folder_name,
				    const char *uid,
				    guint32 flags, guint32 mask);
	void (*set_message_tag)    (MailStub *, const char *folder_name,
				    const char *uid,
				    const char *name, const char *value);
	void (*get_message)        (MailStub *, const char *folder_name,
				    const char *uid);
	void (*search)             (MailStub *, const char *folder_name,
				    const char *text);
	void (*transfer_messages)  (MailStub *, const char *source_name,
				    const char *dest_name, GPtrArray *uids,
				    gboolean delete_originals);
	void (*get_folder_info)    (MailStub *);
	void (*send_message)       (MailStub *, const char *from,
				    GPtrArray *recipients,
				    const char *data, int length);
};

GType             mail_stub_get_type        (void);
void              mail_stub_construct       (MailStub *stub,
					     int cmd_fd, int status_fd);

gboolean          mail_stub_read_args       (MailStub *, ...);

void              mail_stub_return_data     (MailStub *, CamelStubRetval, ...);
void              mail_stub_return_progress (MailStub *stub, int percent);
void              mail_stub_return_ok       (MailStub *);
void              mail_stub_return_error    (MailStub *, const char *);

void              mail_stub_push_changes    (MailStub *);

void              mail_stub_folder_changed  (MailStub *, const char *path,
					     int unread_count);

/* Message flags. This must be kept in sync with camel-folder-summary.h */
#define MAIL_STUB_MESSAGE_ANSWERED	(1 << 0)
#define MAIL_STUB_MESSAGE_DELETED	(1 << 1)
#define MAIL_STUB_MESSAGE_DRAFT		(1 << 2)
#define MAIL_STUB_MESSAGE_FLAGGED	(1 << 3)
#define MAIL_STUB_MESSAGE_SEEN		(1 << 4)
#define MAIL_STUB_MESSAGE_ATTACHMENTS	(1 << 5)
#define MAIL_STUB_MESSAGE_ANSWERED_ALL	(1 << 6)

/* These are our own private ones */
#define MAIL_STUB_MESSAGE_DELEGATED     (1 << 16)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MAIL_STUB_H__ */
