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

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

#include <gtk/gtkmain.h>
#include <bonobo/bonobo-main.h>
#include <libgnomeui/gnome-ui-init.h>
#include <libgnomevfs/gnome-vfs-init.h>
#include <gconf/gconf-client.h>

#include <e-util/e-dialog-utils.h>
#include <e-util/e-passwords.h>

#include "e2k-license.h"
#include "e2k-utils.h"

#include "exchange-autoconfig-wizard.h"

int
main (int argc, char **argv)
{
	int status;

	bindtextdomain (PACKAGE, CONNECTOR_LOCALEDIR);
	textdomain (PACKAGE);

	gnome_program_init ("ximian-connector-setup", VERSION,
			    LIBGNOMEUI_MODULE, argc, argv,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    GNOME_PARAM_HUMAN_READABLE_NAME, _("Ximian Connector for Microsoft Exchange Setup Tool"),
			    NULL);

	status = system ("evolution-" EVOLUTION_BASE_VERSION " --setup-only > /dev/null 2>&1");
	if (status == -1 || !WIFEXITED (status) || WEXITSTATUS (status) == 127) {
		e_notice (NULL, GTK_MESSAGE_ERROR,
			  _("Could not start evolution"));
		exit (1);
	}
	if (WEXITSTATUS (status) != 0) {
		/* evolution should have popped up an error already */
		exit (1);
	}

	e2k_license_validate ();

	exchange_autoconfig_druid_run ();
	e_passwords_shutdown ();
	return 0;
}
