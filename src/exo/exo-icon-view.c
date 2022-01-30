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

#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <gdk/gdkkeysyms.h>

#include "exo-icon-view.h"
#include "exo-string.h"
#include "exo-marshal.h"
#include "exo-private.h"
#include "exo-common.h"

#include "../ptk/ptk-utils.h"

GType exo_icon_view_layout_mode_get_type(void)
{
    static GType type = 0;
    if (type == 0)
    {
        static const GEnumValue values[] = {
            {EXO_ICON_VIEW_LAYOUT_ROWS, "EXO_ICON_VIEW_LAYOUT_ROWS", "rows"},
            {EXO_ICON_VIEW_LAYOUT_COLS, "EXO_ICON_VIEW_LAYOUT_COLS", "cols"},
            {0, NULL, NULL}};
        type = g_enum_register_static("ExoIconViewLayoutMode", values);
    }
    return type;
}
#define EXO_TYPE_ICON_VIEW_LAYOUT_MODE (exo_icon_view_layout_mode_get_type())
/* enumerations from "exo-mount-point.h" */

/* the search dialog timeout (in ms) */
#define EXO_ICON_VIEW_SEARCH_DIALOG_TIMEOUT (5000)

#define SCROLL_EDGE_SIZE 15

/* Property identifiers */
enum
{
    PROP_0,
    PROP_PIXBUF_COLUMN,
    PROP_TEXT_COLUMN,
    PROP_MARKUP_COLUMN,
    PROP_SELECTION_MODE,
    PROP_LAYOUT_MODE,
    PROP_ORIENTATION,
    PROP_MODEL,
    PROP_COLUMNS,
    PROP_ITEM_WIDTH,
    PROP_SPACING,
    PROP_ROW_SPACING,
    PROP_COLUMN_SPACING,
    PROP_MARGIN,
    PROP_REORDERABLE,
    PROP_SINGLE_CLICK,
    PROP_SINGLE_CLICK_TIMEOUT,
    PROP_ENABLE_SEARCH,
    PROP_SEARCH_COLUMN,
#if (GTK_MAJOR_VERSION == 3)
    PROP_HADJUSTMENT,
    PROP_VADJUSTMENT,
    PROP_HSCROLL_POLICY,
    PROP_VSCROLL_POLICY,
#endif
};

/* Signal identifiers */
enum
{
    ITEM_ACTIVATED,
    SELECTION_CHANGED,
    SELECT_ALL,
    UNSELECT_ALL,
    SELECT_CURSOR_ITEM,
    TOGGLE_CURSOR_ITEM,
    MOVE_CURSOR,
    ACTIVATE_CURSOR_ITEM,
    START_INTERACTIVE_SEARCH,
    LAST_SIGNAL
};

/* Icon view flags */
typedef enum ExoIconViewFlags
{
    EXO_ICON_VIEW_DRAW_KEYFOCUS = (1l << 0), /* whether to draw keyboard focus */
    EXO_ICON_VIEW_ITERS_PERSIST =
        (1l << 1), /* whether current model provides persistent iterators */
} ExoIconViewFlags;

// clang-format off
#define EXO_ICON_VIEW_SET_FLAG(icon_view, flag)   G_STMT_START{ (EXO_ICON_VIEW (icon_view)->priv->flags |= flag); }G_STMT_END
#define EXO_ICON_VIEW_UNSET_FLAG(icon_view, flag) G_STMT_START{ (EXO_ICON_VIEW (icon_view)->priv->flags &= ~(flag)); }G_STMT_END
#define EXO_ICON_VIEW_FLAG_SET(icon_view, flag)   ((EXO_ICON_VIEW (icon_view)->priv->flags & (flag)) == (flag))
// clang-format on

typedef struct ExoIconViewItem
{
    GtkTreeIter iter;

    /* Bounding box (a value of -1 for width indicates
     * that the item needs to be layouted first)
     */
    GdkRectangle area;

    /* Individual cells.
     * box[i] is the actual area occupied by cell i,
     * before, after are used to calculate the cell
     * area relative to the box.
     * See exo_icon_view_get_cell_area().
     */
    int n_cells;
    GdkRectangle* box;
    int* before;
    int* after;

    unsigned int row: ((sizeof(unsigned int) / 2) * 8) - 1;
    unsigned int col: ((sizeof(unsigned int) / 2) * 8) - 1;
    unsigned int selected : 1;
    unsigned int selected_before_rubberbanding : 1;
} ExoIconViewItem;

typedef struct ExoIconViewCellInfo
{
    GtkCellRenderer* cell;
    unsigned int expand : 1;
    unsigned int pack : 1;
    unsigned int editing : 1;
    int position;
    GSList* attributes;
    GtkCellLayoutDataFunc func;
    void* func_data;
    GDestroyNotify destroy;
    bool is_text;
} ExoIconViewCellInfo;

typedef struct ExoIconViewChild
{
    ExoIconViewItem* item;
    GtkWidget* widget;
    int cell;
} ExoIconViewChild;

typedef struct ExoIconViewPrivate
{
    int width, height;
    int rows, cols;

    GtkSelectionMode selection_mode;

    ExoIconViewLayoutMode layout_mode;

    GdkWindow* bin_window;

    GList* children;

    GtkTreeModel* model;

    GList* items;

    GtkAdjustment* hadjustment;
    GtkAdjustment* vadjustment;
#if (GTK_MAJOR_VERSION == 3)
    GtkScrollablePolicy hscroll_policy;
    GtkScrollablePolicy vscroll_policy;
#endif

    unsigned int layout_idle_id;

    bool doing_rubberband;
    int rubberband_x1, rubberband_y1;
    int rubberband_x2, rubberband_y2;
#if (GTK_MAJOR_VERSION == 3)
    GdkRGBA rubberband_border_color;
    GdkRGBA rubberband_fill_color;
#elif (GTK_MAJOR_VERSION == 2)
    GdkColor* rubberband_border_color;
    GdkColor* rubberband_fill_color;
#endif

    unsigned int scroll_timeout_id;
    int scroll_value_diff;
    int event_last_x, event_last_y;

    ExoIconViewItem* anchor_item;
    ExoIconViewItem* cursor_item;
    ExoIconViewItem* edited_item;
    GtkCellEditable* editable;
    ExoIconViewItem* prelit_item;

    ExoIconViewItem* last_single_clicked;

    GList* cell_list;
    int n_cells;

    int cursor_cell;

    GtkOrientation orientation;

    int columns;
    int item_width;
    int spacing;
    int row_spacing;
    int column_spacing;
    int margin;

    int text_column;
    int markup_column;
    int pixbuf_column;

    int pixbuf_cell;
    int text_cell;

    /* Drag-and-drop. */
    GdkModifierType start_button_mask;
    int pressed_button;
    int press_start_x;
    int press_start_y;

    GtkTargetList* source_targets;
    GdkDragAction source_actions;

    GtkTargetList* dest_targets;
    GdkDragAction dest_actions;

    GtkTreeRowReference* dest_item;
    ExoIconViewDropPosition dest_pos;

    /* delayed scrolling */
    GtkTreeRowReference* scroll_to_path;
    float scroll_to_row_align;
    float scroll_to_col_align;
    unsigned int scroll_to_use_align : 1;

    /* misc flags */
    unsigned int source_set : 1;
    unsigned int dest_set : 1;
    unsigned int reorderable : 1;
    unsigned int empty_view_drop : 1;

    unsigned int ctrl_pressed : 1;
    unsigned int shift_pressed : 1;

    /* Single-click support
     * The single_click_timeout is the timeout after which the
     * prelited item will be automatically selected in single
     * click mode (0 to disable).
     */
    unsigned int single_click : 1;
    unsigned int single_click_timeout;
    unsigned int single_click_timeout_id;
    unsigned int single_click_timeout_state;

    /* Interactive search support */
    unsigned int enable_search : 1;
    unsigned int search_imcontext_changed : 1;
    int search_column;
    int search_selected_iter;
    unsigned int search_timeout_id;
    bool search_disable_popdown;
    ExoIconViewSearchEqualFunc search_equal_func;
    void* search_equal_data;
    GDestroyNotify search_equal_destroy;
    ExoIconViewSearchPositionFunc search_position_func;
    void* search_position_data;
    GDestroyNotify search_position_destroy;
    int search_entry_changed_id;
    GtkWidget* search_entry;
    GtkWidget* search_window;

    /* ExoIconViewFlags */
    unsigned int flags;
} ExoIconViewPrivate;

#define EXO_ICON_VIEW_CELL_INFO(obj) ((ExoIconViewCellInfo*)(obj))
#define EXO_ICON_VIEW_CHILD(obj)     ((ExoIconViewChild*)(obj))
#define EXO_ICON_VIEW_ITEM(obj)      ((ExoIconViewItem*)(obj))

static void exo_icon_view_class_init(ExoIconViewClass* klass);
static void exo_icon_view_cell_layout_init(GtkCellLayoutIface* iface);
static void exo_icon_view_dispose(GObject* object);
static void exo_icon_view_finalize(GObject* object);
static void exo_icon_view_get_property(GObject* object, unsigned int prop_id, GValue* value,
                                       GParamSpec* pspec);
static void exo_icon_view_set_property(GObject* object, unsigned int prop_id, const GValue* value,
                                       GParamSpec* pspec);
static void exo_icon_view_realize(GtkWidget* widget);
static void exo_icon_view_unrealize(GtkWidget* widget);
static void exo_icon_view_size_allocate(GtkWidget* widget, GtkAllocation* allocation);
static void exo_icon_view_style_set(GtkWidget* widget, GtkStyle* previous_style);
#if (GTK_MAJOR_VERSION == 3)
static bool exo_icon_view_draw(GtkWidget* widget, cairo_t* cr);
#elif (GTK_MAJOR_VERSION == 2)
static bool exo_icon_view_expose_event(GtkWidget* widget, GdkEventExpose* event);
#endif
static void exo_icon_view_set_adjustments(ExoIconView* icon_view, GtkAdjustment* hadj,
                                          GtkAdjustment* vadj);
static void exo_icon_view_size_request(GtkWidget* widget, GtkRequisition* requisition);
static bool exo_icon_view_motion_notify_event(GtkWidget* widget, GdkEventMotion* event);
static bool exo_icon_view_button_press_event(GtkWidget* widget, GdkEventButton* event);
static bool exo_icon_view_button_release_event(GtkWidget* widget, GdkEventButton* event);
static bool exo_icon_view_scroll_event(GtkWidget* widget, GdkEventScroll* event);
static bool exo_icon_view_key_press_event(GtkWidget* widget, GdkEventKey* event);
static bool exo_icon_view_focus_out_event(GtkWidget* widget, GdkEventFocus* event);
static bool exo_icon_view_leave_notify_event(GtkWidget* widget, GdkEventCrossing* event);
static void exo_icon_view_remove(GtkContainer* container, GtkWidget* widget);
static void exo_icon_view_forall(GtkContainer* container, bool include_internals,
                                 GtkCallback callback, void* callback_data);
static void exo_icon_view_real_select_all(ExoIconView* icon_view);
static void exo_icon_view_real_unselect_all(ExoIconView* icon_view);
static void exo_icon_view_real_select_cursor_item(ExoIconView* icon_view);
static void exo_icon_view_real_toggle_cursor_item(ExoIconView* icon_view);
static bool exo_icon_view_real_activate_cursor_item(ExoIconView* icon_view);
static bool exo_icon_view_real_start_interactive_search(ExoIconView* icon_view);
static void exo_icon_view_adjustment_changed(GtkAdjustment* adjustment, ExoIconView* icon_view);
static int exo_icon_view_layout_cols(ExoIconView* icon_view, int item_height, int* x,
                                     int* maximum_height, int max_rows);
static int exo_icon_view_layout_rows(ExoIconView* icon_view, int item_width, int* y,
                                     int* maximum_width, int max_cols);
static void exo_icon_view_layout(ExoIconView* icon_view);
#if (GTK_MAJOR_VERSION == 3)
static void exo_icon_view_paint_item(ExoIconView* icon_view, ExoIconViewItem* item,
                                     GdkRectangle* area, cairo_t* cr, int x, int y,
                                     bool draw_focus);
#elif (GTK_MAJOR_VERSION == 2)
static void exo_icon_view_paint_item(ExoIconView* icon_view, ExoIconViewItem* item,
                                     GdkRectangle* area, GdkDrawable* drawable, int x, int y,
                                     bool draw_focus);
#endif
static void exo_icon_view_queue_draw_item(ExoIconView* icon_view, ExoIconViewItem* item);
static void exo_icon_view_queue_layout(ExoIconView* icon_view);
static void exo_icon_view_set_cursor_item(ExoIconView* icon_view, ExoIconViewItem* item,
                                          int cursor_cell);
static void exo_icon_view_start_rubberbanding(ExoIconView* icon_view, int x, int y);
static void exo_icon_view_stop_rubberbanding(ExoIconView* icon_view);
static void exo_icon_view_update_rubberband_selection(ExoIconView* icon_view);
static bool exo_icon_view_item_hit_test(ExoIconView* icon_view, ExoIconViewItem* item, int x, int y,
                                        int width, int height);
static bool exo_icon_view_unselect_all_internal(ExoIconView* icon_view);
static void exo_icon_view_calculate_item_size(ExoIconView* icon_view, ExoIconViewItem* item);
static void exo_icon_view_calculate_item_size2(ExoIconView* icon_view, ExoIconViewItem* item,
                                               int* max_width, int* max_height);
static void exo_icon_view_update_rubberband(void* data);
static void exo_icon_view_invalidate_sizes(ExoIconView* icon_view);
static void exo_icon_view_add_move_binding(GtkBindingSet* binding_set, unsigned int keyval,
                                           unsigned int modmask, GtkMovementStep step, int count);
static bool exo_icon_view_real_move_cursor(ExoIconView* icon_view, GtkMovementStep step, int count);
static void exo_icon_view_move_cursor_up_down(ExoIconView* icon_view, int count);
static void exo_icon_view_move_cursor_page_up_down(ExoIconView* icon_view, int count);
static void exo_icon_view_move_cursor_left_right(ExoIconView* icon_view, int count);
static void exo_icon_view_move_cursor_start_end(ExoIconView* icon_view, int count);
static void exo_icon_view_scroll_to_item(ExoIconView* icon_view, ExoIconViewItem* item);
static void exo_icon_view_select_item(ExoIconView* icon_view, ExoIconViewItem* item);
static void exo_icon_view_unselect_item(ExoIconView* icon_view, ExoIconViewItem* item);
static bool exo_icon_view_select_all_between(ExoIconView* icon_view, ExoIconViewItem* anchor,
                                             ExoIconViewItem* cursor);
static ExoIconViewItem* exo_icon_view_get_item_at_coords(const ExoIconView* icon_view, int x, int y,
                                                         bool only_in_cell,
                                                         ExoIconViewCellInfo** cell_at_pos);
static void exo_icon_view_get_cell_area(ExoIconView* icon_view, ExoIconViewItem* item,
                                        ExoIconViewCellInfo* cell_info, GdkRectangle* cell_area);
static ExoIconViewCellInfo* exo_icon_view_get_cell_info(ExoIconView* icon_view,
                                                        GtkCellRenderer* renderer);
static void exo_icon_view_set_cell_data(const ExoIconView* icon_view, ExoIconViewItem* item);
static void exo_icon_view_cell_layout_pack_start(GtkCellLayout* layout, GtkCellRenderer* renderer,
                                                 bool expand);
static void exo_icon_view_cell_layout_pack_end(GtkCellLayout* layout, GtkCellRenderer* renderer,
                                               bool expand);
static void exo_icon_view_cell_layout_add_attribute(GtkCellLayout* layout,
                                                    GtkCellRenderer* renderer,
                                                    const char* attribute, int column);
static void exo_icon_view_cell_layout_clear(GtkCellLayout* layout);
static void exo_icon_view_cell_layout_clear_attributes(GtkCellLayout* layout,
                                                       GtkCellRenderer* renderer);
static void exo_icon_view_cell_layout_set_cell_data_func(GtkCellLayout* layout,
                                                         GtkCellRenderer* cell,
                                                         GtkCellLayoutDataFunc func,
                                                         void* func_data, GDestroyNotify destroy);
static void exo_icon_view_cell_layout_reorder(GtkCellLayout* layout, GtkCellRenderer* cell,
                                              int position);
static void exo_icon_view_item_activate_cell(ExoIconView* icon_view, ExoIconViewItem* item,
                                             ExoIconViewCellInfo* cell_info, GdkEvent* event);
static void exo_icon_view_put(ExoIconView* icon_view, GtkWidget* widget, ExoIconViewItem* item,
                              int cell);
static void exo_icon_view_remove_widget(GtkCellEditable* editable, ExoIconView* icon_view);
static void exo_icon_view_start_editing(ExoIconView* icon_view, ExoIconViewItem* item,
                                        ExoIconViewCellInfo* cell_info, GdkEvent* event);
static void exo_icon_view_stop_editing(ExoIconView* icon_view, bool cancel_editing);

/* Source side drag signals */
static void exo_icon_view_drag_begin(GtkWidget* widget, GdkDragContext* context);
static void exo_icon_view_drag_end(GtkWidget* widget, GdkDragContext* context);
static void exo_icon_view_drag_data_get(GtkWidget* widget, GdkDragContext* context,
                                        GtkSelectionData* selection_data, unsigned int info,
                                        unsigned int drag_time);
static void exo_icon_view_drag_data_delete(GtkWidget* widget, GdkDragContext* context);

/* Target side drag signals */
static void exo_icon_view_drag_leave(GtkWidget* widget, GdkDragContext* context,
                                     unsigned int drag_time);
static bool exo_icon_view_drag_motion(GtkWidget* widget, GdkDragContext* context, int x, int y,
                                      unsigned int drag_time);
static bool exo_icon_view_drag_drop(GtkWidget* widget, GdkDragContext* context, int x, int y,
                                    unsigned int drag_time);
static void exo_icon_view_drag_data_received(GtkWidget* widget, GdkDragContext* context, int x,
                                             int y, GtkSelectionData* selection_data,
                                             unsigned int info, unsigned int drag_time);
static bool exo_icon_view_maybe_begin_drag(ExoIconView* icon_view, GdkEventMotion* event);

static void remove_scroll_timeout(ExoIconView* icon_view);

/* single-click autoselection support */
static bool exo_icon_view_single_click_timeout(void* user_data);
static void exo_icon_view_single_click_timeout_destroy(void* user_data);

/* Interactive search support */
static void exo_icon_view_search_activate(GtkEntry* entry, ExoIconView* icon_view);
static void exo_icon_view_search_dialog_hide(GtkWidget* search_dialog, ExoIconView* icon_view);
static void exo_icon_view_search_ensure_directory(ExoIconView* icon_view);
static void exo_icon_view_search_init(GtkWidget* search_entry, ExoIconView* icon_view);
static bool exo_icon_view_search_iter(ExoIconView* icon_view, GtkTreeModel* model,
                                      GtkTreeIter* iter, const char* text, int* count, int n);
static void exo_icon_view_search_move(GtkWidget* widget, ExoIconView* icon_view, bool move_up);
#if (GTK_MAJOR_VERSION == 3)
static void exo_icon_view_search_preedit_changed(GtkEntry* entry, char* preedit,
#else
static void exo_icon_view_search_preedit_changed(GtkIMContext* im_context,
#endif
                                                 ExoIconView* icon_view);
static bool exo_icon_view_search_start(ExoIconView* icon_view, bool keybinding);
static bool exo_icon_view_search_equal_func(GtkTreeModel* model, int column, const char* key,
                                            GtkTreeIter* iter, void* user_data);
static void exo_icon_view_search_position_func(ExoIconView* icon_view, GtkWidget* search_dialog,
                                               void* user_data);
static bool exo_icon_view_search_button_press_event(GtkWidget* widget, GdkEventButton* event,
                                                    ExoIconView* icon_view);
static bool exo_icon_view_search_delete_event(GtkWidget* widget, GdkEventAny* event,
                                              ExoIconView* icon_view);
static bool exo_icon_view_search_key_press_event(GtkWidget* widget, GdkEventKey* event,
                                                 ExoIconView* icon_view);
static bool exo_icon_view_search_scroll_event(GtkWidget* widget, GdkEventScroll* event,
                                              ExoIconView* icon_view);
static bool exo_icon_view_search_timeout(void* user_data);
static void exo_icon_view_search_timeout_destroy(void* user_data);

static unsigned int icon_view_signals[LAST_SIGNAL];

#if (GTK_MAJOR_VERSION == 3)
static void exo_icon_view_get_preferred_width(GtkWidget* widget, int* minimal_width,
                                              int* natural_width)
{
    const ExoIconViewPrivate* priv = EXO_ICON_VIEW(widget)->priv;
    ExoIconViewChild* child;
    int child_minimal, child_natural;
    GList* lp;

    /* well, this is easy */
    if (priv->item_width < 0)
        *minimal_width = priv->width;
    *natural_width = priv->width;
}

static void exo_icon_view_get_preferred_height(GtkWidget* widget, int* minimal_height,
                                               int* natural_height)
{
    const ExoIconViewPrivate* priv = EXO_ICON_VIEW(widget)->priv;
    ExoIconViewChild* child;
    int child_minimal, child_natural;
    GList* lp;

    /* well, this is easy */
    *minimal_height = priv->height;
    *natural_height = priv->height;
}
#endif

G_DEFINE_TYPE_WITH_CODE(ExoIconView, exo_icon_view, GTK_TYPE_CONTAINER,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_CELL_LAYOUT, exo_icon_view_cell_layout_init)
#if (GTK_MAJOR_VERSION == 3)
                            G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL)
#endif
                                G_ADD_PRIVATE(ExoIconView))

