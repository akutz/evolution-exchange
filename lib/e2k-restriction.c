/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2001-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* e2k-restriction.c: message restrictions (WHERE clauses / Rule conditions) */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e2k-restriction.h"
#include "e2k-properties.h"

#include <stdarg.h>
#include <string.h>

struct _E2kRestriction {
	E2kRestrictionType type;
	int ref_count;

	union {
		struct {
			guint                nrns;
			E2kRestriction     **rns;
		} and;

		struct {
			guint                nrns;
			E2kRestriction     **rns;
		} or;

		struct {
			E2kRestriction      *rn;
		} not;

		struct {
			guint                fuzzy_level;
			E2kPropValue         prop;
		} content;

		struct {
			E2kRestrictionRelop  relop;
			E2kPropType          type;
			E2kPropValue         prop;
		} property;

		struct {
			E2kRestrictionRelop  relop;
			const char          *propname1;
			const char          *propname2;
		} compare;

		struct {
			E2kRestrictionBitop  bitop;
			const char          *propname;
			guint32              mask;
		} bitmask;

		struct {
			E2kRestrictionRelop  relop;
			const char          *propname;
			guint32              size;
		} size;

		struct {
			const char          *propname;
		} exist;

#ifdef NOTYET
		struct {
			const char          *subtable;
			E2kRestriction      *rn;
		} sub;

		struct {
			guint32              nprops;
			E2kRestriction      *rn;
			E2kPropValue        *props;
		} comment;
#endif
	} res;
};

static E2kRestriction *
conjoin (E2kRestrictionType type, int nrns, E2kRestriction **rns, gboolean unref)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);
	int i;

	ret->type = type;
	ret->res.and.nrns = nrns;
	ret->res.and.rns = g_new (E2kRestriction *, nrns);
	for (i = 0; i < nrns; i++) {
		ret->res.and.rns[i] = rns[i];
		if (!unref)
			e2k_restriction_ref (rns[i]);
	}

	return ret;
}

E2kRestriction *
e2k_restriction_and (int nrns, E2kRestriction **rns, gboolean unref)
{
	return conjoin (E2K_RESTRICTION_AND, nrns, rns, unref);
}

E2kRestriction *
e2k_restriction_or (int nrns, E2kRestriction **rns, gboolean unref)
{
	return conjoin (E2K_RESTRICTION_OR, nrns, rns, unref);
}

static E2kRestriction *
conjoinv (E2kRestrictionType type, E2kRestriction *rn, va_list ap)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);
	GPtrArray *rns;

	rns = g_ptr_array_new ();
	while (rn) {
		g_ptr_array_add (rns, rn);
		rn = va_arg (ap, E2kRestriction *);
	}
	va_end (ap);

	ret->type = type;
	ret->res.and.nrns = rns->len;
	ret->res.and.rns = (E2kRestriction **)rns->pdata;
	g_ptr_array_free (rns, FALSE);

	return ret;
}

E2kRestriction *
e2k_restriction_andv (E2kRestriction *rn, ...)
{
	va_list ap;

	va_start (ap, rn);
	return conjoinv (E2K_RESTRICTION_AND, rn, ap);
}

E2kRestriction *
e2k_restriction_orv (E2kRestriction *rn, ...)
{
	va_list ap;

	va_start (ap, rn);
	return conjoinv (E2K_RESTRICTION_OR, rn, ap);
}

E2kRestriction *
e2k_restriction_not (E2kRestriction *rn, gboolean unref)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_NOT;
	ret->res.not.rn = rn;
	if (!unref)
		e2k_restriction_ref (rn);

	return ret;
}

E2kRestriction *
e2k_restriction_content (const char *propname, guint fuzzy_level,
			 const char *value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_CONTENT;
	ret->res.content.fuzzy_level = fuzzy_level;
	ret->res.content.prop.propname = propname;
	ret->res.content.prop.value = g_strdup (value);

	return ret;
}

E2kRestriction *
e2k_restriction_prop_bool (const char *propname, E2kRestrictionRelop relop,
			   gboolean value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	ret->res.property.type = E2K_PROP_TYPE_BOOL;
	ret->res.property.prop.propname = propname;
	ret->res.property.prop.value = GUINT_TO_POINTER (value);

	return ret;
}

