/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2000-2004 Novell, Inc. */

#ifndef CAMEL_STUB_MARSHAL_H
#define CAMEL_STUB_MARSHAL_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <glib.h>

typedef struct {
	GByteArray *in, *out;
	char *inptr;
	int fd;

	char *last_folder;
} CamelStubMarshal;

CamelStubMarshal *camel_stub_marshal_new           (int fd);
void              camel_stub_marshal_free          (CamelStubMarshal *marshal);

void              camel_stub_marshal_encode_uint32 (CamelStubMarshal *marshal,
						    guint32 value);
int               camel_stub_marshal_decode_uint32 (CamelStubMarshal *marshal,
						    guint32 *dest);
void              camel_stub_marshal_encode_string (CamelStubMarshal *marshal,
						    const char *str);
int               camel_stub_marshal_decode_string (CamelStubMarshal *marshal,
						    char **str);
void              camel_stub_marshal_encode_folder (CamelStubMarshal *marshal,
						    const char *name);
int               camel_stub_marshal_decode_folder (CamelStubMarshal *marshal,
						    char **name);
void              camel_stub_marshal_encode_bytes  (CamelStubMarshal *marshal,
						    GByteArray *ba);
int               camel_stub_marshal_decode_bytes  (CamelStubMarshal *marshal,
						    GByteArray **ba);

int               camel_stub_marshal_flush         (CamelStubMarshal *marshal);
gboolean          camel_stub_marshal_eof           (CamelStubMarshal *marshal);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_STUB_MARSHAL_H */


