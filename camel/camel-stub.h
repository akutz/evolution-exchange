/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

/* camel-stub.h: class for a stub to talk to the backend */


#ifndef CAMEL_STUB_H
#define CAMEL_STUB_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-object.h>
#include <camel/camel-operation.h>

#include "camel-stub-constants.h"
#include "camel-stub-marshal.h"
#include <pthread.h>

#define CAMEL_STUB_TYPE     (camel_stub_get_type ())
#define CAMEL_STUB(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_STUB_TYPE, CamelStub))
#define CAMEL_STUB_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_STUB_TYPE, CamelStubClass))
#define CAMEL_IS_STUB(o)    (CAMEL_CHECK_TYPE((o), CAMEL_STUB_TYPE))

typedef struct {
	CamelObject parent_object;

	char *backend_name;

	GMutex *read_lock, *write_lock;
	CamelStubMarshal *cmd, *status;

	CamelOperation *op;      /* for cancelling */
	pthread_t status_thread;
	gboolean have_status_thread;
} CamelStub;

typedef struct {
	CamelObjectClass parent_class;

} CamelStubClass;


/* Standard Camel function */
CamelType  camel_stub_get_type    (void);

CamelStub *camel_stub_new         (const char *socket_path,
				   const char *backend_name,
				   CamelException *ex);

gboolean   camel_stub_send        (CamelStub *stub, CamelException *ex,
				   CamelStubCommand command, ...);
gboolean   camel_stub_send_oneway (CamelStub *stub,
				   CamelStubCommand command, ...);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_STUB_H */


