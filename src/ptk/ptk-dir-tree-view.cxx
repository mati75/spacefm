/**
 * Copyright (C) 2006 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <string>
#include <string_view>

#include <filesystem>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "ptk/ptk-dir-tree.hxx"
#include "ptk/ptk-file-menu.hxx"
#include "ptk/ptk-file-task.hxx"

#include "ptk/ptk-dir-tree-view.hxx"

static GQuark dir_tree_view_data = 0;

static GtkTreeModel* get_dir_tree_model();

static void on_dir_tree_view_row_expanded(GtkTreeView* treeview, GtkTreeIter* iter,
                                          GtkTreePath* path, void* user_data);

static void on_dir_tree_view_row_collapsed(GtkTreeView* treeview, GtkTreeIter* iter,
                                           GtkTreePath* path, void* user_data);

static bool on_dir_tree_view_button_press(GtkWidget* view, GdkEventButton* evt,
                                          PtkFileBrowser* browser);

static bool on_dir_tree_view_key_press(GtkWidget* view, GdkEventKey* evt, PtkFileBrowser* browser);

static bool sel_func(GtkTreeSelection* selection, GtkTreeModel* model, GtkTreePath* path,
                     bool path_currently_selected, void* data);

/*  Drag & Drop/Clipboard targets  */
static GtkTargetEntry drag_targets[] = {{ztd::strdup("text/uri-list"), 0, 0}};

// MOD drag n drop...
static void on_dir_tree_view_drag_data_received(GtkWidget* widget, GdkDragContext* drag_context,
                                                i32 x, i32 y, GtkSelectionData* sel_data, u32 info,
                                                u32 time, void* user_data);
static bool on_dir_tree_view_drag_motion(GtkWidget* widget, GdkDragContext* drag_context, i32 x,
                                         i32 y, u32 time, PtkFileBrowser* file_browser);

static bool on_dir_tree_view_drag_leave(GtkWidget* widget, GdkDragContext* drag_context, u32 time,
                                        PtkFileBrowser* file_browser);

static bool on_dir_tree_view_drag_drop(GtkWidget* widget, GdkDragContext* drag_context, i32 x,
                                       i32 y, u32 time, PtkFileBrowser* file_browser);

inline constexpr GdkDragAction GDK_ACTION_ALL =
    GdkDragAction(GdkDragAction::GDK_ACTION_MOVE | GdkDragAction::GDK_ACTION_COPY |
                  GdkDragAction::GDK_ACTION_LINK);

