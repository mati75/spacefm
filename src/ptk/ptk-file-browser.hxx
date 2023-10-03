/**
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

#pragma once

#include <string_view>

#include <filesystem>

#include <span>

#include <optional>

#include <gtkmm.h>
#include <sigc++/sigc++.h>

#include <xset/xset.hxx>

#include "vfs/vfs-dir.hxx"

#include "types.hxx"

#define PTK_FILE_BROWSER(obj)             (static_cast<PtkFileBrowser*>(obj))
#define PTK_FILE_BROWSER_REINTERPRET(obj) (reinterpret_cast<PtkFileBrowser*>(obj))

#define PTK_TYPE_FILE_BROWSER    (ptk_file_browser_get_type())
#define PTK_IS_FILE_BROWSER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), PTK_TYPE_FILE_BROWSER))

namespace ptk
{
    enum class open_action
    {
        dir,
        new_tab,
        new_window,
        terminal,
        file,
    };
}

namespace ptk::file_browser
{
    enum class view_mode
    {
        icon_view,
        list_view,
        compact_view,
    };

    enum class sort_order
    {
        name,
        size,
        bytes,
        type,
        mime,
        perm,
        owner,
        group,
        atime,
        btime,
        ctime,
        mtime,
    };

    enum class chdir_mode
    {
        normal,
        add_history,
        no_history,
        back,
        forward,
    };

} // namespace ptk::file_browser

// forward declare
struct PtkFileBrowser;
struct MainWindow;

struct PtkFileBrowser
{
    /* parent class */
    GtkBox parent;

    /* <private> */
    GList* history_{nullptr};
    GList* curHistory_{nullptr};
    GList* histsel_{nullptr};
    GList* curhistsel_{nullptr};

    vfs::dir dir_{nullptr};
    GtkTreeModel* file_list_{nullptr};
    i32 max_thumbnail_{0};
    i32 n_sel_files_{0};
    u64 sel_size_{0};
    u64 sel_disk_size_{0};
    u32 sel_change_idle_{0};

    // path bar auto seek
    bool inhibit_focus_{false};
    std::optional<std::filesystem::path> seek_name_{std::nullopt};

    // sorting
    GtkSortType sort_type_;
    ptk::file_browser::sort_order sort_order_{ptk::file_browser::sort_order::perm};
    ptk::file_browser::view_mode view_mode_{ptk::file_browser::view_mode::compact_view};

    bool single_click_{true};
    bool show_hidden_files_{true};
    bool large_icons_{true};
    bool busy_{false};
    bool pending_drag_status_{true};
    dev_t drag_source_dev_{0};
    ino_t drag_source_inode_{0};
    i32 drag_x_{0};
    i32 drag_y_{0};
    bool pending_drag_status_tree_{true};
    dev_t drag_source_dev_tree_{0};
    bool is_drag_{true};
    bool skip_release_{true};
    bool menu_shown_{true};

    /* directory view */
    GtkWidget* folder_view_{nullptr};
    GtkWidget* folder_view_scroll_{nullptr};
    GtkCellRenderer* icon_render_{nullptr};

    // MOD
    panel_t panel_{0};

    MainWindow* main_window_{nullptr};
    GtkWidget* notebook_{nullptr};
    GtkWidget* task_view_{nullptr};
    GtkWidget* toolbox_{nullptr};
    GtkWidget* path_bar_{nullptr};
    GtkWidget* hpane{nullptr};
    GtkWidget* side_vbox{nullptr};
    GtkWidget* side_toolbox{nullptr};
    GtkWidget* side_vpane_top{nullptr};
    GtkWidget* side_vpane_bottom{nullptr};
    GtkWidget* side_dir_scroll{nullptr};
    GtkWidget* side_dev_scroll{nullptr};
    GtkWidget* side_dir{nullptr};
    GtkWidget* side_dev{nullptr};
    GtkWidget* status_bar{nullptr};
    GtkFrame* status_frame{nullptr};
    GtkLabel* status_label{nullptr};
    GtkWidget* status_image{nullptr};
    GtkWidget* toolbar{nullptr};
    GtkWidget* side_toolbar{nullptr};
    GSList* toolbar_widgets[10];

    std::optional<std::filesystem::path> select_path_{std::nullopt};

  public:
    // folder_path should be encodede in on-disk encoding
    bool chdir(const std::filesystem::path& folder_path,
               const ptk::file_browser::chdir_mode mode) noexcept;

    const std::filesystem::path cwd() const noexcept;
    void canon(const std::filesystem::path& path) noexcept;

    u64 get_n_all_files() const noexcept;
    u64 get_n_visible_files() const noexcept;
    u64 get_n_sel(u64* sel_size, u64* sel_disk_size) const noexcept;

    void go_home() noexcept;
    void go_default() noexcept;
    void go_tab(tab_t tab) noexcept;
    void go_back() noexcept;
    void go_forward() noexcept;
    void go_up() noexcept;
    void refresh() noexcept;

    void show_hidden_files(bool show) noexcept;
    void set_single_click(bool single_click) noexcept;

    void new_tab() noexcept;
    void new_tab_here() noexcept;
    void close_tab() noexcept;
    void restore_tab() noexcept;
    void open_in_tab(const std::filesystem::path& file_path, const tab_t tab) const noexcept;
    void set_default_folder() const noexcept;

    const std::vector<vfs::file_info> selected_files() noexcept;

    void open_selected_files() noexcept;
    void open_selected_files_with_app(const std::string_view app_desktop = "") noexcept;

    void rename_selected_files(const std::span<const vfs::file_info> selected_files,
                               const std::filesystem::path& cwd) noexcept;
    void hide_selected(const std::span<const vfs::file_info> selected_files,
                       const std::filesystem::path& cwd) noexcept;

    void copycmd(const std::span<const vfs::file_info> selected_files,
                 const std::filesystem::path& cwd, xset::name setname) noexcept;

    void set_sort_order(ptk::file_browser::sort_order order) noexcept;
    void set_sort_type(GtkSortType order) noexcept;
    void set_sort_extra(xset::name setname) const noexcept;
    void read_sort_extra() const noexcept;

    void paste_link() const noexcept;
    void paste_target() const noexcept;

    void select_all() const noexcept;
    void unselect_all() const noexcept;
    void select_last() noexcept;
    void select_pattern(const std::string_view search_key = "") noexcept;
    void invert_selection() noexcept;

    void view_as_icons() noexcept;
    void view_as_compact_list() noexcept;
    void view_as_list() noexcept;

    void show_thumbnails(i32 max_file_size) noexcept; // sets icons size using this->large_icons_
    void show_thumbnails(i32 max_file_size, bool large_icons) noexcept;

    void update_views() noexcept;
    void focus(i32 job) noexcept;
    void focus_me() noexcept;
    void save_column_widths(GtkTreeView* view) noexcept;
    bool slider_release(GtkWidget* widget) noexcept;

    void rebuild_toolbars() noexcept;

    GList* selected_items(GtkTreeModel** model) noexcept;
    void select_file(const std::filesystem::path& path, const bool unselect_others = true) noexcept;
    void unselect_file(const std::filesystem::path& path,
                       const bool unselect_others = true) noexcept;
    void seek_path(const std::filesystem::path& seek_dir,
                   const std::filesystem::path& seek_name) noexcept;
    void update_toolbar_widgets(xset::tool tool_type) noexcept;
    void update_toolbar_widgets(xset_t set_ptr, xset::tool tool_type) noexcept;

    void show_history_menu(bool is_back_history, GdkEventButton* event) noexcept;

    void on_permission(GtkMenuItem* item, const std::span<const vfs::file_info> selected_files,
                       const std::filesystem::path& cwd) noexcept;
    void on_action(const xset::name setname) noexcept;

    void focus_folder_view() noexcept;
    void enable_toolbar() noexcept;

    ////////////////

    bool using_large_icons() const noexcept;
    bool is_busy() const noexcept;

    bool pending_drag_status_tree() const noexcept;
    void pending_drag_status_tree(bool val) noexcept;

    // sorting

    GtkSortType sort_type() const noexcept;
    bool is_sort_type(GtkSortType type) const noexcept;

    ptk::file_browser::sort_order sort_order() const noexcept;
    bool is_sort_order(ptk::file_browser::sort_order type) const noexcept;

    ptk::file_browser::view_mode view_mode() const noexcept;
    bool is_view_mode(ptk::file_browser::view_mode type) const noexcept;

    // directory view

    GtkWidget* folder_view() const noexcept;
    void folder_view(GtkWidget* new_folder_view) noexcept;

    GtkWidget* folder_view_scroll() const noexcept;
    GtkCellRenderer* icon_render() const noexcept;

    // other
    panel_t panel() const noexcept;
    GtkWidget* task_view() const noexcept;
    MainWindow* main_window() const noexcept;
    GtkWidget* path_bar() const noexcept;

    // Signals
  public:
    // Signals function types
    using evt_chdir_before_t = void(PtkFileBrowser*, MainWindow*);
    using evt_chdir_begin_t = void(PtkFileBrowser*, MainWindow*);
    using evt_chdir_after_t = void(PtkFileBrowser*, MainWindow*);
    using evt_open_file_t = void(PtkFileBrowser*, const std::filesystem::path&, ptk::open_action,
                                 MainWindow*);
    using evt_change_content_t = void(PtkFileBrowser*, MainWindow*);
    using evt_change_sel_t = void(PtkFileBrowser*, MainWindow*);
    using evt_change_pane_mode_t = void(PtkFileBrowser*, MainWindow*);

    // Signals Add Event
    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::chdir_before, sigc::connection>::type
    add_event(evt_chdir_before_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::chdir_before");
        this->evt_data_window = window;
        return this->evt_chdir_before.connect(sigc::ptr_fun(fun));
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::chdir_begin, sigc::connection>::type
    add_event(evt_chdir_begin_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::chdir_begin");
        this->evt_data_window = window;
        return this->evt_chdir_begin.connect(sigc::ptr_fun(fun));
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::chdir_after, sigc::connection>::type
    add_event(evt_chdir_after_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::chdir_after");
        this->evt_data_window = window;
        return this->evt_chdir_after.connect(sigc::ptr_fun(fun));
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::open_item, sigc::connection>::type
    add_event(evt_open_file_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::open_item");
        this->evt_data_window = window;
        return this->evt_open_file.connect(sigc::ptr_fun(fun));
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::change_content, sigc::connection>::type
    add_event(evt_change_content_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::change_content");
        this->evt_data_window = window;
        return this->evt_change_content.connect(sigc::ptr_fun(fun));
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::change_sel, sigc::connection>::type
    add_event(evt_change_sel_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::change_sel");
        this->evt_data_window = window;
        return this->evt_change_sel.connect(sigc::ptr_fun(fun));
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::change_pane, sigc::connection>::type
    add_event(evt_change_pane_mode_t fun, MainWindow* window)
    {
        // ztd::logger::trace("Signal Connect   : spacefm::signal::change_pane");
        this->evt_data_window = window;
        return this->evt_change_pane_mode.connect(sigc::ptr_fun(fun));
    }

    // Signals Run Event
    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::chdir_before, void>::type
    run_event()
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::chdir_before");
        this->evt_chdir_before.emit(this, this->evt_data_window);
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::chdir_begin, void>::type
    run_event()
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::chdir_begin");
        this->evt_chdir_begin.emit(this, this->evt_data_window);
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::chdir_after, void>::type
    run_event()
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::chdir_after");
        this->evt_chdir_after.emit(this, this->evt_data_window);
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::open_item, void>::type
    run_event(const std::filesystem::path& path, ptk::open_action action)
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::open_item");
        this->evt_open_file.emit(this, path, action, this->evt_data_window);
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::change_content, void>::type
    run_event()
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::change_content");
        this->evt_change_content.emit(this, this->evt_data_window);
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::change_sel, void>::type
    run_event()
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::change_sel");
        this->evt_change_sel.emit(this, this->evt_data_window);
    }

    template<spacefm::signal evt>
    typename std::enable_if<evt == spacefm::signal::change_pane, void>::type
    run_event()
    {
        // ztd::logger::trace("Signal Execute   : spacefm::signal::change_pane");
        this->evt_change_pane_mode.emit(this, this->evt_data_window);
    }

    // Signals
  private:
    // Signal types
    sigc::signal<evt_chdir_before_t> evt_chdir_before;
    sigc::signal<evt_chdir_begin_t> evt_chdir_begin;
    sigc::signal<evt_chdir_after_t> evt_chdir_after;
    sigc::signal<evt_open_file_t> evt_open_file;
    sigc::signal<evt_change_content_t> evt_change_content;
    sigc::signal<evt_change_sel_t> evt_change_sel;
    sigc::signal<evt_change_pane_mode_t> evt_change_pane_mode;

  private:
    // Signal data
    // TODO/FIXME has to be a better way to do this
    MainWindow* evt_data_window{nullptr};

  public:
    // Signals we connect to
    sigc::connection signal_file_created;
    sigc::connection signal_file_deleted;
    sigc::connection signal_file_changed;
    sigc::connection signal_file_listed;
};

struct PtkFileBrowserClass
{
    GtkPanedClass parent;

    /* Default signal handlers */
    void (*before_chdir)(PtkFileBrowser* file_browser, const std::filesystem::path& path);
    void (*begin_chdir)(PtkFileBrowser* file_browser);
    void (*after_chdir)(PtkFileBrowser* file_browser);
    void (*open_item)(PtkFileBrowser* file_browser, const std::filesystem::path& path, i32 action);
    void (*content_change)(PtkFileBrowser* file_browser);
    void (*sel_change)(PtkFileBrowser* file_browser);
    void (*pane_mode_change)(PtkFileBrowser* file_browser);
};

GType ptk_file_browser_get_type();

GtkWidget* ptk_file_browser_new(i32 curpanel, GtkWidget* notebook, GtkWidget* task_view,
                                MainWindow* main_window);

bool ptk_file_browser_write_access(const std::filesystem::path& cwd);
bool ptk_file_browser_read_access(const std::filesystem::path& cwd);

void ptk_file_browser_add_toolbar_widget(xset_t set, GtkWidget* widget);

// MOD

// xset callback wrapper functions
void ptk_file_browser_go_home(GtkWidget* item, PtkFileBrowser* file_browser);
void ptk_file_browser_go_default(GtkWidget* item, PtkFileBrowser* file_browser);
void ptk_file_browser_go_tab(GtkMenuItem* item, PtkFileBrowser* file_browser);
void ptk_file_browser_go_back(GtkWidget* item, PtkFileBrowser* file_browser);
void ptk_file_browser_go_forward(GtkWidget* item, PtkFileBrowser* file_browser);
void ptk_file_browser_go_up(GtkWidget* item, PtkFileBrowser* file_browser);

void ptk_file_browser_refresh(GtkWidget* item, PtkFileBrowser* file_browser);

void ptk_file_browser_new_tab(GtkMenuItem* item, PtkFileBrowser* file_browser);
void ptk_file_browser_new_tab_here(GtkMenuItem* item, PtkFileBrowser* file_browser);
void ptk_file_browser_close_tab(GtkMenuItem* item, PtkFileBrowser* file_browser);
void ptk_file_browser_restore_tab(GtkMenuItem* item, PtkFileBrowser* file_browser);
void ptk_file_browser_set_default_folder(GtkWidget* item, PtkFileBrowser* file_browser);

void ptk_file_browser_select_all(GtkWidget* item, PtkFileBrowser* file_browser);
void ptk_file_browser_unselect_all(GtkWidget* item, PtkFileBrowser* file_browser);
void ptk_file_browser_invert_selection(GtkWidget* item, PtkFileBrowser* file_browser);

void ptk_file_browser_focus(GtkMenuItem* item, PtkFileBrowser* file_browser);
bool ptk_file_browser_slider_release(GtkWidget* widget, GdkEventButton* event,
                                     PtkFileBrowser* file_browser);
