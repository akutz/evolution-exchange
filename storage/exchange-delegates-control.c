/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* ExchangeDelegatesControl: Config dialog control for delegation */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-delegates-control.h"
#include "exchange-component.h"
#include "exchange-account.h"
#include "e2k-utils.h"

#include <gtk/gtklabel.h>

#define PARENT_TYPE EVOLUTION_TYPE_CONFIG_CONTROL
static EvolutionConfigControlClass *parent_class = NULL;

static void dispose (GObject *);

static void
class_init (GObjectClass *object_class)
{
	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->dispose = dispose;
}

static void
dispose (GObject *object)
{
	ExchangeDelegatesControl *control =
		EXCHANGE_DELEGATES_CONTROL (object);

	if (control->self_dn) {
		g_free (control->self_dn);
		control->self_dn = NULL;
	}

	if (control->xml) {
		g_object_unref (control->xml);
		control->xml = NULL;
	}

	exchange_delegates_delegates_destroy (control);
	exchange_delegates_delegators_destroy (control);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}


E2K_MAKE_TYPE (exchange_delegates_control, ExchangeDelegatesControl, class_init, NULL, PARENT_TYPE)


void
exchange_delegates_control_get_self_dn (ExchangeDelegatesControl *control)
{
	E2kGlobalCatalog *gc;
	E2kGlobalCatalogStatus status;
	E2kGlobalCatalogEntry *entry;

	gc = exchange_account_get_global_catalog (control->account);
	status = e2k_global_catalog_lookup (
		gc, E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN,
		control->account->legacy_exchange_dn, 0, &entry);
	if (status != E2K_GLOBAL_CATALOG_OK)
		return;

	control->self_dn = g_strdup (entry->dn);
	e2k_global_catalog_entry_free (gc, entry);
}

static void
control_apply_cb (EvolutionConfigControl *config_control, gpointer user_data)
{
	ExchangeDelegatesControl *control =
		EXCHANGE_DELEGATES_CONTROL (config_control);

	exchange_delegates_delegates_apply (control);
	exchange_delegates_delegators_apply (control);
}


static BonoboObject *
dummy_control (const char *message)
{
	GtkWidget *label;

	label = gtk_label_new (message);
	gtk_widget_show (label);

	return BONOBO_OBJECT (evolution_config_control_new (label));
}

BonoboObject *
exchange_delegates_control_new (void)
{
	ExchangeDelegatesControl *control;
	ExchangeAccount *account;
	GtkWidget *notebook;

	account = exchange_component_get_account_for_uri (NULL);
	if (!account)
		return dummy_control (_("No Exchange accounts configured."));

	control = g_object_new (EXCHANGE_TYPE_DELEGATES_CONTROL, NULL);

	control->account = account;
	g_object_ref (account);

	/* The gui */
	control->xml = glade_xml_new (CONNECTOR_GLADEDIR "/exchange-delegates.glade",
				      "delegates_notebook", NULL);
	if (!control->xml) {
		bonobo_object_unref (BONOBO_OBJECT (control));
		return dummy_control (_("Unable to load delegate configuration UI."));
	}

	/* Put the (already parentless) glade widgets into the control */
	notebook = glade_xml_get_widget (control->xml, "delegates_notebook");
	evolution_config_control_construct (EVOLUTION_CONFIG_CONTROL (control),
					    notebook);

	/* Set up the two pages */
	exchange_delegates_delegates_construct (control);
	exchange_delegates_delegators_construct (control);

	/* Set up config control signals */
	g_signal_connect (control, "apply", G_CALLBACK (control_apply_cb), NULL);

	return BONOBO_OBJECT (control);
}