static bool
filter_func(GtkTreeModel* model, GtkTreeIter* iter, void* data)
{
    vfs::file_info file;
    GtkTreeView* view = GTK_TREE_VIEW(data);
    const bool show_hidden =
        GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(view), dir_tree_view_data));

    if (show_hidden)
    {
        return true;
    }

    gtk_tree_model_get(model, iter, ptk::dir_tree::column::info, &file, -1);
    if (file)
    {
        if (file->is_hidden())
        {
            vfs_file_info_unref(file);
            return false;
        }
        vfs_file_info_unref(file);
    }
    return true;
}
static void
on_destroy(GtkWidget* w)
{
    do
    {
    } while (g_source_remove_by_user_data(w));
}
/* Create a new dir tree view */
GtkWidget*
ptk_dir_tree_view_new(PtkFileBrowser* browser, bool show_hidden)
{
    GtkTreeViewColumn* col;
    GtkCellRenderer* renderer;
    GtkTreeModel* model;
    GtkTreeSelection* selection = nullptr;
    GtkTreePath* tree_path;
    GtkTreeModel* filter;

    GtkTreeView* dir_tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
    gtk_tree_view_set_headers_visible(dir_tree_view, false);
    gtk_tree_view_set_enable_tree_lines(dir_tree_view, true);

    // MOD enabled DND   FIXME: Temporarily disable drag & drop since it does not work right now.
    /*    exo_icon_view_enable_model_drag_dest (
                EXO_ICON_VIEW( dir_tree_view ),
                drag_targets, G_N_ELEMENTS( drag_targets ), GDK_ACTION_ALL ); */
    gtk_tree_view_enable_model_drag_dest(dir_tree_view,
                                         drag_targets,
                                         sizeof(drag_targets) / sizeof(GtkTargetEntry),
                                         GdkDragAction(GdkDragAction::GDK_ACTION_MOVE |
                                                       GdkDragAction::GDK_ACTION_COPY |
                                                       GdkDragAction::GDK_ACTION_LINK));
    /*
        gtk_tree_view_enable_model_drag_source ( dir_tree_view,
                                                 ( GdkModifierType::GDK_CONTROL_MASK |
       GdkModifierType::GDK_BUTTON1_MASK | GdkModifierType::GDK_BUTTON3_MASK ), drag_targets,
       sizeof( drag_targets ) / sizeof( GtkTargetEntry ), GdkDragAction::GDK_ACTION_DEFAULT |
       GdkDragAction::GDK_ACTION_COPY | GdkDragAction::GDK_ACTION_MOVE |
       GdkDragAction::GDK_ACTION_LINK );
      */

    col = gtk_tree_view_column_new();

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, false);
    gtk_tree_view_column_set_attributes(col,
                                        renderer,
                                        "pixbuf",
                                        ptk::dir_tree::column::icon,
                                        "info",
                                        ptk::dir_tree::column::info,
                                        nullptr);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, true);
    gtk_tree_view_column_set_attributes(col,
                                        renderer,
                                        "text",
                                        ptk::dir_tree::column::disp_name,
                                        nullptr);

    gtk_tree_view_append_column(dir_tree_view, col);

    selection = gtk_tree_view_get_selection(dir_tree_view);
    gtk_tree_selection_set_select_function(selection,
                                           (GtkTreeSelectionFunc)sel_func,
                                           nullptr,
                                           nullptr);

    if (!dir_tree_view_data)
    {
        dir_tree_view_data = g_quark_from_static_string("show_hidden");
    }
    g_object_set_qdata(G_OBJECT(dir_tree_view), dir_tree_view_data, GINT_TO_POINTER(show_hidden));
    model = get_dir_tree_model();
    filter = gtk_tree_model_filter_new(model, nullptr);
    g_object_unref(G_OBJECT(model));
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),
                                           (GtkTreeModelFilterVisibleFunc)filter_func,
                                           dir_tree_view,
                                           nullptr);
    gtk_tree_view_set_model(dir_tree_view, filter);
    g_object_unref(G_OBJECT(filter));

    g_signal_connect(dir_tree_view,
                     "row-expanded",
                     G_CALLBACK(on_dir_tree_view_row_expanded),
                     model);

    g_signal_connect_data(dir_tree_view,
                          "row-collapsed",
                          G_CALLBACK(on_dir_tree_view_row_collapsed),
                          model,
                          nullptr,
                          G_CONNECT_AFTER);

    g_signal_connect(dir_tree_view,
                     "button-press-event",
                     G_CALLBACK(on_dir_tree_view_button_press),
                     browser);

    g_signal_connect(dir_tree_view,
                     "key-press-event",
                     G_CALLBACK(on_dir_tree_view_key_press),
                     browser);

    // MOD drag n drop
    g_signal_connect((void*)dir_tree_view,
                     "drag-data-received",
                     G_CALLBACK(on_dir_tree_view_drag_data_received),
                     browser);
    g_signal_connect((void*)dir_tree_view,
                     "drag-motion",
                     G_CALLBACK(on_dir_tree_view_drag_motion),
                     browser);

    g_signal_connect((void*)dir_tree_view,
                     "drag-leave",
                     G_CALLBACK(on_dir_tree_view_drag_leave),
                     browser);

    g_signal_connect((void*)dir_tree_view,
                     "drag-drop",
                     G_CALLBACK(on_dir_tree_view_drag_drop),
                     browser);

    tree_path = gtk_tree_path_new_first();
    gtk_tree_view_expand_row(dir_tree_view, tree_path, false);
    gtk_tree_path_free(tree_path);

    g_signal_connect(dir_tree_view, "destroy", G_CALLBACK(on_destroy), nullptr);
    return GTK_WIDGET(dir_tree_view);
}

