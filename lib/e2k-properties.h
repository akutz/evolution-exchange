/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_PROPERTIES_H__
#define __E2K_PROPERTIES_H__

#include <glib.h>
#include <libxml/tree.h>

typedef struct E2kProperties E2kProperties;

typedef enum {
	E2K_PROP_TYPE_UNKNOWN,

	E2K_PROP_TYPE_STRING,
	E2K_PROP_TYPE_BINARY,
	E2K_PROP_TYPE_STRING_ARRAY,
	E2K_PROP_TYPE_BINARY_ARRAY,
	E2K_PROP_TYPE_XML,

	/* These are all stored as STRING or STRING_ARRAY */
	E2K_PROP_TYPE_INT,
	E2K_PROP_TYPE_INT_ARRAY,
	E2K_PROP_TYPE_BOOL,
	E2K_PROP_TYPE_FLOAT,
	E2K_PROP_TYPE_DATE
} E2kPropType;

#define E2K_PROP_TYPE_IS_ARRAY(t) (((t) == E2K_PROP_TYPE_STRING_ARRAY) || ((t) == E2K_PROP_TYPE_BINARY_ARRAY) || ((t) == E2K_PROP_TYPE_INT_ARRAY))

E2kProperties *e2k_properties_new               (void);
E2kProperties *e2k_properties_copy              (E2kProperties *props);
void           e2k_properties_free              (E2kProperties *props);

gpointer       e2k_properties_get_prop          (E2kProperties *props,
						 const char    *propname);
gboolean       e2k_properties_empty             (E2kProperties *props);

void           e2k_properties_set_string        (E2kProperties *props,
						 const char    *propname,
						 char          *value);
void           e2k_properties_set_static_string (E2kProperties *props,
						 const char    *propname,
						 const char    *value);
void           e2k_properties_set_string_array  (E2kProperties *props,
						 const char    *propname,
						 GPtrArray     *value);
void           e2k_properties_set_binary        (E2kProperties *props,
						 const char    *propname,
						 GByteArray    *value);
void           e2k_properties_set_binary_array  (E2kProperties *props,
						 const char    *propname,
						 GPtrArray     *value);
void           e2k_properties_set_int           (E2kProperties *props,
						 const char    *propname,
						 int            value);
void           e2k_properties_set_int_array     (E2kProperties *props,
						 const char    *propname,
						 GPtrArray     *value);
void           e2k_properties_set_xml           (E2kProperties *props,
						 const char    *propname,
						 xmlNode       *value);
void           e2k_properties_set_bool          (E2kProperties *props,
						 const char    *propname,
						 gboolean       value);
void           e2k_properties_set_float         (E2kProperties *props,
						 const char    *propname,
						 float          value);
void           e2k_properties_set_date          (E2kProperties *props,
						 const char    *propname,
						 char          *value);

void           e2k_properties_set_type_as_string       (E2kProperties *props,
							const char    *pname,
							E2kPropType    type,
							char          *value);
void           e2k_properties_set_type_as_string_array (E2kProperties *props,
							const char    *pname,
							E2kPropType    type,
							GPtrArray     *value);

void           e2k_properties_remove            (E2kProperties *props,
						 const char    *propname);


typedef void (*E2kPropertiesForeachFunc)        (const char    *propname,
						 E2kPropType    type,
						 gpointer       value,
						 gpointer       user_data);
void           e2k_properties_foreach           (E2kProperties *props,
						 E2kPropertiesForeachFunc cb,
						 gpointer       user_data);
void           e2k_properties_foreach_removed   (E2kProperties *props,
						 E2kPropertiesForeachFunc cb,
						 gpointer       user_data);


typedef void(*E2kPropertiesForeachNamespaceFunc)(const char    *namespace,
						 char           abbrev,
						 gpointer       user_data);
void           e2k_properties_foreach_namespace (E2kProperties *props,
						 E2kPropertiesForeachNamespaceFunc callback,
						 gpointer user_data);

const char *e2k_prop_namespace_name   (const char *prop);
char        e2k_prop_namespace_abbrev (const char *prop);
const char *e2k_prop_property_name    (const char *prop);

void        e2k_prop_write_value      (GString     *out,
				       const char  *propname,
				       E2kPropType  type,
				       gpointer     value);

#endif /* __E2K_PROPERTIES_H__ */
