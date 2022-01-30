/*
 *  C Interface: ptkfileiconrenderer
 *
 * Description: PtkFileIconRenderer is used to render file icons
 *
 *
 * Author: Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>, (C) 2006
 *
 * Copyright: See COPYING file that comes with this distribution
 *
 */

#ifndef _PTK_FILE_ICON_RENDERER_H_
#define _PTK_FILE_ICON_RENDERER_H_

#include <stdbool.h>

#include <gtk/gtk.h>

#include "../vfs/vfs-file-info.h"

G_BEGIN_DECLS

#define PTK_TYPE_FILE_ICON_RENDERER (ptk_file_icon_renderer_get_type())
#define PTK_FILE_ICON_RENDERER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), PTK_TYPE_FILE_ICON_RENDERER, PtkFileIconRenderer))

typedef struct PtkFileIconRenderer
{
    GtkCellRendererPixbuf parent;

    /* Private */
    /* FIXME: draw some additional marks for symlinks */
    VFSFileInfo* info;
    /* long flags; */
    bool follow_state;
} PtkFileIconRenderer;

typedef struct PtkFileIconRendererClass
{
    GtkCellRendererPixbufClass parent_class;
} PtkFileIconRendererClass;

GType ptk_file_icon_renderer_get_type(void);

GtkCellRenderer* ptk_file_icon_renderer_new(void);

G_END_DECLS

#endif /* _PTK_FILE_ICON_RENDERER_H_ */