E2kRestriction *
e2k_restriction_prop_int (const char *propname, E2kRestrictionRelop relop,
			  int value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	ret->res.property.type = E2K_PROP_TYPE_INT;
	ret->res.property.prop.propname = propname;
	ret->res.property.prop.value = GINT_TO_POINTER (value);

	return ret;
}

E2kRestriction *
e2k_restriction_prop_date (const char *propname, E2kRestrictionRelop relop,
			   const char *value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	ret->res.property.type = E2K_PROP_TYPE_DATE;
	ret->res.property.prop.propname = propname;
	ret->res.property.prop.value = g_strdup (value);

	return ret;
}

E2kRestriction *
e2k_restriction_prop_string (const char *propname, E2kRestrictionRelop relop,
			     const char *value)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_PROPERTY;
	ret->res.property.relop = relop;
	ret->res.property.type = E2K_PROP_TYPE_STRING;
	ret->res.property.prop.propname = propname;
	ret->res.property.prop.value = g_strdup (value);

	return ret;
}

E2kRestriction *
e2k_restriction_compare (const char *propname1, E2kRestrictionRelop relop,
			 const char *propname2)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_COMPAREPROPS;
	ret->res.compare.relop = relop;
	ret->res.compare.propname1 = propname1;
	ret->res.compare.propname2 = propname2;

	return ret;
}

E2kRestriction *
e2k_restriction_bitmask (const char *propname, E2kRestrictionBitop bitop,
			 guint32 mask)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_BITMASK;
	ret->res.bitmask.bitop = bitop;
	ret->res.bitmask.propname = propname;
	ret->res.bitmask.mask = mask;

	return ret;
}

E2kRestriction *
e2k_restriction_size (const char *propname, E2kRestrictionRelop relop,
		      guint32 size)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_SIZE;
	ret->res.size.relop = relop;
	ret->res.size.propname = propname;
	ret->res.size.size = size;

	return ret;
}

E2kRestriction *
e2k_restriction_exist (const char *propname)
{
	E2kRestriction *ret = g_new0 (E2kRestriction, 1);

	ret->type = E2K_RESTRICTION_EXIST;
	ret->res.exist.propname = propname;

	return ret;
}

void
e2k_restriction_unref (E2kRestriction *rn)
{
	int i;

	if (rn->ref_count--)
		return;

	switch (rn->type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR:
		for (i = 0; i < rn->res.and.nrns; i++)
			e2k_restriction_unref (rn->res.and.rns[i]);
		g_free (rn->res.and.rns);
		break;

	case E2K_RESTRICTION_NOT:
		e2k_restriction_unref (rn->res.not.rn);
		break;

	case E2K_RESTRICTION_CONTENT:
		g_free (rn->res.content.prop.value);
		break;

	case E2K_RESTRICTION_PROPERTY:
		if (rn->res.property.type == E2K_PROP_TYPE_DATE ||
		    rn->res.property.type == E2K_PROP_TYPE_STRING)
			g_free (rn->res.property.prop.value);
		break;

	default:
		break;
	}

	g_free (rn);
}

void
e2k_restriction_ref (E2kRestriction *rn)
{
	rn->ref_count++;
}


static gboolean rn_to_sql (E2kRestriction *rn, GString *sql, E2kRestrictionType inside);

static const char *sql_relops[] = { "<", "<=", ">", ">=", "=", "!=", NULL };

static gboolean
rns_to_sql (E2kRestrictionType type, E2kRestriction **rns, int nrns, GString *sql)
{
	int i;
	gboolean need_op = FALSE;
	gboolean rv = FALSE;

	for (i = 0; i < nrns; i++) {
		if (need_op) {
			g_string_append (sql, type == E2K_RESTRICTION_AND ?
					 " AND " : " OR ");
			need_op = FALSE;
		}
		if (rn_to_sql (rns[i], sql, type)) {
			need_op = TRUE;
			rv = TRUE;
		}
	}
	return rv;
}

static void
append_sql_quoted (GString *sql, const char *string)
{
	while (*string) {
		if (*string == '\'')
			g_string_append (sql, "''");
		else
			g_string_append_c (sql, *string);
		string++;
	}
}