bool
ptk_dir_tree_view_chdir(GtkTreeView* dir_tree_view, const std::filesystem::path& path)
{
    GtkTreeIter it;
    GtkTreeIter parent_it;
    GtkTreePath* tree_path = nullptr;

    if (!ztd::startswith(path.string(), "/"))
    {
        return false;
    }

    GtkTreeModel* model = gtk_tree_view_get_model(dir_tree_view);

    if (!gtk_tree_model_iter_children(model, &parent_it, nullptr))
    {
        return false;
    }

    // special case: root dir
    if (std::filesystem::equivalent(path, "/"))
    {
        it = parent_it;
        tree_path = gtk_tree_model_get_path(model, &parent_it);

        gtk_tree_selection_select_path(gtk_tree_view_get_selection(dir_tree_view), tree_path);

        gtk_tree_view_scroll_to_cell(dir_tree_view, tree_path, nullptr, false, 0.5, 0.5);

        gtk_tree_path_free(tree_path);

        return true;
    }

    const std::vector<std::string> dirs = ztd::split(path.string(), "/");
    for (const std::string_view dir : dirs)
    {
        if (dir.empty())
        {
            continue; // first item will be empty because of how ztd::split works
        }

        if (!gtk_tree_model_iter_children(model, &it, &parent_it))
        {
            return false;
        }

        bool found = false;
        vfs::file_info file;
        do
        {
            gtk_tree_model_get(model, &it, ptk::dir_tree::column::info, &file, -1);
            if (!file)
            {
                continue;
            }

            if (ztd::same(file->name(), dir))
            {
                tree_path = gtk_tree_model_get_path(model, &it);

                if (dir[1])
                {
                    gtk_tree_view_expand_row(dir_tree_view, tree_path, false);
                    gtk_tree_model_get_iter(model, &parent_it, tree_path);
                }
                found = true;
                vfs_file_info_unref(file);
                break;
            }
            vfs_file_info_unref(file);
        } while (gtk_tree_model_iter_next(model, &it));

        if (!found)
        {
            return false; /* Error! */
        }

        if (tree_path && dir[1])
        {
            gtk_tree_path_free(tree_path);
            tree_path = nullptr;
        }
    }

    gtk_tree_selection_select_path(gtk_tree_view_get_selection(dir_tree_view), tree_path);

    gtk_tree_view_scroll_to_cell(dir_tree_view, tree_path, nullptr, false, 0.5, 0.5);

    gtk_tree_path_free(tree_path);

    return true;
}

/* FIXME: should this API be put here? Maybe it belongs to prk-dir-tree.c */
char*
ptk_dir_view_get_dir_path(GtkTreeModel* model, GtkTreeIter* it)
{
    GtkTreeIter real_it;

    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &real_it, it);
    GtkTreeModel* tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
    return ptk_dir_tree_get_dir_path(PTK_DIR_TREE_REINTERPRET(tree), &real_it);
}

/* Return a newly allocated string containing path of current selected dir. */
char*
ptk_dir_tree_view_get_selected_dir(GtkTreeView* dir_tree_view)
{
    GtkTreeModel* model;
    GtkTreeIter it;

    GtkTreeSelection* selection = gtk_tree_view_get_selection(dir_tree_view);
    if (gtk_tree_selection_get_selected(selection, &model, &it))
    {
        return ptk_dir_view_get_dir_path(model, &it);
    }
    return nullptr;
}

static GtkTreeModel*
get_dir_tree_model()
{
    static PtkDirTree* dir_tree_model = nullptr;

    if (!dir_tree_model)
    {
        dir_tree_model = ptk_dir_tree_new();
        g_object_add_weak_pointer(G_OBJECT(dir_tree_model), (void**)GTK_WIDGET(&dir_tree_model));
    }
    else
    {
        g_object_ref(G_OBJECT(dir_tree_model));
    }
    return GTK_TREE_MODEL(dir_tree_model);
}

static bool
sel_func(GtkTreeSelection* selection, GtkTreeModel* model, GtkTreePath* path,
         bool path_currently_selected, void* data)
{
    (void)selection;
    (void)path_currently_selected;
    (void)data;
    GtkTreeIter it;
    vfs::file_info file;

    if (!gtk_tree_model_get_iter(model, &it, path))
    {
        return false;
    }
    gtk_tree_model_get(model, &it, ptk::dir_tree::column::info, &file, -1);
    if (!file)
    {
        return false;
    }
    vfs_file_info_unref(file);
    return true;
}