static void exo_icon_view_class_init(ExoIconViewClass* klass)
{
    GtkContainerClass* gtkcontainer_class;
    GtkWidgetClass* gtkwidget_class;
    GtkBindingSet* gtkbinding_set;
    GObjectClass* gobject_class;

    gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = exo_icon_view_dispose;
    gobject_class->finalize = exo_icon_view_finalize;
    gobject_class->set_property = exo_icon_view_set_property;
    gobject_class->get_property = exo_icon_view_get_property;

    gtkwidget_class = GTK_WIDGET_CLASS(klass);
    gtkwidget_class->realize = exo_icon_view_realize;
    gtkwidget_class->unrealize = exo_icon_view_unrealize;
    gtkwidget_class->size_allocate = exo_icon_view_size_allocate;
    gtkwidget_class->style_set = exo_icon_view_style_set;
#if (GTK_MAJOR_VERSION == 3)
    gtkwidget_class->draw = exo_icon_view_draw;
    gtkwidget_class->get_preferred_width = exo_icon_view_get_preferred_width;
    gtkwidget_class->get_preferred_height = exo_icon_view_get_preferred_height;
#elif (GTK_MAJOR_VERSION == 2)
    gtkwidget_class->size_request = exo_icon_view_size_request;
    gtkwidget_class->expose_event = exo_icon_view_expose_event;
#endif
    gtkwidget_class->motion_notify_event = exo_icon_view_motion_notify_event;
    gtkwidget_class->button_press_event = exo_icon_view_button_press_event;
    gtkwidget_class->button_release_event = exo_icon_view_button_release_event;
    gtkwidget_class->scroll_event = exo_icon_view_scroll_event;
    gtkwidget_class->key_press_event = exo_icon_view_key_press_event;
    gtkwidget_class->focus_out_event = exo_icon_view_focus_out_event;
    gtkwidget_class->leave_notify_event = exo_icon_view_leave_notify_event;
    gtkwidget_class->drag_begin = exo_icon_view_drag_begin;
    gtkwidget_class->drag_end = exo_icon_view_drag_end;
    gtkwidget_class->drag_data_get = exo_icon_view_drag_data_get;
    gtkwidget_class->drag_data_delete = exo_icon_view_drag_data_delete;
    gtkwidget_class->drag_leave = exo_icon_view_drag_leave;
    gtkwidget_class->drag_motion = exo_icon_view_drag_motion;
    gtkwidget_class->drag_drop = exo_icon_view_drag_drop;
    gtkwidget_class->drag_data_received = exo_icon_view_drag_data_received;

    gtkcontainer_class = GTK_CONTAINER_CLASS(klass);
    gtkcontainer_class->remove = exo_icon_view_remove;
    gtkcontainer_class->forall = exo_icon_view_forall;

#if (GTK_MAJOR_VERSION == 2)
    klass->set_scroll_adjustments = exo_icon_view_set_adjustments;
#endif
    klass->select_all = exo_icon_view_real_select_all;
    klass->unselect_all = exo_icon_view_real_unselect_all;
    klass->select_cursor_item = exo_icon_view_real_select_cursor_item;
    klass->toggle_cursor_item = exo_icon_view_real_toggle_cursor_item;
    klass->move_cursor = exo_icon_view_real_move_cursor;
    klass->activate_cursor_item = exo_icon_view_real_activate_cursor_item;
    klass->start_interactive_search = exo_icon_view_real_start_interactive_search;

    /**
     * ExoIconView:column-spacing:
     *
     * The column-spacing property specifies the space which is inserted between
     * the columns of the icon view.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_COLUMN_SPACING,
                                    g_param_spec_int("column-spacing",
                                                     "Column Spacing",
                                                     "Space which is inserted between grid column",
                                                     0,
                                                     G_MAXINT,
                                                     6,
                                                     EXO_PARAM_READWRITE));

    /**
     * ExoIconView:columns:
     *
     * The columns property contains the number of the columns in which the
     * items should be displayed. If it is -1, the number of columns will
     * be chosen automatically to fill the available area.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_COLUMNS,
                                    g_param_spec_int("columns",
                                                     "Number of columns",
                                                     "Number of columns to display",
                                                     -1,
                                                     G_MAXINT,
                                                     -1,
                                                     EXO_PARAM_READWRITE));

    /**
     * ExoIconView:enable-search:
     *
     * View allows user to search through columns interactively.
     *
     * Since: 0.3.1.3
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_ENABLE_SEARCH,
        g_param_spec_boolean("enable-search",
                             "Enable Search",
                             "View allows user to search through columns interactively",
                             TRUE,
                             EXO_PARAM_READWRITE));

    /**
     * ExoIconView:item-width:
     *
     * The item-width property specifies the width to use for each item.
     * If it is set to -1, the icon view will automatically determine a
     * suitable item size.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_ITEM_WIDTH,
                                    g_param_spec_int("item-width",
                                                     "Width for each item",
                                                     "The width used for each item",
                                                     -1,
                                                     G_MAXINT,
                                                     -1,
                                                     EXO_PARAM_READWRITE));

    /**
     * ExoIconView:layout-mode:
     *
     * The layout-mode property specifies the way items are layed out in
     * the #ExoIconView. This can be either %EXO_ICON_VIEW_LAYOUT_ROWS,
     * which is the default, where items are layed out horizontally in
     * rows from top to bottom, or %EXO_ICON_VIEW_LAYOUT_COLS, where items
     * are layed out vertically in columns from left to right.
     *
     * Since: 0.3.1.5
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_LAYOUT_MODE,
                                    g_param_spec_enum("layout-mode",
                                                      "Layout mode",
                                                      "The layout mode",
                                                      EXO_TYPE_ICON_VIEW_LAYOUT_MODE,
                                                      EXO_ICON_VIEW_LAYOUT_ROWS,
                                                      EXO_PARAM_READWRITE));

    /**
     * ExoIconView:margin:
     *
     * The margin property specifies the space which is inserted
     * at the edges of the icon view.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_MARGIN,
        g_param_spec_int("margin",
                         "Margin",
                         "Space which is inserted at the edges of the icon view",
                         0,
                         G_MAXINT,
                         6,
                         EXO_PARAM_READWRITE));

    /**
     * ExoIconView:markup-column:
     *
     * The markup-column property contains the number of the model column
     * containing markup information to be displayed. The markup column must be
     * of type #G_TYPE_STRING. If this property and the text-column property
     * are both set to column numbers, it overrides the text column.
     * If both are set to -1, no texts are displayed.
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_MARKUP_COLUMN,
        g_param_spec_int("markup-column",
                         "Markup column",
                         "Model column used to retrieve the text if using Pango markup",
                         -1,
                         G_MAXINT,
                         -1,
                         EXO_PARAM_READWRITE));

    /**
     * ExoIconView:model:
     *
     * The model property contains the #GtkTreeModel, which should be
     * display by this icon view. Setting this property to %NULL turns
     * off the display of anything.
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_MODEL,
                                    g_param_spec_object("model",
                                                        "Icon View Model",
                                                        "The model for the icon view",
                                                        GTK_TYPE_TREE_MODEL,
                                                        EXO_PARAM_READWRITE));

    /**
     * ExoIconView:orientation:
     *
     * The orientation property specifies how the cells (i.e. the icon and
     * the text) of the item are positioned relative to each other.
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_ORIENTATION,
        g_param_spec_enum(
            "orientation",
            "Orientation",
            "How the text and icon of each item are positioned relative to each other",
            GTK_TYPE_ORIENTATION,
            GTK_ORIENTATION_VERTICAL,
            EXO_PARAM_READWRITE));

    /**
     * ExoIconView:pixbuf-column:
     *
     * The ::pixbuf-column property contains the number of the model column
     * containing the pixbufs which are displayed. The pixbuf column must be
     * of type #GDK_TYPE_PIXBUF. Setting this property to -1 turns off the
     * display of pixbufs.
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_PIXBUF_COLUMN,
        g_param_spec_int("pixbuf-column",
                         "Pixbuf column",
                         "Model column used to retrieve the icon pixbuf from",
                         -1,
                         G_MAXINT,
                         -1,
                         EXO_PARAM_READWRITE));

    /**
     * ExoIconView:reorderable:
     *
     * The reorderable property specifies if the items can be reordered
     * by Drag and Drop.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_REORDERABLE,
                                    g_param_spec_boolean("reorderable",
                                                         "Reorderable",
                                                         "View is reorderable",
                                                         FALSE,
                                                         EXO_PARAM_READWRITE));

    /**
     * ExoIconView:row-spacing:
     *
     * The row-spacing property specifies the space which is inserted between
     * the rows of the icon view.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_ROW_SPACING,
                                    g_param_spec_int("row-spacing",
                                                     "Row Spacing",
                                                     "Space which is inserted between grid rows",
                                                     0,
                                                     G_MAXINT,
                                                     6,
                                                     EXO_PARAM_READWRITE));

    /**
     * ExoIconView:search-column:
     *
     * Model column to search through when searching through code.
     *
     * Since: 0.3.1.3
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_SEARCH_COLUMN,
        g_param_spec_int("search-column",
                         "Search Column",
                         "Model column to search through when searching through item",
                         -1,
                         G_MAXINT,
                         -1,
                         EXO_PARAM_READWRITE));

    /**
     * ExoIconView:selection-mode:
     *
     * The selection-mode property specifies the selection mode of
     * icon view. If the mode is #GTK_SELECTION_MULTIPLE, rubberband selection
     * is enabled, for the other modes, only keyboard selection is possible.
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_SELECTION_MODE,
                                    g_param_spec_enum("selection-mode",
                                                      "Selection mode",
                                                      "The selection mode",
                                                      GTK_TYPE_SELECTION_MODE,
                                                      GTK_SELECTION_SINGLE,
                                                      EXO_PARAM_READWRITE));

    /**
     * ExoIconView:single-click:
     *
     * Determines whether items can be activated by single or double clicks.
     *
     * Since: 0.3.1.3
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_SINGLE_CLICK,
        g_param_spec_boolean("single-click",
                             "Single Click",
                             "Whether the items in the view can be activated with single clicks",
                             FALSE,
                             EXO_PARAM_READWRITE));

    /**
     * ExoIconView:single-click-timeout:
     *
     * The amount of time in milliseconds after which a prelited item (an item
     * which is hovered by the mouse cursor) will be selected automatically in
     * single click mode. A value of %0 disables the automatic selection.
     *
     * Since: 0.3.1.5
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_SINGLE_CLICK_TIMEOUT,
        g_param_spec_uint("single-click-timeout",
                          "Single Click Timeout",
                          "The amount of time after which the item under the mouse cursor "
                          "will be selected automatically in single click mode",
                          0,
                          G_MAXUINT,
                          0,
                          EXO_PARAM_READWRITE));

    /**
     * ExoIconView:spacing:
     *
     * The spacing property specifies the space which is inserted between
     * the cells (i.e. the icon and the text) of an item.
     *
     * Since: 0.3.1
     **/
    g_object_class_install_property(
        gobject_class,
        PROP_SPACING,
        g_param_spec_int("spacing",
                         "Spacing",
                         "Space which is inserted between cells of an item",
                         0,
                         G_MAXINT,
                         0,
                         EXO_PARAM_READWRITE));

    /**
     * ExoIconView:text-column:
     *
     * The text-column property contains the number of the model column
     * containing the texts which are displayed. The text column must be
     * of type #G_TYPE_STRING. If this property and the markup-column
     * property are both set to -1, no texts are displayed.
     **/
    g_object_class_install_property(gobject_class,
                                    PROP_TEXT_COLUMN,
                                    g_param_spec_int("text-column",
                                                     "Text column",
                                                     "Model column used to retrieve the text from",
                                                     -1,
                                                     G_MAXINT,
                                                     -1,
                                                     EXO_PARAM_READWRITE));

    gtk_widget_class_install_style_property(gtkwidget_class,
                                            g_param_spec_boxed("selection-box-color",
                                                               "Selection Box Color",
                                                               "Color of the selection box",
                                                               GDK_TYPE_COLOR,
                                                               EXO_PARAM_READABLE));

    gtk_widget_class_install_style_property(gtkwidget_class,
                                            g_param_spec_uchar("selection-box-alpha",
                                                               "Selection Box Alpha",
                                                               "Opacity of the selection box",
                                                               0,
                                                               0xff,
                                                               0x40,
                                                               EXO_PARAM_READABLE));

#if (GTK_MAJOR_VERSION == 3)
    g_object_class_override_property(gobject_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(gobject_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(gobject_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(gobject_class, PROP_VSCROLL_POLICY, "vscroll-policy");
#endif

    /**
     * ExoIconView::item-activated:
     * @icon_view : a #ExoIconView.
     * @path      :
     **/
    icon_view_signals[ITEM_ACTIVATED] =
        g_signal_new(I_("item-activated"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(ExoIconViewClass, item_activated),
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     GTK_TYPE_TREE_PATH);

    /**
     * ExoIconView::selection-changed:
     * @icon_view : a #ExoIconView.
     **/
    icon_view_signals[SELECTION_CHANGED] =
        g_signal_new(I_("selection-changed"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(ExoIconViewClass, selection_changed),
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

#if (GTK_MAJOR_VERSION == 2)
    /**
     * ExoIconView::set-scroll-adjustments:
     * @icon_view   : a #ExoIconView.
     * @hadjustment :
     * @vadjustment :
     **/
    gtkwidget_class->set_scroll_adjustments_signal =
        g_signal_new(I_("set-scroll-adjustments"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(ExoIconViewClass, set_scroll_adjustments),
                     NULL,
                     NULL,
                     _exo_marshal_VOID__OBJECT_OBJECT,
                     G_TYPE_NONE,
                     2,
                     GTK_TYPE_ADJUSTMENT,
                     GTK_TYPE_ADJUSTMENT);
#endif

    /**
     * ExoIconView::select-all:
     * @icon_view : a #ExoIconView.
     **/
    icon_view_signals[SELECT_ALL] = g_signal_new(I_("select-all"),
                                                 G_TYPE_FROM_CLASS(gobject_class),
                                                 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                 G_STRUCT_OFFSET(ExoIconViewClass, select_all),
                                                 NULL,
                                                 NULL,
                                                 g_cclosure_marshal_VOID__VOID,
                                                 G_TYPE_NONE,
                                                 0);

    /**
     * ExoIconView::unselect-all:
     * @icon_view : a #ExoIconView.
     **/
    icon_view_signals[UNSELECT_ALL] = g_signal_new(I_("unselect-all"),
                                                   G_TYPE_FROM_CLASS(gobject_class),
                                                   G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                   G_STRUCT_OFFSET(ExoIconViewClass, unselect_all),
                                                   NULL,
                                                   NULL,
                                                   g_cclosure_marshal_VOID__VOID,
                                                   G_TYPE_NONE,
                                                   0);

    /**
     * ExoIconView::select-cursor-item:
     * @icon_view : a #ExoIconView.
     **/
    icon_view_signals[SELECT_CURSOR_ITEM] =
        g_signal_new(I_("select-cursor-item"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                     G_STRUCT_OFFSET(ExoIconViewClass, select_cursor_item),
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    /**
     * ExoIconView::toggle-cursor-item:
     * @icon_view : a #ExoIconView.
     **/
    icon_view_signals[TOGGLE_CURSOR_ITEM] =
        g_signal_new(I_("toggle-cursor-item"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                     G_STRUCT_OFFSET(ExoIconViewClass, toggle_cursor_item),
                     NULL,
                     NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    /**
     * ExoIconView::activate-cursor-item:
     * @icon_view : a #ExoIconView.
     *
     * Return value:
     **/
    icon_view_signals[ACTIVATE_CURSOR_ITEM] =
        g_signal_new(I_("activate-cursor-item"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                     G_STRUCT_OFFSET(ExoIconViewClass, activate_cursor_item),
                     NULL,
                     NULL,
                     _exo_marshal_BOOLEAN__VOID,
                     G_TYPE_BOOLEAN,
                     0);

    /**
     * ExoIconView::start-interactive-search:
     * @iconb_view : a #ExoIconView.
     *
     * Return value:
     **/
    icon_view_signals[START_INTERACTIVE_SEARCH] =
        g_signal_new(I_("start-interactive-search"),
                     G_TYPE_FROM_CLASS(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                     G_STRUCT_OFFSET(ExoIconViewClass, start_interactive_search),
                     NULL,
                     NULL,
                     _exo_marshal_BOOLEAN__VOID,
                     G_TYPE_BOOLEAN,
                     0);

    /**
     * ExoIconView::move-cursor:
     * @icon_view : a #ExoIconView.
     * @step      :
     * @count     :
     *
     * Return value:
     **/
    icon_view_signals[MOVE_CURSOR] = g_signal_new(I_("move-cursor"),
                                                  G_TYPE_FROM_CLASS(gobject_class),
                                                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                  G_STRUCT_OFFSET(ExoIconViewClass, move_cursor),
                                                  NULL,
                                                  NULL,
                                                  _exo_marshal_BOOLEAN__ENUM_INT,
                                                  G_TYPE_BOOLEAN,
                                                  2,
                                                  GTK_TYPE_MOVEMENT_STEP,
                                                  G_TYPE_INT);

    /* Key bindings */
    gtkbinding_set = gtk_binding_set_by_class(klass);
    gtk_binding_entry_add_signal(gtkbinding_set, GDK_KEY_a, GDK_CONTROL_MASK, "select-all", 0);
    gtk_binding_entry_add_signal(gtkbinding_set,
                                 GDK_KEY_a,
                                 GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                                 "unselect-all",
                                 0);
    gtk_binding_entry_add_signal(gtkbinding_set,
                                 GDK_KEY_space,
                                 GDK_CONTROL_MASK,
                                 "toggle-cursor-item",
                                 0);
    gtk_binding_entry_add_signal(gtkbinding_set, GDK_KEY_space, 0, "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(gtkbinding_set, GDK_KEY_Return, 0, "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(gtkbinding_set, GDK_KEY_ISO_Enter, 0, "activate-cursor-item", 0);
    gtk_binding_entry_add_signal(gtkbinding_set, GDK_KEY_KP_Enter, 0, "activate-cursor-item", 0);
    //  gtk_binding_entry_add_signal (gtkbinding_set, GDK_f, GDK_CONTROL_MASK,
    //  "start-interactive-search", 0);   //MOD stole ctrl-f for create new directory
    gtk_binding_entry_add_signal(gtkbinding_set,
                                 GDK_KEY_F,
                                 GDK_CONTROL_MASK,
                                 "start-interactive-search",
                                 0);

    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_Up, 0, GTK_MOVEMENT_DISPLAY_LINES, -1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_KP_Up,
                                   0,
                                   GTK_MOVEMENT_DISPLAY_LINES,
                                   -1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_Down, 0, GTK_MOVEMENT_DISPLAY_LINES, 1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_KP_Down,
                                   0,
                                   GTK_MOVEMENT_DISPLAY_LINES,
                                   1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_p,
                                   GDK_CONTROL_MASK,
                                   GTK_MOVEMENT_DISPLAY_LINES,
                                   -1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_n,
                                   GDK_CONTROL_MASK,
                                   GTK_MOVEMENT_DISPLAY_LINES,
                                   1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_Home, 0, GTK_MOVEMENT_BUFFER_ENDS, -1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_KP_Home,
                                   0,
                                   GTK_MOVEMENT_BUFFER_ENDS,
                                   -1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_End, 0, GTK_MOVEMENT_BUFFER_ENDS, 1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_KP_End, 0, GTK_MOVEMENT_BUFFER_ENDS, 1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_Page_Up, 0, GTK_MOVEMENT_PAGES, -1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_KP_Page_Up, 0, GTK_MOVEMENT_PAGES, -1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_Page_Down, 0, GTK_MOVEMENT_PAGES, 1);
    exo_icon_view_add_move_binding(gtkbinding_set, GDK_KEY_KP_Page_Down, 0, GTK_MOVEMENT_PAGES, 1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_Right,
                                   0,
                                   GTK_MOVEMENT_VISUAL_POSITIONS,
                                   1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_Left,
                                   0,
                                   GTK_MOVEMENT_VISUAL_POSITIONS,
                                   -1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_KP_Right,
                                   0,
                                   GTK_MOVEMENT_VISUAL_POSITIONS,
                                   1);
    exo_icon_view_add_move_binding(gtkbinding_set,
                                   GDK_KEY_KP_Left,
                                   0,
                                   GTK_MOVEMENT_VISUAL_POSITIONS,
                                   -1);
}

static void exo_icon_view_cell_layout_init(GtkCellLayoutIface* iface)
{
    iface->pack_start = exo_icon_view_cell_layout_pack_start;
    iface->pack_end = exo_icon_view_cell_layout_pack_end;
    iface->clear = exo_icon_view_cell_layout_clear;
    iface->add_attribute = exo_icon_view_cell_layout_add_attribute;
    iface->set_cell_data_func = exo_icon_view_cell_layout_set_cell_data_func;
    iface->clear_attributes = exo_icon_view_cell_layout_clear_attributes;
    iface->reorder = exo_icon_view_cell_layout_reorder;
}

static void exo_icon_view_init(ExoIconView* icon_view)
{
    icon_view->priv = exo_icon_view_get_instance_private(icon_view);

    icon_view->priv->selection_mode = GTK_SELECTION_SINGLE;
    icon_view->priv->pressed_button = -1;
    icon_view->priv->press_start_x = -1;
    icon_view->priv->press_start_y = -1;
    icon_view->priv->text_column = -1;
    icon_view->priv->markup_column = -1;
    icon_view->priv->pixbuf_column = -1;
    icon_view->priv->text_cell = -1;
    icon_view->priv->pixbuf_cell = -1;

    gtk_widget_set_can_focus(GTK_WIDGET(icon_view), TRUE);

#if (GTK_MAJOR_VERSION == 2)
    exo_icon_view_set_adjustments(icon_view, NULL, NULL);
#endif

    icon_view->priv->cursor_cell = -1;

    icon_view->priv->orientation = GTK_ORIENTATION_VERTICAL;

    icon_view->priv->columns = -1;
    icon_view->priv->item_width = -1;
    icon_view->priv->row_spacing = 6;
    icon_view->priv->column_spacing = 6;
    icon_view->priv->margin = 6;

    icon_view->priv->enable_search = TRUE;
    icon_view->priv->search_column = -1;
    icon_view->priv->search_equal_func = exo_icon_view_search_equal_func;
    icon_view->priv->search_position_func = exo_icon_view_search_position_func;

    icon_view->priv->flags = EXO_ICON_VIEW_DRAW_KEYFOCUS;
}

static void exo_icon_view_dispose(GObject* object)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(object);

    /* cancel any pending search timeout */
    if (G_UNLIKELY(icon_view->priv->search_timeout_id != 0))
        g_source_remove(icon_view->priv->search_timeout_id);

    /* destroy the interactive search dialog */
    if (G_UNLIKELY(icon_view->priv->search_window != NULL))
    {
        gtk_widget_destroy(icon_view->priv->search_window);
        icon_view->priv->search_entry = NULL;
        icon_view->priv->search_window = NULL;
    }

    /* drop search equal and position functions (if any) */
    exo_icon_view_set_search_equal_func(icon_view, NULL, NULL, NULL);
    exo_icon_view_set_search_position_func(icon_view, NULL, NULL, NULL);

    /* reset the drag dest item */
    exo_icon_view_set_drag_dest_item(icon_view, NULL, EXO_ICON_VIEW_NO_DROP);

    /* drop the scroll to path (if any) */
    if (G_UNLIKELY(icon_view->priv->scroll_to_path != NULL))
    {
        gtk_tree_row_reference_free(icon_view->priv->scroll_to_path);
        icon_view->priv->scroll_to_path = NULL;
    }

    /* reset the model (also stops any active editing) */
    exo_icon_view_set_model(icon_view, NULL);

    /* drop the scroll timer */
    remove_scroll_timeout(icon_view);

    (*G_OBJECT_CLASS(exo_icon_view_parent_class)->dispose)(object);
}

static void exo_icon_view_finalize(GObject* object)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(object);

    /* drop the scroll adjustments */
    g_object_unref(G_OBJECT(icon_view->priv->hadjustment));
    g_object_unref(G_OBJECT(icon_view->priv->vadjustment));

    /* drop the cell renderers */
    exo_icon_view_cell_layout_clear(GTK_CELL_LAYOUT(icon_view));

    /* be sure to cancel the single click timeout */
    if (G_UNLIKELY(icon_view->priv->single_click_timeout_id != 0))
        g_source_remove(icon_view->priv->single_click_timeout_id);

    /* kill the layout idle source (it's important to have this last!) */
    if (G_UNLIKELY(icon_view->priv->layout_idle_id != 0))
        g_source_remove(icon_view->priv->layout_idle_id);

    (*G_OBJECT_CLASS(exo_icon_view_parent_class)->finalize)(object);
}

static void exo_icon_view_get_property(GObject* object, unsigned int prop_id, GValue* value,
                                       GParamSpec* pspec)
{
    const ExoIconViewPrivate* priv = EXO_ICON_VIEW(object)->priv;

    switch (prop_id)
    {
        case PROP_COLUMN_SPACING:
            g_value_set_int(value, priv->column_spacing);
            break;

        case PROP_COLUMNS:
            g_value_set_int(value, priv->columns);
            break;

        case PROP_ENABLE_SEARCH:
            g_value_set_boolean(value, priv->enable_search);
            break;

        case PROP_ITEM_WIDTH:
            g_value_set_int(value, priv->item_width);
            break;

        case PROP_MARGIN:
            g_value_set_int(value, priv->margin);
            break;

        case PROP_MARKUP_COLUMN:
            g_value_set_int(value, priv->markup_column);
            break;

        case PROP_MODEL:
            g_value_set_object(value, priv->model);
            break;

        case PROP_ORIENTATION:
            g_value_set_enum(value, priv->orientation);
            break;

        case PROP_PIXBUF_COLUMN:
            g_value_set_int(value, priv->pixbuf_column);
            break;

        case PROP_REORDERABLE:
            g_value_set_boolean(value, priv->reorderable);
            break;

        case PROP_ROW_SPACING:
            g_value_set_int(value, priv->row_spacing);
            break;

        case PROP_SEARCH_COLUMN:
            g_value_set_int(value, priv->search_column);
            break;

        case PROP_SELECTION_MODE:
            g_value_set_enum(value, priv->selection_mode);
            break;

        case PROP_SINGLE_CLICK:
            g_value_set_boolean(value, priv->single_click);
            break;

        case PROP_SINGLE_CLICK_TIMEOUT:
            g_value_set_uint(value, priv->single_click_timeout);
            break;

        case PROP_SPACING:
            g_value_set_int(value, priv->spacing);
            break;

        case PROP_TEXT_COLUMN:
            g_value_set_int(value, priv->text_column);
            break;

        case PROP_LAYOUT_MODE:
            g_value_set_enum(value, priv->layout_mode);
            break;

#if (GTK_MAJOR_VERSION == 3)
        case PROP_HADJUSTMENT:
            g_value_set_object(value, priv->hadjustment);
            break;

        case PROP_VADJUSTMENT:
            g_value_set_object(value, priv->vadjustment);
            break;

        case PROP_HSCROLL_POLICY:
            g_value_set_enum(value, priv->hscroll_policy);
            break;

        case PROP_VSCROLL_POLICY:
            g_value_set_enum(value, priv->vscroll_policy);
            break;
#endif

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void exo_icon_view_set_property(GObject* object, unsigned int prop_id, const GValue* value,
                                       GParamSpec* pspec)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(object);

    switch (prop_id)
    {
        case PROP_COLUMN_SPACING:
            exo_icon_view_set_column_spacing(icon_view, g_value_get_int(value));
            break;

        case PROP_COLUMNS:
            exo_icon_view_set_columns(icon_view, g_value_get_int(value));
            break;

        case PROP_ENABLE_SEARCH:
            exo_icon_view_set_enable_search(icon_view, g_value_get_boolean(value));
            break;

        case PROP_ITEM_WIDTH:
            exo_icon_view_set_item_width(icon_view, g_value_get_int(value));
            break;

        case PROP_MARGIN:
            exo_icon_view_set_margin(icon_view, g_value_get_int(value));
            break;

        case PROP_MARKUP_COLUMN:
            exo_icon_view_set_markup_column(icon_view, g_value_get_int(value));
            break;

        case PROP_MODEL:
            exo_icon_view_set_model(icon_view, g_value_get_object(value));
            break;

        case PROP_ORIENTATION:
            exo_icon_view_set_orientation(icon_view, g_value_get_enum(value));
            break;

        case PROP_PIXBUF_COLUMN:
            exo_icon_view_set_pixbuf_column(icon_view, g_value_get_int(value));
            break;

        case PROP_REORDERABLE:
            exo_icon_view_set_reorderable(icon_view, g_value_get_boolean(value));
            break;

        case PROP_ROW_SPACING:
            exo_icon_view_set_row_spacing(icon_view, g_value_get_int(value));
            break;

        case PROP_SEARCH_COLUMN:
            exo_icon_view_set_search_column(icon_view, g_value_get_int(value));
            break;

        case PROP_SELECTION_MODE:
            exo_icon_view_set_selection_mode(icon_view, g_value_get_enum(value));
            break;

        case PROP_SINGLE_CLICK:
            exo_icon_view_set_single_click(icon_view, g_value_get_boolean(value));
            break;

        case PROP_SINGLE_CLICK_TIMEOUT:
            exo_icon_view_set_single_click_timeout(icon_view, g_value_get_uint(value));
            break;

        case PROP_SPACING:
            exo_icon_view_set_spacing(icon_view, g_value_get_int(value));
            break;

        case PROP_TEXT_COLUMN:
            exo_icon_view_set_text_column(icon_view, g_value_get_int(value));
            break;

        case PROP_LAYOUT_MODE:
            exo_icon_view_set_layout_mode(icon_view, g_value_get_enum(value));
            break;

#if (GTK_MAJOR_VERSION == 3)
        case PROP_HADJUSTMENT:
            exo_icon_view_set_adjustments(icon_view,
                                          g_value_get_object(value),
                                          icon_view->priv->vadjustment);
            break;

        case PROP_VADJUSTMENT:
            exo_icon_view_set_adjustments(icon_view,
                                          icon_view->priv->hadjustment,
                                          g_value_get_object(value));
            break;

        case PROP_HSCROLL_POLICY:
            if (icon_view->priv->hscroll_policy != (GtkScrollablePolicy)g_value_get_enum(value))
            {
                icon_view->priv->hscroll_policy = g_value_get_enum(value);
                gtk_widget_queue_resize(GTK_WIDGET(icon_view));
                g_object_notify_by_pspec(object, pspec);
            }
            break;

        case PROP_VSCROLL_POLICY:
            if (icon_view->priv->vscroll_policy != (GtkScrollablePolicy)g_value_get_enum(value))
            {
                icon_view->priv->vscroll_policy = g_value_get_enum(value);
                gtk_widget_queue_resize(GTK_WIDGET(icon_view));
                g_object_notify_by_pspec(object, pspec);
            }
            break;
#endif

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void exo_icon_view_realize(GtkWidget* widget)
{
    ExoIconViewPrivate* priv = EXO_ICON_VIEW(widget)->priv;
    GdkWindowAttr attributes;
    GtkAllocation allocation;
    int attributes_mask;

    gtk_widget_set_realized(widget, TRUE);
    gtk_widget_get_allocation(widget, &allocation);

    /* Allocate the clipping window */
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK;
    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;
#if (GTK_MAJOR_VERSION == 3)
#define GDK_WA_COLORMAP 0
#elif (GTK_MAJOR_VERSION == 2)
    attributes_mask |= GDK_WA_COLORMAP;
    attributes.colormap = gtk_widget_get_colormap(widget);
#endif
    gtk_widget_set_window(
        widget,
        gdk_window_new(gtk_widget_get_parent_window(widget), &attributes, attributes_mask));
    gdk_window_set_user_data(gtk_widget_get_window(widget), widget);

    /* Allocate the icons window */
    attributes.x = 0;
    attributes.y = 0;
    attributes.width = MAX(priv->width, allocation.width);
    attributes.height = MAX(priv->height, allocation.height);
    //clang-format off
    attributes.event_mask = GDK_EXPOSURE_MASK | GDK_SCROLL_MASK
#if (GTK_MAJOR_VERSION == 3)
                            | GDK_SMOOTH_SCROLL_MASK
#endif
                            | GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK |
                            GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK |
                            GDK_KEY_RELEASE_MASK | gtk_widget_get_events(widget);
    //clang-format on
    priv->bin_window = gdk_window_new(gtk_widget_get_window(widget), &attributes, attributes_mask);
    gdk_window_set_user_data(priv->bin_window, widget);

#if (GTK_MAJOR_VERSION == 2)
    /* GTK2: Attach style/background - this breaks 'dark theme version' styles
     * in GTK3 - https://github.com/IgnorantGuru/spacefm/issues/578 */

    /* This widget is fully reimplementing realize, so must attach a style
     * (nothing is there currently) - there is no need to then call
     * gtk_widget_set_style, however if you do with a non-NULL style, GTK
     * considers the style hardcoded and therefore outside of its inherited 'rc
     * style' system, which results in exo_icon_view_style_set no longer being
     * called on a theme change
     * https://github.com/IgnorantGuru/spacefm/issues/627 shows that this correct
     * method of setting the style causes a slowdown (??), so going back to the
     * broken method */
    // widget->style = gtk_style_attach (widget->style, widget->window);
    gtk_widget_set_style(
        widget,
        gtk_style_attach(gtk_widget_get_style(widget), gtk_widget_get_window(widget)));

    /* However the true widget window background remains black without the below
     * call - the documentation recommends to call gtk_style_set_background,
     * however this has no effect with any requested GtkStateType. Both the
     * ExoIconView's window and presumably the earlier GTK window (??) need to
     * have the background set - if you don't do the earlier window, the widget
     * has a black background prior to loading directory contents - see issue 627 */
    gdk_window_set_background(priv->bin_window,
                              &gtk_widget_get_style(widget)->base[gtk_widget_get_state(widget)]);
    gdk_window_set_background(gtk_widget_get_window(widget),
                              &gtk_widget_get_style(widget)->base[gtk_widget_get_state(widget)]);

#elif (GTK_MAJOR_VERSION == 3)
    /* GTK3: Adding style class (styling works fine for me without this, but
     * one user so far has reported breakage) */
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(widget)),
                                GTK_STYLE_CLASS_VIEW);
#endif

    /* map the icons window */
    gdk_window_show(priv->bin_window);
}

static void exo_icon_view_unrealize(GtkWidget* widget)
{
    ExoIconViewPrivate* priv = EXO_ICON_VIEW(widget)->priv;

    /* drop the icons window */
    gdk_window_set_user_data(priv->bin_window, NULL);
    gdk_window_destroy(priv->bin_window);
    priv->bin_window = NULL;

    /* let GtkWidget destroy children and widget->window */
    if (GTK_WIDGET_CLASS(exo_icon_view_parent_class)->unrealize)
        (*GTK_WIDGET_CLASS(exo_icon_view_parent_class)->unrealize)(widget);
}

static void exo_icon_view_size_request(GtkWidget* widget, GtkRequisition* requisition)
{
    const ExoIconViewPrivate* priv = EXO_ICON_VIEW(widget)->priv;
    ExoIconViewChild* child;
    GtkRequisition child_requisition;
    GList* lp;

    /* well, this is easy */
    requisition->width = priv->width;
    requisition->height = priv->height;

    /* handle the child widgets */
    for (lp = priv->children; lp != NULL; lp = lp->next)
    {
        child = lp->data;
        if (gtk_widget_get_visible(child->widget))
            gtk_widget_size_request(child->widget, &child_requisition);
    }
}

static void exo_icon_view_allocate_children(ExoIconView* icon_view)
{
    const ExoIconViewPrivate* priv = icon_view->priv;
    const ExoIconViewChild* child;
    GtkAllocation allocation;
    const GList* lp;
    int focus_line_width;
    int focus_padding;

    for (lp = priv->children; lp != NULL; lp = lp->next)
    {
        child = EXO_ICON_VIEW_CHILD(lp->data);

        /* totally ignore our child's requisition */
        if (child->cell < 0)
            allocation = child->item->area;
        else
            allocation = child->item->box[child->cell];

        /* increase the item area by focus width/padding */
        gtk_widget_style_get(GTK_WIDGET(icon_view),
                             "focus-line-width",
                             &focus_line_width,
                             "focus-padding",
                             &focus_padding,
                             NULL);
        allocation.x = MAX(0, allocation.x - (focus_line_width + focus_padding));
        allocation.y = MAX(0, allocation.y - (focus_line_width + focus_padding));
        allocation.width = MIN(priv->width - allocation.x,
                               allocation.width + 2 * (focus_line_width + focus_padding));
        allocation.height = MIN(priv->height - allocation.y,
                                allocation.height + 2 * (focus_line_width + focus_padding));

        /* allocate the area to the child */
        gtk_widget_size_allocate(child->widget, &allocation);
    }
}

static void exo_icon_view_size_allocate(GtkWidget* widget, GtkAllocation* allocation)
{
    GtkAdjustment* hadjustment;
    GtkAdjustment* vadjustment;
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);

    /* apply the new size allocation */
    gtk_widget_set_allocation(widget, allocation);

    /* move/resize the clipping window, the icons window
     * will be handled by exo_icon_view_layout().
     */
    if (gtk_widget_get_realized(widget))
        gdk_window_move_resize(gtk_widget_get_window(widget),
                               allocation->x,
                               allocation->y,
                               allocation->width,
                               allocation->height);

    /* layout the items */
    exo_icon_view_layout(icon_view);

    /* allocate space to the widgets (editing) */
    exo_icon_view_allocate_children(icon_view);

    /* update the horizontal scroll adjustment accordingly */
    hadjustment = icon_view->priv->hadjustment;
    gtk_adjustment_set_page_size(hadjustment, allocation->width);
    gtk_adjustment_set_page_increment(hadjustment, allocation->width * 0.9);
    gtk_adjustment_set_step_increment(hadjustment, allocation->width * 0.1);
    gtk_adjustment_set_lower(hadjustment, 0);
    gtk_adjustment_set_upper(hadjustment, MAX(allocation->width, icon_view->priv->width));
    if (gtk_adjustment_get_value(hadjustment) >
        gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment))
        gtk_adjustment_set_value(
            hadjustment,
            MAX(0,
                gtk_adjustment_get_upper(hadjustment) - gtk_adjustment_get_page_size(hadjustment)));

    /* update the vertical scroll adjustment accordingly */
    vadjustment = icon_view->priv->vadjustment;
    gtk_adjustment_set_page_size(vadjustment, allocation->height);
    gtk_adjustment_set_page_increment(vadjustment, allocation->height * 0.9);
    gtk_adjustment_set_step_increment(vadjustment, allocation->height * 0.1);
    gtk_adjustment_set_lower(vadjustment, 0);
    gtk_adjustment_set_upper(vadjustment, MAX(allocation->height, icon_view->priv->height));
    if (gtk_adjustment_get_value(vadjustment) >
        gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment))
        gtk_adjustment_set_value(
            vadjustment,
            MAX(0,
                gtk_adjustment_get_upper(vadjustment) - gtk_adjustment_get_page_size(vadjustment)));

    /* we need to emit "changed" ourselves */
    gtk_adjustment_changed(hadjustment);
    gtk_adjustment_changed(vadjustment);
}

static void exo_icon_view_style_set(GtkWidget* widget, GtkStyle* previous_style)
{
#if (GTK_MAJOR_VERSION == 2)
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
#endif

    /* let GtkWidget do its work */
    (*GTK_WIDGET_CLASS(exo_icon_view_parent_class)->style_set)(widget, previous_style);

#if (GTK_MAJOR_VERSION == 2)
    /* Apply the new style for the bin_window if we're realized - GTK3 handles
     * this on its own, the background colour is wrong if you do this for GTK3 */
    if (gtk_widget_get_realized(widget))
        gdk_window_set_background(
            icon_view->priv->bin_window,
            &gtk_widget_get_style(widget)->base[gtk_widget_get_state(widget)]);
#endif
}

#if (GTK_MAJOR_VERSION == 3)
static bool exo_icon_view_draw(GtkWidget* widget, cairo_t* cr)
#elif (GTK_MAJOR_VERSION == 2)
static bool exo_icon_view_expose_event(GtkWidget* widget, GdkEventExpose* event)
#endif
{
    ExoIconViewDropPosition dest_pos;
    ExoIconViewPrivate* priv = EXO_ICON_VIEW(widget)->priv;
    ExoIconViewItem* dest_item = NULL;
    ExoIconViewItem* item;
#if (GTK_MAJOR_VERSION == 3)
    GtkAllocation event_area;
#elif (GTK_MAJOR_VERSION == 2)
    GdkRectangle event_area = event->area;
#endif
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    GtkTreePath* path;
    GdkRectangle rubber_rect;
    const GList* lp;
    int event_area_last;
    int dest_index = -1;

    /* verify that the expose happened on the icon window */
#if (GTK_MAJOR_VERSION == 3)
    if (!G_UNLIKELY(gtk_cairo_should_draw_window(cr, priv->bin_window)))
        return FALSE;
#elif (GTK_MAJOR_VERSION == 2)
    if (G_UNLIKELY(event->window != priv->bin_window))
        return FALSE;
#endif

    /* don't handle expose if the layout isn't done yet; the layout
     * method will schedule a redraw when done.
     */
    if (G_UNLIKELY(priv->layout_idle_id != 0))
        return FALSE;

#if (GTK_MAJOR_VERSION == 3)
    gtk_cairo_transform_to_window(cr, widget, priv->bin_window);
#elif (GTK_MAJOR_VERSION == 2)
    cairo_t* cr;
    cr = gdk_cairo_create(priv->bin_window);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
#endif
    cairo_set_line_width(cr, 1);

    /* scroll to the previously remembered path (if any) */
    if (G_UNLIKELY(priv->scroll_to_path != NULL))
    {
        /* grab the path from the reference and invalidate the reference */
        path = gtk_tree_row_reference_get_path(priv->scroll_to_path);
        gtk_tree_row_reference_free(priv->scroll_to_path);
        priv->scroll_to_path = NULL;

        /* check if the reference was still valid */
        if (G_LIKELY(path != NULL))
        {
            /* try to scroll again */
            exo_icon_view_scroll_to_path(icon_view,
                                         path,
                                         priv->scroll_to_use_align,
                                         priv->scroll_to_row_align,
                                         priv->scroll_to_col_align);

            /* release the path */
            gtk_tree_path_free(path);
        }
    }

    /* check if we need to draw a drag indicator */
    exo_icon_view_get_drag_dest_item(icon_view, &path, &dest_pos);
    if (G_UNLIKELY(path != NULL))
    {
        dest_index = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
    }

    /* paint the rubberband background */
    if (G_UNLIKELY(priv->doing_rubberband))
    {
        /* calculate the rubberband area */
        rubber_rect.x = MIN(priv->rubberband_x1, priv->rubberband_x2);
        rubber_rect.y = MIN(priv->rubberband_y1, priv->rubberband_y2);
        rubber_rect.width = ABS(priv->rubberband_x1 - priv->rubberband_x2) + 1;
        rubber_rect.height = ABS(priv->rubberband_y1 - priv->rubberband_y2) + 1;

        /* we take advantage of double-buffering here and use only a single
         * draw_rectangle() operation w/o having to take care of clipping.
         */
#if (GTK_MAJOR_VERSION == 3)
        gdk_cairo_set_source_rgba(cr, &priv->rubberband_fill_color);
#elif (GTK_MAJOR_VERSION == 2)
        gdk_cairo_set_source_color(cr, priv->rubberband_fill_color);
#endif
        cairo_rectangle(cr,
                        rubber_rect.x + 0.5,
                        rubber_rect.y + 0.5,
                        rubber_rect.width - 1,
                        rubber_rect.height - 1);
        cairo_fill(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* determine the last interesting coordinate (depending on the layout mode) */
#if (GTK_MAJOR_VERSION == 3)
    gtk_widget_get_allocation(widget, &event_area);
    gdk_window_get_position(priv->bin_window, &event_area.x, &event_area.y);
    event_area.x = -event_area.x;
    event_area.y = -event_area.y;
#endif
    event_area_last = (priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS)
                          ? event_area.y + event_area.height
                          : event_area.x + event_area.width;

    /* paint all items that are affected by the expose event */
    for (lp = priv->items; lp != NULL; lp = lp->next)
    {
        /* check if this item is in the visible area */
        item = EXO_ICON_VIEW_ITEM(lp->data);
        if (G_LIKELY(priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS))
        {
            if (item->area.y > event_area_last)
                break;
            else if (item->area.y + item->area.height < event_area.y)
                continue;
        }
        else
        {
            if (item->area.x > event_area_last)
                break;
            else if (item->area.x + item->area.width < event_area.x)
                continue;
        }

        /* check if this item needs an update */
#if (GTK_MAJOR_VERSION == 3)
        if (G_LIKELY(gdk_rectangle_intersect(&event_area, &item->area, NULL)))
        {
            exo_icon_view_paint_item(icon_view,
                                     item,
                                     &event_area,
                                     cr,
                                     item->area.x,
                                     item->area.y,
                                     TRUE);
#elif (GTK_MAJOR_VERSION == 2)
        if (G_LIKELY(gdk_region_rect_in(event->region, &item->area) != GDK_OVERLAP_RECTANGLE_OUT))
        {
            exo_icon_view_paint_item(icon_view,
                                     item,
                                     &event_area,
                                     event->window,
                                     item->area.x,
                                     item->area.y,
                                     TRUE);
#endif
            if (G_UNLIKELY(dest_index >= 0 && dest_item == NULL &&
                           dest_index == g_list_index(priv->items, item)))
                dest_item = item;
        }
    }

    /* draw the drag indicator */
    if (G_UNLIKELY(dest_item != NULL))
    {
        switch (dest_pos)
        {
            case EXO_ICON_VIEW_DROP_INTO:
#if (GTK_MAJOR_VERSION == 3)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                cr,
                                gtk_widget_get_state(widget),
                                widget,
#elif (GTK_MAJOR_VERSION == 2)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                priv->bin_window,
                                gtk_widget_get_state(widget),
                                NULL,
                                widget,
#endif
                                "iconview-drop-indicator",
                                dest_item->area.x,
                                dest_item->area.y,
                                dest_item->area.width,
                                dest_item->area.height);
                break;

            case EXO_ICON_VIEW_DROP_ABOVE:
#if (GTK_MAJOR_VERSION == 3)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                cr,
                                gtk_widget_get_state(widget),
                                widget,
#elif (GTK_MAJOR_VERSION == 2)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                priv->bin_window,
                                gtk_widget_get_state(widget),
                                NULL,
                                widget,
#endif
                                "iconview-drop-indicator",
                                dest_item->area.x,
                                dest_item->area.y - 1,
                                dest_item->area.width,
                                2);
                break;

            case EXO_ICON_VIEW_DROP_LEFT:
#if (GTK_MAJOR_VERSION == 3)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                cr,
                                gtk_widget_get_state(widget),
                                widget,
#elif (GTK_MAJOR_VERSION == 2)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                priv->bin_window,
                                gtk_widget_get_state(widget),
                                NULL,
                                widget,
#endif
                                "iconview-drop-indicator",
                                dest_item->area.x - 1,
                                dest_item->area.y,
                                2,
                                dest_item->area.height);
                break;

            case EXO_ICON_VIEW_DROP_BELOW:
#if (GTK_MAJOR_VERSION == 3)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                cr,
                                gtk_widget_get_state(widget),
                                widget,
#elif (GTK_MAJOR_VERSION == 2)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                priv->bin_window,
                                gtk_widget_get_state(widget),
                                NULL,
                                widget,
#endif
                                "iconview-drop-indicator",
                                dest_item->area.x,
                                dest_item->area.y + dest_item->area.height - 1,
                                dest_item->area.width,
                                2);
                break;

            case EXO_ICON_VIEW_DROP_RIGHT:
#if (GTK_MAJOR_VERSION == 3)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                cr,
                                gtk_widget_get_state(widget),
                                widget,
#elif (GTK_MAJOR_VERSION == 2)
                gtk_paint_focus(gtk_widget_get_style(widget),
                                priv->bin_window,
                                gtk_widget_get_state(widget),
                                NULL,
                                widget,
#endif
                                "iconview-drop-indicator",
                                dest_item->area.x + dest_item->area.width - 1,
                                dest_item->area.y,
                                2,
                                dest_item->area.height);

                break;
            case EXO_ICON_VIEW_NO_DROP:
                break;

            default:
                g_assert_not_reached();
        }
    }

    /* draw the rubberband border */
    if (G_UNLIKELY(priv->doing_rubberband))
    {
        /* draw the border */
#if (GTK_MAJOR_VERSION == 3)
        gdk_cairo_set_source_rgba(cr, &priv->rubberband_border_color);
#elif (GTK_MAJOR_VERSION == 2)
        gdk_cairo_set_source_color(cr, priv->rubberband_border_color);
#endif
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr,
                        rubber_rect.x + 0.5,
                        rubber_rect.y + 0.5,
                        rubber_rect.width - 1,
                        rubber_rect.height - 1);
        cairo_stroke(cr);
    }

#if (GTK_MAJOR_VERSION == 2)
    cairo_destroy(cr);

    /* let the GtkContainer forward the expose event to all children */
    (*GTK_WIDGET_CLASS(exo_icon_view_parent_class)->expose_event)(widget, event);
#endif

    return FALSE;
}

static bool rubberband_scroll_timeout(void* user_data)
{
    GtkAdjustment* adjustment;
    ExoIconView* icon_view = EXO_ICON_VIEW(user_data);
    double value;

    /* determine the adjustment for the scroll direction */
    adjustment = (icon_view->priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS)
                     ? icon_view->priv->vadjustment
                     : icon_view->priv->hadjustment;

    /* determine the new scroll value */
    value = MIN(gtk_adjustment_get_value(adjustment) + icon_view->priv->scroll_value_diff,
                gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));

    /* apply the new value */
    gtk_adjustment_set_value(adjustment, value);

    /* update the rubberband */
    exo_icon_view_update_rubberband(icon_view);

    return TRUE;
}

static bool exo_icon_view_motion_notify_event(GtkWidget* widget, GdkEventMotion* event)
{
    ExoIconViewItem* item;
    GtkAllocation allocation;
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    GdkCursor* cursor;
    int size;
    int abso;

    exo_icon_view_maybe_begin_drag(icon_view, event);

    gtk_widget_get_allocation(widget, &allocation);

    if (icon_view->priv->doing_rubberband)
    {
        if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
            icon_view->priv->ctrl_pressed = TRUE;
        if ((event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK)
            icon_view->priv->shift_pressed = TRUE;

        exo_icon_view_update_rubberband(widget);

        if (icon_view->priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS)
        {
            abso = event->y - icon_view->priv->height *
                                  (gtk_adjustment_get_value(icon_view->priv->vadjustment) /
                                   (gtk_adjustment_get_upper(icon_view->priv->vadjustment) -
                                    gtk_adjustment_get_lower(icon_view->priv->vadjustment)));

            size = allocation.height;
        }
        else
        {
            abso = event->x - icon_view->priv->width *
                                  (gtk_adjustment_get_value(icon_view->priv->hadjustment) /
                                   (gtk_adjustment_get_upper(icon_view->priv->hadjustment) -
                                    gtk_adjustment_get_lower(icon_view->priv->hadjustment)));

            size = allocation.width;
        }

        if (abso < 0 || abso > size)
        {
            if (abso < 0)
                icon_view->priv->scroll_value_diff = abso;
            else
                icon_view->priv->scroll_value_diff = abso - size;
            icon_view->priv->event_last_x = event->x;
            icon_view->priv->event_last_y = event->y;

            if (icon_view->priv->scroll_timeout_id == 0)
                icon_view->priv->scroll_timeout_id =
                    gdk_threads_add_timeout(30, rubberband_scroll_timeout, icon_view);
        }
        else
        {
            remove_scroll_timeout(icon_view);
        }
    }
    else
    {
        item = exo_icon_view_get_item_at_coords(icon_view, event->x, event->y, TRUE, NULL);
        if (item != icon_view->priv->prelit_item)
        {
            if (G_LIKELY(icon_view->priv->prelit_item != NULL))
                exo_icon_view_queue_draw_item(icon_view, icon_view->priv->prelit_item);
            icon_view->priv->prelit_item = item;
            if (G_LIKELY(item != NULL))
                exo_icon_view_queue_draw_item(icon_view, item);

            /* check if we are in single click mode right now */
            if (G_UNLIKELY(icon_view->priv->single_click))
            {
                /* display a hand cursor when pointer is above an item */
                if (G_LIKELY(item != NULL))
                {
                    /* hand2 seems to be what we should use */
                    cursor = gdk_cursor_new_for_display(gdk_window_get_display(event->window),
                                                        GDK_HAND2);
                    gdk_window_set_cursor(event->window, cursor);
                    gdk_cursor_unref(cursor);
                }
                else
                {
                    /* reset the cursor */
                    gdk_window_set_cursor(event->window, NULL);
                }

                /* check if autoselection is enabled */
                if (G_LIKELY(icon_view->priv->single_click_timeout > 0))
                {
                    /* drop any running timeout */
                    if (G_LIKELY(icon_view->priv->single_click_timeout_id != 0))
                        g_source_remove(icon_view->priv->single_click_timeout_id);

                    /* remember the current event state */
                    icon_view->priv->single_click_timeout_state = event->state;

                    /* schedule a new timeout */
                    icon_view->priv->single_click_timeout_id =
                        g_timeout_add_full(G_PRIORITY_LOW,
                                           icon_view->priv->single_click_timeout,
                                           exo_icon_view_single_click_timeout,
                                           icon_view,
                                           exo_icon_view_single_click_timeout_destroy);
                }
            }
        }
    }

    return TRUE;
}

static void exo_icon_view_remove(GtkContainer* container, GtkWidget* widget)
{
    ExoIconViewChild* child;
    ExoIconView* icon_view = EXO_ICON_VIEW(container);
    GList* lp;

    for (lp = icon_view->priv->children; lp != NULL; lp = lp->next)
    {
        child = lp->data;
        if (G_LIKELY(child->widget == widget))
        {
            icon_view->priv->children = g_list_delete_link(icon_view->priv->children, lp);
            gtk_widget_unparent(widget);
            g_slice_free(ExoIconViewChild, child);
            return;
        }
    }
}

static void exo_icon_view_forall(GtkContainer* container, bool include_internals,
                                 GtkCallback callback, void* callback_data)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(container);
    GList* lp;

    for (lp = icon_view->priv->children; lp != NULL; lp = lp->next)
        (*callback)(((ExoIconViewChild*)lp->data)->widget, callback_data);
}

static void exo_icon_view_item_activate_cell(ExoIconView* icon_view, ExoIconViewItem* item,
                                             ExoIconViewCellInfo* info, GdkEvent* event)
{
    GtkCellRendererMode mode;
    GdkRectangle cell_area;
    GtkTreePath* path;
    bool visible;
    char* path_string;

    exo_icon_view_set_cell_data(icon_view, item);

    g_object_get(G_OBJECT(info->cell), "visible", &visible, "mode", &mode, NULL);

    if (G_UNLIKELY(visible && mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE))
    {
        exo_icon_view_get_cell_area(icon_view, item, info, &cell_area);

        path = gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);
        path_string = gtk_tree_path_to_string(path);
        gtk_tree_path_free(path);

        gtk_cell_renderer_activate(info->cell,
                                   event,
                                   GTK_WIDGET(icon_view),
                                   path_string,
                                   &cell_area,
                                   &cell_area,
                                   0);

        g_free(path_string);
    }
}

static void exo_icon_view_put(ExoIconView* icon_view, GtkWidget* widget, ExoIconViewItem* item,
                              int cell)
{
    ExoIconViewChild* child;

    /* allocate the new child */
    child = g_slice_new(ExoIconViewChild);
    child->widget = widget;
    child->item = item;
    child->cell = cell;

    /* hook up the child */
    icon_view->priv->children = g_list_append(icon_view->priv->children, child);

    /* setup the parent for the child */
    if (gtk_widget_get_realized(GTK_WIDGET(icon_view)))
        gtk_widget_set_parent_window(child->widget, icon_view->priv->bin_window);
    gtk_widget_set_parent(widget, GTK_WIDGET(icon_view));
}

static void exo_icon_view_remove_widget(GtkCellEditable* editable, ExoIconView* icon_view)
{
    ExoIconViewItem* item;
    GList* lp;

    if (G_LIKELY(icon_view->priv->edited_item != NULL))
    {
        item = icon_view->priv->edited_item;
        icon_view->priv->edited_item = NULL;
        icon_view->priv->editable = NULL;

        for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
            ((ExoIconViewCellInfo*)lp->data)->editing = FALSE;

        if (gtk_widget_has_focus(GTK_WIDGET(editable)))
            gtk_widget_grab_focus(GTK_WIDGET(icon_view));

        g_signal_handlers_disconnect_by_func(editable, exo_icon_view_remove_widget, icon_view);
        gtk_container_remove(GTK_CONTAINER(icon_view), GTK_WIDGET(editable));

        exo_icon_view_queue_draw_item(icon_view, item);
    }
}

static void exo_icon_view_start_editing(ExoIconView* icon_view, ExoIconViewItem* item,
                                        ExoIconViewCellInfo* info, GdkEvent* event)
{
    GtkCellRendererMode mode;
    GtkCellEditable* editable;
    GdkRectangle cell_area;
    GtkTreePath* path;
    bool visible;
    char* path_string;

    /* setup cell data for the given item */
    exo_icon_view_set_cell_data(icon_view, item);

    /* check if the cell is visible and editable (given the updated cell data) */
    g_object_get(info->cell, "visible", &visible, "mode", &mode, NULL);
    if (G_LIKELY(visible && mode == GTK_CELL_RENDERER_MODE_EDITABLE))
    {
        /* draw keyboard focus while editing */
        EXO_ICON_VIEW_SET_FLAG(icon_view, EXO_ICON_VIEW_DRAW_KEYFOCUS);

        /* determine the cell area */
        exo_icon_view_get_cell_area(icon_view, item, info, &cell_area);

        /* determine the tree path */
        path = gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);
        path_string = gtk_tree_path_to_string(path);
        gtk_tree_path_free(path);

        /* allocate the editable from the cell renderer */
        editable = gtk_cell_renderer_start_editing(info->cell,
                                                   event,
                                                   GTK_WIDGET(icon_view),
                                                   path_string,
                                                   &cell_area,
                                                   &cell_area,
                                                   0);

        /* ugly hack, but works */
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(editable), "has-frame") != NULL)
            g_object_set(editable, "has-frame", TRUE, NULL);

        /* setup the editing widget */
        icon_view->priv->edited_item = item;
        icon_view->priv->editable = editable;
        info->editing = TRUE;

        exo_icon_view_put(icon_view, GTK_WIDGET(editable), item, info->position);
        gtk_cell_editable_start_editing(GTK_CELL_EDITABLE(editable), (GdkEvent*)event);
        gtk_widget_grab_focus(GTK_WIDGET(editable));
        g_signal_connect(G_OBJECT(editable),
                         "remove-widget",
                         G_CALLBACK(exo_icon_view_remove_widget),
                         icon_view);

        /* cleanup */
        g_free(path_string);
    }
}

static void exo_icon_view_stop_editing(ExoIconView* icon_view, bool cancel_editing)
{
    ExoIconViewItem* item;
    GtkCellRenderer* cell = NULL;
    GList* lp;

    if (icon_view->priv->edited_item == NULL)
        return;

    /*
     * This is very evil. We need to do this, because
     * gtk_cell_editable_editing_done may trigger exo_icon_view_row_changed
     * later on. If exo_icon_view_row_changed notices
     * icon_view->priv->edited_item != NULL, it'll call
     * exo_icon_view_stop_editing again. Bad things will happen then.
     *
     * Please read that again if you intend to modify anything here.
     */

    item = icon_view->priv->edited_item;
    icon_view->priv->edited_item = NULL;

    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
    {
        ExoIconViewCellInfo* info = lp->data;
        if (info->editing)
        {
            cell = info->cell;
            break;
        }
    }

    if (G_UNLIKELY(cell == NULL))
        return;

    gtk_cell_renderer_stop_editing(cell, cancel_editing);
    if (G_LIKELY(!cancel_editing))
        gtk_cell_editable_editing_done(icon_view->priv->editable);

    icon_view->priv->edited_item = item;

    gtk_cell_editable_remove_widget(icon_view->priv->editable);
}

static bool exo_icon_view_button_press_event(GtkWidget* widget, GdkEventButton* event)
{
    ExoIconViewCellInfo* info = NULL;
    GtkCellRendererMode mode;
    ExoIconViewItem* item;
    ExoIconView* icon_view;
    GtkTreePath* path;
    bool dirty = FALSE;
    int cursor_cell;

    icon_view = EXO_ICON_VIEW(widget);

    if (event->window != icon_view->priv->bin_window)
        return FALSE;

    /* stop any pending "single-click-timeout" */
    if (G_UNLIKELY(icon_view->priv->single_click_timeout_id != 0))
        g_source_remove(icon_view->priv->single_click_timeout_id);

    if (G_UNLIKELY(!gtk_widget_has_focus(widget)))
        gtk_widget_grab_focus(widget);

    if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
    {
        item = exo_icon_view_get_item_at_coords(icon_view, event->x, event->y, TRUE, &info);
        if (item != NULL)
        {
            g_object_get(info->cell, "mode", &mode, NULL);

            if (mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE ||
                mode == GTK_CELL_RENDERER_MODE_EDITABLE)
                cursor_cell = g_list_index(icon_view->priv->cell_list, info);
            else
                cursor_cell = -1;

            exo_icon_view_scroll_to_item(icon_view, item);

            if (icon_view->priv->selection_mode == GTK_SELECTION_NONE)
            {
                exo_icon_view_set_cursor_item(icon_view, item, cursor_cell);
            }
            else if (icon_view->priv->selection_mode == GTK_SELECTION_MULTIPLE &&
                     (event->state & GDK_SHIFT_MASK))
            {
                if (!(event->state & GDK_CONTROL_MASK))
                    exo_icon_view_unselect_all_internal(icon_view);

                exo_icon_view_set_cursor_item(icon_view, item, cursor_cell);
                if (!icon_view->priv->anchor_item)
                    icon_view->priv->anchor_item = item;
                else
                    exo_icon_view_select_all_between(icon_view, icon_view->priv->anchor_item, item);
                dirty = TRUE;
            }
            else
            {
                if ((icon_view->priv->selection_mode == GTK_SELECTION_MULTIPLE ||
                     ((icon_view->priv->selection_mode == GTK_SELECTION_SINGLE) &&
                      item->selected)) &&
                    (event->state & GDK_CONTROL_MASK))
                {
                    item->selected = !item->selected;
                    exo_icon_view_queue_draw_item(icon_view, item);
                    dirty = TRUE;
                }
                else
                {
                    if (!item->selected)
                    {
                        exo_icon_view_unselect_all_internal(icon_view);

                        item->selected = TRUE;
                        exo_icon_view_queue_draw_item(icon_view, item);
                        dirty = TRUE;
                    }
                }
                exo_icon_view_set_cursor_item(icon_view, item, cursor_cell);
                icon_view->priv->anchor_item = item;
            }

            /* Save press to possibly begin a drag */
            if (icon_view->priv->pressed_button < 0)
            {
                icon_view->priv->pressed_button = event->button;
                icon_view->priv->press_start_x = event->x;
                icon_view->priv->press_start_y = event->y;
            }

            icon_view->priv->last_single_clicked = item;

            /* cancel the current editing, if it exists */
            exo_icon_view_stop_editing(icon_view, TRUE);

            if (mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE)
                exo_icon_view_item_activate_cell(icon_view, item, info, (GdkEvent*)event);
            else if (mode == GTK_CELL_RENDERER_MODE_EDITABLE)
                exo_icon_view_start_editing(icon_view, item, info, (GdkEvent*)event);
        }
        else
        {
            /* cancel the current editing, if it exists */
            exo_icon_view_stop_editing(icon_view, TRUE);

            if (icon_view->priv->selection_mode != GTK_SELECTION_BROWSE &&
                !(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)))
            {
                dirty = exo_icon_view_unselect_all_internal(icon_view);
            }

            if (icon_view->priv->selection_mode == GTK_SELECTION_MULTIPLE)
                exo_icon_view_start_rubberbanding(icon_view, event->x, event->y);
        }
    }
    else if (event->button == 1 && event->type == GDK_2BUTTON_PRESS)
    {
        /* ignore double-click events in single-click mode */
        if (G_LIKELY(!icon_view->priv->single_click))
        {
            item = exo_icon_view_get_item_at_coords(icon_view, event->x, event->y, TRUE, NULL);
            if (G_LIKELY(item != NULL))
            {
                path =
                    gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);
                exo_icon_view_item_activated(icon_view, path);
                gtk_tree_path_free(path);
            }
        }

        icon_view->priv->last_single_clicked = NULL;
        icon_view->priv->pressed_button = -1;
    }

    /* grab focus and stop drawing the keyboard focus indicator on single clicks */
    if (G_LIKELY(event->type != GDK_2BUTTON_PRESS && event->type != GDK_3BUTTON_PRESS))
    {
        if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
            gtk_widget_grab_focus(GTK_WIDGET(icon_view));
        EXO_ICON_VIEW_UNSET_FLAG(icon_view, EXO_ICON_VIEW_DRAW_KEYFOCUS);
    }

    if (dirty)
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);

    return event->button == 1;
}

