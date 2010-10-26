#ifndef __GTK_COMPAT_H__
#define __GTK_COMPAT_H__

#include <gtk/gtk.h>

/* Provide a GTK+ compatibility layer. */

#if !GTK_CHECK_VERSION (2,23,0)
#define gtk_combo_box_text_new			gtk_combo_box_new_text
#define gtk_combo_box_text_append_text		gtk_combo_box_append_text
#define gtk_combo_box_text_remove		gtk_combo_box_remove_text
#define gtk_combo_box_text_prepend_text		gtk_combo_box_prepend_text
#define gtk_combo_box_text_get_active_text	gtk_combo_box_get_active_text
#define GTK_COMBO_BOX_TEXT			GTK_COMBO_BOX
#define GTK_IS_COMBO_BOX_TEXT			GTK_IS_COMBO_BOX
#define GtkComboBoxText				GtkComboBox
#endif

#if GTK_CHECK_VERSION (2,23,0)
#define GTK_COMBO_BOX_ENTRY			GTK_COMBO_BOX
#define GTK_IS_COMBO_BOX_ENTRY			GTK_IS_COMBO_BOX
#else
#define gtk_combo_box_set_entry_text_column \
	gtk_combo_box_entry_set_text_column
#endif

#endif /* __GTK_COMPAT_H__ */
