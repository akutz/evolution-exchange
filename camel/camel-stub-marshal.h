/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

/* camel-stub-marshal.h: stub marshal/demarshal functions */

#ifndef CAMEL_STUB_MARSHAL_H
#define CAMEL_STUB_MARSHAL_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <glib.h>

typedef struct {
	GByteArray *in, *out;
	gchar *inptr;
	gint fd;

	gchar *last_folder;
} CamelStubMarshal;

CamelStubMarshal *camel_stub_marshal_new           (gint fd);
void              camel_stub_marshal_free          (CamelStubMarshal *marshal);

void              camel_stub_marshal_encode_uint32 (CamelStubMarshal *marshal,
						    guint32 value);
gint               camel_stub_marshal_decode_uint32 (CamelStubMarshal *marshal,
						    guint32 *dest);
void              camel_stub_marshal_encode_string (CamelStubMarshal *marshal,
						    const gchar *str);
gint               camel_stub_marshal_decode_string (CamelStubMarshal *marshal,
						    gchar **str);
void              camel_stub_marshal_encode_folder (CamelStubMarshal *marshal,
						    const gchar *name);
gint               camel_stub_marshal_decode_folder (CamelStubMarshal *marshal,
						    gchar **name);
void              camel_stub_marshal_encode_bytes  (CamelStubMarshal *marshal,
						    GByteArray *ba);
gint               camel_stub_marshal_decode_bytes  (CamelStubMarshal *marshal,
						    GByteArray **ba);

gint               camel_stub_marshal_flush         (CamelStubMarshal *marshal);
gboolean          camel_stub_marshal_eof           (CamelStubMarshal *marshal);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_STUB_MARSHAL_H */