static bool exo_icon_view_button_release_event(GtkWidget* widget, GdkEventButton* event)
{
    ExoIconViewItem* item;
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    GtkTreePath* path;

    if (icon_view->priv->pressed_button == (int)event->button)
    {
        if (G_LIKELY(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 0)
        {
            /* determine the item at the mouse coords and check if this is the last single clicked
             * one */
            item = exo_icon_view_get_item_at_coords(icon_view, event->x, event->y, TRUE, NULL);
            if (G_LIKELY(item != NULL && item == icon_view->priv->last_single_clicked))
            {
                if (icon_view->priv->single_click)
                {
                    /* emit an "item-activated" signal for this item */
                    path =
                        gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item),
                                                       -1);
                    exo_icon_view_item_activated(icon_view, path);
                    gtk_tree_path_free(path);
                }

                /* reduce the selection to just the clicked item  */
                exo_icon_view_unselect_all_internal(icon_view);
                exo_icon_view_select_item(icon_view, item);
            }

            /* reset the last single clicked item */
            icon_view->priv->last_single_clicked = NULL;
        }

        /* reset the pressed_button state */
        icon_view->priv->pressed_button = -1;
    }

    exo_icon_view_stop_rubberbanding(icon_view);

    remove_scroll_timeout(icon_view);

    return TRUE;
}

#if (GTK_MAJOR_VERSION == 3)
static bool exo_icon_view_scroll_event(GtkWidget* widget, GdkEventScroll* event)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);

    /* we don't care for scroll events in "rows" layout mode, as
     * that's completely handled by GtkScrolledWindow.
     */
    if (icon_view->priv->layout_mode != EXO_ICON_VIEW_LAYOUT_COLS)
        return FALSE;

    /* convert vertical scroll events to horizontal ones in "columns" layout mode */
    if (event->direction == GDK_SCROLL_UP)
        event->direction = GDK_SCROLL_LEFT;
    else if (event->direction == GDK_SCROLL_DOWN)
        event->direction = GDK_SCROLL_RIGHT;
    else if (event->direction == GDK_SCROLL_SMOOTH)
    {
        event->delta_x = event->delta_y;
        event->delta_y = 0.0;
    }

    /* scrolling will be handled by GtkScrolledWindow */
    return FALSE;
}
#elif (GTK_MAJOR_VERSION == 2)
static bool exo_icon_view_scroll_event(GtkWidget* widget, GdkEventScroll* event)
{
    GtkAdjustment* adjustment;
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    double delta;
    double value;

    /* we don't care for scroll events in "rows" layout mode, as
     * that's completely handled by GtkScrolledWindow.
     */
    if (icon_view->priv->layout_mode != EXO_ICON_VIEW_LAYOUT_COLS)
        return FALSE;

    /* also, we don't care for anything but Up/Down, as
     * everything else will be handled by GtkScrolledWindow.
     */
    if (event->direction != GDK_SCROLL_UP && event->direction != GDK_SCROLL_DOWN)
        return FALSE;

    /* determine the horizontal adjustment */
    adjustment = icon_view->priv->hadjustment;

    /* determine the scroll delta */
    delta = pow(gtk_adjustment_get_page_size(adjustment), 2.0 / 3.0);
    delta = (event->direction == GDK_SCROLL_UP) ? -delta : delta;

    /* apply the new adjustment value */
    value = CLAMP(gtk_adjustment_get_value(adjustment) + delta,
                  gtk_adjustment_get_lower(adjustment),
                  gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
    gtk_adjustment_set_value(adjustment, value);

    return TRUE;
}
#endif

static bool exo_icon_view_key_press_event(GtkWidget* widget, GdkEventKey* event)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    GdkScreen* screen;
    GdkEvent* new_event;
    bool retval;
    unsigned long popup_menu_id;
    char* new_text;
    char* old_text;

    /* let the parent class handle the key bindings and stuff */
    if ((*GTK_WIDGET_CLASS(exo_icon_view_parent_class)->key_press_event)(widget, event))
        return TRUE;

    /* check if typeahead search is enabled */
    if (G_UNLIKELY(!icon_view->priv->enable_search))
        return FALSE;

    exo_icon_view_search_ensure_directory(icon_view);

    /* make sure the search window is realized */
    gtk_widget_realize(icon_view->priv->search_window);

    /* make a copy of the current text */
    old_text = gtk_editable_get_chars(GTK_EDITABLE(icon_view->priv->search_entry), 0, -1);

    /* make sure we don't accidently popup the context menu */
    popup_menu_id = g_signal_connect(G_OBJECT(icon_view->priv->search_entry),
                                     "popup-menu",
                                     G_CALLBACK(gtk_true),
                                     NULL);

    /* move the search window offscreen */
    screen = gtk_widget_get_screen(GTK_WIDGET(icon_view));
#if (GTK_MAJOR_VERSION == 3)
    GdkRectangle workarea = {0};
    gdk_monitor_get_workarea(gdk_display_get_primary_monitor(gdk_display_get_default()), &workarea);
    gtk_window_move(GTK_WINDOW(icon_view->priv->search_window),
                    workarea.width + 1,
                    workarea.height + 1);
#elif (GTK_MAJOR_VERSION == 2)
    gtk_window_move(GTK_WINDOW(icon_view->priv->search_window),
                    gdk_screen_get_width(screen) + 1,
                    gdk_screen_get_height(screen) + 1);
#endif
    gtk_widget_show(icon_view->priv->search_window);

    /* allocate a new event to forward */
    new_event = gdk_event_copy((GdkEvent*)event);
    g_object_unref(G_OBJECT(new_event->key.window));
    new_event->key.window =
        g_object_ref(G_OBJECT(gtk_widget_get_window(GTK_WIDGET(icon_view->priv->search_entry))));

    /* send the event to the search entry. If the "preedit-changed" signal is
     * emitted during this event, priv->search_imcontext_changed will be set.
     */
    icon_view->priv->search_imcontext_changed = FALSE;
    retval = gtk_widget_event(icon_view->priv->search_entry, new_event);
    gtk_widget_hide(icon_view->priv->search_window);

    /* release the temporary event */
    gdk_event_free(new_event);

    /* disconnect the popup menu prevention */
    g_signal_handler_disconnect(G_OBJECT(icon_view->priv->search_entry), popup_menu_id);

    /* we check to make sure that the entry tried to handle the,
     * and that the text has actually changed.
     */
    new_text = gtk_editable_get_chars(GTK_EDITABLE(icon_view->priv->search_entry), 0, -1);
    retval = retval && (strcmp(new_text, old_text) != 0);
    g_free(old_text);
    g_free(new_text);

    /* if we're in a preedit or the text was modified */
    if (icon_view->priv->search_imcontext_changed || retval)
    {
        if (exo_icon_view_search_start(icon_view, FALSE))
        {
            gtk_widget_grab_focus(GTK_WIDGET(icon_view));
            return TRUE;
        }
        else
        {
            gtk_entry_set_text(GTK_ENTRY(icon_view->priv->search_entry), "");
            return FALSE;
        }
    }

    return FALSE;
}

static bool exo_icon_view_focus_out_event(GtkWidget* widget, GdkEventFocus* event)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);

    /* be sure to cancel any single-click timeout */
    if (G_UNLIKELY(icon_view->priv->single_click_timeout_id != 0))
        g_source_remove(icon_view->priv->single_click_timeout_id);

    /* reset the cursor if we're still realized */
    if (G_LIKELY(icon_view->priv->bin_window != NULL))
        gdk_window_set_cursor(icon_view->priv->bin_window, NULL);

    /* destroy the interactive search dialog */
    if (G_UNLIKELY(icon_view->priv->search_window != NULL))
        exo_icon_view_search_dialog_hide(icon_view->priv->search_window, icon_view);

    /* schedule a redraw with the new focus state */
    gtk_widget_queue_draw(widget);

    return FALSE;
}

static bool exo_icon_view_leave_notify_event(GtkWidget* widget, GdkEventCrossing* event)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    /* reset cursor to default */
    if (gtk_widget_get_realized(widget))
        gdk_window_set_cursor(gtk_widget_get_window(widget), NULL);

    /* reset the prelit item (if any) */
    if (G_LIKELY(icon_view->priv->prelit_item != NULL))
    {
        exo_icon_view_queue_draw_item(icon_view, icon_view->priv->prelit_item);
        icon_view->priv->prelit_item = NULL;
    }

    /* call the parent's leave_notify_event (if any) */
    if (GTK_WIDGET_CLASS(exo_icon_view_parent_class)->leave_notify_event != NULL)
        return (*GTK_WIDGET_CLASS(exo_icon_view_parent_class)->leave_notify_event)(widget, event);

    /* other signal handlers may be invoked */
    return FALSE;
}

static void exo_icon_view_update_rubberband(void* data)
{
    ExoIconView* icon_view;
    int x, y;
    GdkRectangle old_area;
    GdkRectangle new_area;
    GdkRectangle common;
#if (GTK_MAJOR_VERSION == 3)
    GdkSeat* seat;
    GdkDevice* pointer_dev;
    cairo_region_t* invalid_region;
#elif (GTK_MAJOR_VERSION == 2)
    GdkRegion* invalid_region;
#endif

    icon_view = EXO_ICON_VIEW(data);

#if (GTK_MAJOR_VERSION == 3)
    seat = gdk_display_get_default_seat(gdk_window_get_display(icon_view->priv->bin_window));
    pointer_dev = gdk_seat_get_pointer(seat);
    gdk_window_get_device_position(icon_view->priv->bin_window, pointer_dev, &x, &y, NULL);
#elif (GTK_MAJOR_VERSION == 2)
    gdk_window_get_pointer(icon_view->priv->bin_window, &x, &y, NULL);
#endif

    x = MAX(x, 0);
    y = MAX(y, 0);

    old_area.x = MIN(icon_view->priv->rubberband_x1, icon_view->priv->rubberband_x2);
    old_area.y = MIN(icon_view->priv->rubberband_y1, icon_view->priv->rubberband_y2);
    old_area.width = ABS(icon_view->priv->rubberband_x2 - icon_view->priv->rubberband_x1) + 1;
    old_area.height = ABS(icon_view->priv->rubberband_y2 - icon_view->priv->rubberband_y1) + 1;

    new_area.x = MIN(icon_view->priv->rubberband_x1, x);
    new_area.y = MIN(icon_view->priv->rubberband_y1, y);
    new_area.width = ABS(x - icon_view->priv->rubberband_x1) + 1;
    new_area.height = ABS(y - icon_view->priv->rubberband_y1) + 1;

#if (GTK_MAJOR_VERSION == 3)
    invalid_region = cairo_region_create_rectangle((cairo_rectangle_int_t*)&old_area);
    cairo_region_union_rectangle(invalid_region, (cairo_rectangle_int_t*)&new_area);
#elif (GTK_MAJOR_VERSION == 2)
    invalid_region = gdk_region_rectangle(&old_area);
    gdk_region_union_with_rect(invalid_region, &new_area);
#endif

    gdk_rectangle_intersect(&old_area, &new_area, &common);
    if (common.width > 2 && common.height > 2)
    {
#if (GTK_MAJOR_VERSION == 3)
        cairo_region_t* common_region;
#elif (GTK_MAJOR_VERSION == 2)
        GdkRegion* common_region;
#endif

        /* make sure the border is invalidated */
        common.x += 1;
        common.y += 1;
        common.width -= 2;
        common.height -= 2;

#if (GTK_MAJOR_VERSION == 3)
        common_region = cairo_region_create_rectangle((cairo_rectangle_int_t*)&common);
        cairo_region_subtract(invalid_region, common_region);

        cairo_region_destroy(common_region);
#elif (GTK_MAJOR_VERSION == 2)
        common_region = gdk_region_rectangle(&common);

        gdk_region_subtract(invalid_region, common_region);
        gdk_region_destroy(common_region);
#endif
    }

    gdk_window_invalidate_region(icon_view->priv->bin_window, invalid_region, TRUE);

#if (GTK_MAJOR_VERSION == 3)
    cairo_region_destroy(invalid_region);
#elif (GTK_MAJOR_VERSION == 2)
    gdk_region_destroy(invalid_region);
#endif

    icon_view->priv->rubberband_x2 = x;
    icon_view->priv->rubberband_y2 = y;

    exo_icon_view_update_rubberband_selection(icon_view);
}

static void exo_icon_view_start_rubberbanding(ExoIconView* icon_view, int x, int y)
{
#if (GTK_MAJOR_VERSION == 2)
    const GdkColor* background_color;
    GdkColor* color;
    unsigned char alpha;
#endif
    void* drag_data;
    GList* items;

    /* be sure to disable any previously active rubberband */
    exo_icon_view_stop_rubberbanding(icon_view);

    for (items = icon_view->priv->items; items; items = items->next)
    {
        ExoIconViewItem* item = items->data;
        item->selected_before_rubberbanding = item->selected;
    }

    icon_view->priv->rubberband_x1 = x;
    icon_view->priv->rubberband_y1 = y;
    icon_view->priv->rubberband_x2 = x;
    icon_view->priv->rubberband_y2 = y;

    icon_view->priv->doing_rubberband = TRUE;

#if (GTK_MAJOR_VERSION == 3)
    GtkStyleContext* style_ctx = gtk_widget_get_style_context(GTK_WIDGET(icon_view));
    gtk_style_context_save(style_ctx);
    gtk_style_context_add_class(style_ctx, GTK_STYLE_CLASS_RUBBERBAND);

    /* set the rubberband border color */
    gtk_style_context_get_border_color(style_ctx,
                                       GTK_STATE_FLAG_NORMAL,
                                       &icon_view->priv->rubberband_border_color);

    /* set the rubberband fill color */
    gtk_style_context_get_background_color(style_ctx,
                                           GTK_STATE_FLAG_NORMAL,
                                           &icon_view->priv->rubberband_fill_color);
    gtk_style_context_restore(style_ctx);
#elif (GTK_MAJOR_VERSION == 2)
    /* determine the border color */
    gtk_widget_style_get(GTK_WIDGET(icon_view), "selection-box-color", &color, NULL);
    if (G_LIKELY(color == NULL))
        color =
            gdk_color_copy(&gtk_widget_get_style(GTK_WIDGET(icon_view))->base[GTK_STATE_SELECTED]);

    /* set the rubberband border color */
    icon_view->priv->rubberband_border_color = color;

    /* determine the fill color and alpha setting */
    gtk_widget_style_get(GTK_WIDGET(icon_view),
                         "selection-box-color",
                         &color,
                         "selection-box-alpha",
                         &alpha,
                         NULL);
    if (G_LIKELY(color == NULL))
        color =
            gdk_color_copy(&gtk_widget_get_style(GTK_WIDGET(icon_view))->base[GTK_STATE_SELECTED]);

    /* calculate the fill color (based on the fill color, the alpha setting and the background
     * color) */
    background_color = &gtk_widget_get_style(GTK_WIDGET(icon_view))->base[GTK_STATE_NORMAL];
    color->red =
        ((color->red * (alpha / 255.0)) + (background_color->red * (255.0 - alpha / 255.0)));
    color->green =
        ((color->green * (alpha / 255.0)) + (background_color->green * (255.0 - alpha / 255.0)));
    color->blue =
        ((color->blue * (alpha / 255.0)) + (background_color->blue * (255.0 - alpha / 255.0)));

    /* set the rubberband background color */
    icon_view->priv->rubberband_fill_color = color;
#endif

    gtk_grab_add(GTK_WIDGET(icon_view));

    /* be sure to disable Gtk+ DnD callbacks, because else rubberbanding will be interrupted */
    drag_data = g_object_get_data(G_OBJECT(icon_view), I_("gtk-site-data"));
    if (G_LIKELY(drag_data != NULL))
    {
        g_signal_handlers_block_matched(G_OBJECT(icon_view),
                                        G_SIGNAL_MATCH_DATA,
                                        0,
                                        0,
                                        NULL,
                                        NULL,
                                        drag_data);
    }
}

