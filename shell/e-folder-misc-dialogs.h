/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-folder-misc-dialogs.h
 *
 * Copyright (C) 2000-2004 Novell, Inc.
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

#ifndef E_FOLDER_MISC_DIALOGS_H
#define E_FOLDER_MISC_DIALOGS_H

#include "e-storage-set-view.h"

void e_folder_delete_dialog (EStorageSetView *storage_set_view,
			     EFolder         *folder,
			     const char      *folder_path);

void e_folder_rename_dialog (EStorageSetView *storage_set_view,
			     EFolder         *folder);

void e_folder_add_foreign_dialog    (EStorageSetView *storage_set_view);
void e_folder_remove_foreign_dialog (EStorageSetView *storage_set_view,
				     EFolder         *folder,
				     const char      *folder_path);

#endif /* E_FOLDER_MISC_DIALOGS_H */
