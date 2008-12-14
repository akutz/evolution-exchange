/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2004 Novell, Inc.
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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <utime.h>
#include <string.h>

#include <gtk/gtk.h>
#include <libedataserver/e-data-server-util.h>
#include <e-util/e-folder-map.h>

#include <exchange-types.h>
#include "exchange-migrate.h"

static GtkWidget *window;
static GtkLabel *label;
static GtkProgressBar *progress;

CORBA_short maj=0;
CORBA_short min=0;
CORBA_short rev=0;

#ifndef G_OS_WIN32

/* No need for migration code from evolution-exchange 1.x on Win32.
 * If and when the same code is partly used for migrating between
 * future evolution-exchange versions, that code will have to be
 * revised and ported as necessary.
 */

gchar *label_string = NULL;

static void
setup_progress_dialog ()
{
	GtkWidget *vbox, *hbox, *w;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), _("Migrating Exchange Folders..."));
	gtk_window_set_modal (GTK_WINDOW (window), TRUE);
	gtk_container_set_border_width (GTK_CONTAINER (window), 6);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_widget_show (vbox);
	gtk_container_add ((GtkContainer *) window, vbox);

	label_string = g_strdup_printf (_("The location and hierarchy of the "
                     "Evolution exchange account folders are changed "
                     "since Evolution %d.%d.%d.\n\nPlease be patient while "
                     "Evolution migrates your folders..."), maj, min, rev);

	w = gtk_label_new (label_string);

	gtk_label_set_line_wrap ((GtkLabel *) w, TRUE);
	gtk_widget_show (w);
	gtk_box_pack_start ((GtkBox *) vbox, w, TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_widget_show (hbox);
	gtk_box_pack_start ((GtkBox *) vbox, hbox, TRUE, TRUE, 0);

	label = (GtkLabel *) gtk_label_new ("");
	gtk_widget_show ((GtkWidget *) label);
	gtk_box_pack_start ((GtkBox *) hbox, (GtkWidget *) label, TRUE, TRUE, 0);

	progress = (GtkProgressBar *) gtk_progress_bar_new ();
	gtk_widget_show ((GtkWidget *) progress);
	gtk_box_pack_start ((GtkBox *) hbox, (GtkWidget *) progress, TRUE, TRUE, 0);

	gtk_widget_show (window);
}

static void
show_error_dialog()
{
	GtkWidget *window;
	GtkWidget *error_dialog;
	gchar *err_string;

	err_string = g_strdup_printf ( _("Warning: Evolution could not migrate "
		  			"all the Exchange account data from "
		  			"the version %d.%d.%d. \nThe data "
		  			"hasn't been deleted, but will not be "
		  			"seen by this version of Evolution"),
		  			maj, min, rev);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	error_dialog = gtk_message_dialog_new (GTK_WINDOW (window),
					       GTK_DIALOG_DESTROY_WITH_PARENT,
					       GTK_MESSAGE_ERROR,
					       GTK_BUTTONS_OK,
					       err_string);
	gtk_dialog_run (GTK_DIALOG (error_dialog));
	gtk_widget_destroy (error_dialog);

	g_free (err_string);
}

static void
dialog_close ()
{
	gtk_widget_destroy ((GtkWidget *) window);
	g_free(label_string);
}

static void
dialog_set_folder_name (const char *folder_name)
{
	char *text;

	text = g_strdup_printf (_("Migrating `%s':"), folder_name);
	gtk_label_set_text (label, text);
	g_free (text);

	gtk_progress_bar_set_fraction (progress, 0.0);

	while (gtk_events_pending ())
		gtk_main_iteration ();
}

static void
dialog_set_progress (double percent)
{
	char text[5];

	snprintf (text, sizeof (text), "%d%%", (int) (percent * 100.0f));

	gtk_progress_bar_set_fraction (progress, percent);
	gtk_progress_bar_set_text (progress, text);

	while (gtk_events_pending ())
		gtk_main_iteration ();
}