static void exo_icon_view_stop_rubberbanding(ExoIconView* icon_view)
{
    void* drag_data;

    if (G_LIKELY(icon_view->priv->doing_rubberband))
    {
        icon_view->priv->doing_rubberband = FALSE;
        icon_view->priv->ctrl_pressed = FALSE;
        icon_view->priv->shift_pressed = FALSE;
        gtk_grab_remove(GTK_WIDGET(icon_view));
        gtk_widget_queue_draw(GTK_WIDGET(icon_view));

#if (GTK_MAJOR_VERSION == 2)
        /* Free the colors for drawing the rubberband */
        gdk_color_free(icon_view->priv->rubberband_border_color);
        gdk_color_free(icon_view->priv->rubberband_fill_color);
        icon_view->priv->rubberband_border_color = NULL;
        icon_view->priv->rubberband_fill_color = NULL;
#endif

        /* re-enable Gtk+ DnD callbacks again */
        drag_data = g_object_get_data(G_OBJECT(icon_view), I_("gtk-site-data"));
        if (G_LIKELY(drag_data != NULL))
        {
            g_signal_handlers_unblock_matched(G_OBJECT(icon_view),
                                              G_SIGNAL_MATCH_DATA,
                                              0,
                                              0,
                                              NULL,
                                              NULL,
                                              drag_data);
        }
    }
}

static void exo_icon_view_update_rubberband_selection(ExoIconView* icon_view)
{
    ExoIconViewItem* item;
    bool selected;
    bool changed = FALSE;
    bool is_in;
    GList* lp;
    int x, y;
    int width;
    int height;

    /* determine the new rubberband area */
    x = MIN(icon_view->priv->rubberband_x1, icon_view->priv->rubberband_x2);
    y = MIN(icon_view->priv->rubberband_y1, icon_view->priv->rubberband_y2);
    width = ABS(icon_view->priv->rubberband_x1 - icon_view->priv->rubberband_x2);
    height = ABS(icon_view->priv->rubberband_y1 - icon_view->priv->rubberband_y2);
    /* check all items */
    for (lp = icon_view->priv->items; lp != NULL; lp = lp->next)
    {
        item = EXO_ICON_VIEW_ITEM(lp->data);
        is_in = exo_icon_view_item_hit_test(icon_view, item, x, y, width, height);
        selected = is_in ^ item->selected_before_rubberbanding;

        if (G_UNLIKELY(item->selected != selected))
        {
            changed = TRUE;
            item->selected = selected;
            exo_icon_view_queue_draw_item(icon_view, item);
            /* extend */
            if (icon_view->priv->shift_pressed && !icon_view->priv->ctrl_pressed)
            {
                if (!item->selected)
                {
                    changed = TRUE;
                    item->selected = TRUE;
                }
            }
            /* add/remove */
            else
            {
                changed = TRUE;
                item->selected = selected;
            }

            if (changed)
                exo_icon_view_queue_draw_item(icon_view, item);
        }

        if (item->selected)
            icon_view->priv->cursor_item = item;
    }
    if (G_LIKELY(changed))
        g_signal_emit(G_OBJECT(icon_view), icon_view_signals[SELECTION_CHANGED], 0);
}

static bool exo_icon_view_item_hit_test(ExoIconView* icon_view, ExoIconViewItem* item, int x, int y,
                                        int width, int height)
{
    GList* l;
    GdkRectangle box;

    for (l = icon_view->priv->cell_list; l; l = l->next)
    {
        ExoIconViewCellInfo* info = (ExoIconViewCellInfo*)l->data;

        if (!gtk_cell_renderer_get_visible(info->cell) || item->box == NULL)
            continue;

        box = item->box[info->position];

        if (MIN(x + width, box.x + box.width) - MAX(x, box.x) > 0 &&
            MIN(y + height, box.y + box.height) - MAX(y, box.y) > 0)
            return TRUE;
    }

    return FALSE;
}

static bool exo_icon_view_unselect_all_internal(ExoIconView* icon_view)
{
    ExoIconViewItem* item;
    bool dirty = FALSE;
    GList* lp;

    if (G_LIKELY(icon_view->priv->selection_mode != GTK_SELECTION_NONE))
    {
        for (lp = icon_view->priv->items; lp != NULL; lp = lp->next)
        {
            item = EXO_ICON_VIEW_ITEM(lp->data);
            if (item->selected)
            {
                dirty = TRUE;
                item->selected = FALSE;
                exo_icon_view_queue_draw_item(icon_view, item);
            }
        }
    }

    return dirty;
}

static void exo_icon_view_set_adjustments(ExoIconView* icon_view, GtkAdjustment* hadj,
                                          GtkAdjustment* vadj)
{
    bool need_adjust = FALSE;

    if (hadj)
        _exo_return_if_fail(GTK_IS_ADJUSTMENT(hadj));
    else
        hadj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
    if (vadj)
        _exo_return_if_fail(GTK_IS_ADJUSTMENT(vadj));
    else
        vadj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

    if (icon_view->priv->hadjustment && (icon_view->priv->hadjustment != hadj))
    {
        g_signal_handlers_disconnect_matched(icon_view->priv->hadjustment,
                                             G_SIGNAL_MATCH_DATA,
                                             0,
                                             0,
                                             NULL,
                                             NULL,
                                             icon_view);
        g_object_unref(icon_view->priv->hadjustment);
    }

    if (icon_view->priv->vadjustment && (icon_view->priv->vadjustment != vadj))
    {
        g_signal_handlers_disconnect_matched(icon_view->priv->vadjustment,
                                             G_SIGNAL_MATCH_DATA,
                                             0,
                                             0,
                                             NULL,
                                             NULL,
                                             icon_view);
        g_object_unref(icon_view->priv->vadjustment);
    }

    if (icon_view->priv->hadjustment != hadj)
    {
        icon_view->priv->hadjustment = hadj;
        g_object_ref_sink(icon_view->priv->hadjustment);

        g_signal_connect(icon_view->priv->hadjustment,
                         "value-changed",
                         G_CALLBACK(exo_icon_view_adjustment_changed),
                         icon_view);
        need_adjust = TRUE;
    }

    if (icon_view->priv->vadjustment != vadj)
    {
        icon_view->priv->vadjustment = vadj;
        g_object_ref_sink(icon_view->priv->vadjustment);

        g_signal_connect(icon_view->priv->vadjustment,
                         "value-changed",
                         G_CALLBACK(exo_icon_view_adjustment_changed),
                         icon_view);
        need_adjust = TRUE;
    }

    if (need_adjust)
        exo_icon_view_adjustment_changed(NULL, icon_view);
}

static void exo_icon_view_real_select_all(ExoIconView* icon_view)
{
    exo_icon_view_select_all(icon_view);
}

static void exo_icon_view_real_unselect_all(ExoIconView* icon_view)
{
    exo_icon_view_unselect_all(icon_view);
}

static void exo_icon_view_real_select_cursor_item(ExoIconView* icon_view)
{
    exo_icon_view_unselect_all(icon_view);

    if (icon_view->priv->cursor_item != NULL)
        exo_icon_view_select_item(icon_view, icon_view->priv->cursor_item);
}

static bool exo_icon_view_real_activate_cursor_item(ExoIconView* icon_view)
{
    GtkTreePath* path;
    GtkCellRendererMode mode;
    ExoIconViewCellInfo* info = NULL;

    if (!icon_view->priv->cursor_item)
        return FALSE;

    info = g_list_nth_data(icon_view->priv->cell_list, icon_view->priv->cursor_cell);

    if (info)
    {
        g_object_get(info->cell, "mode", &mode, NULL);

        if (mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE)
        {
            exo_icon_view_item_activate_cell(icon_view, icon_view->priv->cursor_item, info, NULL);
            return TRUE;
        }
        else if (mode == GTK_CELL_RENDERER_MODE_EDITABLE)
        {
            exo_icon_view_start_editing(icon_view, icon_view->priv->cursor_item, info, NULL);
            return TRUE;
        }
    }

    path = gtk_tree_path_new_from_indices(
        g_list_index(icon_view->priv->items, icon_view->priv->cursor_item),
        -1);
    exo_icon_view_item_activated(icon_view, path);
    gtk_tree_path_free(path);

    return TRUE;
}

static bool exo_icon_view_real_start_interactive_search(ExoIconView* icon_view)
{
    return exo_icon_view_search_start(icon_view, TRUE);
}

static void exo_icon_view_real_toggle_cursor_item(ExoIconView* icon_view)
{
    if (G_LIKELY(icon_view->priv->cursor_item != NULL))
    {
        switch (icon_view->priv->selection_mode)
        {
            case GTK_SELECTION_NONE:
                break;

            case GTK_SELECTION_BROWSE:
                exo_icon_view_select_item(icon_view, icon_view->priv->cursor_item);
                break;

            case GTK_SELECTION_SINGLE:
                if (icon_view->priv->cursor_item->selected)
                    exo_icon_view_unselect_item(icon_view, icon_view->priv->cursor_item);
                else
                    exo_icon_view_select_item(icon_view, icon_view->priv->cursor_item);
                break;

            case GTK_SELECTION_MULTIPLE:
                icon_view->priv->cursor_item->selected = !icon_view->priv->cursor_item->selected;
                g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
                exo_icon_view_queue_draw_item(icon_view, icon_view->priv->cursor_item);
                break;

            default:
                g_assert_not_reached();
        }
    }
}

static void exo_icon_view_adjustment_changed(GtkAdjustment* adjustment, ExoIconView* icon_view)
{
    if (gtk_widget_get_realized(GTK_WIDGET(icon_view)))
    {
        gdk_window_move(icon_view->priv->bin_window,
                        -gtk_adjustment_get_value(icon_view->priv->hadjustment),
                        -gtk_adjustment_get_value(icon_view->priv->vadjustment));

        if (G_UNLIKELY(icon_view->priv->doing_rubberband))
            exo_icon_view_update_rubberband(GTK_WIDGET(icon_view));

        gdk_window_process_updates(icon_view->priv->bin_window, TRUE);
    }
}

static GList* exo_icon_view_layout_single_row(ExoIconView* icon_view, GList* first_item,
                                              int item_width, int row, int* y, int* maximum_width,
                                              int max_cols)
{
    ExoIconViewPrivate* priv = icon_view->priv;
    ExoIconViewItem* item;
    bool rtl;
    GList* last_item;
    GList* items = first_item;
    int* max_width;
    int* max_height;
    int focus_width;
    int current_width;
    int colspan;
    int col = 0;
    int x;
    int i;
    GtkAllocation allocation;

    rtl = (gtk_widget_get_direction(GTK_WIDGET(icon_view)) == GTK_TEXT_DIR_RTL);
    gtk_widget_get_allocation(GTK_WIDGET(icon_view), &allocation);

    max_width = g_newa(int, priv->n_cells);
    max_height = g_newa(int, priv->n_cells);
    for (i = priv->n_cells; --i >= 0;)
    {
        max_width[i] = 0;
        max_height[i] = 0;
    }

    gtk_widget_style_get(GTK_WIDGET(icon_view), "focus-line-width", &focus_width, NULL);

    x = priv->margin + focus_width;
    current_width = 2 * (priv->margin + focus_width);

    for (items = first_item; items != NULL; items = items->next)
    {
        item = EXO_ICON_VIEW_ITEM(items->data);

        exo_icon_view_calculate_item_size(icon_view, item);
        colspan = 1 + (item->area.width - 1) / (item_width + priv->column_spacing);

        item->area.width = colspan * item_width + (colspan - 1) * priv->column_spacing;

        current_width += item->area.width + priv->column_spacing + 2 * focus_width;

        if (G_LIKELY(items != first_item))
        {
            if ((priv->columns <= 0 && current_width > allocation.width) ||
                (priv->columns > 0 && col >= priv->columns) || (max_cols > 0 && col >= max_cols))
                break;
        }

        item->area.y = *y + focus_width;
        item->area.x = rtl ? allocation.width - item->area.width - x : x;

        x = current_width - (priv->margin + focus_width);

        for (i = 0; i < priv->n_cells; i++)
        {
            max_width[i] = MAX(max_width[i], item->box[i].width);
            max_height[i] = MAX(max_height[i], item->box[i].height);
        }

        if (current_width > *maximum_width)
            *maximum_width = current_width;

        item->row = row;
        item->col = col;

        col += colspan;
    }

    last_item = items;

    /* Now go through the row again and align the icons */
    for (items = first_item; items != last_item; items = items->next)
    {
        item = EXO_ICON_VIEW_ITEM(items->data);

        exo_icon_view_calculate_item_size2(icon_view, item, max_width, max_height);

        /* We may want to readjust the new y coordinate. */
        if (item->area.y + item->area.height + focus_width + priv->row_spacing > *y)
            *y = item->area.y + item->area.height + focus_width + priv->row_spacing;

        if (G_UNLIKELY(rtl))
            item->col = col - 1 - item->col;
    }

    return last_item;
}

static GList* exo_icon_view_layout_single_col(ExoIconView* icon_view, GList* first_item,
                                              int item_height, int col, int* x, int* maximum_height,
                                              int max_rows)
{
    ExoIconViewPrivate* priv = icon_view->priv;
    ExoIconViewItem* item;
    GList* items = first_item;
    GList* last_item;
    int* max_width;
    int* max_height;
    int focus_width;
    int current_height;
    int rowspan;
    int row = 0;
    int y;
    int i;
    GtkAllocation allocation;

    max_width = g_newa(int, priv->n_cells);
    max_height = g_newa(int, priv->n_cells);
    for (i = priv->n_cells; --i >= 0;)
    {
        max_width[i] = 0;
        max_height[i] = 0;
    }

    gtk_widget_style_get(GTK_WIDGET(icon_view), "focus-line-width", &focus_width, NULL);
    gtk_widget_get_allocation(GTK_WIDGET(icon_view), &allocation);

    y = priv->margin + focus_width;
    current_height = 2 * (priv->margin + focus_width);

    for (items = first_item; items != NULL; items = items->next)
    {
        item = EXO_ICON_VIEW_ITEM(items->data);

        exo_icon_view_calculate_item_size(icon_view, item);

        rowspan = 1 + (item->area.height - 1) / (item_height + priv->row_spacing);

        item->area.height = rowspan * item_height + (rowspan - 1) * priv->row_spacing;

        current_height += item->area.height + priv->row_spacing + 2 * focus_width;

        if (G_LIKELY(items != first_item))
        {
            if (current_height >= allocation.height || (max_rows > 0 && row >= max_rows))
                break;
        }

        item->area.y = y + focus_width;
        item->area.x = *x;

        y = current_height - (priv->margin + focus_width);

        for (i = 0; i < priv->n_cells; i++)
        {
            max_width[i] = MAX(max_width[i], item->box[i].width);
            max_height[i] = MAX(max_height[i], item->box[i].height);
        }

        if (current_height > *maximum_height)
            *maximum_height = current_height;

        item->row = row;
        item->col = col;

        row += rowspan;
    }

    last_item = items;

    /* Now go through the column again and align the icons */
    for (items = first_item; items != last_item; items = items->next)
    {
        item = EXO_ICON_VIEW_ITEM(items->data);

        exo_icon_view_calculate_item_size2(icon_view, item, max_width, max_height);

        /* We may want to readjust the new x coordinate. */
        if (item->area.x + item->area.width + focus_width + priv->column_spacing > *x)
            *x = item->area.x + item->area.width + focus_width + priv->column_spacing;
    }

    return last_item;
}

static void exo_icon_view_set_adjustment_upper(GtkAdjustment* adj, double upper)
{
    if (upper != gtk_adjustment_get_upper(adj))
    {
        double min = MAX(0.0, upper - gtk_adjustment_get_page_size(adj));
#if (GTK_MAJOR_VERSION == 2)
        bool value_changed = FALSE;
#endif

        gtk_adjustment_set_upper(adj, upper);

        if (gtk_adjustment_get_value(adj) > min)
        {
            gtk_adjustment_set_value(adj, min);
#if (GTK_MAJOR_VERSION == 2)
            value_changed = TRUE;
#endif
        }
#if (GTK_MAJOR_VERSION == 2)
        gtk_adjustment_changed(adj);

        if (value_changed)
            gtk_adjustment_value_changed(adj);
#endif
    }
}

static int exo_icon_view_layout_cols(ExoIconView* icon_view, int item_height, int* x,
                                     int* maximum_height, int max_rows)
{
    GList* icons = icon_view->priv->items;
    GList* items;
    int col = 0;
    int rows = 0;

    *x = icon_view->priv->margin;

    do
    {
        icons = exo_icon_view_layout_single_col(icon_view,
                                                icons,
                                                item_height,
                                                col,
                                                x,
                                                maximum_height,
                                                max_rows);

        /* count the number of rows in the first column */
        if (G_UNLIKELY(col == 0))
        {
            for (items = icon_view->priv->items, rows = 0; items != icons;
                 items = items->next, ++rows)
                ;
        }

        col++;
    } while (icons != NULL);

    *x += icon_view->priv->margin;
    icon_view->priv->cols = col;

    return rows;
}

static int exo_icon_view_layout_rows(ExoIconView* icon_view, int item_width, int* y,
                                     int* maximum_width, int max_cols)
{
    GList* icons = icon_view->priv->items;
    GList* items;
    int row = 0;
    int cols = 0;

    *y = icon_view->priv->margin;

    do
    {
        icons = exo_icon_view_layout_single_row(icon_view,
                                                icons,
                                                item_width,
                                                row,
                                                y,
                                                maximum_width,
                                                max_cols);

        /* count the number of columns in the first row */
        if (G_UNLIKELY(row == 0))
        {
            for (items = icon_view->priv->items, cols = 0; items != icons;
                 items = items->next, ++cols)
                ;
        }

        row++;
    } while (icons != NULL);

    *y += icon_view->priv->margin;
    icon_view->priv->rows = row;

    return cols;
}

static void exo_icon_view_layout(ExoIconView* icon_view)
{
    ExoIconViewPrivate* priv = icon_view->priv;
    ExoIconViewItem* item;
    GList* icons;
    int maximum_height = 0;
    int maximum_width = 0;
    int item_height;
    int item_width;
    int rows, cols;
    int x, y;
    GtkAllocation allocation;
    GtkRequisition requisition;

    /* verify that we still have a valid model */
    if (G_UNLIKELY(priv->model == NULL))
        return;

    gtk_widget_get_allocation(GTK_WIDGET(icon_view), &allocation);

#if (GTK_MAJOR_VERSION == 3)
    gtk_widget_get_preferred_width(GTK_WIDGET(icon_view), NULL, &requisition.width);
    gtk_widget_get_preferred_height(GTK_WIDGET(icon_view), NULL, &requisition.height);
#elif (GTK_MAJOR_VERSION == 2)
    gtk_widget_get_requisition(GTK_WIDGET(icon_view), &requisition);
#endif

    /* determine the layout mode */
    if (G_LIKELY(priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS))
    {
        /* calculate item sizes on-demand */
        item_width = priv->item_width;
        if (item_width < 0)
        {
            for (icons = priv->items; icons != NULL; icons = icons->next)
            {
                item = icons->data;
                exo_icon_view_calculate_item_size(icon_view, item);
                item_width = MAX(item_width, item->area.width);
            }
        }

        cols = exo_icon_view_layout_rows(icon_view, item_width, &y, &maximum_width, 0);

        /* If, by adding another column, we increase the height of the icon view, thus forcing a
         * vertical scrollbar to appear that would prevent the last column from being able to fit,
         * we need to relayout the icons with one less column.
         */
        if (cols == priv->cols + 1 && y > allocation.height && priv->height <= allocation.height)
        {
            cols = exo_icon_view_layout_rows(icon_view, item_width, &y, &maximum_width, priv->cols);
        }

        priv->width = maximum_width;
        priv->height = y;
        priv->cols = cols;
    }
    else
    {
        /* calculate item sizes on-demand */
        for (icons = priv->items, item_height = 0; icons != NULL; icons = icons->next)
        {
            item = icons->data;
            exo_icon_view_calculate_item_size(icon_view, item);
            item_height = MAX(item_height, item->area.height);
        }

        rows = exo_icon_view_layout_cols(icon_view, item_height, &x, &maximum_height, 0);

        /* If, by adding another row, we increase the width of the icon view, thus forcing a
         * horizontal scrollbar to appear that would prevent the last row from being able to fit,
         * we need to relayout the icons with one less row.
         */
        if (rows == priv->rows + 1 && x > allocation.width && priv->width <= allocation.width)
        {
            rows =
                exo_icon_view_layout_cols(icon_view, item_height, &x, &maximum_height, priv->rows);
        }

        priv->height = maximum_height;
        priv->width = x;
        priv->rows = rows;
    }

    exo_icon_view_set_adjustment_upper(priv->hadjustment, priv->width);
    exo_icon_view_set_adjustment_upper(priv->vadjustment, priv->height);

    if (gtk_widget_get_realized(GTK_WIDGET(icon_view)))
    {
        gdk_window_resize(priv->bin_window,
                          MAX(priv->width, allocation.width),
                          MAX(priv->height, allocation.height));
    }

    /* drop any pending layout idle source */
    if (priv->layout_idle_id != 0)
        g_source_remove(priv->layout_idle_id);

    gtk_widget_queue_draw(GTK_WIDGET(icon_view));
}

static void exo_icon_view_get_cell_area(ExoIconView* icon_view, ExoIconViewItem* item,
                                        ExoIconViewCellInfo* info, GdkRectangle* cell_area)
{
    if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        cell_area->x = item->box[info->position].x - item->before[info->position];
        cell_area->y = item->area.y;
        cell_area->width = item->box[info->position].width + item->before[info->position] +
                           item->after[info->position];
        cell_area->height = item->area.height;
    }
    else
    {
        cell_area->x = item->area.x;
        cell_area->y = item->box[info->position].y - item->before[info->position];
        cell_area->width = item->area.width;
        cell_area->height = item->box[info->position].height + item->before[info->position] +
                            item->after[info->position];
    }
}

static void exo_icon_view_calculate_item_size(ExoIconView* icon_view, ExoIconViewItem* item)
{
    ExoIconViewCellInfo* info;
    GList* lp;
    char* buffer;

    if (G_LIKELY(item->area.width != -1))
        return;

    if (G_UNLIKELY(item->n_cells != icon_view->priv->n_cells))
    {
        /* apply the new cell size */
        item->n_cells = icon_view->priv->n_cells;

        /* release the memory chunk (if any) */
        g_free(item->box);

        /* allocate a single memory chunk for box, after and before */
        buffer = g_malloc0(item->n_cells * (sizeof(GdkRectangle) + 2 * sizeof(int)));

        /* assign the memory */
        item->box = (GdkRectangle*)buffer;
        item->after = (int*)(buffer + item->n_cells * sizeof(GdkRectangle));
        item->before = item->after + item->n_cells;
    }

    exo_icon_view_set_cell_data(icon_view, item);

    item->area.width = 0;
    item->area.height = 0;
    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
    {
        info = EXO_ICON_VIEW_CELL_INFO(lp->data);
        if (G_UNLIKELY(!gtk_cell_renderer_get_visible(info->cell)))
            continue;

        {
#if (GTK_MAJOR_VERSION == 3)
            GtkRequisition req;

            gtk_cell_renderer_get_preferred_size(info->cell, GTK_WIDGET(icon_view), &req, NULL);

            if (info->is_text && icon_view->priv->orientation == GTK_ORIENTATION_VERTICAL)
            {
                GdkRectangle cell_area, aligned_area;

                cell_area.width = req.width;
                cell_area.height = req.height;

                gtk_cell_renderer_get_aligned_area(info->cell,
                                                   GTK_WIDGET(icon_view),
                                                   0,
                                                   &cell_area,
                                                   &aligned_area);

                /* 4px padding for width to avoid truncated text in small zoom levels */
                item->box[info->position].width = aligned_area.width + 4;
                item->box[info->position].height = aligned_area.height;
            }
            else
            {
                item->box[info->position].width = req.width;
                item->box[info->position].height = req.height;
            }
#elif (GTK_MAJOR_VERSION == 2)
            gtk_cell_renderer_get_size(info->cell,
                                       GTK_WIDGET(icon_view),
                                       NULL,
                                       NULL,
                                       NULL,
                                       &item->box[info->position].width,
                                       &item->box[info->position].height);
#endif
        }

        if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            item->area.width += item->box[info->position].width +
                                (info->position > 0 ? icon_view->priv->spacing : 0);
            item->area.height = MAX(item->area.height, item->box[info->position].height);
        }
        else
        {
            item->area.width = MAX(item->area.width, item->box[info->position].width);
            item->area.height += item->box[info->position].height +
                                 (info->position > 0 ? icon_view->priv->spacing : 0);
        }
    }
}

static void exo_icon_view_calculate_item_size2(ExoIconView* icon_view, ExoIconViewItem* item,
                                               int* max_width, int* max_height)
{
    ExoIconViewCellInfo* info;
    GdkRectangle* box;
    GdkRectangle cell_area;
    bool rtl;
    GList* lp;
    int spacing;
    int i, k;
    float cell_xalign, cell_yalign;
    int cell_xpad, cell_ypad;

    rtl = (gtk_widget_get_direction(GTK_WIDGET(icon_view)) == GTK_TEXT_DIR_RTL);

    spacing = icon_view->priv->spacing;

    if (G_LIKELY(icon_view->priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS))
    {
        item->area.height = 0;
        for (i = 0; i < icon_view->priv->n_cells; ++i)
        {
            if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
                item->area.height = MAX(item->area.height, max_height[i]);
            else
                item->area.height += max_height[i] + (i > 0 ? spacing : 0);
        }
    }
    else
    {
        item->area.width = 0;
        for (i = 0; i < icon_view->priv->n_cells; ++i)
        {
            if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
                item->area.width += max_width[i] + (i > 0 ? spacing : 0);
            else
                item->area.width = MAX(item->area.width, max_width[i]);
        }
    }

    cell_area.x = item->area.x;
    cell_area.y = item->area.y;

    for (k = 0; k < 2; ++k)
    {
        for (lp = icon_view->priv->cell_list, i = 0; lp != NULL; lp = lp->next, ++i)
        {
            info = EXO_ICON_VIEW_CELL_INFO(lp->data);
            if (G_UNLIKELY(!gtk_cell_renderer_get_visible(info->cell) ||
                           info->pack == (k ? GTK_PACK_START : GTK_PACK_END)))
                continue;

            gtk_cell_renderer_get_alignment(info->cell, &cell_xalign, &cell_yalign);
            gtk_cell_renderer_get_padding(info->cell, &cell_xpad, &cell_ypad);

            if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
                cell_area.width = item->box[info->position].width;
                cell_area.height = item->area.height;
            }
            else
            {
                cell_area.width = item->area.width;
                cell_area.height = max_height[i];
            }

            box = item->box + info->position;
            box->x = cell_area.x + cell_xalign * (cell_area.width - box->width - (2 * cell_xpad));
            box->x = MAX(box->x, 0);
            box->y = cell_area.y + cell_yalign * (cell_area.height - box->height - (2 * cell_ypad));
            box->y = MAX(box->y, 0);

            if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
                item->before[info->position] = item->box[info->position].x - cell_area.x;
                item->after[info->position] = cell_area.width - item->box[info->position].width -
                                              item->before[info->position];
                cell_area.x += cell_area.width + spacing;
            }
            else
            {
                if (item->box[info->position].width > item->area.width)
                {
                    item->area.width = item->box[info->position].width;
                    cell_area.width = item->area.width;
                }
                item->before[info->position] = item->box[info->position].y - cell_area.y;
                item->after[info->position] = cell_area.height - item->box[info->position].height -
                                              item->before[info->position];
                cell_area.y += cell_area.height + spacing;
            }
        }
    }

    if (G_UNLIKELY(rtl && icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL))
    {
        for (i = 0; i < icon_view->priv->n_cells; i++)
            item->box[i].x = item->area.x + item->area.width -
                             (item->box[i].x + item->box[i].width - item->area.x);
    }
}

static void exo_icon_view_invalidate_sizes(ExoIconView* icon_view)
{
    GList* lp;

    for (lp = icon_view->priv->items; lp != NULL; lp = lp->next)
        EXO_ICON_VIEW_ITEM(lp->data)->area.width = -1;
    exo_icon_view_queue_layout(icon_view);
}

#if (GTK_MAJOR_VERSION == 3)
static void exo_icon_view_paint_item(ExoIconView* icon_view, ExoIconViewItem* item,
                                     GdkRectangle* area, cairo_t* cr, int x, int y, bool draw_focus)
{
    GtkCellRendererState flags = 0;
    ExoIconViewCellInfo* info;
    GtkStateFlags state;
    GdkRectangle cell_area;
    GdkRectangle aligned_area;
    GtkStyleContext* style_context;
    GList* lp;

    if (G_UNLIKELY(icon_view->priv->model == NULL))
        return;

    exo_icon_view_set_cell_data(icon_view, item);

    style_context = gtk_widget_get_style_context(GTK_WIDGET(icon_view));
    state = gtk_widget_get_state_flags(GTK_WIDGET(icon_view));

    gtk_style_context_save(style_context);
    gtk_style_context_add_class(style_context, GTK_STYLE_CLASS_CELL);

    state &= ~(GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_PRELIGHT);

    if (G_UNLIKELY(EXO_ICON_VIEW_FLAG_SET(icon_view, EXO_ICON_VIEW_DRAW_KEYFOCUS) &&
                   (state & GTK_STATE_FLAG_FOCUSED) && item == icon_view->priv->cursor_item))
    {
        flags |= GTK_CELL_RENDERER_FOCUSED;
    }

    if (G_UNLIKELY(item->selected))
    {
        state |= GTK_STATE_FLAG_SELECTED;
        flags |= GTK_CELL_RENDERER_SELECTED;
    }

    if (G_UNLIKELY(icon_view->priv->prelit_item == item))
    {
        state |= GTK_STATE_FLAG_PRELIGHT;
        flags |= GTK_CELL_RENDERER_PRELIT;
    }

    gtk_style_context_set_state(style_context, state);

    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
    {
        info = EXO_ICON_VIEW_CELL_INFO(lp->data);

        cairo_save(cr);

        if (G_UNLIKELY(!gtk_cell_renderer_get_visible(info->cell)))
            continue;

        exo_icon_view_get_cell_area(icon_view, item, info, &cell_area);

        cell_area.x = x - item->area.x + cell_area.x;
        cell_area.y = y - item->area.y + cell_area.y;

        /* FIXME: this is bad CSS usage */
        if (info->is_text)
        {
            gtk_cell_renderer_get_aligned_area(info->cell,
                                               GTK_WIDGET(icon_view),
                                               flags,
                                               &cell_area,
                                               &aligned_area);

            gtk_render_background(style_context,
                                  cr,
                                  aligned_area.x,
                                  aligned_area.y,
                                  aligned_area.width,
                                  aligned_area.height);

            gtk_render_frame(style_context,
                             cr,
                             aligned_area.x,
                             aligned_area.y,
                             aligned_area.width,
                             aligned_area.height);

            /* draw outline if focused */
            if (flags & GTK_CELL_RENDERER_FOCUSED)
            {
                gtk_render_focus(style_context,
                                 cr,
                                 aligned_area.x,
                                 aligned_area.y,
                                 aligned_area.width,
                                 aligned_area.height);
            }
        }

        gtk_cell_renderer_render(info->cell,
                                 cr,
                                 GTK_WIDGET(icon_view),
                                 &cell_area,
                                 &cell_area,
                                 flags);

        cairo_restore(cr);
    }

    gtk_style_context_restore(style_context);
}
#elif (GTK_MAJOR_VERSION == 2)
static void exo_icon_view_paint_item(ExoIconView* icon_view, ExoIconViewItem* item,
                                     GdkRectangle* area, GdkDrawable* drawable, int x, int y,
                                     bool draw_focus)
{
    GtkCellRendererState flags;
    ExoIconViewCellInfo* info;
    GdkRectangle cell_area;
    GList* lp;

    if (G_UNLIKELY(icon_view->priv->model == NULL))
        return;

    exo_icon_view_set_cell_data(icon_view, item);

    if (item->selected)
    {
        flags = GTK_CELL_RENDERER_SELECTED;
        gtk_widget_has_focus(GTK_WIDGET(icon_view)) ? GTK_STATE_SELECTED : GTK_STATE_ACTIVE;
    }
    else
    {
        flags = 0;
    }

    if (G_UNLIKELY(icon_view->priv->prelit_item == item))
        flags |= GTK_CELL_RENDERER_PRELIT;
    /* NOTE by Hong Jen Yee: We always want to focus rectangle. */
    if (G_UNLIKELY(
            /*EXO_ICON_VIEW_FLAG_SET (icon_view, EXO_ICON_VIEW_DRAW_KEYFOCUS) &&*/ icon_view->priv
                ->cursor_item == item))
        flags |= GTK_CELL_RENDERER_FOCUSED;

    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
    {
        info = EXO_ICON_VIEW_CELL_INFO(lp->data);

        if (G_UNLIKELY(!gtk_cell_renderer_get_visible(info->cell)))
            continue;

        exo_icon_view_get_cell_area(icon_view, item, info, &cell_area);

        cell_area.x = x - item->area.x + cell_area.x;
        cell_area.y = y - item->area.y + cell_area.y;

        gtk_cell_renderer_render(info->cell,
                                 drawable,
                                 GTK_WIDGET(icon_view),
                                 &cell_area,
                                 &cell_area,
                                 area,
                                 flags);
    }
}
#endif

