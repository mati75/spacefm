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

#include <string>
#include <string_view>

#include <format>

#include <filesystem>

#include <span>

#include <vector>

#include <algorithm>

#include <memory>

#include <optional>

#include <glibmm.h>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "vfs/vfs-file-info.hxx"
#include "xset/xset.hxx"

#include "ptk/ptk-dialog.hxx"

#include "ptk/ptk-app-chooser.hxx"
#include "ptk/ptk-archiver.hxx"
#include "ptk/ptk-file-browser.hxx"

#include "vfs/vfs-app-desktop.hxx"

#include "settings/app.hxx"

#include "utils.hxx"

#include "ptk/ptk-file-actions-open.hxx"

struct ParentInfo
{
    PtkFileBrowser* file_browser{nullptr};
    std::filesystem::path cwd{};
};

static bool
open_archives(const std::shared_ptr<ParentInfo>& parent,
              const std::span<const vfs::file_info> selected_files)
{
    const auto is_archive = [](const vfs::file_info file) { return file->is_archive(); };
    if (!std::ranges::all_of(selected_files, is_archive))
    {
        return false;
    }

    if (xset_get_b(xset::name::archive_default_open_with_app))
    { // user has open archives with app option enabled, do not handle these files
        return false;
    }

    const bool extract_here = xset_get_b(xset::name::archive_default_extract);

    // determine default archive action in this dir
    if (extract_here && have_rw_access(parent->cwd))
    {
        // Extract Here
        ptk_archiver_extract(parent->file_browser, selected_files, parent->cwd);
        return true;
    }
    else if (extract_here || xset_get_b(xset::name::archive_default_extract_to))
    {
        // Extract Here but no write access or Extract To option
        ptk_archiver_extract(parent->file_browser, selected_files, "");
        return true;
    }
    else if (xset_get_b(xset::name::archive_default_open_with_archiver))
    {
        ptk_archiver_open(parent->file_browser, selected_files);
        return true;
    }

    // do not handle these files
    return false;
}

static bool
open_files_with_app(const std::shared_ptr<ParentInfo>& parent,
                    const std::span<const std::filesystem::path> open_files,
                    const std::string_view app_desktop)
{
    if (app_desktop.empty())
    {
        return false;
    }

    const vfs::desktop desktop = vfs_get_desktop(app_desktop);

    ztd::logger::info("EXEC({})={}", desktop->path().string(), desktop->exec());

    try
    {
        desktop->open_files(parent->cwd, open_files);
    }
    catch (const VFSAppDesktopException& e)
    {
        GtkWidget* toplevel = parent->file_browser
                                  ? gtk_widget_get_toplevel(GTK_WIDGET(parent->file_browser))
                                  : nullptr;
        ptk_show_error(GTK_WINDOW(toplevel), "Error", e.what());
    }

    return true;
}

void
ptk_open_files_with_app(const std::filesystem::path& cwd,
                        const std::span<const vfs::file_info> selected_files,
                        const std::string_view app_desktop, PtkFileBrowser* file_browser,
                        bool xforce, bool xnever)
{
    if (selected_files.empty())
    {
        return;
    }

    const auto parent = std::make_shared<ParentInfo>(file_browser, cwd);

    if (!app_desktop.empty())
    {
        std::vector<std::filesystem::path> files_to_open;
        files_to_open.reserve(selected_files.size());
        for (const vfs::file_info file : selected_files)
        {
            files_to_open.emplace_back(file->path());
        }

        open_files_with_app(parent, files_to_open, app_desktop);
        return;
    }

    // No app specified - Use default app for each file

    std::vector<std::filesystem::path> dirs_to_open;
    std::map<std::string, std::vector<std::filesystem::path>> files_to_open;
    for (const vfs::file_info file : selected_files)
    {
        // Is a dir?  Open in browser
        if (file->is_directory())
        {
            dirs_to_open.emplace_back(file->path());
            continue;
        }

        // If this file is an executable file, run it.
        if (!xnever && file->is_executable() && (app_settings.click_executes() || xforce))
        {
            Glib::spawn_command_line_async(file->path());
            if (file_browser)
            {
                file_browser->run_event<spacefm::signal::open_item>(file->path(),
                                                                    ptk::open_action::file);
            }
            continue;
        }

        // Find app to open this file
        std::optional<std::string> alloc_desktop = std::nullopt;

        vfs::mime_type mime_type = file->mime_type();

        // archive has special handling
        if (!selected_files.empty() && open_archives(parent, selected_files))
        { // all files were handled by open_archives
            break;
        }

        // The file itself is a desktop entry file.
        if (!alloc_desktop)
        {
            if (file->is_desktop_entry() && (app_settings.click_executes() || xforce))
            {
                alloc_desktop = file->path();
            }
            else
            {
                alloc_desktop = mime_type->default_action();
            }
        }

        if (!alloc_desktop && mime_type_is_text_file(file->path(), mime_type->type()))
        {
            // FIXME: special handling for plain text file
            mime_type = vfs_mime_type_get_from_type(XDG_MIME_TYPE_PLAIN_TEXT);
            alloc_desktop = mime_type->default_action();
        }

        if (!alloc_desktop && file->is_symlink())
        {
            // broken link?
            try
            {
                const auto target_path = std::filesystem::read_symlink(file->path());

                if (!std::filesystem::exists(target_path))
                {
                    GtkWidget* toplevel =
                        file_browser ? gtk_widget_get_toplevel(GTK_WIDGET(file_browser)) : nullptr;
                    ptk_show_error(GTK_WINDOW(toplevel),
                                   "Broken Link",
                                   std::format("This symlink's target is missing or you do not "
                                               "have permission to access it:\n{}\n\nTarget: {}",
                                               file->path().string(),
                                               target_path.string()));
                    continue;
                }
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                ztd::logger::warn("{}", e.what());
                continue;
            }
        }
        if (!alloc_desktop)
        {
            // Let the user choose an application
            GtkWidget* toplevel =
                file_browser ? gtk_widget_get_toplevel(GTK_WIDGET(file_browser)) : nullptr;
            const auto ptk_app = ptk_choose_app_for_mime_type(GTK_WINDOW(toplevel),
                                                              mime_type,
                                                              true,
                                                              true,
                                                              true,
                                                              !file_browser);

            if (ptk_app)
            {
                alloc_desktop = ptk_app;
            }
        }
        if (!alloc_desktop)
        {
            continue;
        }

        const auto desktop = alloc_desktop.value();
        if (files_to_open.contains(desktop))
        {
            files_to_open[desktop].emplace_back(file->path());
        }
        else
        {
            files_to_open[desktop] = {file->path()};
        }
    }

    for (const auto& [desktop, open_files] : files_to_open)
    {
        open_files_with_app(parent, open_files, desktop);
    }

    if (file_browser && dirs_to_open.size() != 0)
    {
        if (dirs_to_open.size() == 1)
        {
            file_browser->run_event<spacefm::signal::open_item>(dirs_to_open.front(),
                                                                ptk::open_action::dir);
        }
        else
        {
            for (const auto& dir : dirs_to_open)
            {
                file_browser->run_event<spacefm::signal::open_item>(dir, ptk::open_action::new_tab);
            }
        }
    }
}
