/* ptk-text-renderer.h
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
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
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
    This file is originally copied from gtkcellrenderertext.h of gtk+ library.
    2006.07.16 modified by Hong Jen Yee to produce a simplified text renderer
    which supports center alignment of text to be used in PCMan File Manager
*/

#ifndef __PTK_TEXT_RENDERER_H__
#define __PTK_TEXT_RENDERER_H__

#include <pango/pango.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PTK_TYPE_TEXT_RENDERER (ptk_text_renderer_get_type())
#define PTK_TEXT_RENDERER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), PTK_TYPE_TEXT_RENDERER, PtkTextRenderer))

typedef struct PtkTextRenderer
{
    GtkCellRenderer parent;

    /*< private >*/
    char* text;
    PangoFontDescription* font;
    PangoColor foreground;
    PangoColor background;

    PangoUnderline underline_style;

    unsigned int foreground_set : 1;
    unsigned int background_set : 1;
    unsigned int underline_set : 1;
    unsigned int ellipsize_set : 1;

    int wrap_width;
    PangoEllipsizeMode ellipsize;
    PangoWrapMode wrap_mode;
} PtkTextRenderer;

typedef struct PtkTextRendererClass
{
    GtkCellRendererClass parent_class;
} PtkTextRendererClass;

GType ptk_text_renderer_get_type(void) G_GNUC_CONST;
GtkCellRenderer* ptk_text_renderer_new(void);

G_END_DECLS

#endif /* __PTK_TEXT_RENDERER_H__ */