static void exo_icon_view_queue_draw_item(ExoIconView* icon_view, ExoIconViewItem* item)
{
    GdkRectangle rect;
    int focus_width;

    gtk_widget_style_get(GTK_WIDGET(icon_view), "focus-line-width", &focus_width, NULL);

    rect.x = item->area.x - focus_width;
    rect.y = item->area.y - focus_width;
    rect.width = item->area.width + 2 * focus_width;
    rect.height = item->area.height + 2 * focus_width;

    if (icon_view->priv->bin_window)
    {
        // printf("draw_item %d,%d %dx%d\n", rect.x, rect.y, rect.width, rect.height );
        gdk_window_invalidate_rect(icon_view->priv->bin_window, &rect, TRUE);
    }
}

static bool layout_callback(void* user_data)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(user_data);

    exo_icon_view_layout(icon_view);

    return FALSE;
}

static void layout_destroy(void* user_data)
{
    EXO_ICON_VIEW(user_data)->priv->layout_idle_id = 0;
}

static void exo_icon_view_queue_layout(ExoIconView* icon_view)
{
    if (G_UNLIKELY(icon_view->priv->layout_idle_id == 0))
        icon_view->priv->layout_idle_id =
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, layout_callback, icon_view, layout_destroy);
    // icon_view->priv->layout_idle_id = gdk_threads_add_idle_full(G_PRIORITY_DEFAULT_IDLE,
    // layout_callback, icon_view, layout_destroy);
}

static void exo_icon_view_set_cursor_item(ExoIconView* icon_view, ExoIconViewItem* item,
                                          int cursor_cell)
{
    if (icon_view->priv->cursor_item == item &&
        (cursor_cell < 0 || cursor_cell == icon_view->priv->cursor_cell))
        return;

    if (icon_view->priv->cursor_item != NULL)
        exo_icon_view_queue_draw_item(icon_view, icon_view->priv->cursor_item);

    icon_view->priv->cursor_item = item;
    if (cursor_cell >= 0)
        icon_view->priv->cursor_cell = cursor_cell;

    exo_icon_view_queue_draw_item(icon_view, item);
}

static ExoIconViewItem* exo_icon_view_get_item_at_coords(const ExoIconView* icon_view, int x, int y,
                                                         bool only_in_cell,
                                                         ExoIconViewCellInfo** cell_at_pos)
{
    const ExoIconViewPrivate* priv = icon_view->priv;
    ExoIconViewCellInfo* info;
    ExoIconViewItem* item;
    GdkRectangle box;
    const GList* items;
    const GList* lp;

    for (items = priv->items; items != NULL; items = items->next)
    {
        item = items->data;
        if (x >= item->area.x - priv->row_spacing / 2 &&
            x <= item->area.x + item->area.width + priv->row_spacing / 2 &&
            y >= item->area.y - priv->column_spacing / 2 &&
            y <= item->area.y + item->area.height + priv->column_spacing / 2)
        {
            if (only_in_cell || cell_at_pos)
            {
                exo_icon_view_set_cell_data(icon_view, item);
                for (lp = priv->cell_list; lp != NULL; lp = lp->next)
                {
                    /* check if the cell is visible */
                    info = (ExoIconViewCellInfo*)lp->data;
                    if (!gtk_cell_renderer_get_visible(info->cell))
                        continue;

                    box = item->box[info->position];
                    if ((x >= box.x && x <= box.x + box.width && y >= box.y &&
                         y <= box.y + box.height))
                    {
                        if (cell_at_pos != NULL)
                            *cell_at_pos = info;

                        return item;
                    }
                }

                if (only_in_cell)
                    return NULL;

                if (cell_at_pos != NULL)
                    *cell_at_pos = NULL;
            }

            return item;
        }
    }

    return NULL;
}

static void exo_icon_view_select_item(ExoIconView* icon_view, ExoIconViewItem* item)
{
    if (item->selected || icon_view->priv->selection_mode == GTK_SELECTION_NONE)
        return;
    else if (icon_view->priv->selection_mode != GTK_SELECTION_MULTIPLE)
        exo_icon_view_unselect_all_internal(icon_view);

    item->selected = TRUE;

    exo_icon_view_queue_draw_item(icon_view, item);

    g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

static void exo_icon_view_unselect_item(ExoIconView* icon_view, ExoIconViewItem* item)
{
    if (!item->selected)
        return;

    if (icon_view->priv->selection_mode == GTK_SELECTION_NONE ||
        icon_view->priv->selection_mode == GTK_SELECTION_BROWSE)
        return;

    item->selected = FALSE;

    g_signal_emit(G_OBJECT(icon_view), icon_view_signals[SELECTION_CHANGED], 0);

    exo_icon_view_queue_draw_item(icon_view, item);
}

static void exo_icon_view_row_changed(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
                                      ExoIconView* icon_view)
{
    ExoIconViewItem* item;

    item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);

    /* stop editing this item */
    if (G_UNLIKELY(item == icon_view->priv->edited_item))
        exo_icon_view_stop_editing(icon_view, TRUE);

    /* emit "selection-changed" if the item is selected */
    if (G_UNLIKELY(item->selected))
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);

    /* recalculate layout (a value of -1 for width
     * indicates that the item needs to be layouted).
     */
    item->area.width = -1;
    exo_icon_view_queue_layout(icon_view);
}

static void exo_icon_view_row_inserted(GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
                                       ExoIconView* icon_view)
{
    ExoIconViewItem* item;
    int idx;

    idx = gtk_tree_path_get_indices(path)[0];

    /* allocate the new item */
    item = g_slice_new0(ExoIconViewItem);
    item->iter = *iter;
    item->area.width = -1;
    icon_view->priv->items = g_list_insert(icon_view->priv->items, item, idx);

    /* recalculate the layout */
    exo_icon_view_queue_layout(icon_view);
}

static void exo_icon_view_row_deleted(GtkTreeModel* model, GtkTreePath* path,
                                      ExoIconView* icon_view)
{
    ExoIconViewItem* item;
    bool changed = FALSE;
    GList* list;

    /* determine the position and the item for the path */
    list = g_list_nth(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);
    item = list->data;

    if (G_UNLIKELY(item == icon_view->priv->edited_item))
        exo_icon_view_stop_editing(icon_view, TRUE);

    /* use the next item (if any) as anchor, else use prev, otherwise reset anchor */
    if (G_UNLIKELY(item == icon_view->priv->anchor_item))
        icon_view->priv->anchor_item = (list->next != NULL)
                                           ? list->next->data
                                           : ((list->prev != NULL) ? list->prev->data : NULL);

    /* use the next item (if any) as cursor, else use prev, otherwise reset cursor */
    if (G_UNLIKELY(item == icon_view->priv->cursor_item))
        icon_view->priv->cursor_item = (list->next != NULL)
                                           ? list->next->data
                                           : ((list->prev != NULL) ? list->prev->data : NULL);

    if (G_UNLIKELY(item == icon_view->priv->prelit_item))
    {
        /* reset the prelit item */
        icon_view->priv->prelit_item = NULL;

        /* cancel any pending single click timer */
        if (G_UNLIKELY(icon_view->priv->single_click_timeout_id != 0))
            g_source_remove(icon_view->priv->single_click_timeout_id);

        /* in single click mode, we also reset the cursor when realized */
        if (G_UNLIKELY(icon_view->priv->single_click &&
                       gtk_widget_get_realized(GTK_WIDGET(icon_view))))
            gdk_window_set_cursor(icon_view->priv->bin_window, NULL);
    }

    /* check if the selection changed */
    if (G_UNLIKELY(item->selected))
        changed = TRUE;

    /* release the item resources */
    g_free(item->box);

    /* drop the item from the list */
    icon_view->priv->items = g_list_delete_link(icon_view->priv->items, list);

    /* release the item */
    g_slice_free(ExoIconViewItem, item);

    /* recalculate the layout */
    exo_icon_view_queue_layout(icon_view);

    /* if we removed a previous selected item, we need
     * to tell others that we have a new selection.
     */
    if (G_UNLIKELY(changed))
        g_signal_emit(G_OBJECT(icon_view), icon_view_signals[SELECTION_CHANGED], 0);
}

static void exo_icon_view_rows_reordered(GtkTreeModel* model, GtkTreePath* parent,
                                         GtkTreeIter* iter, int* new_order, ExoIconView* icon_view)
{
    GList** list_array;
    GList* list;
    int* order;
    int length;
    int i;

    /* cancel any editing attempt */
    exo_icon_view_stop_editing(icon_view, TRUE);

    /* determine the number of items to reorder */
    length = gtk_tree_model_iter_n_children(model, NULL);
    if (G_UNLIKELY(length == 0))
        return;

    list_array = g_newa(GList*, length);
    order = g_newa(int, length);

    for (i = 0; i < length; i++)
        order[new_order[i]] = i;

    for (i = 0, list = icon_view->priv->items; list != NULL; list = list->next, i++)
        list_array[order[i]] = list;

    /* hook up the first item */
    icon_view->priv->items = list_array[0];
    list_array[0]->prev = NULL;

    /* hook up the remaining items */
    for (i = 1; i < length; ++i)
    {
        list_array[i - 1]->next = list_array[i];
        list_array[i]->prev = list_array[i - 1];
    }

    /* hook up the last item */
    list_array[length - 1]->next = NULL;

    exo_icon_view_queue_layout(icon_view);
}

static void exo_icon_view_add_move_binding(GtkBindingSet* binding_set, unsigned int keyval,
                                           unsigned int modmask, GtkMovementStep step, int count)
{
    gtk_binding_entry_add_signal(binding_set,
                                 keyval,
                                 modmask,
                                 "move-cursor",
                                 2,
                                 G_TYPE_ENUM,
                                 step,
                                 G_TYPE_INT,
                                 count);
    gtk_binding_entry_add_signal(binding_set,
                                 keyval,
                                 GDK_SHIFT_MASK,
                                 "move-cursor",
                                 2,
                                 G_TYPE_ENUM,
                                 step,
                                 G_TYPE_INT,
                                 count);

    if ((modmask & GDK_CONTROL_MASK) != GDK_CONTROL_MASK)
    {
        gtk_binding_entry_add_signal(binding_set,
                                     keyval,
                                     GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                                     "move-cursor",
                                     2,
                                     G_TYPE_ENUM,
                                     step,
                                     G_TYPE_INT,
                                     count);
        gtk_binding_entry_add_signal(binding_set,
                                     keyval,
                                     GDK_CONTROL_MASK,
                                     "move-cursor",
                                     2,
                                     G_TYPE_ENUM,
                                     step,
                                     G_TYPE_INT,
                                     count);
    }
}

static bool exo_icon_view_real_move_cursor(ExoIconView* icon_view, GtkMovementStep step, int count)
{
    GdkModifierType state;

    _exo_return_val_if_fail(EXO_ICON_VIEW(icon_view), FALSE);
    _exo_return_val_if_fail(step == GTK_MOVEMENT_LOGICAL_POSITIONS ||
                                step == GTK_MOVEMENT_VISUAL_POSITIONS ||
                                step == GTK_MOVEMENT_DISPLAY_LINES || step == GTK_MOVEMENT_PAGES ||
                                step == GTK_MOVEMENT_BUFFER_ENDS,
                            FALSE);

    if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
        return FALSE;

    exo_icon_view_stop_editing(icon_view, FALSE);
    EXO_ICON_VIEW_SET_FLAG(icon_view, EXO_ICON_VIEW_DRAW_KEYFOCUS);
    gtk_widget_grab_focus(GTK_WIDGET(icon_view));

    if (gtk_get_current_event_state(&state))
    {
        if ((state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
            icon_view->priv->ctrl_pressed = TRUE;
        if ((state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK)
            icon_view->priv->shift_pressed = TRUE;
    }
    /* else we assume not pressed */

    switch (step)
    {
        case GTK_MOVEMENT_LOGICAL_POSITIONS:
        case GTK_MOVEMENT_VISUAL_POSITIONS:
            exo_icon_view_move_cursor_left_right(icon_view, count);
            break;
        case GTK_MOVEMENT_DISPLAY_LINES:
            exo_icon_view_move_cursor_up_down(icon_view, count);
            break;
        case GTK_MOVEMENT_PAGES:
            exo_icon_view_move_cursor_page_up_down(icon_view, count);
            break;
        case GTK_MOVEMENT_BUFFER_ENDS:
            exo_icon_view_move_cursor_start_end(icon_view, count);
            break;
        default:
            g_assert_not_reached();
    }

    icon_view->priv->ctrl_pressed = FALSE;
    icon_view->priv->shift_pressed = FALSE;

    return TRUE;
}

static int find_cell(ExoIconView* icon_view, ExoIconViewItem* item, int cell,
                     GtkOrientation orientation, int step, int* count)
{
    int n_focusable;
    int* focusable;
    int first_text;
    int current;
    int i, k;
    GList* l;
    GtkCellRendererMode mode;

    if (icon_view->priv->orientation != orientation)
        return cell;

    exo_icon_view_set_cell_data(icon_view, item);

    focusable = g_new0(int, icon_view->priv->n_cells);
    n_focusable = 0;

    first_text = 0;
    current = 0;
    for (k = 0; k < 2; k++)
        for (l = icon_view->priv->cell_list, i = 0; l; l = l->next, i++)
        {
            ExoIconViewCellInfo* info = (ExoIconViewCellInfo*)l->data;

            if (info->pack == (k ? GTK_PACK_START : GTK_PACK_END))
                continue;

            if (!gtk_cell_renderer_get_visible(info->cell))
                continue;

            if (GTK_IS_CELL_RENDERER_TEXT(info->cell))
                first_text = i;

            g_object_get(info->cell, "mode", &mode, NULL);
            if (mode != GTK_CELL_RENDERER_MODE_INERT)
            {
                if (cell == i)
                    current = n_focusable;

                focusable[n_focusable] = i;

                n_focusable++;
            }
        }

    if (n_focusable == 0)
        focusable[n_focusable++] = first_text;

    if (cell < 0)
    {
        current = step > 0 ? 0 : n_focusable - 1;
        cell = focusable[current];
    }

    if (current + *count < 0)
    {
        cell = -1;
        *count = current + *count;
    }
    else if (current + *count > n_focusable - 1)
    {
        cell = -1;
        *count = current + *count - (n_focusable - 1);
    }
    else
    {
        cell = focusable[current + *count];
        *count = 0;
    }

    g_free(focusable);

    return cell;
}

static ExoIconViewItem* find_item_page_up_down(ExoIconView* icon_view, ExoIconViewItem* current,
                                               int count)
{
    GList* item = g_list_find(icon_view->priv->items, current);
    GList* next;
    int col = current->col;
    int y = current->area.y + count * gtk_adjustment_get_page_size(icon_view->priv->vadjustment);

    if (count > 0)
    {
        for (; item != NULL; item = item->next)
        {
            for (next = item->next; next; next = next->next)
                if (EXO_ICON_VIEW_ITEM(next->data)->col == col)
                    break;

            if (next == NULL || EXO_ICON_VIEW_ITEM(next->data)->area.y > y)
                break;
        }
    }
    else
    {
        for (; item != NULL; item = item->prev)
        {
            for (next = item->prev; next; next = next->prev)
                if (EXO_ICON_VIEW_ITEM(next->data)->col == col)
                    break;

            if (next == NULL || EXO_ICON_VIEW_ITEM(next->data)->area.y < y)
                break;
        }
    }

    return (item != NULL) ? item->data : NULL;
}

static bool exo_icon_view_select_all_between(ExoIconView* icon_view, ExoIconViewItem* anchor,
                                             ExoIconViewItem* cursor)
{
    GList* items;
    ExoIconViewItem* item;
    ExoIconViewItem* last = NULL;
    bool dirty = FALSE;

    for (items = icon_view->priv->items; items; items = items->next)
    {
        item = items->data;

        if (item == anchor)
        {
            last = cursor;
            break;
        }
        else if (item == cursor)
        {
            last = anchor;
            break;
        }
    }

    for (; items; items = items->next)
    {
        item = items->data;

        if (!item->selected)
            dirty = TRUE;

        item->selected = TRUE;

        exo_icon_view_queue_draw_item(icon_view, item);

        if (item == last)
            break;
    }

    return dirty;
}

static void exo_icon_view_move_cursor_up_down(ExoIconView* icon_view, int count)
{
    ExoIconViewItem* item;
    bool dirty = FALSE;
    GList* list;
    int cell = -1;
    int step;
    GtkDirectionType direction;
    GtkWidget* toplevel;

    if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
        return;

    direction = count < 0 ? GTK_DIR_UP : GTK_DIR_DOWN;

    if (!icon_view->priv->cursor_item)
    {
        if (count > 0)
            list = icon_view->priv->items;
        else
            list = g_list_last(icon_view->priv->items);

        item = list ? list->data : NULL;
    }
    else
    {
        item = icon_view->priv->cursor_item;
        cell = icon_view->priv->cursor_cell;
        step = count > 0 ? 1 : -1;
        while (item)
        {
            cell = find_cell(icon_view, item, cell, GTK_ORIENTATION_VERTICAL, step, &count);
            if (count == 0)
                break;

            /* determine the list position for the item */
            list = g_list_find(icon_view->priv->items, item);

            if (G_LIKELY(icon_view->priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS))
            {
                /* determine the item in the next/prev row */
                if (step > 0)
                {
                    for (list = list->next; list != NULL; list = list->next)
                        if (EXO_ICON_VIEW_ITEM(list->data)->row == item->row + step &&
                            EXO_ICON_VIEW_ITEM(list->data)->col == item->col)
                            break;
                }
                else
                {
                    for (list = list->prev; list != NULL; list = list->prev)
                        if (EXO_ICON_VIEW_ITEM(list->data)->row == item->row + step &&
                            EXO_ICON_VIEW_ITEM(list->data)->col == item->col)
                            break;
                }
            }
            else
            {
                list = (step > 0) ? list->next : list->prev;
            }

            /* check if we found a matching item */
            item = (list != NULL) ? list->data : NULL;

            count = count - step;
        }
    }

    if (!item)
    {
        if (!gtk_widget_keynav_failed(GTK_WIDGET(icon_view), direction))
        {
            toplevel = gtk_widget_get_toplevel(GTK_WIDGET(icon_view));
            if (toplevel != NULL)
            {
                gtk_widget_child_focus(toplevel,
                                       direction == GTK_DIR_UP ? GTK_DIR_TAB_BACKWARD
                                                               : GTK_DIR_TAB_FORWARD);
            }
        }
        return;
    }

    if (icon_view->priv->ctrl_pressed || !icon_view->priv->shift_pressed ||
        !icon_view->priv->anchor_item || icon_view->priv->selection_mode != GTK_SELECTION_MULTIPLE)
        icon_view->priv->anchor_item = item;

    exo_icon_view_set_cursor_item(icon_view, item, cell);

    if (!icon_view->priv->ctrl_pressed && icon_view->priv->selection_mode != GTK_SELECTION_NONE)
    {
        dirty = exo_icon_view_unselect_all_internal(icon_view);
        dirty = exo_icon_view_select_all_between(icon_view, icon_view->priv->anchor_item, item) ||
                dirty;
    }

    exo_icon_view_scroll_to_item(icon_view, item);

    if (dirty)
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

static void exo_icon_view_move_cursor_page_up_down(ExoIconView* icon_view, int count)
{
    ExoIconViewItem* item;
    bool dirty = FALSE;

    if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
        return;

    if (!icon_view->priv->cursor_item)
    {
        GList* list;

        if (count > 0)
            list = icon_view->priv->items;
        else
            list = g_list_last(icon_view->priv->items);

        item = list ? list->data : NULL;
    }
    else
        item = find_item_page_up_down(icon_view, icon_view->priv->cursor_item, count);

    if (!item)
        return;

    if (icon_view->priv->ctrl_pressed || !icon_view->priv->shift_pressed ||
        !icon_view->priv->anchor_item || icon_view->priv->selection_mode != GTK_SELECTION_MULTIPLE)
        icon_view->priv->anchor_item = item;

    exo_icon_view_set_cursor_item(icon_view, item, -1);

    if (!icon_view->priv->ctrl_pressed && icon_view->priv->selection_mode != GTK_SELECTION_NONE)
    {
        dirty = exo_icon_view_unselect_all_internal(icon_view);
        dirty = exo_icon_view_select_all_between(icon_view, icon_view->priv->anchor_item, item) ||
                dirty;
    }

    exo_icon_view_scroll_to_item(icon_view, item);

    if (dirty)
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

static void exo_icon_view_move_cursor_left_right(ExoIconView* icon_view, int count)
{
    ExoIconViewItem* item;
    bool dirty = FALSE;
    GList* list;
    int cell = -1;
    int step;
    GtkDirectionType direction;
    GtkWidget* toplevel;

    if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
        return;

    if (gtk_widget_get_direction(GTK_WIDGET(icon_view)) == GTK_TEXT_DIR_RTL)
        count *= -1;

    direction = count < 0 ? GTK_DIR_LEFT : GTK_DIR_RIGHT;

    if (!icon_view->priv->cursor_item)
    {
        if (count > 0)
            list = icon_view->priv->items;
        else
            list = g_list_last(icon_view->priv->items);

        item = list ? list->data : NULL;
    }
    else
    {
        item = icon_view->priv->cursor_item;
        cell = icon_view->priv->cursor_cell;
        step = count > 0 ? 1 : -1;
        while (item)
        {
            cell = find_cell(icon_view, item, cell, GTK_ORIENTATION_HORIZONTAL, step, &count);
            if (count == 0)
                break;

            /* lookup the item in the list */
            list = g_list_find(icon_view->priv->items, item);

            if (G_LIKELY(icon_view->priv->layout_mode == EXO_ICON_VIEW_LAYOUT_ROWS))
            {
                /* determine the next/prev list item depending on step,
                 * support wrapping around on the edges, as requested
                 * in http://bugzilla.xfce.org/show_bug.cgi?id=1623.
                 */
                list = (step > 0) ? list->next : list->prev;
            }
            else
            {
                /* determine the item in the next/prev row */
                if (step > 0)
                {
                    for (list = list->next; list != NULL; list = list->next)
                        if (EXO_ICON_VIEW_ITEM(list->data)->col == item->col + step &&
                            EXO_ICON_VIEW_ITEM(list->data)->row == item->row)
                            break;
                }
                else
                {
                    for (list = list->prev; list != NULL; list = list->prev)
                        if (EXO_ICON_VIEW_ITEM(list->data)->col == item->col + step &&
                            EXO_ICON_VIEW_ITEM(list->data)->row == item->row)
                            break;
                }
            }

            /* determine the item for the list position (if any) */
            item = (list != NULL) ? list->data : NULL;

            count = count - step;
        }
    }

    if (!item)
    {
        if (!gtk_widget_keynav_failed(GTK_WIDGET(icon_view), direction))
        {
            toplevel = gtk_widget_get_toplevel(GTK_WIDGET(icon_view));
            if (toplevel != NULL)
            {
                gtk_widget_child_focus(toplevel,
                                       direction == GTK_DIR_LEFT ? GTK_DIR_TAB_BACKWARD
                                                                 : GTK_DIR_TAB_FORWARD);
            }
        }
        return;
    }

    if (icon_view->priv->ctrl_pressed || !icon_view->priv->shift_pressed ||
        !icon_view->priv->anchor_item || icon_view->priv->selection_mode != GTK_SELECTION_MULTIPLE)
        icon_view->priv->anchor_item = item;

    exo_icon_view_set_cursor_item(icon_view, item, cell);

    if (!icon_view->priv->ctrl_pressed && icon_view->priv->selection_mode != GTK_SELECTION_NONE)
    {
        dirty = exo_icon_view_unselect_all_internal(icon_view);
        dirty = exo_icon_view_select_all_between(icon_view, icon_view->priv->anchor_item, item) ||
                dirty;
    }

    exo_icon_view_scroll_to_item(icon_view, item);

    if (dirty)
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

static void exo_icon_view_move_cursor_start_end(ExoIconView* icon_view, int count)
{
    ExoIconViewItem* item;
    bool dirty = FALSE;
    GList* lp;

    if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
        return;

    lp = (count < 0) ? icon_view->priv->items : g_list_last(icon_view->priv->items);
    if (G_UNLIKELY(lp == NULL))
        return;

    item = EXO_ICON_VIEW_ITEM(lp->data);
    if (icon_view->priv->ctrl_pressed || !icon_view->priv->shift_pressed ||
        !icon_view->priv->anchor_item || icon_view->priv->selection_mode != GTK_SELECTION_MULTIPLE)
        icon_view->priv->anchor_item = item;

    exo_icon_view_set_cursor_item(icon_view, item, -1);

    if (!icon_view->priv->ctrl_pressed && icon_view->priv->selection_mode != GTK_SELECTION_NONE)
    {
        dirty = exo_icon_view_unselect_all_internal(icon_view);
        dirty = exo_icon_view_select_all_between(icon_view, icon_view->priv->anchor_item, item) ||
                dirty;
    }

    exo_icon_view_scroll_to_item(icon_view, item);

    if (G_UNLIKELY(dirty))
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

/* Get the actual size needed by an item (as opposed to the size
 * allocated based on the largest item in the same row/column).
 */
static void exo_icon_view_get_item_needed_size(ExoIconView* icon_view, ExoIconViewItem* item,
                                               int* width, int* height)
{
    GList* lp;
    ExoIconViewCellInfo* info;

    *width = 0;
    *height = 0;

    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
    {
        info = EXO_ICON_VIEW_CELL_INFO(lp->data);
        if (G_UNLIKELY(!gtk_cell_renderer_get_visible(info->cell)))
            continue;

        if (icon_view->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            *width += item->box[info->position].width +
                      (info->position > 0 ? icon_view->priv->spacing : 0);
            *height = MAX(*height, item->box[info->position].height);
        }
        else
        {
            *width = MAX(*width, item->box[info->position].width);
            *height += item->box[info->position].height +
                       (info->position > 0 ? icon_view->priv->spacing : 0);
        }
    }
}

static void exo_icon_view_scroll_to_item(ExoIconView* icon_view, ExoIconViewItem* item)
{
    int x, y;
    int focus_width;
    int item_width, item_height;
    GtkAllocation allocation;
    GtkTreePath* path;

    /* Delay scrolling if either not realized or pending layout() */
    if (!gtk_widget_get_realized(GTK_WIDGET(icon_view)) || icon_view->priv->layout_idle_id != 0)
    {
        /* release the previous scroll_to_path reference */
        if (G_UNLIKELY(icon_view->priv->scroll_to_path != NULL))
            gtk_tree_row_reference_free(icon_view->priv->scroll_to_path);

        /* remember a reference for the new path and settings */

        path = gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);
        icon_view->priv->scroll_to_path =
            gtk_tree_row_reference_new_proxy(G_OBJECT(icon_view), icon_view->priv->model, path);
        gtk_tree_path_free(path);

        icon_view->priv->scroll_to_use_align = FALSE;

        return;
    }

    gtk_widget_style_get(GTK_WIDGET(icon_view), "focus-line-width", &focus_width, NULL);

    gdk_window_get_position(icon_view->priv->bin_window, &x, &y);
    exo_icon_view_get_item_needed_size(icon_view, item, &item_width, &item_height);
    gtk_widget_get_allocation(GTK_WIDGET(icon_view), &allocation);

    /*
     * If an item reaches beyond the edges of the view, we scroll just enough
     * to make as much of it visible as possible.  This avoids interfering
     * with double-click (since the second click will not scroll differently),
     * prevents narrow items in wide columns from being scrolled out of view
     * when selected, and ensures that items will be brought into view when
     * selected even if it was done by a keystroke instead of a mouse click.
     * See XFCE bugs 1683 and 6014 for some problems seen in the past.
     */

    if (y + item->area.y - focus_width < 0)
    {
        gtk_adjustment_set_value(icon_view->priv->vadjustment,
                                 gtk_adjustment_get_value(icon_view->priv->vadjustment) + y +
                                     item->area.y - focus_width);
    }
    else if (y + item->area.y + item_height + focus_width > allocation.height &&
             y + item->area.y - focus_width > 0)
    {
        gtk_adjustment_set_value(
            icon_view->priv->vadjustment,
            gtk_adjustment_get_value(icon_view->priv->vadjustment) +
                MIN(y + item->area.y - focus_width,
                    y + item->area.y + item_height + focus_width - allocation.height));
    }

    if (x + item->area.x - focus_width < 0)
    {
        gtk_adjustment_set_value(icon_view->priv->hadjustment,
                                 gtk_adjustment_get_value(icon_view->priv->hadjustment) + x +
                                     item->area.x - focus_width);
    }
    else if (x + item->area.x + item_width + focus_width > allocation.width &&
             x + item->area.x - focus_width > 0)
    {
        gtk_adjustment_set_value(
            icon_view->priv->hadjustment,
            gtk_adjustment_get_value(icon_view->priv->hadjustment) +
                MIN(x + item->area.x - focus_width,
                    x + item->area.x + item_width + focus_width - allocation.width));
    }

    gtk_adjustment_changed(icon_view->priv->hadjustment);
    gtk_adjustment_changed(icon_view->priv->vadjustment);
}

static ExoIconViewCellInfo* exo_icon_view_get_cell_info(ExoIconView* icon_view,
                                                        GtkCellRenderer* renderer)
{
    GList* lp;

    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
        if (EXO_ICON_VIEW_CELL_INFO(lp->data)->cell == renderer)
            return lp->data;

    return NULL;
}

static void exo_icon_view_set_cell_data(const ExoIconView* icon_view, ExoIconViewItem* item)
{
    ExoIconViewCellInfo* info;
    GtkTreePath* path;
    GtkTreeIter iter;
    GValue value = {
        0,
    };
    GSList* slp;
    GList* lp;

    if (G_UNLIKELY(!EXO_ICON_VIEW_FLAG_SET(icon_view, EXO_ICON_VIEW_ITERS_PERSIST)))
    {
        path = gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);
        gtk_tree_model_get_iter(icon_view->priv->model, &iter, path);
        gtk_tree_path_free(path);
    }
    else
    {
        iter = item->iter;
    }

    for (lp = icon_view->priv->cell_list; lp != NULL; lp = lp->next)
    {
        info = EXO_ICON_VIEW_CELL_INFO(lp->data);

        for (slp = info->attributes; slp != NULL && slp->next != NULL; slp = slp->next->next)
        {
            gtk_tree_model_get_value(icon_view->priv->model,
                                     &iter,
                                     GPOINTER_TO_INT(slp->next->data),
                                     &value);
            g_object_set_property(G_OBJECT(info->cell), slp->data, &value);
            g_value_unset(&value);
        }

        if (G_UNLIKELY(info->func != NULL))
            (*info->func)(GTK_CELL_LAYOUT(icon_view),
                          info->cell,
                          icon_view->priv->model,
                          &iter,
                          info->func_data);
    }
}

static void free_cell_attributes(ExoIconViewCellInfo* info)
{
    GSList* lp;

    for (lp = info->attributes; lp != NULL && lp->next != NULL; lp = lp->next->next)
        g_free(lp->data);
    g_slist_free(info->attributes);
    info->attributes = NULL;
}

static void free_cell_info(ExoIconViewCellInfo* info)
{
    if (G_UNLIKELY(info->destroy != NULL))
        (*info->destroy)(info->func_data);

    free_cell_attributes(info);
    g_object_unref(G_OBJECT(info->cell));
    g_slice_free(ExoIconViewCellInfo, info);
}

static void exo_icon_view_cell_layout_pack_start(GtkCellLayout* layout, GtkCellRenderer* renderer,
                                                 bool expand)
{
    ExoIconViewCellInfo* info;
    ExoIconView* icon_view = EXO_ICON_VIEW(layout);

    _exo_return_if_fail(GTK_IS_CELL_RENDERER(renderer));
    _exo_return_if_fail(exo_icon_view_get_cell_info(icon_view, renderer) == NULL);

    g_object_ref_sink(renderer);

    info = g_slice_new0(ExoIconViewCellInfo);
    info->cell = renderer;
    info->expand = expand ? TRUE : FALSE;
    info->pack = GTK_PACK_START;
    info->position = icon_view->priv->n_cells;
    info->is_text = GTK_IS_CELL_RENDERER_TEXT(renderer);

    icon_view->priv->cell_list = g_list_append(icon_view->priv->cell_list, info);
    icon_view->priv->n_cells++;

    exo_icon_view_invalidate_sizes(icon_view);
}

static void exo_icon_view_cell_layout_pack_end(GtkCellLayout* layout, GtkCellRenderer* renderer,
                                               bool expand)
{
    ExoIconViewCellInfo* info;
    ExoIconView* icon_view = EXO_ICON_VIEW(layout);

    _exo_return_if_fail(GTK_IS_CELL_RENDERER(renderer));
    _exo_return_if_fail(exo_icon_view_get_cell_info(icon_view, renderer) == NULL);

    g_object_ref_sink(renderer);

    info = g_slice_new0(ExoIconViewCellInfo);
    info->cell = renderer;
    info->expand = expand ? TRUE : FALSE;
    info->pack = GTK_PACK_END;
    info->position = icon_view->priv->n_cells;
    info->is_text = GTK_IS_CELL_RENDERER_TEXT(renderer);

    icon_view->priv->cell_list = g_list_append(icon_view->priv->cell_list, info);
    icon_view->priv->n_cells++;

    exo_icon_view_invalidate_sizes(icon_view);
}

static void exo_icon_view_cell_layout_add_attribute(GtkCellLayout* layout,
                                                    GtkCellRenderer* renderer,
                                                    const char* attribute, int column)
{
    ExoIconViewCellInfo* info;

    info = exo_icon_view_get_cell_info(EXO_ICON_VIEW(layout), renderer);
    if (G_LIKELY(info != NULL))
    {
        info->attributes = g_slist_prepend(info->attributes, GINT_TO_POINTER(column));
        info->attributes = g_slist_prepend(info->attributes, g_strdup(attribute));

        exo_icon_view_invalidate_sizes(EXO_ICON_VIEW(layout));
    }
}

static void exo_icon_view_cell_layout_clear(GtkCellLayout* layout)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(layout);

    g_list_foreach(icon_view->priv->cell_list, (GFunc)(void (*)(void))free_cell_info, NULL);
    g_list_free(icon_view->priv->cell_list);
    icon_view->priv->cell_list = NULL;
    icon_view->priv->n_cells = 0;

    exo_icon_view_invalidate_sizes(icon_view);
}

static void exo_icon_view_cell_layout_set_cell_data_func(GtkCellLayout* layout,
                                                         GtkCellRenderer* cell,
                                                         GtkCellLayoutDataFunc func,
                                                         void* func_data, GDestroyNotify destroy)
{
    ExoIconViewCellInfo* info;
    GDestroyNotify notify;

    info = exo_icon_view_get_cell_info(EXO_ICON_VIEW(layout), cell);
    if (G_LIKELY(info != NULL))
    {
        if (G_UNLIKELY(info->destroy != NULL))
        {
            notify = info->destroy;
            info->destroy = NULL;
            (*notify)(info->func_data);
        }

        info->func = func;
        info->func_data = func_data;
        info->destroy = destroy;

        exo_icon_view_invalidate_sizes(EXO_ICON_VIEW(layout));
    }
}

static void exo_icon_view_cell_layout_clear_attributes(GtkCellLayout* layout,
                                                       GtkCellRenderer* renderer)
{
    ExoIconViewCellInfo* info;

    info = exo_icon_view_get_cell_info(EXO_ICON_VIEW(layout), renderer);
    if (G_LIKELY(info != NULL))
    {
        free_cell_attributes(info);

        exo_icon_view_invalidate_sizes(EXO_ICON_VIEW(layout));
    }
}

static void exo_icon_view_cell_layout_reorder(GtkCellLayout* layout, GtkCellRenderer* cell,
                                              int position)
{
    ExoIconViewCellInfo* info;
    ExoIconView* icon_view = EXO_ICON_VIEW(layout);
    GList* lp;
    int n;

    info = exo_icon_view_get_cell_info(icon_view, cell);
    if (G_LIKELY(info != NULL))
    {
        lp = g_list_find(icon_view->priv->cell_list, info);

        icon_view->priv->cell_list = g_list_remove_link(icon_view->priv->cell_list, lp);
        icon_view->priv->cell_list = g_list_insert(icon_view->priv->cell_list, info, position);

        for (lp = icon_view->priv->cell_list, n = 0; lp != NULL; lp = lp->next, ++n)
            EXO_ICON_VIEW_CELL_INFO(lp->data)->position = n;

        exo_icon_view_invalidate_sizes(icon_view);
    }
}

/**
 * exo_icon_view_new:
 *
 * Creates a new #ExoIconView widget
 *
 * Return value: A newly created #ExoIconView widget
 **/
GtkWidget* exo_icon_view_new(void)
{
    return g_object_new(EXO_TYPE_ICON_VIEW, NULL);
}

/**
 * exo_icon_view_widget_to_icon_coords:
 * @icon_view : a #ExoIconView.
 * @wx        : widget x coordinate.
 * @wy        : widget y coordinate.
 * @ix        : return location for icon x coordinate or %NULL.
 * @iy        : return location for icon y coordinate or %NULL.
 *
 * Converts widget coordinates to coordinates for the icon window
 * (the full scrollable area of the icon view).
 **/
void exo_icon_view_widget_to_icon_coords(const ExoIconView* icon_view, int wx, int wy, int* ix,
                                         int* iy)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(ix != NULL))
        *ix = wx + gtk_adjustment_get_value(icon_view->priv->hadjustment);
    if (G_LIKELY(iy != NULL))
        *iy = wy + gtk_adjustment_get_value(icon_view->priv->vadjustment);
}

/**
 * exo_icon_view_get_path_at_pos:
 * @icon_view : A #ExoIconView.
 * @x         : The x position to be identified
 * @y         : The y position to be identified
 *
 * Finds the path at the point (@x, @y), relative to widget coordinates.
 * See exo_icon_view_get_item_at_pos(), if you are also interested in
 * the cell at the specified position.
 *
 * Return value: The #GtkTreePath corresponding to the icon or %NULL
 *               if no icon exists at that position.
 **/
GtkTreePath* exo_icon_view_get_path_at_pos(const ExoIconView* icon_view, int x, int y)
{
    ExoIconViewItem* item;

    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), NULL);

    /* translate the widget coordinates to icon window coordinates */
    /*
  NOTE by Hong Jen Yee: This should be disabled for pcmanfm to work. So weird.
  x += icon_view->priv->hadjustment->value;
  y += icon_view->priv->vadjustment->value;
  */
    item = exo_icon_view_get_item_at_coords(icon_view, x, y, TRUE, NULL);

    return (item != NULL)
               ? gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1)
               : NULL;
}

