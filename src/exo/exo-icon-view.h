/*-
 * Copyright (c) 2004-2006  os-cillation e.K.
 * Copyright (c) 2002,2004  Anders Carlsson <andersca@gnu.org>
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
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* Modified by Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * on 2008.05.11 for use in PCManFM */

#ifndef __EXO_ICON_VIEW_H__
#define __EXO_ICON_VIEW_H__

#include <stdbool.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS;

typedef struct ExoIconViewPrivate ExoIconViewPrivate;
typedef struct ExoIconViewClass ExoIconViewClass;
typedef struct ExoIconView ExoIconView;

// clang-format off
#define EXO_TYPE_ICON_VIEW            (exo_icon_view_get_type ())
#define EXO_ICON_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXO_TYPE_ICON_VIEW, ExoIconView))
#define EXO_ICON_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXO_TYPE_ICON_VIEW, ExoIconViewClass))
#define EXO_IS_ICON_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXO_TYPE_ICON_VIEW))
#define EXO_IS_ICON_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EXO_TYPE_ICON_VIEW))
#define EXO_ICON_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), EXO_TYPE_ICON_VIEW, ExoIconViewClass))
// clang-format on

/**
 * ExoIconViewSearchEqualFunc:
 * @model       : the #GtkTreeModel being searched.
 * @column      : the search column set by exo_icon_view_set_search_column().
 * @key         : the key string to compare with.
 * @iter        : the #GtkTreeIter of the current item.
 * @search_data : user data from exo_icon_view_set_search_equal_func().
 *
 * A function used for checking whether a row in @model matches a search key string
 * entered by the user. Note the return value is reversed from what you would normally
 * expect, though it has some similarity to strcmp() returning 0 for equal strings.
 *
 * Return value: %FALSE if the row matches, %TRUE otherwise.
 **/
typedef bool (*ExoIconViewSearchEqualFunc)(GtkTreeModel* model, int column, const char* key,
                                           GtkTreeIter* iter, void* search_data);

/**
 * ExoIconViewSearchPositionFunc:
 * @icon_view     : an #ExoIconView.
 * @search_dialog : the search dialog window to place.
 * @user_data     : user data from exo_icon_view_set_search_position_func().
 *
 * A function used to place the @search_dialog for the @icon_view.
 **/
typedef void (*ExoIconViewSearchPositionFunc)(ExoIconView* icon_view, GtkWidget* search_dialog,
                                              void* user_data);

/**
 * ExoIconViewDropPosition:
 * @EXO_ICON_VIEW_NO_DROP    : no drop indicator.
 * @EXO_ICON_VIEW_DROP_INTO  : drop indicator on an item.
 * @EXO_ICON_VIEW_DROP_LEFT  : drop indicator on the left of an item.
 * @EXO_ICON_VIEW_DROP_RIGHT : drop indicator on the right of an item.
 * @EXO_ICON_VIEW_DROP_ABOVE : drop indicator above an item.
 * @EXO_ICON_VIEW_DROP_BELOW : drop indicator below an item.
 *
 * Specifies whether to display the drop indicator,
 * i.e. where to drop into the icon view.
 **/
typedef enum ExoIconViewDropPosition
{
    EXO_ICON_VIEW_NO_DROP,
    EXO_ICON_VIEW_DROP_INTO,
    EXO_ICON_VIEW_DROP_LEFT,
    EXO_ICON_VIEW_DROP_RIGHT,
    EXO_ICON_VIEW_DROP_ABOVE,
    EXO_ICON_VIEW_DROP_BELOW
} ExoIconViewDropPosition;

/**
 * ExoIconViewLayoutMode:
 * @EXO_ICON_VIEW_LAYOUT_ROWS : layout items in rows.
 * @EXO_ICON_VIEW_LAYOUT_COLS : layout items in columns.
 *
 * Specifies the layouting mode of an #ExoIconView. @EXO_ICON_VIEW_LAYOUT_ROWS
 * is the default, which lays out items vertically in rows from top to bottom.
 * @EXO_ICON_VIEW_LAYOUT_COLS lays out items horizontally in columns from left
 * to right.
 **/
typedef enum ExoIconViewLayoutMode
{
    EXO_ICON_VIEW_LAYOUT_ROWS,
    EXO_ICON_VIEW_LAYOUT_COLS
} ExoIconViewLayoutMode;

typedef struct ExoIconView
{
    GtkContainer __parent__;

    /*< private >*/
    ExoIconViewPrivate* priv;
} ExoIconView;

typedef struct ExoIconViewClass
{
    GtkContainerClass __parent__;

    /* virtual methods */
    void (*set_scroll_adjustments)(ExoIconView* icon_view, GtkAdjustment* hadjustment,
                                   GtkAdjustment* vadjustment);

    /* signals */
    void (*item_activated)(ExoIconView* icon_view, GtkTreePath* path);
    void (*selection_changed)(ExoIconView* icon_view);

    /* Key binding signals */
    void (*select_all)(ExoIconView* icon_view);
    void (*unselect_all)(ExoIconView* icon_view);
    void (*select_cursor_item)(ExoIconView* icon_view);
    void (*toggle_cursor_item)(ExoIconView* icon_view);
    bool (*move_cursor)(ExoIconView* icon_view, GtkMovementStep step, int count);
    bool (*activate_cursor_item)(ExoIconView* icon_view);
    bool (*start_interactive_search)(ExoIconView* icon_view);
} ExoIconViewClass;

