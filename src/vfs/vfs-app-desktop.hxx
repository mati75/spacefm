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

#pragma once

#include <string>
#include <string_view>

#include <filesystem>

#include <span>

#include <vector>

#include <optional>

#include <exception>

#include <gtkmm.h>

#include <ztd/ztd.hxx>

class VFSAppDesktop
{
  public:
    VFSAppDesktop() = delete;
    ~VFSAppDesktop() = default;
    // ~VFSAppDesktop() { ztd::logger::info("VFSAppDesktop destructor") };

    VFSAppDesktop(const std::filesystem::path& desktop_file) noexcept;

    [[nodiscard]] const std::string_view name() const noexcept;
    [[nodiscard]] const std::string_view display_name() const noexcept;
    [[nodiscard]] const std::string_view exec() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] const std::string_view icon_name() const noexcept;
    [[nodiscard]] GdkPixbuf* icon(i32 size) const noexcept;
    [[nodiscard]] bool use_terminal() const noexcept;
    void open_file(const std::filesystem::path& working_dir,
                   const std::filesystem::path& file_path) const;
    void open_files(const std::filesystem::path& working_dir,
                    const std::span<const std::filesystem::path> file_paths) const;

    const std::vector<std::string> supported_mime_types() const noexcept;

  private:
    bool open_multiple_files() const noexcept;
    const std::optional<std::vector<std::vector<std::string>>>
    app_exec_generate_desktop_argv(const std::span<const std::filesystem::path> file_list,
                                   bool quote_file_list) const noexcept;
    void exec_in_terminal(const std::filesystem::path& cwd,
                          const std::string_view cmd) const noexcept;
    void exec_desktop(const std::filesystem::path& working_dir,
                      const std::span<const std::filesystem::path> file_paths) const noexcept;

  private:
    std::string filename_{};
    std::filesystem::path path_{};
    bool loaded_{false};

    struct desktop_entry_data
    {
        // https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#recognized-keys
        std::string type{};
        std::string name{};
        std::string generic_name{};
        bool no_display{false};
        std::string comment{};
        std::string icon{};
        std::string exec{};
        std::string try_exec{};
        std::string path{}; // working dir
        bool terminal{false};
        std::string actions{};
        std::string mime_type{};
        std::string categories{};
        std::string keywords{};
        bool startup_notify{false};
    };

    desktop_entry_data desktop_entry_;
};

namespace vfs
{
    using desktop = std::shared_ptr<VFSAppDesktop>;
} // namespace vfs

class VFSAppDesktopException : virtual public std::exception
{
  protected:
    std::string error_message;

  public:
    explicit VFSAppDesktopException(const std::string_view msg) : error_message(msg)
    {
    }

    virtual ~VFSAppDesktopException() throw()
    {
    }

    virtual const char*
    what() const throw()
    {
        return error_message.data();
    }
};

// get cached VFSAppDesktop
vfs::desktop vfs_get_desktop(const std::filesystem::path& desktop_file);