/**
 * exo_icon_view_set_selection_mode:
 * @icon_view : A #ExoIconView.
 * @mode      : The selection mode
 *
 * Sets the selection mode of the @icon_view.
 **/
void exo_icon_view_set_selection_mode(ExoIconView* icon_view, GtkSelectionMode mode)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(mode != icon_view->priv->selection_mode))
    {
        if (mode == GTK_SELECTION_NONE || icon_view->priv->selection_mode == GTK_SELECTION_MULTIPLE)
            exo_icon_view_unselect_all(icon_view);

        icon_view->priv->selection_mode = mode;

        g_object_notify(G_OBJECT(icon_view), "selection-mode");
    }
}

/**
 * exo_icon_view_set_layout_mode:
 * @icon_view   : a #ExoIconView.
 * @layout_mode : the new #ExoIconViewLayoutMode for @icon_view.
 *
 * Sets the layout mode of @icon_view to @layout_mode.
 *
 * Since: 0.3.1.5
 **/
void exo_icon_view_set_layout_mode(ExoIconView* icon_view, ExoIconViewLayoutMode layout_mode)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    /* check if we have a new setting */
    if (G_LIKELY(icon_view->priv->layout_mode != layout_mode))
    {
        /* apply the new setting */
        icon_view->priv->layout_mode = layout_mode;

        /* cancel any active cell editor */
        exo_icon_view_stop_editing(icon_view, TRUE);

        /* invalidate the current item sizes */
        exo_icon_view_invalidate_sizes(icon_view);
        exo_icon_view_queue_layout(icon_view);

        /* notify listeners */
        g_object_notify(G_OBJECT(icon_view), "layout-mode");
    }
}

/**
 * exo_icon_view_get_model:
 * @icon_view : a #ExoIconView
 *
 * Returns the model the #ExoIconView is based on. Returns %NULL if the
 * model is unset.
 *
 * Return value: A #GtkTreeModel, or %NULL if none is currently being used.
 **/
GtkTreeModel* exo_icon_view_get_model(const ExoIconView* icon_view)
{
    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), NULL);
    return icon_view->priv->model;
}

/**
 * exo_icon_view_set_model:
 * @icon_view : A #ExoIconView.
 * @model     : The model.
 *
 * Sets the model for a #ExoIconView.
 * If the @icon_view already has a model set, it will remove
 * it before setting the new model.  If @model is %NULL, then
 * it will unset the old model.
 **/
void exo_icon_view_set_model(ExoIconView* icon_view, GtkTreeModel* model)
{
    ExoIconViewItem* item;
    GtkTreeIter iter;
    GList* items = NULL;
    GList* lp;
    int n;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(model == NULL || GTK_IS_TREE_MODEL(model));

    /* verify that we don't already use that model */
    if (G_UNLIKELY(icon_view->priv->model == model))
        return;

    /* verify the new model */
    if (G_LIKELY(model != NULL))
    {
        g_return_if_fail(gtk_tree_model_get_flags(model) & GTK_TREE_MODEL_LIST_ONLY);

        if (G_UNLIKELY(icon_view->priv->pixbuf_column != -1))
            g_return_if_fail(
                gtk_tree_model_get_column_type(model, icon_view->priv->pixbuf_column) ==
                GDK_TYPE_PIXBUF);

        if (G_UNLIKELY(icon_view->priv->text_column != -1))
            g_return_if_fail(gtk_tree_model_get_column_type(model, icon_view->priv->text_column) ==
                             G_TYPE_STRING);

        if (G_UNLIKELY(icon_view->priv->markup_column != -1))
            g_return_if_fail(
                gtk_tree_model_get_column_type(model, icon_view->priv->markup_column) ==
                G_TYPE_STRING);
    }

    /* be sure to cancel any pending editor */
    exo_icon_view_stop_editing(icon_view, TRUE);

    /* disconnect from the previous model */
    if (G_LIKELY(icon_view->priv->model != NULL))
    {
        /* disconnect signals handlers from the previous model */
        g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->model),
                                             exo_icon_view_row_changed,
                                             icon_view);
        g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->model),
                                             exo_icon_view_row_inserted,
                                             icon_view);
        g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->model),
                                             exo_icon_view_row_deleted,
                                             icon_view);
        g_signal_handlers_disconnect_by_func(G_OBJECT(icon_view->priv->model),
                                             exo_icon_view_rows_reordered,
                                             icon_view);

        /* release our reference on the model */
        g_object_unref(G_OBJECT(icon_view->priv->model));

        /* drop all items belonging to the previous model */
        for (lp = icon_view->priv->items; lp != NULL; lp = lp->next)
        {
            g_free(EXO_ICON_VIEW_ITEM(lp->data)->box);
            g_slice_free(ExoIconViewItem, lp->data);
        }
        g_list_free(icon_view->priv->items);
        icon_view->priv->items = NULL;

        /* reset statistics */
        icon_view->priv->search_column = -1;
        icon_view->priv->anchor_item = NULL;
        icon_view->priv->cursor_item = NULL;
        icon_view->priv->prelit_item = NULL;
        icon_view->priv->last_single_clicked = NULL;
        icon_view->priv->width = 0;
        icon_view->priv->height = 0;

        /* cancel any pending single click timer */
        if (G_UNLIKELY(icon_view->priv->single_click_timeout_id != 0))
            g_source_remove(icon_view->priv->single_click_timeout_id);

        /* reset cursor when in single click mode and realized */
        if (G_UNLIKELY(icon_view->priv->single_click &&
                       gtk_widget_get_realized(GTK_WIDGET(icon_view))))
            gdk_window_set_cursor(icon_view->priv->bin_window, NULL);
    }

    /* be sure to drop any previous scroll_to_path reference,
     * as it points to the old (no longer valid) model.
     */
    if (G_UNLIKELY(icon_view->priv->scroll_to_path != NULL))
    {
        gtk_tree_row_reference_free(icon_view->priv->scroll_to_path);
        icon_view->priv->scroll_to_path = NULL;
    }

    /* activate the new model */
    icon_view->priv->model = model;

    /* connect to the new model */
    if (G_LIKELY(model != NULL))
    {
        /* take a reference on the model */
        g_object_ref(G_OBJECT(model));

        /* connect signals */
        g_signal_connect(G_OBJECT(model),
                         "row-changed",
                         G_CALLBACK(exo_icon_view_row_changed),
                         icon_view);
        g_signal_connect(G_OBJECT(model),
                         "row-inserted",
                         G_CALLBACK(exo_icon_view_row_inserted),
                         icon_view);
        g_signal_connect(G_OBJECT(model),
                         "row-deleted",
                         G_CALLBACK(exo_icon_view_row_deleted),
                         icon_view);
        g_signal_connect(G_OBJECT(model),
                         "rows-reordered",
                         G_CALLBACK(exo_icon_view_rows_reordered),
                         icon_view);

        /* check if the new model supports persistent iterators */
        if (gtk_tree_model_get_flags(model) & GTK_TREE_MODEL_ITERS_PERSIST)
            EXO_ICON_VIEW_SET_FLAG(icon_view, EXO_ICON_VIEW_ITERS_PERSIST);
        else
            EXO_ICON_VIEW_UNSET_FLAG(icon_view, EXO_ICON_VIEW_ITERS_PERSIST);

        /* determine an appropriate search column */
        if (icon_view->priv->search_column <= 0)
        {
            /* we simply use the first string column */
            for (n = 0; n < gtk_tree_model_get_n_columns(model); ++n)
                if (g_value_type_transformable(gtk_tree_model_get_column_type(model, n),
                                               G_TYPE_STRING))
                {
                    icon_view->priv->search_column = n;
                    break;
                }
        }

        /* build up the initial items list */
        if (gtk_tree_model_get_iter_first(model, &iter))
        {
            do
            {
                item = g_slice_new0(ExoIconViewItem);
                item->iter = iter;
                item->area.width = -1;
                items = g_list_prepend(items, item);
            } while (gtk_tree_model_iter_next(model, &iter));
        }
        icon_view->priv->items = g_list_reverse(items);

        /* layout the new items */
        exo_icon_view_queue_layout(icon_view);
    }

    /* hide the interactive search dialog (if any) */
    if (G_LIKELY(icon_view->priv->search_window != NULL))
        exo_icon_view_search_dialog_hide(icon_view->priv->search_window, icon_view);

    /* notify listeners */
    g_object_notify(G_OBJECT(icon_view), "model");

    if (gtk_widget_get_realized(GTK_WIDGET(icon_view)))
        gtk_widget_queue_resize(GTK_WIDGET(icon_view));
}

static void update_text_cell(ExoIconView* icon_view)
{
    ExoIconViewCellInfo* info;
    GList* l;
    int i;

    if (icon_view->priv->text_column == -1 && icon_view->priv->markup_column == -1)
    {
        if (icon_view->priv->text_cell != -1)
        {
            info = g_list_nth_data(icon_view->priv->cell_list, icon_view->priv->text_cell);

            icon_view->priv->cell_list = g_list_remove(icon_view->priv->cell_list, info);

            free_cell_info(info);

            icon_view->priv->n_cells--;
            icon_view->priv->text_cell = -1;
        }
    }
    else
    {
        if (icon_view->priv->text_cell == -1)
        {
            GtkCellRenderer* cell = gtk_cell_renderer_text_new();
            gtk_cell_layout_pack_end(GTK_CELL_LAYOUT(icon_view), cell, FALSE);
            for (l = icon_view->priv->cell_list, i = 0; l; l = l->next, i++)
            {
                info = l->data;
                if (info->cell == cell)
                {
                    icon_view->priv->text_cell = i;
                    break;
                }
            }
        }

        info = g_list_nth_data(icon_view->priv->cell_list, icon_view->priv->text_cell);

        if (icon_view->priv->markup_column != -1)
            gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(icon_view),
                                           info->cell,
                                           "markup",
                                           icon_view->priv->markup_column,
                                           NULL);
        else
            gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(icon_view),
                                           info->cell,
                                           "text",
                                           icon_view->priv->text_column,
                                           NULL);
    }
}

static void update_pixbuf_cell(ExoIconView* icon_view)
{
    ExoIconViewCellInfo* info;
    GList* l;
    int i;

    if (icon_view->priv->pixbuf_column == -1)
    {
        if (icon_view->priv->pixbuf_cell != -1)
        {
            info = g_list_nth_data(icon_view->priv->cell_list, icon_view->priv->pixbuf_cell);

            icon_view->priv->cell_list = g_list_remove(icon_view->priv->cell_list, info);

            free_cell_info(info);

            icon_view->priv->n_cells--;
            icon_view->priv->pixbuf_cell = -1;
        }
    }
    else
    {
        if (icon_view->priv->pixbuf_cell == -1)
        {
            GtkCellRenderer* cell = gtk_cell_renderer_pixbuf_new();

            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(icon_view), cell, FALSE);
            for (l = icon_view->priv->cell_list, i = 0; l; l = l->next, i++)
            {
                info = l->data;
                if (info->cell == cell)
                {
                    icon_view->priv->pixbuf_cell = i;
                    break;
                }
            }
        }

        info = g_list_nth_data(icon_view->priv->cell_list, icon_view->priv->pixbuf_cell);

        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(icon_view),
                                       info->cell,
                                       "pixbuf",
                                       icon_view->priv->pixbuf_column,
                                       NULL);
    }
}

/**
 * exo_icon_view_set_text_column:
 * @icon_view: A #ExoIconView.
 * @column: A column in the currently used model.
 *
 * Sets the column with text for @icon_view to be @column. The text
 * column must be of type #G_TYPE_STRING.
 *
 * Deprecated: Use the more powerful #GtkCellRenderer<!---->s instead, as #ExoIconView
 *             now implements #GtkCellLayout.
 **/
void exo_icon_view_set_text_column(ExoIconView* icon_view, int column)
{
    GType column_type;

    if (G_UNLIKELY(column == icon_view->priv->text_column))
        return;

    if (column == -1)
    {
        icon_view->priv->text_column = -1;
    }
    else
    {
        if (icon_view->priv->model != NULL)
        {
            column_type = gtk_tree_model_get_column_type(icon_view->priv->model, column);
            g_return_if_fail(column_type == G_TYPE_STRING);
        }

        icon_view->priv->text_column = column;
    }

    exo_icon_view_stop_editing(icon_view, TRUE);

    update_text_cell(icon_view);

    exo_icon_view_invalidate_sizes(icon_view);

    g_object_notify(G_OBJECT(icon_view), "text-column");
}

/**
 * exo_icon_view_set_markup_column:
 * @icon_view : A #ExoIconView.
 * @column    : A column in the currently used model.
 *
 * Sets the column with markup information for @icon_view to be
 * @column. The markup column must be of type #G_TYPE_STRING.
 * If the markup column is set to something, it overrides
 * the text column set by exo_icon_view_set_text_column().
 *
 * Deprecated: Use the more powerful #GtkCellRenderer<!---->s instead, as #ExoIconView
 *             now implements #GtkCellLayout.
 **/
void exo_icon_view_set_markup_column(ExoIconView* icon_view, int column)
{
    if (G_UNLIKELY(column == icon_view->priv->markup_column))
        return;

    if (column == -1)
        icon_view->priv->markup_column = -1;
    else
    {
        if (icon_view->priv->model != NULL)
        {
            GType column_type;

            column_type = gtk_tree_model_get_column_type(icon_view->priv->model, column);

            g_return_if_fail(column_type == G_TYPE_STRING);
        }

        icon_view->priv->markup_column = column;
    }

    exo_icon_view_stop_editing(icon_view, TRUE);

    update_text_cell(icon_view);

    exo_icon_view_invalidate_sizes(icon_view);

    g_object_notify(G_OBJECT(icon_view), "markup-column");
}

/**
 * exo_icon_view_set_pixbuf_column:
 * @icon_view : A #ExoIconView.
 * @column    : A column in the currently used model.
 *
 * Sets the column with pixbufs for @icon_view to be @column. The pixbuf
 * column must be of type #GDK_TYPE_PIXBUF
 *
 * Deprecated: Use the more powerful #GtkCellRenderer<!---->s instead, as #ExoIconView
 *             now implements #GtkCellLayout.
 **/
void exo_icon_view_set_pixbuf_column(ExoIconView* icon_view, int column)
{
    GType column_type;

    if (G_UNLIKELY(column == icon_view->priv->pixbuf_column))
        return;

    if (column == -1)
    {
        icon_view->priv->pixbuf_column = -1;
    }
    else
    {
        if (icon_view->priv->model != NULL)
        {
            column_type = gtk_tree_model_get_column_type(icon_view->priv->model, column);
            g_return_if_fail(column_type == GDK_TYPE_PIXBUF);
        }

        icon_view->priv->pixbuf_column = column;
    }

    exo_icon_view_stop_editing(icon_view, TRUE);

    update_pixbuf_cell(icon_view);

    exo_icon_view_invalidate_sizes(icon_view);

    g_object_notify(G_OBJECT(icon_view), "pixbuf-column");
}

/**
 * exo_icon_view_select_path:
 * @icon_view : A #ExoIconView.
 * @path      : The #GtkTreePath to be selected.
 *
 * Selects the row at @path.
 **/
void exo_icon_view_select_path(ExoIconView* icon_view, GtkTreePath* path)
{
    ExoIconViewItem* item;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(icon_view->priv->model != NULL);
    g_return_if_fail(gtk_tree_path_get_depth(path) > 0);

    item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);
    if (G_LIKELY(item != NULL))
        exo_icon_view_select_item(icon_view, item);
}

/**
 * exo_icon_view_unselect_path:
 * @icon_view : A #ExoIconView.
 * @path      : The #GtkTreePath to be unselected.
 *
 * Unselects the row at @path.
 **/
void exo_icon_view_unselect_path(ExoIconView* icon_view, GtkTreePath* path)
{
    ExoIconViewItem* item;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(icon_view->priv->model != NULL);
    g_return_if_fail(gtk_tree_path_get_depth(path) > 0);

    item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);
    if (G_LIKELY(item != NULL))
        exo_icon_view_unselect_item(icon_view, item);
}

/**
 * exo_icon_view_get_selected_items:
 * @icon_view: A #ExoIconView.
 *
 * Creates a list of paths of all selected items. Additionally, if you are
 * planning on modifying the model after calling this function, you may
 * want to convert the returned list into a list of #GtkTreeRowReference<!-- -->s.
 * To do this, you can use gtk_tree_row_reference_new().
 *
 * To free the return value, use:
 * <informalexample><programlisting>
 * g_list_foreach (list, gtk_tree_path_free, NULL);
 * g_list_free (list);
 * </programlisting></informalexample>
 *
 * Return value: A #GList containing a #GtkTreePath for each selected row.
 **/
GList* exo_icon_view_get_selected_items(const ExoIconView* icon_view)
{
    GList* selected = NULL;
    GList* lp;
    int i;

    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), NULL);

    for (i = 0, lp = icon_view->priv->items; lp != NULL; ++i, lp = lp->next)
    {
        if (EXO_ICON_VIEW_ITEM(lp->data)->selected)
            selected = g_list_prepend(selected, gtk_tree_path_new_from_indices(i, -1));
    }

    return selected;
}

/**
 * exo_icon_view_select_all:
 * @icon_view : A #ExoIconView.
 *
 * Selects all the icons. @icon_view must has its selection mode set
 * to #GTK_SELECTION_MULTIPLE.
 **/
void exo_icon_view_select_all(ExoIconView* icon_view)
{
    GList* items;
    bool dirty = FALSE;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (icon_view->priv->selection_mode != GTK_SELECTION_MULTIPLE)
        return;

    for (items = icon_view->priv->items; items; items = items->next)
    {
        ExoIconViewItem* item = items->data;

        if (!item->selected)
        {
            dirty = TRUE;
            item->selected = TRUE;
            exo_icon_view_queue_draw_item(icon_view, item);
        }
    }

    if (dirty)
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

/**
 * exo_icon_view_unselect_all:
 * @icon_view : A #ExoIconView.
 *
 * Unselects all the icons.
 **/
void exo_icon_view_unselect_all(ExoIconView* icon_view)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_UNLIKELY(icon_view->priv->selection_mode == GTK_SELECTION_BROWSE))
        return;

    if (exo_icon_view_unselect_all_internal(icon_view))
        g_signal_emit(icon_view, icon_view_signals[SELECTION_CHANGED], 0);
}

/**
 * exo_icon_view_path_is_selected:
 * @icon_view: A #ExoIconView.
 * @path: A #GtkTreePath to check selection on.
 *
 * Returns %TRUE if the icon pointed to by @path is currently
 * selected. If @icon does not point to a valid location, %FALSE is returned.
 *
 * Return value: %TRUE if @path is selected.
 **/
bool exo_icon_view_path_is_selected(const ExoIconView* icon_view, GtkTreePath* path)
{
    ExoIconViewItem* item;

    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);
    g_return_val_if_fail(icon_view->priv->model != NULL, FALSE);
    g_return_val_if_fail(gtk_tree_path_get_depth(path) > 0, FALSE);

    item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);

    return (item != NULL && item->selected);
}

/**
 * exo_icon_view_item_activated:
 * @icon_view : a #ExoIconView
 * @path      : the #GtkTreePath to be activated
 *
 * Activates the item determined by @path.
 **/
void exo_icon_view_item_activated(ExoIconView* icon_view, GtkTreePath* path)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(gtk_tree_path_get_depth(path) > 0);

    g_signal_emit(icon_view, icon_view_signals[ITEM_ACTIVATED], 0, path);
}

/**
 * exo_icon_view_get_cursor:
 * @icon_view : A #ExoIconView
 * @path      : Return location for the current cursor path, or %NULL
 * @cell      : Return location the current focus cell, or %NULL
 *
 * Fills in @path and @cell with the current cursor path and cell.
 * If the cursor isn't currently set, then *@path will be %NULL.
 * If no cell currently has focus, then *@cell will be %NULL.
 *
 * The returned #GtkTreePath must be freed with gtk_tree_path_free().
 *
 * Return value: %TRUE if the cursor is set.
 *
 * Since: 0.3.1
 **/
bool exo_icon_view_get_cursor(const ExoIconView* icon_view, GtkTreePath** path,
                              GtkCellRenderer** cell)
{
    ExoIconViewCellInfo* info;
    ExoIconViewItem* item;

    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);

    item = icon_view->priv->cursor_item;
    info = (icon_view->priv->cursor_cell < 0)
               ? NULL
               : g_list_nth_data(icon_view->priv->cell_list, icon_view->priv->cursor_cell);

    if (G_LIKELY(path != NULL))
        *path = (item != NULL)
                    ? gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1)
                    : NULL;

    if (G_LIKELY(cell != NULL))
        *cell = (info != NULL) ? info->cell : NULL;

    return (item != NULL);
}

/**
 * exo_icon_view_set_cursor:
 * @icon_view     : a #ExoIconView
 * @path          : a #GtkTreePath
 * @cell          : a #GtkCellRenderer or %NULL
 * @start_editing : %TRUE if the specified cell should start being edited.
 *
 * Sets the current keyboard focus to be at @path, and selects it.  This is
 * useful when you want to focus the user's attention on a particular item.
 * If @cell is not %NULL, then focus is given to the cell specified by
 * it. Additionally, if @start_editing is %TRUE, then editing should be
 * started in the specified cell.
 *
 * This function is often followed by <literal>gtk_widget_grab_focus
 * (icon_view)</literal> in order to give keyboard focus to the widget.
 * Please note that editing can only happen when the widget is realized.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_set_cursor(ExoIconView* icon_view, GtkTreePath* path, GtkCellRenderer* cell,
                              bool start_editing)
{
    ExoIconViewItem* item;
    ExoIconViewCellInfo* info = NULL;
    GList* l;
    int i, cell_pos;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(path != NULL);
    g_return_if_fail(cell == NULL || GTK_IS_CELL_RENDERER(cell));

    exo_icon_view_stop_editing(icon_view, TRUE);

    item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);
    if (G_UNLIKELY(item == NULL))
        return;

    cell_pos = -1;
    for (l = icon_view->priv->cell_list, i = 0; l; l = l->next, i++)
    {
        info = l->data;

        if (info->cell == cell)
        {
            cell_pos = i;
            break;
        }

        info = NULL;
    }

    /* place the cursor on the item */
    exo_icon_view_set_cursor_item(icon_view, item, cell_pos);
    icon_view->priv->anchor_item = item;

    /* scroll to the item (maybe delayed) */
    exo_icon_view_scroll_to_path(icon_view, path, FALSE, 0.0f, 0.0f);

    if (!info)
        return;

    if (start_editing)
        exo_icon_view_start_editing(icon_view, item, info, NULL);
}

/**
 * exo_icon_view_scroll_to_path:
 * @icon_view: A #ExoIconView.
 * @path: The path of the item to move to.
 * @use_align: whether to use alignment arguments, or %FALSE.
 * @row_align: The vertical alignment of the item specified by @path.
 * @col_align: The horizontal alignment of the item specified by @column.
 *
 * Moves the alignments of @icon_view to the position specified by @path.
 * @row_align determines where the row is placed, and @col_align determines where
 * @column is placed.  Both are expected to be between 0.0 and 1.0.
 * 0.0 means left/top alignment, 1.0 means right/bottom alignment, 0.5 means center.
 *
 * If @use_align is %FALSE, then the alignment arguments are ignored, and the
 * tree does the minimum amount of work to scroll the item onto the screen.
 * This means that the item will be scrolled to the edge closest to its current
 * position.  If the item is currently visible on the screen, nothing is done.
 *
 * This function only works if the model is set, and @path is a valid row on the
 * model.  If the model changes before the @tree_view is realized, the centered
 * path will be modified to reflect this change.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_scroll_to_path(ExoIconView* icon_view, GtkTreePath* path, bool use_align,
                                  float row_align, float col_align)
{
    ExoIconViewItem* item;
    GtkAllocation allocation;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(gtk_tree_path_get_depth(path) > 0);
    g_return_if_fail(row_align >= 0.0 && row_align <= 1.0);
    g_return_if_fail(col_align >= 0.0 && col_align <= 1.0);

    gtk_widget_get_allocation(GTK_WIDGET(icon_view), &allocation);

    /* Delay scrolling if either not realized or pending layout() */
    if (!gtk_widget_get_realized(GTK_WIDGET(icon_view)) || icon_view->priv->layout_idle_id != 0)
    {
        /* release the previous scroll_to_path reference */
        if (G_UNLIKELY(icon_view->priv->scroll_to_path != NULL))
            gtk_tree_row_reference_free(icon_view->priv->scroll_to_path);

        /* remember a reference for the new path and settings */
        icon_view->priv->scroll_to_path =
            gtk_tree_row_reference_new_proxy(G_OBJECT(icon_view), icon_view->priv->model, path);
        icon_view->priv->scroll_to_use_align = use_align;
        icon_view->priv->scroll_to_row_align = row_align;
        icon_view->priv->scroll_to_col_align = col_align;
    }
    else
    {
        item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);
        if (G_UNLIKELY(item == NULL))
            return;

        if (use_align)
        {
            int x, y;
            int focus_width;
            float offset, value;

            gtk_widget_style_get(GTK_WIDGET(icon_view), "focus-line-width", &focus_width, NULL);

            gdk_window_get_position(icon_view->priv->bin_window, &x, &y);

            offset = y + item->area.y - focus_width -
                     row_align * (allocation.height - item->area.height);
            value = CLAMP(gtk_adjustment_get_value(icon_view->priv->vadjustment) + offset,
                          gtk_adjustment_get_lower(icon_view->priv->vadjustment),
                          gtk_adjustment_get_upper(icon_view->priv->vadjustment) -
                              gtk_adjustment_get_page_size(icon_view->priv->vadjustment));
            gtk_adjustment_set_value(icon_view->priv->vadjustment, value);

            offset =
                x + item->area.x - focus_width - col_align * (allocation.width - item->area.width);
            value = CLAMP(gtk_adjustment_get_value(icon_view->priv->hadjustment) + offset,
                          gtk_adjustment_get_lower(icon_view->priv->hadjustment),
                          gtk_adjustment_get_upper(icon_view->priv->hadjustment) -
                              gtk_adjustment_get_page_size(icon_view->priv->hadjustment));
            gtk_adjustment_set_value(icon_view->priv->hadjustment, value);

            gtk_adjustment_changed(icon_view->priv->hadjustment);
            gtk_adjustment_changed(icon_view->priv->vadjustment);
        }
        else
        {
            exo_icon_view_scroll_to_item(icon_view, item);
        }
    }
}

/**
 * exo_icon_view_set_orientation:
 * @icon_view   : a #ExoIconView
 * @orientation : the relative position of texts and icons
 *
 * Sets the ::orientation property which determines whether the labels
 * are drawn beside the icons instead of below.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_set_orientation(ExoIconView* icon_view, GtkOrientation orientation)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(icon_view->priv->orientation != orientation))
    {
        icon_view->priv->orientation = orientation;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_invalidate_sizes(icon_view);

        update_text_cell(icon_view);
        update_pixbuf_cell(icon_view);

        g_object_notify(G_OBJECT(icon_view), "orientation");
    }
}

/**
 * exo_icon_view_set_columns:
 * @icon_view : a #ExoIconView
 * @columns   : the number of columns
 *
 * Sets the ::columns property which determines in how
 * many columns the icons are arranged. If @columns is
 * -1, the number of columns will be chosen automatically
 * to fill the available area.
 *
 * Since: 0.3.1
 */
void exo_icon_view_set_columns(ExoIconView* icon_view, int columns)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(icon_view->priv->columns != columns))
    {
        icon_view->priv->columns = columns;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_queue_layout(icon_view);

        g_object_notify(G_OBJECT(icon_view), "columns");
    }
}

/**
 * exo_icon_view_set_item_width:
 * @icon_view  : a #ExoIconView
 * @item_width : the width for each item
 *
 * Sets the ::item-width property which specifies the width
 * to use for each item. If it is set to -1, the icon view will
 * automatically determine a suitable item size.
 *
 * Since: 0.3.1
 */
void exo_icon_view_set_item_width(ExoIconView* icon_view, int item_width)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (icon_view->priv->item_width != item_width)
    {
        icon_view->priv->item_width = item_width;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_invalidate_sizes(icon_view);

        update_text_cell(icon_view);

        g_object_notify(G_OBJECT(icon_view), "item-width");
    }
}

/**
 * exo_icon_view_set_spacing:
 * @icon_view : a #ExoIconView
 * @spacing   : the spacing
 *
 * Sets the ::spacing property which specifies the space
 * which is inserted between the cells (i.e. the icon and
 * the text) of an item.
 *
 * Since: 0.3.1
 */
void exo_icon_view_set_spacing(ExoIconView* icon_view, int spacing)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(icon_view->priv->spacing != spacing))
    {
        icon_view->priv->spacing = spacing;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_invalidate_sizes(icon_view);

        g_object_notify(G_OBJECT(icon_view), "spacing");
    }
}

/**
 * exo_icon_view_set_row_spacing:
 * @icon_view   : a #ExoIconView
 * @row_spacing : the row spacing
 *
 * Sets the ::row-spacing property which specifies the space
 * which is inserted between the rows of the icon view.
 *
 * Since: 0.3.1
 */
void exo_icon_view_set_row_spacing(ExoIconView* icon_view, int row_spacing)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(icon_view->priv->row_spacing != row_spacing))
    {
        icon_view->priv->row_spacing = row_spacing;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_invalidate_sizes(icon_view);

        g_object_notify(G_OBJECT(icon_view), "row-spacing");
    }
}