GType exo_icon_view_get_type(void) G_GNUC_CONST;

GtkWidget* exo_icon_view_new(void);

GtkTreeModel* exo_icon_view_get_model(const ExoIconView* icon_view);
void exo_icon_view_set_model(ExoIconView* icon_view, GtkTreeModel* model);

#ifndef EXO_DISABLE_DEPRECATED
void exo_icon_view_set_text_column(ExoIconView* icon_view, int column);
void exo_icon_view_set_markup_column(ExoIconView* icon_view, int column);
void exo_icon_view_set_pixbuf_column(ExoIconView* icon_view, int column);
#endif

void exo_icon_view_set_orientation(ExoIconView* icon_view, GtkOrientation orientation);
void exo_icon_view_set_columns(ExoIconView* icon_view, int columns);
void exo_icon_view_set_item_width(ExoIconView* icon_view, int item_width);
void exo_icon_view_set_spacing(ExoIconView* icon_view, int spacing);
void exo_icon_view_set_row_spacing(ExoIconView* icon_view, int row_spacing);
void exo_icon_view_set_column_spacing(ExoIconView* icon_view, int column_spacing);
void exo_icon_view_set_margin(ExoIconView* icon_view, int margin);
void exo_icon_view_set_selection_mode(ExoIconView* icon_view, GtkSelectionMode mode);
void exo_icon_view_set_layout_mode(ExoIconView* icon_view, ExoIconViewLayoutMode layout_mode);
void exo_icon_view_set_single_click(ExoIconView* icon_view, bool single_click);
void exo_icon_view_set_single_click_timeout(ExoIconView* icon_view,
                                            unsigned int single_click_timeout);

void exo_icon_view_widget_to_icon_coords(const ExoIconView* icon_view, int wx, int wy, int* ix,
                                         int* iy);

GtkTreePath* exo_icon_view_get_path_at_pos(const ExoIconView* icon_view, int x, int y);

void exo_icon_view_select_path(ExoIconView* icon_view, GtkTreePath* path);
void exo_icon_view_unselect_path(ExoIconView* icon_view, GtkTreePath* path);
bool exo_icon_view_path_is_selected(const ExoIconView* icon_view, GtkTreePath* path);
GList* exo_icon_view_get_selected_items(const ExoIconView* icon_view);
void exo_icon_view_select_all(ExoIconView* icon_view);
void exo_icon_view_unselect_all(ExoIconView* icon_view);
void exo_icon_view_item_activated(ExoIconView* icon_view, GtkTreePath* path);

bool exo_icon_view_get_cursor(const ExoIconView* icon_view, GtkTreePath** path,
                              GtkCellRenderer** cell);
void exo_icon_view_set_cursor(ExoIconView* icon_view, GtkTreePath* path, GtkCellRenderer* cell,
                              bool start_editing);

void exo_icon_view_scroll_to_path(ExoIconView* icon_view, GtkTreePath* path, bool use_align,
                                  float row_align, float col_align);

/* Drag-and-Drop support */
void exo_icon_view_enable_model_drag_source(ExoIconView* icon_view,
                                            GdkModifierType start_button_mask,
                                            const GtkTargetEntry* targets, int n_targets,
                                            GdkDragAction actions);
void exo_icon_view_enable_model_drag_dest(ExoIconView* icon_view, const GtkTargetEntry* targets,
                                          int n_targets, GdkDragAction actions);
void exo_icon_view_unset_model_drag_source(ExoIconView* icon_view);
void exo_icon_view_unset_model_drag_dest(ExoIconView* icon_view);
void exo_icon_view_set_reorderable(ExoIconView* icon_view, bool reorderable);

/* These are useful to implement your own custom stuff. */
void exo_icon_view_set_drag_dest_item(ExoIconView* icon_view, GtkTreePath* path,
                                      ExoIconViewDropPosition pos);
void exo_icon_view_get_drag_dest_item(ExoIconView* icon_view, GtkTreePath** path,
                                      ExoIconViewDropPosition* pos);
bool exo_icon_view_get_dest_item_at_pos(ExoIconView* icon_view, int drag_x, int drag_y,
                                        GtkTreePath** path, ExoIconViewDropPosition* pos);
#if (GTK_MAJOR_VERSION == 3)
cairo_surface_t* exo_icon_view_create_drag_icon(ExoIconView* icon_view,
#elif (GTK_MAJOR_VERSION == 2)
GdkPixmap* exo_icon_view_create_drag_icon(ExoIconView* icon_view,
#endif
                                                GtkTreePath* path);

/* Interactive search support */
void exo_icon_view_set_enable_search(ExoIconView* icon_view, bool enable_search);
void exo_icon_view_set_search_column(ExoIconView* icon_view, int search_column);
void exo_icon_view_set_search_equal_func(ExoIconView* icon_view,
                                         ExoIconViewSearchEqualFunc search_equal_func,
                                         void* search_equal_data,
                                         GDestroyNotify search_equal_destroy);
void exo_icon_view_set_search_position_func(ExoIconView* icon_view,
                                            ExoIconViewSearchPositionFunc search_position_func,
                                            void* search_position_data,
                                            GDestroyNotify search_position_destroy);

bool exo_icon_view_is_rubber_banding_active(ExoIconView* icon_view); // sfm

G_END_DECLS;

#endif /* __EXO_ICON_VIEW_H__ */