void
ptk_dir_tree_view_show_hidden_files(GtkTreeView* dir_tree_view, bool show_hidden)
{
    g_object_set_qdata(G_OBJECT(dir_tree_view), dir_tree_view_data, GINT_TO_POINTER(show_hidden));
    GtkTreeModel* filter = gtk_tree_view_get_model(dir_tree_view);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
}

static void
on_dir_tree_view_row_expanded(GtkTreeView* treeview, GtkTreeIter* iter, GtkTreePath* path,
                              void* user_data)
{
    GtkTreeIter real_it;
    GtkTreeModel* filter = gtk_tree_view_get_model(treeview);
    PtkDirTree* tree = PTK_DIR_TREE(user_data);
    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter), &real_it, iter);
    GtkTreePath* real_path =
        gtk_tree_model_filter_convert_path_to_child_path(GTK_TREE_MODEL_FILTER(filter), path);
    ptk_dir_tree_expand_row(tree, &real_it, real_path);
    gtk_tree_path_free(real_path);
}

static void
on_dir_tree_view_row_collapsed(GtkTreeView* treeview, GtkTreeIter* iter, GtkTreePath* path,
                               void* user_data)
{
    GtkTreeIter real_it;
    GtkTreeModel* filter = gtk_tree_view_get_model(treeview);
    PtkDirTree* tree = PTK_DIR_TREE(user_data);
    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter), &real_it, iter);
    GtkTreePath* real_path =
        gtk_tree_model_filter_convert_path_to_child_path(GTK_TREE_MODEL_FILTER(filter), path);
    ptk_dir_tree_collapse_row(tree, &real_it, real_path);
    gtk_tree_path_free(real_path);
}

static bool
on_dir_tree_view_button_press(GtkWidget* view, GdkEventButton* event, PtkFileBrowser* file_browser)
{
    GtkTreePath* tree_path;
    GtkTreeViewColumn* tree_col;
    GtkTreeIter it;

    if (event->type == GdkEventType::GDK_BUTTON_PRESS && (event->button == 1 || event->button == 3))
    {
        // middle click 2 handled in ptk-file-browser.c on_dir_tree_button_press
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),
                                          static_cast<i32>(event->x),
                                          static_cast<i32>(event->y),
                                          &tree_path,
                                          &tree_col,
                                          nullptr,
                                          nullptr))
        {
            if (gtk_tree_model_get_iter(model, &it, tree_path))
            {
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), tree_path, tree_col, false);

                if (event->button == 3)
                {
                    // right click
                    const std::filesystem::path dir_path =
                        ptk_dir_tree_view_get_selected_dir(GTK_TREE_VIEW(view));
                    if (file_browser->chdir(dir_path, ptk::file_browser::chdir_mode::add_history))
                    {
                        /* show right-click menu
                         * This simulates a right-click in the file list when
                         * no files are selected (even if some are) since
                         * actions are to be taken on the dir itself. */
                        GtkWidget* popup = ptk_file_menu_new(file_browser);
                        if (popup)
                        {
                            gtk_menu_popup_at_pointer(GTK_MENU(popup), nullptr);
                        }
                        gtk_tree_path_free(tree_path);
                        return true;
                    }
                }
                else
                {
                    gtk_tree_view_row_activated(GTK_TREE_VIEW(view), tree_path, tree_col);
                }
            }
            gtk_tree_path_free(tree_path);
        }
    }
    else if (event->type == GdkEventType::GDK_2BUTTON_PRESS && event->button == 1)
    {
        // f64 click - expand/collapse
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),
                                          static_cast<i32>(event->x),
                                          static_cast<i32>(event->y),
                                          &tree_path,
                                          nullptr,
                                          nullptr,
                                          nullptr))
        {
            if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(view), tree_path))
            {
                gtk_tree_view_collapse_row(GTK_TREE_VIEW(view), tree_path);
            }
            else
            {
                gtk_tree_view_expand_row(GTK_TREE_VIEW(view), tree_path, false);
            }
            gtk_tree_path_free(tree_path);
            return true;
        }
    }
    return false;
}