/**
 * exo_icon_view_set_column_spacing:
 * @icon_view      : a #ExoIconView
 * @column_spacing : the column spacing
 *
 * Sets the ::column-spacing property which specifies the space
 * which is inserted between the columns of the icon view.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_set_column_spacing(ExoIconView* icon_view, int column_spacing)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(icon_view->priv->column_spacing != column_spacing))
    {
        icon_view->priv->column_spacing = column_spacing;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_invalidate_sizes(icon_view);

        g_object_notify(G_OBJECT(icon_view), "column-spacing");
    }
}

/**
 * exo_icon_view_set_margin:
 * @icon_view : a #ExoIconView
 * @margin    : the margin
 *
 * Sets the ::margin property which specifies the space
 * which is inserted at the top, bottom, left and right
 * of the icon view.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_set_margin(ExoIconView* icon_view, int margin)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (G_LIKELY(icon_view->priv->margin != margin))
    {
        icon_view->priv->margin = margin;

        exo_icon_view_stop_editing(icon_view, TRUE);
        exo_icon_view_invalidate_sizes(icon_view);

        g_object_notify(G_OBJECT(icon_view), "margin");
    }
}

/* Get/set whether drag_motion requested the drag data and
 * drag_data_received should thus not actually insert the data,
 * since the data doesn't result from a drop.
 */
static void set_status_pending(GdkDragContext* context, GdkDragAction suggested_action)
{
    g_object_set_data(G_OBJECT(context),
                      I_("exo-icon-view-status-pending"),
                      GINT_TO_POINTER(suggested_action));
}

static GdkDragAction get_status_pending(GdkDragContext* context)
{
    return GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(context), I_("exo-icon-view-status-pending")));
}

static void unset_reorderable(ExoIconView* icon_view)
{
    if (icon_view->priv->reorderable)
    {
        icon_view->priv->reorderable = FALSE;
        g_object_notify(G_OBJECT(icon_view), "reorderable");
    }
}

static void clear_source_info(ExoIconView* icon_view)
{
    if (icon_view->priv->source_targets)
        gtk_target_list_unref(icon_view->priv->source_targets);
    icon_view->priv->source_targets = NULL;

    icon_view->priv->source_set = FALSE;
}

static void clear_dest_info(ExoIconView* icon_view)
{
    if (icon_view->priv->dest_targets)
        gtk_target_list_unref(icon_view->priv->dest_targets);
    icon_view->priv->dest_targets = NULL;

    icon_view->priv->dest_set = FALSE;
}

static void set_source_row(GdkDragContext* context, GtkTreeModel* model, GtkTreePath* source_row)
{
    if (source_row)
        g_object_set_data_full(G_OBJECT(context),
                               I_("exo-icon-view-source-row"),
                               gtk_tree_row_reference_new(model, source_row),
                               (GDestroyNotify)gtk_tree_row_reference_free);
    else
        g_object_set_data_full(G_OBJECT(context), I_("exo-icon-view-source-row"), NULL, NULL);
}

static GtkTreePath* get_source_row(GdkDragContext* context)
{
    GtkTreeRowReference* ref;

    ref = g_object_get_data(G_OBJECT(context), I_("exo-icon-view-source-row"));

    if (ref)
        return gtk_tree_row_reference_get_path(ref);
    else
        return NULL;
}

typedef struct
{
    GtkTreeRowReference* dest_row;
    bool empty_view_drop;
    bool drop_append_mode;
} DestRow;

static void dest_row_free(void* data)
{
    DestRow* dr = (DestRow*)data;

    gtk_tree_row_reference_free(dr->dest_row);
    _exo_slice_free(DestRow, dr);
}

static void set_dest_row(GdkDragContext* context, GtkTreeModel* model, GtkTreePath* dest_row,
                         bool empty_view_drop, bool drop_append_mode)
{
    DestRow* dr;

    if (!dest_row)
    {
        g_object_set_data_full(G_OBJECT(context), I_("exo-icon-view-dest-row"), NULL, NULL);
        return;
    }

    dr = g_slice_new0(DestRow);

    dr->dest_row = gtk_tree_row_reference_new(model, dest_row);
    dr->empty_view_drop = empty_view_drop;
    dr->drop_append_mode = drop_append_mode;
    g_object_set_data_full(G_OBJECT(context),
                           I_("exo-icon-view-dest-row"),
                           dr,
                           (GDestroyNotify)dest_row_free);
}

static GtkTreePath* get_dest_row(GdkDragContext* context)
{
    DestRow* dr;

    dr = g_object_get_data(G_OBJECT(context), I_("exo-icon-view-dest-row"));

    if (dr)
    {
        GtkTreePath* path = NULL;

        if (dr->dest_row)
            path = gtk_tree_row_reference_get_path(dr->dest_row);
        else if (dr->empty_view_drop)
            path = gtk_tree_path_new_from_indices(0, -1);
        else
            path = NULL;

        if (path && dr->drop_append_mode)
            gtk_tree_path_next(path);

        return path;
    }
    else
        return NULL;
}

static bool check_model_dnd(GtkTreeModel* model, GType required_iface, const char* _signal)
{
    if (model == NULL || !G_TYPE_CHECK_INSTANCE_TYPE((model), required_iface))
    {
        g_warning(
            "You must override the default '%s' handler "
            "on ExoIconView when using models that don't support "
            "the %s interface and enabling drag-and-drop. The simplest way to do this "
            "is to connect to '%s' and call "
            "g_signal_stop_emission_by_name() in your signal handler to prevent "
            "the default handler from running. Look at the source code "
            "for the default handler in gtkiconview.c to get an idea what "
            "your handler should do. (gtkiconview.c is in the GTK+ source "
            "code.) If you're using GTK+ from a language other than C, "
            "there may be a more natural way to override default handlers, e.g. via derivation.",
            _signal,
            g_type_name(required_iface),
            _signal);
        return FALSE;
    }
    else
        return TRUE;
}

static void remove_scroll_timeout(ExoIconView* icon_view)
{
    if (icon_view->priv->scroll_timeout_id != 0)
    {
        g_source_remove(icon_view->priv->scroll_timeout_id);

        icon_view->priv->scroll_timeout_id = 0;
    }
}

static void exo_icon_view_autoscroll(ExoIconView* icon_view)
{
    int px, py, x, y, width, height;
    int hoffset, voffset;
    float value;

    gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(icon_view)), &px, &py, NULL);
#if (GTK_MAJOR_VERSION == 3)
    GdkSeat* seat;
    GdkDevice* pointer_dev;
    seat = gdk_display_get_default_seat(
        gdk_window_get_display(gtk_widget_get_window(GTK_WIDGET(icon_view))));
    pointer_dev = gdk_seat_get_pointer(seat);
    gdk_window_get_device_position(gtk_widget_get_window(GTK_WIDGET(icon_view)),
                                   pointer_dev,
                                   &px,
                                   &py,
                                   NULL);
    gdk_window_get_geometry(gtk_widget_get_window(GTK_WIDGET(icon_view)), &x, &y, &width, &height);
#elif (GTK_MAJOR_VERSION == 2)
    gdk_window_get_geometry(gtk_widget_get_window(GTK_WIDGET(icon_view)),
                            &x,
                            &y,
                            &width,
                            &height,
                            NULL);
#endif

    /* see if we are near the edge. */
    voffset = py - (y + 2 * SCROLL_EDGE_SIZE);
    if (voffset > 0)
        voffset = MAX(py - (y + height - 2 * SCROLL_EDGE_SIZE), 0);

    hoffset = px - (x + 2 * SCROLL_EDGE_SIZE);
    if (hoffset > 0)
        hoffset = MAX(px - (x + width - 2 * SCROLL_EDGE_SIZE), 0);

    if (voffset != 0)
    {
        value = CLAMP(gtk_adjustment_get_value(icon_view->priv->vadjustment) + voffset,
                      gtk_adjustment_get_lower(icon_view->priv->vadjustment),
                      gtk_adjustment_get_upper(icon_view->priv->vadjustment) -
                          gtk_adjustment_get_page_size(icon_view->priv->vadjustment));
        gtk_adjustment_set_value(icon_view->priv->vadjustment, value);
    }
    if (hoffset != 0)
    {
        value = CLAMP(gtk_adjustment_get_value(icon_view->priv->hadjustment) + hoffset,
                      gtk_adjustment_get_lower(icon_view->priv->hadjustment),
                      gtk_adjustment_get_upper(icon_view->priv->hadjustment) -
                          gtk_adjustment_get_page_size(icon_view->priv->hadjustment));
        gtk_adjustment_set_value(icon_view->priv->hadjustment, value);
    }
}

static bool drag_scroll_timeout(void* data)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(data);
    exo_icon_view_autoscroll(icon_view);
    return TRUE;
}

static bool set_destination(ExoIconView* icon_view, GdkDragContext* context, int x, int y,
                            GdkDragAction* suggested_action, GdkAtom* target)
{
    GtkWidget* widget;
    GtkTreePath* path = NULL;
    ExoIconViewDropPosition pos;
    ExoIconViewDropPosition old_pos;
    GtkTreePath* old_dest_path = NULL;
    bool can_drop = FALSE;

    widget = GTK_WIDGET(icon_view);

    *suggested_action = 0;
    *target = GDK_NONE;

    if (!icon_view->priv->dest_set)
    {
        /* someone unset us as a drag dest, note that if
         * we return FALSE drag_leave isn't called
         */

        exo_icon_view_set_drag_dest_item(icon_view, NULL, EXO_ICON_VIEW_DROP_LEFT);

        remove_scroll_timeout(EXO_ICON_VIEW(widget));

        return FALSE; /* no longer a drop site */
    }

    *target = gtk_drag_dest_find_target(widget, context, icon_view->priv->dest_targets);
    if (*target == GDK_NONE)
        return FALSE;

    if (!exo_icon_view_get_dest_item_at_pos(icon_view, x, y, &path, &pos))
    {
        int n_children;
        GtkTreeModel* model;

        /* the row got dropped on empty space, let's setup a special case
         */

        if (path)
            gtk_tree_path_free(path);

        model = exo_icon_view_get_model(icon_view);

        n_children = gtk_tree_model_iter_n_children(model, NULL);
        if (n_children)
        {
            pos = EXO_ICON_VIEW_DROP_BELOW;
            path = gtk_tree_path_new_from_indices(n_children - 1, -1);
        }
        else
        {
            pos = EXO_ICON_VIEW_DROP_ABOVE;
            path = gtk_tree_path_new_from_indices(0, -1);
        }

        can_drop = TRUE;

        goto out;
    }

    g_assert(path);

    exo_icon_view_get_drag_dest_item(icon_view, &old_dest_path, &old_pos);

    if (old_dest_path)
        gtk_tree_path_free(old_dest_path);

    if (TRUE /* FIXME if the location droppable predicate */)
    {
        can_drop = TRUE;
    }

out:
    if (can_drop)
    {
        GtkWidget* source_widget;

        *suggested_action = gdk_drag_context_get_suggested_action(context);
        source_widget = gtk_drag_get_source_widget(context);

        if (source_widget == widget)
        {
            /* Default to MOVE, unless the user has
             * pressed ctrl or shift to affect available actions
             */
            if ((gdk_drag_context_get_actions(context) & GDK_ACTION_MOVE) != 0)
                *suggested_action = GDK_ACTION_MOVE;
        }

        exo_icon_view_set_drag_dest_item(EXO_ICON_VIEW(widget), path, pos);
    }
    else
    {
        /* can't drop here */
        exo_icon_view_set_drag_dest_item(EXO_ICON_VIEW(widget), NULL, EXO_ICON_VIEW_DROP_LEFT);
    }

    if (path)
        gtk_tree_path_free(path);

    return TRUE;
}

static GtkTreePath* get_logical_destination(ExoIconView* icon_view, bool* drop_append_mode)
{
    /* adjust path to point to the row the drop goes in front of */
    GtkTreePath* path = NULL;
    ExoIconViewDropPosition pos;

    *drop_append_mode = FALSE;

    exo_icon_view_get_drag_dest_item(icon_view, &path, &pos);

    if (path == NULL)
        return NULL;

    if (pos == EXO_ICON_VIEW_DROP_RIGHT || pos == EXO_ICON_VIEW_DROP_BELOW)
    {
        GtkTreeIter iter;
        GtkTreeModel* model = icon_view->priv->model;

        if (!gtk_tree_model_get_iter(model, &iter, path) || !gtk_tree_model_iter_next(model, &iter))
            *drop_append_mode = TRUE;
        else
        {
            *drop_append_mode = FALSE;
            gtk_tree_path_next(path);
        }
    }

    return path;
}

static bool exo_icon_view_maybe_begin_drag(ExoIconView* icon_view, GdkEventMotion* event)
{
    GdkDragContext* context;
    GtkTreePath* path = NULL;
    int button;
    GtkTreeModel* model;
    bool retval = FALSE;

    if (!icon_view->priv->source_set)
        goto out;

    if (icon_view->priv->pressed_button < 0)
        goto out;

    if (!gtk_drag_check_threshold(GTK_WIDGET(icon_view),
                                  icon_view->priv->press_start_x,
                                  icon_view->priv->press_start_y,
                                  event->x,
                                  event->y))
        goto out;

    model = exo_icon_view_get_model(icon_view);

    if (model == NULL)
        goto out;

    button = icon_view->priv->pressed_button;
    icon_view->priv->pressed_button = -1;

    path = exo_icon_view_get_path_at_pos(icon_view,
                                         icon_view->priv->press_start_x,
                                         icon_view->priv->press_start_y);

    if (path == NULL)
        goto out;

    if (!GTK_IS_TREE_DRAG_SOURCE(model) ||
        !gtk_tree_drag_source_row_draggable(GTK_TREE_DRAG_SOURCE(model), path))
        goto out;

    /* FIXME Check whether we're a start button, if not return FALSE and
     * free path
     */

    /* Now we can begin the drag */

    retval = TRUE;

#if (GTK_MAJOR_VERSION == 3)
    context = gtk_drag_begin_with_coordinates(GTK_WIDGET(icon_view),
                                              icon_view->priv->source_targets,
                                              icon_view->priv->source_actions,
                                              button,
                                              (GdkEvent*)event,
                                              event->x,
                                              event->y);
#elif (GTK_MAJOR_VERSION == 2)
    context = gtk_drag_begin(GTK_WIDGET(icon_view),
                             icon_view->priv->source_targets,
                             icon_view->priv->source_actions,
                             button,
                             (GdkEvent*)event);
#endif

    set_source_row(context, model, path);

out:
    if (path)
        gtk_tree_path_free(path);

    return retval;
}

/* Source side drag signals */
static void exo_icon_view_drag_begin(GtkWidget* widget, GdkDragContext* context)
{
    ExoIconView* icon_view;
    ExoIconViewItem* item;
#if (GTK_MAJOR_VERSION == 3)
    cairo_surface_t* icon;
#elif (GTK_MAJOR_VERSION == 2)
    GdkPixmap* icon;
#endif
    int x, y;
    GtkTreePath* path;

    icon_view = EXO_ICON_VIEW(widget);

    /* if the user uses a custom DnD impl, we don't set the icon here */
    if (!icon_view->priv->dest_set && !icon_view->priv->source_set)
        return;

    item = exo_icon_view_get_item_at_coords(icon_view,
                                            icon_view->priv->press_start_x,
                                            icon_view->priv->press_start_y,
                                            TRUE,
                                            NULL);

    _exo_return_if_fail(item != NULL);

    x = icon_view->priv->press_start_x - item->area.x + 1;
    y = icon_view->priv->press_start_y - item->area.y + 1;

    path = gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);
    icon = exo_icon_view_create_drag_icon(icon_view, path);
    gtk_tree_path_free(path);

#if (GTK_MAJOR_VERSION == 3)
    gtk_drag_set_icon_surface(context, icon);

    cairo_surface_destroy(icon);
#elif (GTK_MAJOR_VERSION == 2)
    gtk_drag_set_icon_pixmap(context, gdk_drawable_get_colormap(icon), icon, NULL, x, y);

    g_object_unref(icon);
#endif
}

static void exo_icon_view_drag_end(GtkWidget* widget, GdkDragContext* context)
{
    ExoIconView* icon_view;

    icon_view = EXO_ICON_VIEW(widget);

    /* check if we're in single click mode */
    if (G_UNLIKELY(icon_view->priv->single_click))
    {
        /* reset the cursor if we're still realized */
        if (G_LIKELY(icon_view->priv->bin_window != NULL))
            gdk_window_set_cursor(icon_view->priv->bin_window, NULL);

        /* reset the last single clicked item */
        icon_view->priv->last_single_clicked = NULL;
    }

    /* reset the pressed_button state */
    icon_view->priv->pressed_button = -1;
}

static void exo_icon_view_drag_data_get(GtkWidget* widget, GdkDragContext* context,
                                        GtkSelectionData* selection_data, unsigned int info,
                                        unsigned int drag_time)
{
    ExoIconView* icon_view;
    GtkTreeModel* model;
    GtkTreePath* source_row;

    icon_view = EXO_ICON_VIEW(widget);
    model = exo_icon_view_get_model(icon_view);

    if (model == NULL)
        return;

    if (!icon_view->priv->dest_set)
        return;

    source_row = get_source_row(context);

    if (source_row == NULL)
        return;

    /* We can implement the GTK_TREE_MODEL_ROW target generically for
     * any model; for DragSource models there are some other targets
     * we also support.
     */

    if (GTK_IS_TREE_DRAG_SOURCE(model) &&
        gtk_tree_drag_source_drag_data_get(GTK_TREE_DRAG_SOURCE(model), source_row, selection_data))
        goto done;

    /* If drag_data_get does nothing, try providing row data. */
    if (gtk_selection_data_get_target(selection_data) ==
        gdk_atom_intern("GTK_TREE_MODEL_ROW", FALSE))
        gtk_tree_set_row_drag_data(selection_data, model, source_row);

done:
    gtk_tree_path_free(source_row);
}

static void exo_icon_view_drag_data_delete(GtkWidget* widget, GdkDragContext* context)
{
    GtkTreeModel* model;
    ExoIconView* icon_view;
    GtkTreePath* source_row;

    icon_view = EXO_ICON_VIEW(widget);
    model = exo_icon_view_get_model(icon_view);

    if (!check_model_dnd(model, GTK_TYPE_TREE_DRAG_SOURCE, "drag_data_delete"))
        return;

    if (!icon_view->priv->dest_set)
        return;

    source_row = get_source_row(context);

    if (source_row == NULL)
        return;

    if (GTK_IS_TREE_DRAG_SOURCE(model))
        gtk_tree_drag_source_drag_data_delete(GTK_TREE_DRAG_SOURCE(model), source_row);

    gtk_tree_path_free(source_row);

    set_source_row(context, NULL, NULL);
}

/* Target side drag signals */
static void exo_icon_view_drag_leave(GtkWidget* widget, GdkDragContext* context,
                                     unsigned int drag_time)
{
    ExoIconView* icon_view;

    icon_view = EXO_ICON_VIEW(widget);

    /* unset any highlight row */
    exo_icon_view_set_drag_dest_item(icon_view, NULL, EXO_ICON_VIEW_DROP_LEFT);

    remove_scroll_timeout(icon_view);
}

static bool exo_icon_view_drag_motion(GtkWidget* widget, GdkDragContext* context, int x, int y,
                                      unsigned int drag_time)
{
    ExoIconViewDropPosition pos;
    GdkDragAction suggested_action = 0;
    GtkTreePath* path = NULL;
    ExoIconView* icon_view = EXO_ICON_VIEW(widget);
    bool empty;
    GdkAtom target;

    if (!set_destination(icon_view, context, x, y, &suggested_action, &target))
        return FALSE;

    exo_icon_view_get_drag_dest_item(icon_view, &path, &pos);

    /* we only know this *after* set_desination_row */
    empty = icon_view->priv->empty_view_drop;

    if (path == NULL && !empty)
    {
        /* Can't drop here. */
        gdk_drag_status(context, 0, drag_time);
    }
    else
    {
        if (icon_view->priv->scroll_timeout_id == 0)
            icon_view->priv->scroll_timeout_id =
                gdk_threads_add_timeout(50, drag_scroll_timeout, icon_view);

        if (target == gdk_atom_intern("GTK_TREE_MODEL_ROW", FALSE))
        {
            /* Request data so we can use the source row when
             * determining whether to accept the drop
             */
            set_status_pending(context, suggested_action);
            gtk_drag_get_data(widget, context, target, drag_time);
        }
        else
        {
            set_status_pending(context, 0);
            gdk_drag_status(context, suggested_action, drag_time);
        }
    }

    if (path != NULL)
        gtk_tree_path_free(path);

    return TRUE;
}

static bool exo_icon_view_drag_drop(GtkWidget* widget, GdkDragContext* context, int x, int y,
                                    unsigned int drag_time)
{
    ExoIconView* icon_view;
    GtkTreePath* path;
    GdkDragAction suggested_action = 0;
    GdkAtom target = GDK_NONE;
    GtkTreeModel* model;
    bool drop_append_mode;

    icon_view = EXO_ICON_VIEW(widget);
    model = exo_icon_view_get_model(icon_view);

    remove_scroll_timeout(EXO_ICON_VIEW(widget));

    if (!icon_view->priv->dest_set)
        return FALSE;

    if (!check_model_dnd(model, GTK_TYPE_TREE_DRAG_DEST, "drag_drop"))
        return FALSE;

    if (!set_destination(icon_view, context, x, y, &suggested_action, &target))
        return FALSE;

    path = get_logical_destination(icon_view, &drop_append_mode);

    if (target != GDK_NONE && path != NULL)
    {
        /* in case a motion had requested drag data, change things so we
         * treat drag data receives as a drop.
         */
        set_status_pending(context, 0);
        set_dest_row(context, model, path, icon_view->priv->empty_view_drop, drop_append_mode);
    }

    if (path)
        gtk_tree_path_free(path);

    /* Unset this thing */
    exo_icon_view_set_drag_dest_item(icon_view, NULL, EXO_ICON_VIEW_DROP_LEFT);

    if (target != GDK_NONE)
    {
        gtk_drag_get_data(widget, context, target, drag_time);
        return TRUE;
    }
    else
        return FALSE;
}

static void exo_icon_view_drag_data_received(GtkWidget* widget, GdkDragContext* context, int x,
                                             int y, GtkSelectionData* selection_data,
                                             unsigned int info, unsigned int drag_time)
{
    GtkTreePath* path;
    bool accepted = FALSE;
    GtkTreeModel* model;
    ExoIconView* icon_view;
    GtkTreePath* dest_row;
    GdkDragAction suggested_action;
    bool drop_append_mode;

    icon_view = EXO_ICON_VIEW(widget);
    model = exo_icon_view_get_model(icon_view);

    if (!check_model_dnd(model, GTK_TYPE_TREE_DRAG_DEST, "drag_data_received"))
        return;

    if (!icon_view->priv->dest_set)
        return;

    suggested_action = get_status_pending(context);

    if (suggested_action)
    {
        /* We are getting this data due to a request in drag_motion,
         * rather than due to a request in drag_drop, so we are just
         * supposed to call drag_status, not actually paste in the
         * data.
         */
        path = get_logical_destination(icon_view, &drop_append_mode);

        if (path == NULL)
            suggested_action = 0;

        if (suggested_action)
        {
            if (!gtk_tree_drag_dest_row_drop_possible(GTK_TREE_DRAG_DEST(model),
                                                      path,
                                                      selection_data))
                suggested_action = 0;
        }

        gdk_drag_status(context, suggested_action, drag_time);

        if (path)
            gtk_tree_path_free(path);

        /* If you can't drop, remove user drop indicator until the next motion */
        if (suggested_action == 0)
            exo_icon_view_set_drag_dest_item(icon_view, NULL, EXO_ICON_VIEW_DROP_LEFT);
        return;
    }

    dest_row = get_dest_row(context);

    if (dest_row == NULL)
        return;

    if (gtk_selection_data_get_length(selection_data) >= 0)
    {
        if (gtk_tree_drag_dest_drag_data_received(GTK_TREE_DRAG_DEST(model),
                                                  dest_row,
                                                  selection_data))
            accepted = TRUE;
    }

    gtk_drag_finish(context,
                    accepted,
                    (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE),
                    drag_time);

    gtk_tree_path_free(dest_row);

    /* drop dest_row */
    set_dest_row(context, NULL, NULL, FALSE, FALSE);
}

/**
 * exo_icon_view_enable_model_drag_source:
 * @icon_view         : a #GtkIconTreeView
 * @start_button_mask : Mask of allowed buttons to start drag
 * @targets           : the table of targets that the drag will support
 * @n_targets         : the number of items in @targets
 * @actions           : the bitmask of possible actions for a drag from this widget
 *
 * Turns @icon_view into a drag source for automatic DND.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_enable_model_drag_source(ExoIconView* icon_view,
                                            GdkModifierType start_button_mask,
                                            const GtkTargetEntry* targets, int n_targets,
                                            GdkDragAction actions)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    gtk_drag_source_set(GTK_WIDGET(icon_view), 0, NULL, 0, actions);

    clear_source_info(icon_view);
    icon_view->priv->start_button_mask = start_button_mask;
    icon_view->priv->source_targets = gtk_target_list_new(targets, n_targets);
    icon_view->priv->source_actions = actions;

    icon_view->priv->source_set = TRUE;

    unset_reorderable(icon_view);
}

/**
 * exo_icon_view_enable_model_drag_dest:
 * @icon_view : a #ExoIconView
 * @targets   : the table of targets that the drag will support
 * @n_targets : the number of items in @targets
 * @actions   : the bitmask of possible actions for a drag from this widget
 *
 * Turns @icon_view into a drop destination for automatic DND.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_enable_model_drag_dest(ExoIconView* icon_view, const GtkTargetEntry* targets,
                                          int n_targets, GdkDragAction actions)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    gtk_drag_dest_set(GTK_WIDGET(icon_view), 0, NULL, 0, actions);

    clear_dest_info(icon_view);

    icon_view->priv->dest_targets = gtk_target_list_new(targets, n_targets);
    icon_view->priv->dest_actions = actions;

    icon_view->priv->dest_set = TRUE;

    unset_reorderable(icon_view);
}

/**
 * exo_icon_view_unset_model_drag_source:
 * @icon_view : a #ExoIconView
 *
 * Undoes the effect of #exo_icon_view_enable_model_drag_source().
 *
 * Since: 0.3.1
 **/
void exo_icon_view_unset_model_drag_source(ExoIconView* icon_view)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (icon_view->priv->source_set)
    {
        gtk_drag_source_unset(GTK_WIDGET(icon_view));
        clear_source_info(icon_view);
    }

    unset_reorderable(icon_view);
}

/**
 * exo_icon_view_unset_model_drag_dest:
 * @icon_view : a #ExoIconView
 *
 * Undoes the effect of #exo_icon_view_enable_model_drag_dest().
 *
 * Since: 0.3.1
 **/
void exo_icon_view_unset_model_drag_dest(ExoIconView* icon_view)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (icon_view->priv->dest_set)
    {
        gtk_drag_dest_unset(GTK_WIDGET(icon_view));
        clear_dest_info(icon_view);
    }

    unset_reorderable(icon_view);
}

/**
 * exo_icon_view_set_drag_dest_item:
 * @icon_view : a #ExoIconView
 * @path      : The path of the item to highlight, or %NULL.
 * @pos       : Specifies whether to drop, relative to the item
 *
 * Sets the item that is highlighted for feedback.
 *
 * Since: 0.3.1
 */
void exo_icon_view_set_drag_dest_item(ExoIconView* icon_view, GtkTreePath* path,
                                      ExoIconViewDropPosition pos)
{
    ExoIconViewItem* item;
    GtkTreePath* previous_path;

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    /* Note; this function is exported to allow a custom DND
     * implementation, so it can't touch TreeViewDragInfo
     */

    if (icon_view->priv->dest_item != NULL)
    {
        /* determine and reset the previous path */
        previous_path = gtk_tree_row_reference_get_path(icon_view->priv->dest_item);
        gtk_tree_row_reference_free(icon_view->priv->dest_item);
        icon_view->priv->dest_item = NULL;

        /* check if the path is still valid */
        if (G_LIKELY(previous_path != NULL))
        {
            /* schedule a redraw for the previous path */
            item = g_list_nth_data(icon_view->priv->items,
                                   gtk_tree_path_get_indices(previous_path)[0]);
            if (G_LIKELY(item != NULL))
                exo_icon_view_queue_draw_item(icon_view, item);
            gtk_tree_path_free(previous_path);
        }
    }

    /* special case a drop on an empty model */
    icon_view->priv->empty_view_drop = FALSE;
    if (pos == EXO_ICON_VIEW_NO_DROP && path != NULL && gtk_tree_path_get_depth(path) == 1 &&
        gtk_tree_path_get_indices(path)[0] == 0)
    {
        int n_children;

        n_children = gtk_tree_model_iter_n_children(icon_view->priv->model, NULL);

        if (n_children == 0)
            icon_view->priv->empty_view_drop = TRUE;
    }

    icon_view->priv->dest_pos = pos;

    if (G_LIKELY(path != NULL))
    {
        /* take a row reference for the new item path */
        icon_view->priv->dest_item =
            gtk_tree_row_reference_new_proxy(G_OBJECT(icon_view), icon_view->priv->model, path);

        /* schedule a redraw on the new path */
        item = g_list_nth_data(icon_view->priv->items, gtk_tree_path_get_indices(path)[0]);
        if (G_LIKELY(item != NULL))
            exo_icon_view_queue_draw_item(icon_view, item);
    }
}

/**
 * exo_icon_view_get_drag_dest_item:
 * @icon_view : a #ExoIconView
 * @path      : Return location for the path of the highlighted item, or %NULL.
 * @pos       : Return location for the drop position, or %NULL
 *
 * Gets information about the item that is highlighted for feedback.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_get_drag_dest_item(ExoIconView* icon_view, GtkTreePath** path,
                                      ExoIconViewDropPosition* pos)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (path)
    {
        if (icon_view->priv->dest_item)
            *path = gtk_tree_row_reference_get_path(icon_view->priv->dest_item);
        else
            *path = NULL;
    }

    if (pos)
        *pos = icon_view->priv->dest_pos;
}

/**
 * exo_icon_view_get_dest_item_at_pos:
 * @icon_view : a #ExoIconView
 * @drag_x    : the position to determine the destination item for
 * @drag_y    : the position to determine the destination item for
 * @path      : Return location for the path of the highlighted item, or %NULL.
 * @pos       : Return location for the drop position, or %NULL
 *
 * Determines the destination item for a given position.
 *
 * Both @drag_x and @drag_y are given in icon window coordinates. Use
 * #exo_icon_view_widget_to_icon_coords() if you need to translate
 * widget coordinates first.
 *
 * Return value: whether there is an item at the given position.
 *
 * Since: 0.3.1
 **/
bool exo_icon_view_get_dest_item_at_pos(ExoIconView* icon_view, int drag_x, int drag_y,
                                        GtkTreePath** path, ExoIconViewDropPosition* pos)
{
    ExoIconViewItem* item;

    /* Note; this function is exported to allow a custom DND
     * implementation, so it can't touch TreeViewDragInfo
     */

    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);
    g_return_val_if_fail(drag_x >= 0, FALSE);
    g_return_val_if_fail(drag_y >= 0, FALSE);
    g_return_val_if_fail(icon_view->priv->bin_window != NULL, FALSE);

    if (G_LIKELY(path != NULL))
        *path = NULL;

    item = exo_icon_view_get_item_at_coords(icon_view, drag_x, drag_y, FALSE, NULL);

    if (G_UNLIKELY(item == NULL))
        return FALSE;

    if (G_LIKELY(path != NULL))
        *path = gtk_tree_path_new_from_indices(g_list_index(icon_view->priv->items, item), -1);

    if (G_LIKELY(pos != NULL))
    {
        if (drag_x < item->area.x + item->area.width / 4)
            *pos = EXO_ICON_VIEW_DROP_LEFT;
        else if (drag_x > item->area.x + item->area.width * 3 / 4)
            *pos = EXO_ICON_VIEW_DROP_RIGHT;
        else if (drag_y < item->area.y + item->area.height / 4)
            *pos = EXO_ICON_VIEW_DROP_ABOVE;
        else if (drag_y > item->area.y + item->area.height * 3 / 4)
            *pos = EXO_ICON_VIEW_DROP_BELOW;
        else
            *pos = EXO_ICON_VIEW_DROP_INTO;
    }

    return TRUE;
}

/**
 * exo_icon_view_create_drag_icon:
 * @icon_view : a #ExoIconView
 * @path      : a #GtkTreePath in @icon_view
 *
 * Creates a #GdkPixmap or #cairo_surface_t representation of the item at @path.
 * This image is used for a drag icon.
 *
 * Return value: a newly-allocated pixmap (gtk2) or cairo surface (gtk3) of the drag icon.
 *
 * Since: 0.3.1
 **/
#if (GTK_MAJOR_VERSION == 3)
cairo_surface_t*
#elif (GTK_MAJOR_VERSION == 2)
GdkPixmap*
#endif
exo_icon_view_create_drag_icon(ExoIconView* icon_view, GtkTreePath* path)
{
    GdkRectangle area;
    GtkWidget* widget = GTK_WIDGET(icon_view);
#if (GTK_MAJOR_VERSION == 3)
    cairo_surface_t* drawable;
#elif (GTK_MAJOR_VERSION == 2)
    GdkDrawable* drawable;
#endif
    GList* lp;
    int idx;
    cairo_t* cr;

    g_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), NULL);
    g_return_val_if_fail(gtk_tree_path_get_depth(path) > 0, NULL);

    /* verify that the widget is realized */
    if (G_UNLIKELY(!gtk_widget_get_realized(GTK_WIDGET(icon_view))))
        return NULL;

    idx = gtk_tree_path_get_indices(path)[0];

    for (lp = icon_view->priv->items; lp != NULL; lp = lp->next)
    {
        ExoIconViewItem* item = lp->data;
        if (G_UNLIKELY(idx == g_list_index(icon_view->priv->items, item)))
        {
#if (GTK_MAJOR_VERSION == 3)
            drawable = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                  item->area.width + 2,
                                                  item->area.height + 2);

            cr = cairo_create(drawable);
#elif (GTK_MAJOR_VERSION == 2)
            drawable = gdk_pixmap_new(icon_view->priv->bin_window,
                                      item->area.width + 2,
                                      item->area.height + 2,
                                      -1);

            cr = gdk_cairo_create(drawable);
#endif
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            gdk_cairo_set_source_color(
                cr,
                &gtk_widget_get_style(widget)->base[gtk_widget_get_state(widget)]);
            cairo_rectangle(cr, 0, 0, item->area.width + 2, item->area.height + 2);
            cairo_fill(cr);

            area.x = 0;
            area.y = 0;
            area.width = item->area.width;
            area.height = item->area.height;

#if (GTK_MAJOR_VERSION == 3)
            exo_icon_view_paint_item(icon_view, item, &area, cr, 1, 1, FALSE);
#elif (GTK_MAJOR_VERSION == 2)
            exo_icon_view_paint_item(icon_view, item, &area, drawable, 1, 1, FALSE);
#endif

            gdk_cairo_set_source_color(cr, &gtk_widget_get_style(widget)->black);
            cairo_rectangle(cr, 1, 1, item->area.width + 1, item->area.height + 1);
            cairo_stroke(cr);

            cairo_destroy(cr);

            return drawable;
        }
    }

    return NULL;
}

