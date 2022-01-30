/*-
 * Copyright (c) 2006 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2015 OmegaPhil <OmegaPhil@startmail.com>
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

// sfm-gtk3
#include <gtk/gtk.h>

#include <sys/stat.h>
#include <string.h>

#include "exo-gdk-pixbuf-extensions.h"
#include "exo-private.h"
#include "exo-thumbnail-preview.h"
#include "exo-string.h"
#include "exo-utils.h"

#include "../vfs/vfs-thumbnail-loader.h"

// From exo-thumbnail.h
/**
 * ExoThumbnailSize:
 * @EXO_THUMBNAIL_SIZE_NORMAL : normal sized thumbnails (up to 128px).
 * @EXO_THUMBNAIL_SIZE_LARGE  : large sized thumbnails.
 *
 * Thumbnail sizes used by the thumbnail database.
 **/

typedef enum ExoThumbnailSize
{
    EXO_THUMBNAIL_SIZE_NORMAL = 128,
    EXO_THUMBNAIL_SIZE_LARGE = 256,
} ExoThumbnailSize;

typedef struct ExoThumbnailPreviewClass
{
    GtkFrameClass __parent__;
} ExoThumbnailPreviewClass;

G_DEFINE_TYPE(ExoThumbnailPreview, exo_thumbnail_preview, GTK_TYPE_FRAME)

static void exo_thumbnail_preview_class_init(ExoThumbnailPreviewClass* klass)
{
}

