/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-creation-dialog.h
 *
 * Copyright (C) 2000-2003 Ximian, Inc.
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
 *
 * Author: Ettore Perazzoli
 */

#ifndef E_FOLDER_CREATION_DIALOG_H
#define E_FOLDER_CREATION_DIALOG_H

#include <gtk/gtkwindow.h>

typedef enum {
	E_FOLDER_CREATION_DIALOG_RESULT_SUCCESS,
	E_FOLDER_CREATION_DIALOG_RESULT_FAIL,
	E_FOLDER_CREATION_DIALOG_RESULT_CANCEL
} EFolderCreationDialogResult;

typedef void (* EFolderCreationDialogCallback) (EStorageSet *storage_set,
						EFolderCreationDialogResult result,
						const char *path,
						gpointer data);

void  e_folder_creation_dialog  (EStorageSetView *storage_set_view,
				 EFolder         *default_parent,
				 const char      *default_type,
				 EFolderCreationDialogCallback  result_callback,
				 gpointer         result_callback_data);

#endif /* E_FOLDER_CREATION_DIALOG_H */