/**
 * exo_icon_view_set_reorderable:
 * @icon_view   : A #ExoIconView.
 * @reorderable : %TRUE, if the list of items can be reordered.
 *
 * This function is a convenience function to allow you to reorder models that
 * support the #GtkTreeDragSourceIface and the #GtkTreeDragDestIface.  Both
 * #GtkTreeStore and #GtkListStore support these.  If @reorderable is %TRUE, then
 * the user can reorder the model by dragging and dropping rows.  The
 * developer can listen to these changes by connecting to the model's
 * ::row-inserted and ::row-deleted signals.
 *
 * This function does not give you any degree of control over the order -- any
 * reordering is allowed.  If more control is needed, you should probably
 * handle drag and drop manually.
 *
 * Since: 0.3.1
 **/
void exo_icon_view_set_reorderable(ExoIconView* icon_view, bool reorderable)
{
    static const GtkTargetEntry item_targets[] = {
        {
            "GTK_TREE_MODEL_ROW",
            GTK_TARGET_SAME_WIDGET,
            0,
        },
    };

    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    reorderable = (reorderable != FALSE);

    if (G_UNLIKELY(icon_view->priv->reorderable == reorderable))
        return;

    if (G_LIKELY(reorderable))
    {
        exo_icon_view_enable_model_drag_source(icon_view,
                                               GDK_BUTTON1_MASK,
                                               item_targets,
                                               G_N_ELEMENTS(item_targets),
                                               GDK_ACTION_MOVE);
        exo_icon_view_enable_model_drag_dest(icon_view,
                                             item_targets,
                                             G_N_ELEMENTS(item_targets),
                                             GDK_ACTION_MOVE);
    }
    else
    {
        exo_icon_view_unset_model_drag_source(icon_view);
        exo_icon_view_unset_model_drag_dest(icon_view);
    }

    icon_view->priv->reorderable = reorderable;

    g_object_notify(G_OBJECT(icon_view), "reorderable");
}

/*----------------------*
 * Single-click support *
 *----------------------*/

/**
 * exo_icon_view_set_single_click:
 * @icon_view    : a #ExoIconView.
 * @single_click : %TRUE for single click, %FALSE for double click mode.
 *
 * If @single_click is %TRUE, @icon_view will be in single click mode
 * afterwards, else @icon_view will be in double click mode.
 *
 * Since: 0.3.1.3
 **/
void exo_icon_view_set_single_click(ExoIconView* icon_view, bool single_click)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    /* normalize the value */
    single_click = !!single_click;

    /* check if we have a new setting here */
    if (icon_view->priv->single_click != single_click)
    {
        icon_view->priv->single_click = single_click;
        g_object_notify(G_OBJECT(icon_view), "single-click");
    }
}

/**
 * exo_icon_view_set_single_click_timeout:
 * @icon_view            : a #ExoIconView.
 * @single_click_timeout : the new timeout or %0 to disable.
 *
 * If @single_click_timeout is a value greater than zero, it specifies
 * the amount of time in milliseconds after which the item under the
 * mouse cursor will be selected automatically in single click mode.
 * A value of %0 for @single_click_timeout disables the autoselection
 * for @icon_view.
 *
 * This setting does not have any effect unless the @icon_view is in
 * single-click mode, see exo_icon_view_set_single_click().
 *
 * Since: 0.3.1.5
 **/
void exo_icon_view_set_single_click_timeout(ExoIconView* icon_view,
                                            unsigned int single_click_timeout)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    /* check if we have a new setting */
    if (icon_view->priv->single_click_timeout != single_click_timeout)
    {
        /* apply the new setting */
        icon_view->priv->single_click_timeout = single_click_timeout;

        /* be sure to cancel any pending single click timeout */
        if (G_UNLIKELY(icon_view->priv->single_click_timeout_id != 0))
            g_source_remove(icon_view->priv->single_click_timeout_id);

        /* notify listeners */
        g_object_notify(G_OBJECT(icon_view), "single-click-timeout");
    }
}

static bool exo_icon_view_single_click_timeout(void* user_data)
{
    ExoIconViewItem* item;
    bool dirty = FALSE;
    ExoIconView* icon_view = EXO_ICON_VIEW(user_data);
    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(icon_view));

    /* verify that we are in single-click mode on an active window and have a prelit item */
    if (GTK_IS_WINDOW(toplevel) && gtk_window_is_active(GTK_WINDOW(toplevel)) &&
        icon_view->priv->single_click && icon_view->priv->prelit_item != NULL)
    {
        gtk_widget_grab_focus(GTK_WIDGET(icon_view));

        /* work on the prelit item */
        item = icon_view->priv->prelit_item;

        /* be sure the item is fully visible */
        exo_icon_view_scroll_to_item(icon_view, item);

        /* change the selection appropriately */
        if (G_UNLIKELY(icon_view->priv->selection_mode == GTK_SELECTION_NONE))
        {
            exo_icon_view_set_cursor_item(icon_view, item, -1);
        }
        else if ((icon_view->priv->single_click_timeout_state & GDK_SHIFT_MASK) != 0 &&
                 icon_view->priv->selection_mode == GTK_SELECTION_MULTIPLE)
        {
            if (!(icon_view->priv->single_click_timeout_state & GDK_CONTROL_MASK))
                /* unselect all previously selected items */
                exo_icon_view_unselect_all_internal(icon_view);

            /* select all items between the anchor and the prelit item */
            exo_icon_view_set_cursor_item(icon_view, item, -1);
            if (icon_view->priv->anchor_item == NULL)
                icon_view->priv->anchor_item = item;
            else
                exo_icon_view_select_all_between(icon_view, icon_view->priv->anchor_item, item);

            /* selection was changed */
            dirty = TRUE;
        }
        else
        {
            if ((icon_view->priv->selection_mode == GTK_SELECTION_MULTIPLE ||
                 ((icon_view->priv->selection_mode == GTK_SELECTION_SINGLE) && item->selected)) &&
                (icon_view->priv->single_click_timeout_state & GDK_CONTROL_MASK) != 0)
            {
                item->selected = !item->selected;
                exo_icon_view_queue_draw_item(icon_view, item);
                dirty = TRUE;
            }
            else if (!item->selected)
            {
                exo_icon_view_unselect_all_internal(icon_view);
                exo_icon_view_queue_draw_item(icon_view, item);
                item->selected = TRUE;
                dirty = TRUE;
            }
            exo_icon_view_set_cursor_item(icon_view, item, -1);
            icon_view->priv->anchor_item = item;
        }
    }

    /* emit "selection-changed" and stop drawing keyboard
     * focus indicator if the selection was altered
     */
    if (G_LIKELY(dirty))
    {
        /* reset "draw keyfocus" flag */
        EXO_ICON_VIEW_UNSET_FLAG(icon_view, EXO_ICON_VIEW_DRAW_KEYFOCUS);

        /* emit "selection-changed" */
        g_signal_emit(G_OBJECT(icon_view), icon_view_signals[SELECTION_CHANGED], 0);
    }

    return FALSE;
}

static void exo_icon_view_single_click_timeout_destroy(void* user_data)
{
    EXO_ICON_VIEW(user_data)->priv->single_click_timeout_id = 0;
}

/*----------------------------*
 * Interactive search support *
 *----------------------------*/

/**
 * exo_icon_view_set_enable_search:
 * @icon_view     : an #ExoIconView.
 * @enable_search : %TRUE if the user can search interactively.
 *
 * If @enable_search is set, then the user can type in text to search through
 * the @icon_view interactively (this is sometimes called "typeahead find").
 *
 * Note that even if this is %FALSE, the user can still initiate a search
 * using the "start-interactive-search" key binding.
 *
 * Since: 0.3.1.3
 **/
void exo_icon_view_set_enable_search(ExoIconView* icon_view, bool enable_search)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    enable_search = !!enable_search;

    if (G_LIKELY(icon_view->priv->enable_search != enable_search))
    {
        icon_view->priv->enable_search = enable_search;
        g_object_notify(G_OBJECT(icon_view), "enable-search");
    }
}

/**
 * exo_icon_view_set_search_column:
 * @icon_view     : an #ExoIconView.
 * @search_column : the column of the model to search in, or -1 to disable searching.
 *
 * Sets @search_column as the column where the interactive search code should search in.
 *
 * If the search column is set, user can use the "start-interactive-search" key
 * binding to bring up search popup. The "enable-search" property controls
 * whether simply typing text will also start an interactive search.
 *
 * Note that @search_column refers to a column of the model.
 *
 * Since: 0.3.1.3
 **/
void exo_icon_view_set_search_column(ExoIconView* icon_view, int search_column)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(search_column >= -1);

    if (G_LIKELY(icon_view->priv->search_column != search_column))
    {
        icon_view->priv->search_column = search_column;
        g_object_notify(G_OBJECT(icon_view), "search-column");
    }
}

/**
 * exo_icon_view_set_search_equal_func:
 * @icon_view            : an #ExoIconView.
 * @search_equal_func    : the compare function to use during the search, or %NULL.
 * @search_equal_data    : user data to pass to @search_equal_func, or %NULL.
 * @search_equal_destroy : destroy notifier for @search_equal_data, or %NULL.
 *
 * Sets the compare function for the interactive search capabilities;
 * note that some like strcmp() returning 0 for equality
 * #ExoIconViewSearchEqualFunc returns %FALSE on matches.
 *
 * Specifying %NULL for @search_equal_func will reset @icon_view to use the default
 * search equal function.
 *
 * Since: 0.3.1.3
 **/
void exo_icon_view_set_search_equal_func(ExoIconView* icon_view,
                                         ExoIconViewSearchEqualFunc search_equal_func,
                                         void* search_equal_data,
                                         GDestroyNotify search_equal_destroy)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(search_equal_func != NULL ||
                     (search_equal_data == NULL && search_equal_destroy == NULL));

    /* destroy the previous data (if any) */
    if (G_UNLIKELY(icon_view->priv->search_equal_destroy != NULL &&
                   icon_view->priv->search_equal_data != NULL)) // sfm
        (*icon_view->priv->search_equal_destroy)(icon_view->priv->search_equal_data);

    icon_view->priv->search_equal_func =
        (search_equal_func != NULL) ? search_equal_func : exo_icon_view_search_equal_func;
    icon_view->priv->search_equal_data = search_equal_data;
    icon_view->priv->search_equal_destroy = search_equal_destroy;
}

/**
 * exo_icon_view_set_search_position_func:
 * @icon_view               : an #ExoIconView.
 * @search_position_func    : the function to use to position the search dialog, or %NULL.
 * @search_position_data    : user data to pass to @search_position_func, or %NULL.
 * @search_position_destroy : destroy notifier for @search_position_data, or %NULL.
 *
 * Sets the function to use when positioning the seach dialog.
 *
 * Specifying %NULL for @search_position_func will reset @icon_view to use the default
 * search position function.
 *
 * Since: 0.3.1.3
 **/
void exo_icon_view_set_search_position_func(ExoIconView* icon_view,
                                            ExoIconViewSearchPositionFunc search_position_func,
                                            void* search_position_data,
                                            GDestroyNotify search_position_destroy)
{
    g_return_if_fail(EXO_IS_ICON_VIEW(icon_view));
    g_return_if_fail(search_position_func != NULL ||
                     (search_position_data == NULL && search_position_destroy == NULL));

    /* destroy the previous data (if any) */
    if (icon_view->priv->search_position_destroy != NULL)
        (*icon_view->priv->search_position_destroy)(icon_view->priv->search_position_data);

    icon_view->priv->search_position_func =
        (search_position_func != NULL) ? search_position_func : exo_icon_view_search_position_func;
    icon_view->priv->search_position_data = search_position_data;
    icon_view->priv->search_position_destroy = search_position_destroy;
}

static void exo_icon_view_search_activate(GtkEntry* entry, ExoIconView* icon_view)
{
    GtkTreePath* path;

    /* hide the interactive search dialog */
    exo_icon_view_search_dialog_hide(icon_view->priv->search_window, icon_view);

    /* check if we have a cursor item, and if so, activate it */
    if (exo_icon_view_get_cursor(icon_view, &path, NULL))
    {
        /* only activate the cursor item if it's selected */
        if (exo_icon_view_path_is_selected(icon_view, path))
            exo_icon_view_item_activated(icon_view, path);
        gtk_tree_path_free(path);
    }
}

static void exo_icon_view_search_dialog_hide(GtkWidget* search_dialog, ExoIconView* icon_view)
{
    _exo_return_if_fail(GTK_IS_WIDGET(search_dialog));
    _exo_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    if (icon_view->priv->search_disable_popdown)
        return;

    /* disconnect the "changed" signal handler */
    if (icon_view->priv->search_entry_changed_id != 0)
    {
        g_signal_handler_disconnect(G_OBJECT(icon_view->priv->search_entry),
                                    icon_view->priv->search_entry_changed_id);
        icon_view->priv->search_entry_changed_id = 0;
    }

    /* disable the flush timeout */
    if (icon_view->priv->search_timeout_id != 0)
        g_source_remove(icon_view->priv->search_timeout_id);

    /* send focus-out event */
    _exo_gtk_widget_send_focus_change(icon_view->priv->search_entry, FALSE);
    gtk_widget_hide(search_dialog);
    gtk_entry_set_text(GTK_ENTRY(icon_view->priv->search_entry), "");
}

static void exo_icon_view_search_ensure_directory(ExoIconView* icon_view)
{
    GtkWidget* toplevel;
    GtkWidget* frame;
    GtkWidget* vbox;

    /* determine the toplevel window */
    toplevel = gtk_widget_get_toplevel(GTK_WIDGET(icon_view));

    /* check if we already have a search window */
    if (G_LIKELY(icon_view->priv->search_window != NULL))
    {
        if (gtk_window_has_group(GTK_WINDOW(toplevel)))
            gtk_window_group_add_window(gtk_window_get_group(GTK_WINDOW(toplevel)),
                                        GTK_WINDOW(icon_view->priv->search_window));
        else if (gtk_window_has_group(GTK_WINDOW(icon_view->priv->search_window)))
            gtk_window_group_remove_window(
                gtk_window_get_group(GTK_WINDOW(icon_view->priv->search_window)),
                GTK_WINDOW(icon_view->priv->search_window));
        return;
    }

    /* allocate a new search window */
    icon_view->priv->search_window = gtk_window_new(GTK_WINDOW_POPUP);
    if (gtk_window_has_group(GTK_WINDOW(toplevel)))
        gtk_window_group_add_window(gtk_window_get_group(GTK_WINDOW(toplevel)),
                                    GTK_WINDOW(icon_view->priv->search_window));
    gtk_window_set_modal(GTK_WINDOW(icon_view->priv->search_window), TRUE);
    gtk_window_set_screen(GTK_WINDOW(icon_view->priv->search_window),
                          gtk_widget_get_screen(GTK_WIDGET(icon_view)));

    /* attach the popup window to the toplevel parent (only needed on wayland).
     * see https://bugzilla.xfce.org/show_bug.cgi?id=16768
     */
    gtk_window_set_transient_for(GTK_WINDOW(icon_view->priv->search_window), GTK_WINDOW(toplevel));

    /* connect signal handlers */
    g_signal_connect(G_OBJECT(icon_view->priv->search_window),
                     "delete-event",
                     G_CALLBACK(exo_icon_view_search_delete_event),
                     icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->search_window),
                     "scroll-event",
                     G_CALLBACK(exo_icon_view_search_scroll_event),
                     icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->search_window),
                     "key-press-event",
                     G_CALLBACK(exo_icon_view_search_key_press_event),
                     icon_view);
    g_signal_connect(G_OBJECT(icon_view->priv->search_window),
                     "button-press-event",
                     G_CALLBACK(exo_icon_view_search_button_press_event),
                     icon_view);

    /* allocate the frame widget */
    frame = g_object_new(GTK_TYPE_FRAME, "shadow-type", GTK_SHADOW_ETCHED_IN, NULL);
    gtk_container_add(GTK_CONTAINER(icon_view->priv->search_window), frame);
    gtk_widget_show(frame);

    /* allocate the vertical box */
#if (GTK_MAJOR_VERSION == 3)
    vbox = g_object_new(GTK_TYPE_BOX,
                        "orientation",
                        GTK_ORIENTATION_VERTICAL,
                        "border-width",
                        3,
                        NULL);
#elif (GTK_MAJOR_VERSION == 2)
    vbox = g_object_new(GTK_TYPE_VBOX, "border-width", 3, NULL);
#endif
    gtk_container_add(GTK_CONTAINER(frame), vbox);
    gtk_widget_show(vbox);

    /* allocate the search entry widget */
    icon_view->priv->search_entry = gtk_entry_new();
#if (GTK_MAJOR_VERSION == 3)
    gtk_entry_set_input_hints(GTK_ENTRY(icon_view->priv->search_entry), GTK_INPUT_HINT_NO_EMOJI);
#endif
    g_signal_connect(G_OBJECT(icon_view->priv->search_entry),
                     "activate",
                     G_CALLBACK(exo_icon_view_search_activate),
                     icon_view);
#if (GTK_MAJOR_VERSION == 3)
    g_signal_connect(G_OBJECT(GTK_ENTRY(icon_view->priv->search_entry)),
                     "preedit-changed",
                     G_CALLBACK(exo_icon_view_search_preedit_changed),
                     icon_view);
#elif (GTK_MAJOR_VERSION == 2)
    g_signal_connect(G_OBJECT(GTK_ENTRY(icon_view->priv->search_entry)->im_context),
                     "preedit-changed",
                     G_CALLBACK(exo_icon_view_search_preedit_changed),
                     icon_view);
#endif
    gtk_box_pack_start(GTK_BOX(vbox), icon_view->priv->search_entry, TRUE, TRUE, 0);
    gtk_widget_realize(icon_view->priv->search_entry);
    gtk_widget_show(icon_view->priv->search_entry);
}

static void exo_icon_view_search_init(GtkWidget* search_entry, ExoIconView* icon_view)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    const char* text;
    int length;
    int count = 0;

    _exo_return_if_fail(GTK_IS_ENTRY(search_entry));
    _exo_return_if_fail(EXO_IS_ICON_VIEW(icon_view));

    /* determine the current text for the search entry */
    text = gtk_entry_get_text(GTK_ENTRY(search_entry));
    if (G_UNLIKELY(text == NULL))
        return;

    /* unselect all items */
    exo_icon_view_unselect_all(icon_view);

    /* renew the flush timeout */
    if ((icon_view->priv->search_timeout_id != 0))
    {
        /* drop the previous timeout */
        g_source_remove(icon_view->priv->search_timeout_id);

        /* schedule a new timeout */
        icon_view->priv->search_timeout_id =
            g_timeout_add_full(G_PRIORITY_LOW,
                               EXO_ICON_VIEW_SEARCH_DIALOG_TIMEOUT,
                               exo_icon_view_search_timeout,
                               icon_view,
                               exo_icon_view_search_timeout_destroy);
    }

    /* verify that we have a search text */
    length = strlen(text);
    if (length < 1)
        return;

    /* verify that we have a valid model */
    model = exo_icon_view_get_model(icon_view);
    if (G_UNLIKELY(model == NULL))
        return;

    /* start the interactive search */
    if (gtk_tree_model_get_iter_first(model, &iter))
    {
        /* let's see if we have a match */
        if (exo_icon_view_search_iter(icon_view, model, &iter, text, &count, 1))
            icon_view->priv->search_selected_iter = 1;
    }
}

static bool exo_icon_view_search_iter(ExoIconView* icon_view, GtkTreeModel* model,
                                      GtkTreeIter* iter, const char* text, int* count, int n)
{
    GtkTreePath* path;

    _exo_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);
    _exo_return_val_if_fail(GTK_IS_TREE_MODEL(model), FALSE);
    _exo_return_val_if_fail(count != NULL, FALSE);

    /* search for a matching item */
    do
    {
        if (!(*icon_view->priv->search_equal_func)(model,
                                                   icon_view->priv->search_column,
                                                   text,
                                                   iter,
                                                   icon_view->priv->search_equal_data))
        {
            (*count) += 1;
            if (*count == n)
            {
                /* place cursor on the item and select it */
                path = gtk_tree_model_get_path(model, iter);
                exo_icon_view_select_path(icon_view, path);
                exo_icon_view_set_cursor(icon_view, path, NULL, FALSE);
                gtk_tree_path_free(path);
                return TRUE;
            }
        }
    } while (gtk_tree_model_iter_next(model, iter));

    /* no match */
    return FALSE;
}

static void exo_icon_view_search_move(GtkWidget* widget, ExoIconView* icon_view, bool move_up)
{
    GtkTreeModel* model;
    const char* text;
    GtkTreeIter iter;
    bool retval;
    int length;
    int count = 0;

    /* determine the current text for the search entry */
    text = gtk_entry_get_text(GTK_ENTRY(icon_view->priv->search_entry));
    if (G_UNLIKELY(text == NULL))
        return;

    /* if we already selected the first item, we cannot go up */
    if (move_up && icon_view->priv->search_selected_iter == 1)
        return;

    /* determine the length of the search text */
    length = strlen(text);
    if (G_UNLIKELY(length < 1))
        return;

    /* unselect all items */
    exo_icon_view_unselect_all(icon_view);

    /* verify that we have a valid model */
    model = exo_icon_view_get_model(icon_view);
    if (G_UNLIKELY(model == NULL))
        return;

    /* determine the iterator to the first item */
    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    /* first attempt to search */
    retval = exo_icon_view_search_iter(icon_view,
                                       model,
                                       &iter,
                                       text,
                                       &count,
                                       move_up ? (icon_view->priv->search_selected_iter - 1)
                                               : (icon_view->priv->search_selected_iter + 1));

    /* check if we found something */
    if (G_LIKELY(retval))
    {
        /* match found */
        icon_view->priv->search_selected_iter += move_up ? -1 : 1;
    }
    else
    {
        /* return to old iter */
        if (gtk_tree_model_get_iter_first(model, &iter))
        {
            count = 0;
            exo_icon_view_search_iter(icon_view,
                                      model,
                                      &iter,
                                      text,
                                      &count,
                                      icon_view->priv->search_selected_iter);
        }
    }
}

#if (GTK_MAJOR_VERSION == 3)
static void exo_icon_view_search_preedit_changed(GtkEntry* entry, char* preedit,
#else
static void exo_icon_view_search_preedit_changed(GtkIMContext* im_context,
#endif
                                                 ExoIconView* icon_view)
{
    icon_view->priv->search_imcontext_changed = TRUE;

    /* re-register the search timeout */
    if (G_LIKELY(icon_view->priv->search_timeout_id != 0))
    {
        g_source_remove(icon_view->priv->search_timeout_id);
        icon_view->priv->search_timeout_id =
            g_timeout_add_full(G_PRIORITY_LOW,
                               EXO_ICON_VIEW_SEARCH_DIALOG_TIMEOUT,
                               exo_icon_view_search_timeout,
                               icon_view,
                               exo_icon_view_search_timeout_destroy);
    }
}

static bool exo_icon_view_search_start(ExoIconView* icon_view, bool keybinding)
{
    GTypeClass* klass;

    /* check if typeahead is enabled */
    if (G_UNLIKELY(!icon_view->priv->enable_search && !keybinding))
        return FALSE;

    /* check if we already display the search window */
    if (icon_view->priv->search_window != NULL &&
        gtk_widget_get_visible(icon_view->priv->search_window))
        return TRUE;

    /* we only start interactive search if we have focus,
     * we don't want to start interactive search if one of
     * our children has the focus.
     */
    if (!gtk_widget_has_focus(GTK_WIDGET(icon_view)))
        return FALSE;

    /* verify that we have a search column */
    if (G_UNLIKELY(icon_view->priv->search_column < 0))
        return FALSE;

    exo_icon_view_search_ensure_directory(icon_view);

    /* clear search entry if we were started by a keybinding */
    if (G_UNLIKELY(keybinding))
        gtk_entry_set_text(GTK_ENTRY(icon_view->priv->search_entry), "");

    /* determine the position for the search dialog */
    (*icon_view->priv->search_position_func)(icon_view,
                                             icon_view->priv->search_window,
                                             icon_view->priv->search_position_data);

    /* display the search dialog */
    gtk_widget_show(icon_view->priv->search_window);

    /* connect "changed" signal for the entry */
    if (G_UNLIKELY(icon_view->priv->search_entry_changed_id == 0))
    {
        icon_view->priv->search_entry_changed_id =
            g_signal_connect(G_OBJECT(icon_view->priv->search_entry),
                             "changed",
                             G_CALLBACK(exo_icon_view_search_init),
                             icon_view);
    }

    /* start the search timeout */
    icon_view->priv->search_timeout_id = g_timeout_add_full(G_PRIORITY_LOW,
                                                            EXO_ICON_VIEW_SEARCH_DIALOG_TIMEOUT,
                                                            exo_icon_view_search_timeout,
                                                            icon_view,
                                                            exo_icon_view_search_timeout_destroy);

    /* grab focus will select all the text, we don't want that to happen, so we
     * call the parent instance and bypass the selection change. This is probably
     * really hackish, but GtkTreeView does it as well *hrhr*
     */
    klass = g_type_class_peek_parent(GTK_ENTRY_GET_CLASS(icon_view->priv->search_entry));
    (*GTK_WIDGET_CLASS(klass)->grab_focus)(icon_view->priv->search_entry);

    /* send focus-in event */
    _exo_gtk_widget_send_focus_change(icon_view->priv->search_entry, TRUE);

    /* search first matching iter */
    exo_icon_view_search_init(icon_view->priv->search_entry, icon_view);

    return TRUE;
}

static bool exo_icon_view_search_equal_func(GtkTreeModel* model, int column, const char* key,
                                            GtkTreeIter* iter, void* user_data)
{
    const char* str;
    bool retval = TRUE;
    GValue transformed = {
        0,
    };
    GValue value = {
        0,
    };
    char* case_normalized_string = NULL;
    char* case_normalized_key = NULL;
    char* normalized_string;
    char* normalized_key;

    /* determine the value for the column/iter */
    gtk_tree_model_get_value(model, iter, column, &value);

    /* try to transform the value to a string */
    g_value_init(&transformed, G_TYPE_STRING);
    if (!g_value_transform(&value, &transformed))
    {
        g_value_unset(&value);
        return TRUE;
    }
    g_value_unset(&value);

    /* check if we have a string value */
    str = g_value_get_string(&transformed);
    if (G_UNLIKELY(str == NULL))
    {
        g_value_unset(&transformed);
        return TRUE;
    }

    /* normalize the string and the key */
    normalized_string = g_utf8_normalize(str, -1, G_NORMALIZE_ALL);
    normalized_key = g_utf8_normalize(key, -1, G_NORMALIZE_ALL);

    /* check if we have normalized both string */
    if (G_LIKELY(normalized_string != NULL && normalized_key != NULL))
    {
        case_normalized_string = g_utf8_casefold(normalized_string, -1);
        case_normalized_key = g_utf8_casefold(normalized_key, -1);

        /* compare the casefolded strings */
        if (!strncmp(case_normalized_key, case_normalized_string, strlen(case_normalized_key)))
            retval = FALSE;
    }

    /* cleanup */
    g_free(case_normalized_string);
    g_free(case_normalized_key);
    g_value_unset(&transformed);
    g_free(normalized_string);
    g_free(normalized_key);

    return retval;
}

static void exo_icon_view_search_position_func(ExoIconView* icon_view, GtkWidget* search_dialog,
                                               void* user_data)
{
    GtkRequisition requisition;
    GdkWindow* view_window = gtk_widget_get_window(GTK_WIDGET(icon_view));
    int view_width, view_height;
    int view_x, view_y;
    int x, y;

    GdkDisplay* display;
    GdkRectangle monitor_dimensions;
#if (GTK_MAJOR_VERSION == 3)
    GdkMonitor* monitor;
#elif (GTK_MAJOR_VERSION == 2)
    GdkScreen* screen;
    int monitor_num;
#endif

    /* make sure the search dialog is realized */
    gtk_widget_realize(search_dialog);

    gdk_window_get_origin(view_window, &view_x, &view_y);
    view_width = gdk_window_get_width(view_window);
    view_height = gdk_window_get_height(view_window);

#if (GTK_MAJOR_VERSION == 3)
    gtk_widget_get_preferred_width(search_dialog, NULL, &requisition.width);
    gtk_widget_get_preferred_height(search_dialog, NULL, &requisition.height);
#else
    gtk_widget_size_request(search_dialog, &requisition);
#endif

    x = view_x + view_width - requisition.width;
    y = view_y + view_height;

    display = gdk_window_get_display(view_window);
    if (display)
    {
#if (GTK_MAJOR_VERSION == 3)
        monitor = gdk_display_get_monitor_at_window(display, view_window);
        if (monitor)
        {
            gdk_monitor_get_geometry(monitor, &monitor_dimensions);
            if (y + requisition.height > monitor_dimensions.height)
                y = monitor_dimensions.height - requisition.height;
        }
#elif (GTK_MAJOR_VERSION == 2)
        screen = gdk_display_get_default_screen(display);
        monitor_num = gdk_screen_get_monitor_at_window(screen, view_window);
        if (monitor_num >= 0)
        {
            gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor_dimensions);
            if (y + requisition.height > monitor_dimensions.height)
                y = monitor_dimensions.height - requisition.height;
        }
#endif
    }
    gtk_window_move(GTK_WINDOW(search_dialog), x, y);
}

static bool exo_icon_view_search_button_press_event(GtkWidget* widget, GdkEventButton* event,
                                                    ExoIconView* icon_view)
{
    _exo_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
    _exo_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);

    /* hide the search dialog */
    exo_icon_view_search_dialog_hide(widget, icon_view);

    if (event->window == icon_view->priv->bin_window)
        exo_icon_view_button_press_event(GTK_WIDGET(icon_view), event);

    return TRUE;
}

static bool exo_icon_view_search_delete_event(GtkWidget* widget, GdkEventAny* event,
                                              ExoIconView* icon_view)
{
    _exo_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
    _exo_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);

    /* hide the search dialog */
    exo_icon_view_search_dialog_hide(widget, icon_view);

    return TRUE;
}

static bool exo_icon_view_search_key_press_event(GtkWidget* widget, GdkEventKey* event,
                                                 ExoIconView* icon_view)
{
    bool retval = FALSE;
    int nonlatin_key = 0;

    _exo_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
    _exo_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);

    /* close window and cancel the search */
    if (event->keyval == GDK_KEY_Escape || event->keyval == GDK_KEY_Tab)
    {
        exo_icon_view_search_dialog_hide(widget, icon_view);
        return TRUE;
    }

    // sfm need to transpose nonlatin keyboard layout ?
    if (!((GDK_KEY_0 <= event->keyval && event->keyval <= GDK_KEY_9) ||
          (GDK_KEY_A <= event->keyval && event->keyval <= GDK_KEY_Z) ||
          (GDK_KEY_a <= event->keyval && event->keyval <= GDK_KEY_z)))
    {
        nonlatin_key = event->keyval;
        transpose_nonlatin_keypress(event);
    }

    /* select previous matching iter */
    if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up)
    {
        exo_icon_view_search_move(widget, icon_view, TRUE);
        retval = TRUE;
    }

    if (((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
         (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) &&
        (event->keyval == GDK_KEY_g || event->keyval == GDK_KEY_G))
    {
        exo_icon_view_search_move(widget, icon_view, TRUE);
        retval = TRUE;
    }

    /* select next matching iter */
    if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down)
    {
        exo_icon_view_search_move(widget, icon_view, FALSE);
        retval = TRUE;
    }

    if (((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == GDK_CONTROL_MASK) &&
        (event->keyval == GDK_KEY_g || event->keyval == GDK_KEY_G))
    {
        exo_icon_view_search_move(widget, icon_view, FALSE);
        retval = TRUE;
    }

    /* renew the flush timeout */
    if (retval && (icon_view->priv->search_timeout_id != 0))
    {
        /* drop the previous timeout */
        g_source_remove(icon_view->priv->search_timeout_id);

        /* schedule a new timeout */
        icon_view->priv->search_timeout_id =
            g_timeout_add_full(G_PRIORITY_LOW,
                               EXO_ICON_VIEW_SEARCH_DIALOG_TIMEOUT,
                               exo_icon_view_search_timeout,
                               icon_view,
                               exo_icon_view_search_timeout_destroy);
    }

    if (!retval && nonlatin_key != 0)
        event->keyval = nonlatin_key;
    return retval;
}

static bool exo_icon_view_search_scroll_event(GtkWidget* widget, GdkEventScroll* event,
                                              ExoIconView* icon_view)
{
    bool retval = TRUE;

    _exo_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
    _exo_return_val_if_fail(EXO_IS_ICON_VIEW(icon_view), FALSE);

    if (event->direction == GDK_SCROLL_UP)
        exo_icon_view_search_move(widget, icon_view, TRUE);
    else if (event->direction == GDK_SCROLL_DOWN)
        exo_icon_view_search_move(widget, icon_view, FALSE);
    else
        retval = FALSE;

    return retval;
}

static bool exo_icon_view_search_timeout(void* user_data)
{
    ExoIconView* icon_view = EXO_ICON_VIEW(user_data);

    exo_icon_view_search_dialog_hide(icon_view->priv->search_window, icon_view);

    return FALSE;
}

static void exo_icon_view_search_timeout_destroy(void* user_data)
{
    EXO_ICON_VIEW(user_data)->priv->search_timeout_id = 0;
}

bool exo_icon_view_is_rubber_banding_active(ExoIconView* icon_view) // sfm
{
    return icon_view->priv->doing_rubberband;
}