static gboolean
rn_to_sql (E2kRestriction *rn, GString *sql, E2kRestrictionType inside)
{
	E2kPropValue *pv;

	switch (rn->type) {
	case E2K_RESTRICTION_AND:
	case E2K_RESTRICTION_OR: {
		GString *subsql = g_string_new ("");
		gboolean rv;
		if ((rv = rns_to_sql (rn->type, rn->res.and.rns, rn->res.and.nrns, subsql))) {
			if (rn->type != inside)
				g_string_append (sql, "(");
			g_string_append (sql, subsql->str);
			if (rn->type != inside)
				g_string_append (sql, ")");
		}
		g_string_free (subsql, TRUE);

		return rv;
	}

	case E2K_RESTRICTION_NOT: {
		GString *subsql = g_string_new ("");
		gboolean rv;
		if ((rv = rn_to_sql (rn->res.not.rn, sql, rn->type))) {
			g_string_append (sql, "NOT (");
			g_string_append (sql, subsql->str);
			g_string_append (sql, ")");
		}
		g_string_free (subsql, TRUE);

		return rv;
	}

	case E2K_RESTRICTION_CONTENT:
		pv = &rn->res.content.prop;
		g_string_append_printf (sql, "\"%s\" ", pv->propname);

		switch (rn->res.content.fuzzy_level & 0x3) {
		case E2K_FL_SUBSTRING:
			g_string_append (sql, "LIKE '%");
			append_sql_quoted (sql, pv->value);
			g_string_append (sql, "%'");
			break;

		case E2K_FL_PREFIX:
			g_string_append (sql, "LIKE '");
			append_sql_quoted (sql, pv->value);
			g_string_append (sql, "%'");
			break;

		case E2K_FL_SUFFIX:
			g_string_append (sql, "LIKE '%");
			append_sql_quoted (sql, pv->value);
			g_string_append_c (sql, '\'');
			break;

		case E2K_FL_FULLSTRING:
		default:
			g_string_append (sql, "= '");
			append_sql_quoted (sql, pv->value);
			g_string_append_c (sql, '\'');
			break;
		}
		return TRUE;

	case E2K_RESTRICTION_PROPERTY:
		if (!sql_relops[rn->res.property.relop])
			return FALSE;

		pv = &rn->res.property.prop;
		g_string_append_printf (sql, "\"%s\" %s ", pv->propname,
					sql_relops[rn->res.property.relop]);

		switch (rn->res.property.type) {
		case E2K_PROP_TYPE_INT:
			g_string_append_printf (sql, "%d",
						GPOINTER_TO_UINT (pv->value));
			break;

		case E2K_PROP_TYPE_BOOL:
			g_string_append (sql, pv->value ? "True" : "False");
			break;

		case E2K_PROP_TYPE_DATE:
			g_string_append_printf (sql,
						"cast (\"%s\" as 'dateTime.tz')",
						(char *)pv->value);
			break;

		default:
			g_string_append_c (sql, '\'');
			append_sql_quoted (sql, pv->value);
			g_string_append_c (sql, '\'');
			break;
		}
		return TRUE;

	case E2K_RESTRICTION_COMPAREPROPS:
		if (!sql_relops[rn->res.property.relop])
			return FALSE;

		g_string_append_printf (sql, "\"%s\" %s \"%s\"",
					rn->res.compare.propname1,
					sql_relops[rn->res.compare.relop],
					rn->res.compare.propname2);
		return TRUE;

	case E2K_RESTRICTION_COMMENT:
		return TRUE;

	case E2K_RESTRICTION_BITMASK:
	case E2K_RESTRICTION_EXIST:
	case E2K_RESTRICTION_SIZE:
	case E2K_RESTRICTION_SUBRESTRICTION:
	default:
		return FALSE;

	}
}

char *
e2k_restriction_to_sql (E2kRestriction *rn)
{
	GString *sql;
	char *ret;

	sql = g_string_new (NULL);
	if (!rn_to_sql (rn, sql, E2K_RESTRICTION_AND)) {
		g_string_free (sql, TRUE);
		return NULL;
	}

	if (sql->len)
		g_string_prepend (sql, "WHERE ");

	ret = sql->str;
	g_string_free (sql, FALSE);
	return ret;
}
