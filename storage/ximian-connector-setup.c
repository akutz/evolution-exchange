/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2003, 2004 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gtypes.h>
#include <libgnomeui/gnome-ui-init.h>

#include <e-util/e-dialog-utils.h>
#include <e-util/e-passwords.h>

#include "e2k-utils.h"

#include "exchange-autoconfig-wizard.h"

int
main (int argc, char **argv)
{
	bindtextdomain (PACKAGE, CONNECTOR_LOCALEDIR);
	bind_textdomain_codeset (PACKAGE, "UTF-8");
	textdomain (PACKAGE);

	gnome_program_init ("ximian-connector-setup", VERSION,
			    LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    GNOME_PARAM_HUMAN_READABLE_NAME, _("Ximian Connector for Microsoft Exchange Setup Tool"),
			    NULL);

	exchange_autoconfig_druid_run ();
	e_passwords_shutdown ();
	return 0;
}
