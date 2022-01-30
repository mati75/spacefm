/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#ifndef __EXO_ICON_CHOOSER_DIALOG_H__
#define __EXO_ICON_CHOOSER_DIALOG_H__

#include <stdbool.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct ExoIconChooserDialogPrivate ExoIconChooserDialogPrivate;

// clang-format off
#define EXO_TYPE_ICON_CHOOSER_DIALOG            (exo_icon_chooser_dialog_get_type ())
#define EXO_ICON_CHOOSER_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXO_TYPE_ICON_CHOOSER_DIALOG, ExoIconChooserDialog))
#define EXO_ICON_CHOOSER_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXO_TYPE_ICON_CHOOSER_DIALOG, ExoIconChooserDialogClass))
#define EXO_IS_ICON_CHOOSER_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXO_TYPE_ICON_CHOOSER_DIALOG))
#define EXO_IS_ICON_CHOOSER_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXO_TYPE_ICON_CHOOSER_DIALOG))
#define EXO_ICON_CHOOSER_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), EXO_TYPE_ICON_CHOOSER_DIALOG, ExoIconChooserDialogClass))
// clang-format on

typedef struct ExoIconChooserDialogClass
{
    /*< private >*/
    GtkDialogClass __parent__;

    /* reserved for future expansion */
    void (*reserved1)(void);
    void (*reserved2)(void);
    void (*reserved3)(void);
    void (*reserved4)(void);
    void (*reserved5)(void);
    void (*reserved6)(void);
} ExoIconChooserDialogClass;

typedef struct ExoIconChooserDialog
{
    /*< private >*/
    GtkDialog __parent__;
} ExoIconChooserDialog;

GType exo_icon_chooser_dialog_get_type(void) G_GNUC_CONST;

GtkWidget*
exo_icon_chooser_dialog_new(const char* title, GtkWindow* parent, const char* first_button_text,
                            ...) G_GNUC_MALLOC G_GNUC_NULL_TERMINATED G_GNUC_WARN_UNUSED_RESULT;

char* exo_icon_chooser_dialog_get_icon(ExoIconChooserDialog* icon_chooser_dialog)
    G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;
bool exo_icon_chooser_dialog_set_icon(ExoIconChooserDialog* icon_chooser_dialog, const char* icon);

G_END_DECLS

#endif /* !__EXO_ICON_CHOOSER_DIALOG_H__ */