static bool
on_dir_tree_view_key_press(GtkWidget* view, GdkEventKey* event, PtkFileBrowser* file_browser)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        return false;
    }

    const i32 keymod =
        (event->state & (GdkModifierType::GDK_SHIFT_MASK | GdkModifierType::GDK_CONTROL_MASK |
                         GdkModifierType::GDK_MOD1_MASK | GdkModifierType::GDK_SUPER_MASK |
                         GdkModifierType::GDK_HYPER_MASK | GdkModifierType::GDK_META_MASK));

    GtkTreePath* path = gtk_tree_model_get_path(model, &iter);

    switch (event->keyval)
    {
        case GDK_KEY_Left:
        {
            if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(view), path))
            {
                gtk_tree_view_collapse_row(GTK_TREE_VIEW(view), path);
            }
            else if (gtk_tree_path_up(path))
            {
                gtk_tree_selection_select_path(selection, path);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), path, nullptr, false);
            }
            else
            {
                gtk_tree_path_free(path);
                return false;
            }
            break;
        }
        case GDK_KEY_Right:
        {
            if (!gtk_tree_view_row_expanded(GTK_TREE_VIEW(view), path))
            {
                gtk_tree_view_expand_row(GTK_TREE_VIEW(view), path, false);
            }
            else
            {
                gtk_tree_path_down(path);
                gtk_tree_selection_select_path(selection, path);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), path, nullptr, false);
            }
            break;
        }
        case GDK_KEY_F10:
        case GDK_KEY_Menu:
        {
            if (event->keyval == GDK_KEY_F10 && keymod != GdkModifierType::GDK_SHIFT_MASK)
            {
                gtk_tree_path_free(path);
                return false;
            }

            const std::filesystem::path dir_path =
                ptk_dir_tree_view_get_selected_dir(GTK_TREE_VIEW(view));
            if (file_browser->chdir(dir_path, ptk::file_browser::chdir_mode::add_history))
            {
                /* show right-click menu
                 * This simulates a right-click in the file list when
                 * no files are selected (even if some are) since
                 * actions are to be taken on the dir itself. */
                GtkWidget* popup = ptk_file_menu_new(file_browser);
                if (popup)
                {
                    gtk_menu_popup_at_pointer(GTK_MENU(popup), nullptr);
                }
            }
            break;
        }
        default:
            gtk_tree_path_free(path);
            return false;
    }
    gtk_tree_path_free(path);
    return true;
}

// MOD drag n drop
static char*
dir_tree_view_get_drop_dir(GtkWidget* view, i32 x, i32 y)
{
    GtkTreePath* tree_path = nullptr;
    GtkTreeIter it;
    vfs::file_info file;
    char* dest_path = nullptr;

    // if drag is in progress, get the dest row path
    gtk_tree_view_get_drag_dest_row(GTK_TREE_VIEW(view), &tree_path, nullptr);
    if (!tree_path)
    {
        // no drag in progress, get drop path
        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),
                                           x,
                                           y,
                                           &tree_path,
                                           nullptr,
                                           nullptr,
                                           nullptr))
        {
            tree_path = nullptr;
        }
    }
    if (tree_path)
    {
        GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        if (gtk_tree_model_get_iter(model, &it, tree_path))
        {
            gtk_tree_model_get(model, &it, ptk::dir_tree::column::info, &file, -1);
            if (file)
            {
                dest_path = ptk_dir_view_get_dir_path(model, &it);
                vfs_file_info_unref(file);
            }
        }
        gtk_tree_path_free(tree_path);
    }
    return dest_path;
}