static void exo_thumbnail_preview_init(ExoThumbnailPreview* thumbnail_preview)
{
    GtkWidget* button;
    GtkWidget* label;
    GtkWidget* ebox;
    GtkWidget* vbox;
    GtkWidget* box;

    gtk_frame_set_shadow_type(GTK_FRAME(thumbnail_preview), GTK_SHADOW_IN);
    gtk_widget_set_sensitive(GTK_WIDGET(thumbnail_preview), FALSE);

    ebox = gtk_event_box_new();

#if (GTK_MAJOR_VERSION == 3)
    /* TODO: Gtk3 */
    gtk_widget_override_background_color(ebox,
                                         GTK_STATE_NORMAL,
                                         &gtk_widget_get_style(ebox)->base[GTK_STATE_INSENSITIVE]);
#elif (GTK_MAJOR_VERSION == 2)
    /* IgnorantGuru wants the background of the thumbnail widget to be grey/
     * black depending on the theme, rather than the default white, which is
     * associated with GTK_STATE_NORMAL
     * Note that this event box is what defines the colour, not thumbnail_preview */
    gtk_widget_modify_bg(ebox,
                         GTK_STATE_NORMAL,
                         &gtk_widget_get_style(ebox)->base[GTK_STATE_INSENSITIVE]);
#endif
    gtk_container_add(GTK_CONTAINER(thumbnail_preview), ebox);
    gtk_widget_show(ebox);
#if (GTK_MAJOR_VERSION == 3)
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#elif (GTK_MAJOR_VERSION == 2)
    vbox = gtk_vbox_new(FALSE, 0);
#endif
    gtk_container_add(GTK_CONTAINER(ebox), vbox);
    gtk_widget_show(vbox);

    button = gtk_button_new();
    g_signal_connect(G_OBJECT(button), "button-press-event", G_CALLBACK(exo_noop_true), NULL);
    g_signal_connect(G_OBJECT(button), "button-release-event", G_CALLBACK(exo_noop_true), NULL);
    g_signal_connect(G_OBJECT(button), "enter-notify-event", G_CALLBACK(exo_noop_true), NULL);
    g_signal_connect(G_OBJECT(button), "leave-notify-event", G_CALLBACK(exo_noop_true), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
    gtk_widget_show(button);

    label = gtk_label_new(_("Preview"));
    g_object_set(label, "xalign", 0.0f, "yalign", 0.5f, NULL);
    gtk_container_add(GTK_CONTAINER(button), label);
    gtk_widget_show(label);
#if (GTK_MAJOR_VERSION == 3)
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
#elif (GTK_MAJOR_VERSION == 2)
    box = gtk_vbox_new(FALSE, 2);
#endif
    gtk_container_set_border_width(GTK_CONTAINER(box), 2);
    gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);
    gtk_widget_show(box);

    thumbnail_preview->image = gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_size_request(thumbnail_preview->image,
                                EXO_THUMBNAIL_SIZE_NORMAL + 2 * 12,
                                EXO_THUMBNAIL_SIZE_NORMAL + 2 * 12);
    gtk_image_set_pixel_size(GTK_IMAGE(thumbnail_preview->image), EXO_THUMBNAIL_SIZE_NORMAL / 2);
    gtk_box_pack_start(GTK_BOX(box), thumbnail_preview->image, FALSE, FALSE, 0);
    gtk_widget_show(thumbnail_preview->image);

    thumbnail_preview->name_label = gtk_label_new(_("No file selected"));
    gtk_label_set_justify(GTK_LABEL(thumbnail_preview->name_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_ellipsize(GTK_LABEL(thumbnail_preview->name_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), thumbnail_preview->name_label, FALSE, FALSE, 0);
    gtk_widget_show(thumbnail_preview->name_label);

    thumbnail_preview->size_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(box), thumbnail_preview->size_label, FALSE, FALSE, 0);
    gtk_widget_show(thumbnail_preview->size_label);
}

/**
 * _exo_thumbnail_preview_new:
 *
 * Allocates a new #ExoThumbnailPreview instance.
 *
 * Returns: the newly allocated #ExoThumbnailPreview.
 **/
GtkWidget* _exo_thumbnail_preview_new(void)
{
    return g_object_new(EXO_TYPE_THUMBNAIL_PREVIEW, NULL);
}

/**
 * _exo_thumbnail_preview_set_uri:
 * @thumbnail_preview : an #ExoThumbnailPreview.
 * @uri               : the new URI for which to show a preview or %NULL.
 *
 * Updates the @thumbnail_preview to display a preview of the specified @uri.
 **/
void _exo_thumbnail_preview_set_uri(ExoThumbnailPreview* thumbnail_preview, const char* uri)
{
    struct stat statb;
    GdkPixbuf* thumbnail;
    char* icon_name = NULL;
    char* size_name = NULL;
    char* displayname;
    char* filename;
    char* slash;

    _exo_return_if_fail(EXO_IS_THUMBNAIL_PREVIEW(thumbnail_preview));

    /* Check if we have an URI to preview */
    if (G_UNLIKELY(uri == NULL))
    {
        /* The preview widget is insensitive if we don't have an URI */
        gtk_widget_set_sensitive(GTK_WIDGET(thumbnail_preview), FALSE);
        gtk_image_set_from_icon_name(GTK_IMAGE(thumbnail_preview->image),
                                     "image-missing",
                                     GTK_ICON_SIZE_DIALOG);
        gtk_label_set_text(GTK_LABEL(thumbnail_preview->name_label), _("No file selected"));
    }
    else
    {
        /* Make the preview widget appear sensitive */
        gtk_widget_set_sensitive(GTK_WIDGET(thumbnail_preview), TRUE);

        /* Check if we have a local file here */
        filename = g_filename_from_uri(uri, NULL, NULL);
        if (G_LIKELY(filename != NULL))
        {
            /* Try to stat the file */
            if (stat(filename, &statb) == 0)
            {
                /* icon and size label depends on the mode */
                if (S_ISBLK(statb.st_mode))
                {
                    icon_name = g_strdup("drive-harddisk");
                    size_name = g_strdup(_("Block Device"));
                }
                else if (S_ISCHR(statb.st_mode))
                {
                    icon_name = g_strdup("drive-harddisk");
                    size_name = g_strdup(_("Character Device"));
                }
                else if (S_ISDIR(statb.st_mode))
                {
                    icon_name = g_strdup("gtk-directory");
                    size_name = g_strdup(_("Directory"));
                }
                else if (S_ISFIFO(statb.st_mode))
                {
                    icon_name = g_strdup("drive-harddisk");
                    size_name = g_strdup(_("FIFO"));
                }
                else if (S_ISSOCK(statb.st_mode))
                {
                    icon_name = g_strdup("drive-harddisk");
                    size_name = g_strdup(_("Socket"));
                }
                else if (S_ISREG(statb.st_mode))
                {
                    if (G_UNLIKELY((unsigned long)statb.st_size > 1024ul * 1024ul * 1024ul))
                        size_name =
                            g_strdup_printf("%0.1f GB", statb.st_size / (1024.0 * 1024.0 * 1024.0));
                    else if ((unsigned long)statb.st_size > 1024ul * 1024ul)
                        size_name = g_strdup_printf("%0.1f MB", statb.st_size / (1024.0 * 1024.0));
                    else if ((unsigned long)statb.st_size > 1024ul)
                        size_name = g_strdup_printf("%0.1f kB", statb.st_size / 1024.0);
                    else
                        size_name = g_strdup_printf("%lu B", (unsigned long)statb.st_size);
                }
            }

            /* Determine the basename from the filename */
            displayname = g_filename_display_basename(filename);
        }
        else
        {
            /* Determine the basename from the URI */
            slash = strrchr(uri, '/');
            if (G_LIKELY(!exo_str_is_empty(slash)))
                displayname = g_filename_display_name(slash + 1);
            else
                displayname = g_filename_display_name(uri);
        }

        /* Check if we have an icon-name */
        if (G_UNLIKELY(icon_name != NULL))
        {
            /* Setup the named icon then */
            gtk_image_set_from_icon_name(GTK_IMAGE(thumbnail_preview->image),
                                         icon_name,
                                         GTK_ICON_SIZE_DIALOG);
            g_free(icon_name);
        }
        else
        {
            /* Try to load a thumbnail for the URI */
            // thumbnail = _exo_thumbnail_get_for_uri (uri, EXO_THUMBNAIL_SIZE_NORMAL, NULL);
            thumbnail = vfs_thumbnail_load_for_uri(uri, EXO_THUMBNAIL_SIZE_NORMAL, 0);
            if (thumbnail == NULL && G_LIKELY(filename != NULL))
            {
                /* But we can try to generate a thumbnail */
                // thumbnail = _exo_thumbnail_get_for_file (filename, EXO_THUMBNAIL_SIZE_NORMAL,
                // NULL);
                thumbnail = vfs_thumbnail_load_for_file(filename, EXO_THUMBNAIL_SIZE_NORMAL, 0);
            }

            /* check if we have a thumbnail */
            if (G_LIKELY(thumbnail != NULL))
            {
                /* setup the thumbnail for the image (using a frame if possible) */
                gtk_image_set_from_pixbuf(GTK_IMAGE(thumbnail_preview->image), thumbnail);
                g_object_unref(G_OBJECT(thumbnail));
            }
            else
            {
                /* no thumbnail, cannot display anything useful then */
                gtk_image_set_from_icon_name(GTK_IMAGE(thumbnail_preview->image),
                                             "image-missing",
                                             GTK_ICON_SIZE_DIALOG);
            }
        }

        /* Setup the name label */
        gtk_label_set_text(GTK_LABEL(thumbnail_preview->name_label), displayname);

        /* Cleanup */
        g_free(displayname);
        g_free(filename);
    }

    /* Setup the new size label */
    gtk_label_set_text(GTK_LABEL(thumbnail_preview->size_label),
                       (size_name != NULL) ? size_name : "");
    g_free(size_name);
}

#define __EXO_THUMBNAIL_PREVIEW_C__