static gboolean
cp (const char *src, const char *dest, gboolean show_progress)
{
	unsigned char readbuf[65536];
	ssize_t nread, nwritten;
	int errnosav, readfd, writefd;
	size_t total = 0;
	struct stat st;
	struct utimbuf ut;

	/* if the dest file exists and has content, abort - we don't
 	 * want to corrupt their existing data */

        if (stat (dest, &st) == 0 && st.st_size > 0)
		goto ret;

	if (stat (src, &st) == -1)
		goto ret;

	if ((readfd = open (src, O_RDONLY)) == -1)
		goto ret;

	if ((writefd = open (dest, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
                errnosav = errno;
                close (readfd);
                errno = errnosav;
		goto ret;
        }

        do {
                do {
                        nread = read (readfd, readbuf, sizeof (readbuf));
                } while (nread == -1 && errno == EINTR);

                if (nread == 0)
                        break;
                else if (nread < 0)
                        goto exception;
                do {
                        nwritten = write (writefd, readbuf, nread);
                } while (nwritten == -1 && errno == EINTR);

                if (nwritten < nread)
                        goto exception;

                total += nwritten;

                if (show_progress)
                	dialog_set_progress ((((double) total) /
					      ((double) st.st_size)));
        } while (total < st.st_size);

        if (fsync (writefd) == -1)
                goto exception;

        close (readfd);
        if (close (writefd) == -1)
                goto failclose;

        ut.actime = st.st_atime;
        ut.modtime = st.st_mtime;
        utime (dest, &ut);
        chmod (dest, st.st_mode);
 ret:
	return TRUE;

 exception:

        errnosav = errno;
        close (readfd);
        close (writefd);
        errno = errnosav;

 failclose:

        errnosav = errno;
        unlink (dest);
        errno = errnosav;

	return FALSE;
}

static gboolean
cp_r (char *src, const char *dest)
{
	GString *srcpath, *destpath;
	struct dirent *dent;
	size_t slen, dlen;
	struct stat st;
	DIR *dir;

	if (g_mkdir_with_parents (dest, 0777) == -1)
		return FALSE;

	if (!(dir = opendir (src)))
		return TRUE;

	srcpath = g_string_new (src);
	g_string_append_c (srcpath, '/');
	slen = srcpath->len;

	destpath = g_string_new (dest);
	g_string_append_c (destpath, '/');
	dlen = destpath->len;

	dialog_set_folder_name(src);

	while ((dent = readdir (dir))) {
		if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
                	continue;

		g_string_truncate (srcpath, slen);
		g_string_truncate (destpath, dlen);

		g_string_append (srcpath, dent->d_name);
		g_string_append (destpath, dent->d_name);
		if (stat (srcpath->str, &st) == -1)
			continue;

		if (S_ISDIR (st.st_mode)) {
			cp_r (srcpath->str, destpath->str);
		} else {
			cp (srcpath->str, destpath->str, TRUE);
		}
	}

	closedir (dir);

	g_string_free (destpath, TRUE);
	g_string_free (srcpath, TRUE);

	return TRUE;
}

static gchar *
form_dir_path (char *file_name, char *delim)
{
	GString *path = g_string_new (NULL);
	gchar *dir_path;
	char *token;

	token = strtok (file_name, delim);
	while (token != NULL) {
		g_string_append (path, token);
		g_string_append (path, "/subfolders/");
		token = strtok (NULL, delim);
	}
	dir_path = g_strndup(path->str, strlen(path->str) - 12);
	g_string_free (path, TRUE);
	return dir_path;
}

static gchar*
get_contacts_dir_from_filename(const char *migr_file)
{
	char *file_to_be_migrated = g_strdup (migr_file);
	char *dot, *delim = "_", *file_name;
	gchar *dir_path = NULL;

	dot = strchr (file_to_be_migrated, '.');
	if (dot) {
		file_name = g_strndup (file_to_be_migrated, dot - file_to_be_migrated);
		dir_path = form_dir_path (file_name, delim);
		g_free (file_name);
	}
	g_free (file_to_be_migrated);
	return dir_path;
}

/*
 * Parse the names of summary and changes files in
 * ~/evolution/exchange/user@exchange-server/Addressbook and
 * copy them into the respective directories under
 * ~/.evolution/exchange/user@exchange-server
 *
 */
/* FIXME: handling .changes files */
/* FIXME: handling VCARD attributes changes */

static gboolean
migrate_contacts (gchar *src_path, const gchar *dest_path)
{
	gchar *dest_sub_dir = NULL, *summary_file = NULL;
	gchar *contacts_dir = NULL, *src_file, *dest_file;
	struct dirent *dent;
	DIR *dir;
	gboolean ret = TRUE;

	setup_progress_dialog ();

	dir = opendir (src_path);
	if (dir) {
		while ((dent = readdir (dir))) {
			if (!strcmp (dent->d_name, ".") ||
			    !strcmp (dent->d_name, ".."))
				continue;

			/* Parse the file name and form the dir hierarchy */
			dest_sub_dir = get_contacts_dir_from_filename (
								dent->d_name);

			if (dest_sub_dir) {
				contacts_dir = g_build_filename (
							dest_path,
							dest_sub_dir,
							NULL);

				/* Check if it is a summary file */
				summary_file = g_strrstr (dent->d_name,
							  ".summary");
				if (summary_file)
					dest_file = g_build_filename (
							contacts_dir,
							"summary",
							NULL);
				else
					/* FIXME: replace d_name by change_id */
					dest_file = g_build_filename (
							contacts_dir,
							dent->d_name,
							NULL);

				/* Create destination dir, and copy the files */
       				if (g_mkdir_with_parents (contacts_dir, 0777) == -1) {
					ret = FALSE;
					g_free (dest_file);
					g_free (contacts_dir);
                			continue;
				}

				src_file = g_build_filename ( src_path,
							      dent->d_name,
							      NULL);

				ret = cp (src_file, dest_file, TRUE);
				g_free (dest_file);
				g_free (src_file);
				g_free (contacts_dir);
				g_free (dest_sub_dir);
			}
			else {
				ret = FALSE;
				continue;
			}
		}
		closedir(dir);
	}
	dialog_close ();
	return ret;
}

/*
 * Copy the summary files from ~/evolution/mail/exchange to
 * ~/.evolution/exchange/
 */
static gboolean
migrate_mail (gchar *src_path, const gchar *dest_path)
{
	gboolean ret;

	setup_progress_dialog ();
	ret = cp_r (src_path, dest_path);
	dialog_close ();

	return ret;
}

/*
 * copy personal and public folders from ~/evolution/exchange into
 * ~/.evolution/exchange.
 *
 * This includes copying all the folders and metadata.xml files, also
 * Calendar and Tasks, cache files
 */
static gboolean
migrate_common (gchar *src_path, const gchar *dest_path)
{
	gchar *src_dir, *dest_dir;
	gboolean ret;

	setup_progress_dialog ();

	src_dir = g_build_filename (src_path, "personal", NULL);
	dest_dir = g_build_filename(dest_path, "personal", NULL);
	ret = cp_r (src_dir, dest_dir);
	g_free (src_dir);
	g_free (dest_dir);

	src_dir = g_build_filename (src_path, "public", NULL);
	dest_dir = g_build_filename(dest_path, "public", NULL);
	ret = cp_r (src_dir, dest_dir);
	g_free (src_dir);
	g_free (dest_dir);

	dialog_close ();
	return ret;
}

#endif

void
exchange_migrate (const CORBA_short major,
		  const CORBA_short minor,
		  const CORBA_short revision,
		  const gchar *base_dir,
		  char *account_filename)
{
	gboolean ret = TRUE;
        struct stat st;
	gchar *src_path = NULL;

	maj = major; min = minor; rev = revision;

	/* FIXME: version check */
	if (maj == 1 && min <= 5) {
#ifndef G_OS_WIN32
		if (!base_dir)
			return;

		/* This is not needed if done by cp_r() */
        	if (stat (base_dir, &st) == -1) {
			if (errno != ENOENT ||
			    g_mkdir_with_parents (base_dir, 0777) == -1) {
                       		printf ("Failed to create directory `%s': %s",
				base_dir, g_strerror (errno));
                        	return;
                	}
        	}

		src_path = g_build_filename (g_get_home_dir (),
					     "evolution",
					     "exchange",
					     account_filename,
					     NULL);
		ret = migrate_common (src_path, base_dir);
		g_free (src_path);

		src_path = g_build_filename (g_get_home_dir (),
					     "evolution",
					     "exchange",
					     account_filename,
					     "Addressbook",
					     NULL);
		ret = migrate_contacts ( src_path, base_dir);
		g_free (src_path);

		src_path = g_build_filename (g_get_home_dir (),
					     "evolution",
					     "mail",
					     "exchange",
					     account_filename,
					     NULL);
		ret = migrate_mail ( src_path, base_dir);
		g_free (src_path);

		if (ret == FALSE)
			show_error_dialog ();

#endif
	}
}