static void
on_dir_tree_view_drag_data_received(GtkWidget* widget, GdkDragContext* drag_context, i32 x, i32 y,
                                    GtkSelectionData* sel_data, u32 info, u32 time,
                                    void* user_data) // MOD added
{
    (void)info;
    PtkFileBrowser* file_browser = PTK_FILE_BROWSER(user_data);

    /*  Do not call the default handler  */
    g_signal_stop_emission_by_name(widget, "drag-data-received");

    if ((gtk_selection_data_get_length(sel_data) >= 0) &&
        (gtk_selection_data_get_format(sel_data) == 8))
    {
        char* dest_dir = dir_tree_view_get_drop_dir(widget, x, y);
        if (dest_dir)
        {
            char** list;
            char** puri;
            puri = list = gtk_selection_data_get_uris(sel_data);
            if (file_browser->pending_drag_status_tree())
            {
                // We only want to update drag status, not really want to drop
                const auto dest_statbuf = ztd::statx(dest_dir);
                if (dest_statbuf)
                {
                    if (file_browser->drag_source_dev_tree_ == 0)
                    {
                        file_browser->drag_source_dev_tree_ = dest_statbuf.dev();
                        for (; *puri; ++puri)
                        {
                            const std::filesystem::path file_path = Glib::filename_from_uri(*puri);

                            const auto statbuf = ztd::statx(file_path);
                            if (statbuf && statbuf.dev() != dest_statbuf.dev())
                            {
                                file_browser->drag_source_dev_tree_ = statbuf.dev();
                                break;
                            }
                        }
                    }
                    if (file_browser->drag_source_dev_tree_ != dest_statbuf.dev())
                    { // src and dest are on different devices
                        gdk_drag_status(drag_context, GdkDragAction::GDK_ACTION_COPY, time);
                    }
                    else
                    {
                        gdk_drag_status(drag_context, GdkDragAction::GDK_ACTION_MOVE, time);
                    }
                }
                else
                { // stat failed
                    gdk_drag_status(drag_context, GdkDragAction::GDK_ACTION_COPY, time);
                }

                std::free(dest_dir);
                g_strfreev(list);
                file_browser->pending_drag_status_tree(false);
                return;
            }

            if (puri)
            {
                std::vector<std::filesystem::path> file_list;
                if ((gdk_drag_context_get_selected_action(drag_context) &
                     (GdkDragAction::GDK_ACTION_MOVE | GdkDragAction::GDK_ACTION_COPY |
                      GdkDragAction::GDK_ACTION_LINK)) == 0)
                {
                    gdk_drag_status(drag_context, GdkDragAction::GDK_ACTION_MOVE, time);
                }
                gtk_drag_finish(drag_context, true, false, time);

                for (; *puri; ++puri)
                {
                    std::filesystem::path file_path;
                    if (**puri == '/')
                    {
                        file_path = *puri;
                    }
                    else
                    {
                        file_path = Glib::filename_from_uri(*puri);
                    }

                    file_list.emplace_back(file_path);
                }
                g_strfreev(list);

                vfs::file_task_type file_action;
                switch (gdk_drag_context_get_selected_action(drag_context))
                {
                    case GdkDragAction::GDK_ACTION_COPY:
                        file_action = vfs::file_task_type::copy;
                        break;
                    case GdkDragAction::GDK_ACTION_MOVE:
                        file_action = vfs::file_task_type::move;
                        break;
                    case GdkDragAction::GDK_ACTION_LINK:
                        file_action = vfs::file_task_type::link;
                        break;
                    case GdkDragAction::GDK_ACTION_DEFAULT:
                    case GdkDragAction::GDK_ACTION_PRIVATE:
                    case GdkDragAction::GDK_ACTION_ASK:
                    default:
                        break;
                }
                if (!file_list.empty())
                {
                    /* Accept the drop and perform file actions */
                    {
                        GtkWidget* parent_win = gtk_widget_get_toplevel(GTK_WIDGET(file_browser));
                        PtkFileTask* ptask = ptk_file_task_new(file_action,
                                                               file_list,
                                                               dest_dir,
                                                               GTK_WINDOW(parent_win),
                                                               file_browser->task_view());
                        ptk_file_task_run(ptask);
                    }
                }
                std::free(dest_dir);
                gtk_drag_finish(drag_context, true, false, time);
                return;
            }
            std::free(dest_dir);
        }
        else
        {
            ztd::logger::warn("bad dest_dir in on_dir_tree_view_drag_data_received");
        }
    }
    /* If we are only getting drag status, not finished. */
    if (file_browser->pending_drag_status_tree())
    {
        gdk_drag_status(drag_context, GdkDragAction::GDK_ACTION_COPY, time);
        file_browser->pending_drag_status_tree(false);
        return;
    }
    gtk_drag_finish(drag_context, false, false, time);
}

static bool
on_dir_tree_view_drag_drop(GtkWidget* widget, GdkDragContext* drag_context, i32 x, i32 y, u32 time,
                           PtkFileBrowser* file_browser) // MOD added
{
    (void)x;
    (void)y;
    (void)file_browser;
    GdkAtom target = gdk_atom_intern("text/uri-list", false);

    /*  Do not call the default handler  */
    g_signal_stop_emission_by_name(widget, "drag-drop");

    gtk_drag_get_data(widget, drag_context, target, time);
    return true;
}

