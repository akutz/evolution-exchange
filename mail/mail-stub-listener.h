/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright 2001-2004 Novell, Inc. */

#ifndef __MAIL_STUB_LISTENER_H__
#define __MAIL_STUB_LISTENER_H__

#include <glib-object.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define MAIL_TYPE_STUB_LISTENER            (mail_stub_listener_get_type ())
#define MAIL_STUB_LISTENER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MAIL_TYPE_STUB_LISTENER, MailStubListener))
#define MAIL_STUB_LISTENER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MAIL_TYPE_STUB_LISTENER, MailStubListenerClass))
#define MAIL_STUB_LISTENER_IS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAIL_TYPE_STUB_LISTENER))
#define MAIL_STUB_LISTENER_IS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), MAIL_TYPE_STUB_LISTENER))

typedef struct _MailStubListener        MailStubListener;
typedef struct _MailStubListenerClass   MailStubListenerClass;

struct _MailStubListener {
	GObject parent;

	gpointer stub;
	gchar *socket_path;
	GIOChannel *channel;

	gint cmd_fd;
};

struct _MailStubListenerClass {
	GObjectClass parent_class;

	/* signals */
	void (*new_connection) (MailStubListener *, int, gint);
};

GType             mail_stub_listener_get_type      (void);
gboolean          mail_stub_listener_construct     (MailStubListener *stub,
						    const gchar *socket_path);

MailStubListener *mail_stub_listener_new           (const gchar *socket_path);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MAIL_STUB_LISTENER_H__ */
