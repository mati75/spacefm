/*-
 * Copyright (c) 2004-2006 os-cillation e.K.
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
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

#ifndef __EXO_GDK_PIXBUF_EXTENSIONS_H__
#define __EXO_GDK_PIXBUF_EXTENSIONS_H__

#include <stdbool.h>

#include <gdk/gdk.h>

G_BEGIN_DECLS

GdkPixbuf* exo_gdk_pixbuf_scale_down(GdkPixbuf* source, bool preserve_aspect_ratio, int dest_width,
                                     int dest_height) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

GdkPixbuf* exo_gdk_pixbuf_colorize(const GdkPixbuf* source,
                                   const GdkColor* color) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

GdkPixbuf*
exo_gdk_pixbuf_spotlight(const GdkPixbuf* source) G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

#endif /* !__EXO_GDK_PIXBUF_EXTENSIONS_H__ */