static bool
on_dir_tree_view_drag_motion(GtkWidget* widget, GdkDragContext* drag_context, i32 x, i32 y,
                             u32 time,
                             PtkFileBrowser* file_browser) // MOD added
{
    (void)x;
    (void)y;
    GdkDragAction suggested_action;

    GtkTargetList* target_list = gtk_target_list_new(drag_targets, G_N_ELEMENTS(drag_targets));
    GdkAtom target = gtk_drag_dest_find_target(widget, drag_context, target_list);
    gtk_target_list_unref(target_list);

    if (target == GDK_NONE)
    {
        gdk_drag_status(drag_context, (GdkDragAction)0, time);
    }
    else
    {
        // Need to set suggested_action because default handler assumes copy
        /* Only 'move' is available. The user force move action by pressing Shift key */
        if ((gdk_drag_context_get_actions(drag_context) & GDK_ACTION_ALL) ==
            GdkDragAction::GDK_ACTION_MOVE)
        {
            suggested_action = GdkDragAction::GDK_ACTION_MOVE;
            /* Only 'copy' is available. The user force copy action by pressing Ctrl key */
        }
        else if ((gdk_drag_context_get_actions(drag_context) & GDK_ACTION_ALL) ==
                 GdkDragAction::GDK_ACTION_COPY)
        {
            suggested_action = GdkDragAction::GDK_ACTION_COPY;
            /* Only 'link' is available. The user force link action by pressing Shift+Ctrl key */
        }
        else if ((gdk_drag_context_get_actions(drag_context) & GDK_ACTION_ALL) ==
                 GdkDragAction::GDK_ACTION_LINK)
        {
            suggested_action = GdkDragAction::GDK_ACTION_LINK;
            /* Several different actions are available. We have to figure out a good default action.
             */
        }
        else
        {
            const i32 drag_action = xset_get_int(xset::name::drag_action, xset::var::x);
            if (drag_action == 1)
            {
                suggested_action = GdkDragAction::GDK_ACTION_COPY;
            }
            else if (drag_action == 2)
            {
                suggested_action = GdkDragAction::GDK_ACTION_MOVE;
            }
            else if (drag_action == 3)
            {
                suggested_action = GdkDragAction::GDK_ACTION_LINK;
            }
            else
            {
                // automatic
                file_browser->pending_drag_status_tree(true);
                gtk_drag_get_data(widget, drag_context, target, time);
                suggested_action = gdk_drag_context_get_selected_action(drag_context);
            }
        }
        /* hack to be able to call the default handler with the correct suggested_action */
        struct _GdkDragContext
        {
            GObject parent_instance;

            /*< private >*/
            GdkDragProtocol protocol;

            /* 1.0.6 per Teklad: _GdkDragContext appears to change between
             * different versions of GTK3 which causes the crash. It appears they
             * added/removed some variables from that struct.
             * https://github.com/IgnorantGuru/spacefm/issues/670 */
            GdkDisplay* display;

            bool is_source;
            GdkWindow* source_window;
            GdkWindow* dest_window;

            GList* targets;
            GdkDragAction actions;
            GdkDragAction suggested_action;
            GdkDragAction action;

            u32 start_time;

            GdkDevice* device;

            /* 1.0.6 per Teklad: _GdkDragContext appears to change between
             * different versions of GTK3 which causes the crash. It appears they
             * added/removed some variables from that struct.
             * https://github.com/IgnorantGuru/spacefm/issues/670 */
            u32 drop_done : 1; /* Whether gdk_drag_drop_done() was performed */
        };
        ((struct _GdkDragContext*)drag_context)->suggested_action = suggested_action;
        gdk_drag_status(drag_context, suggested_action, gtk_get_current_event_time());
    }
    return false;
}

static bool
on_dir_tree_view_drag_leave(GtkWidget* widget, GdkDragContext* drag_context, u32 time,
                            PtkFileBrowser* file_browser)
{
    (void)widget;
    (void)drag_context;
    (void)time;
    file_browser->drag_source_dev_tree_ = 0;
    return false;
}
