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

#include <vector>

#include <map>

#include <algorithm>
#include <ranges>

#include <thread>
#include <mutex>
#include <chrono>

#include <optional>

#include <memory>

#include <gtkmm.h>
#include <glibmm.h>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "settings/app.hxx"

#include "mime-type/mime-action.hxx"
#include "mime-type/mime-type.hxx"

#include "vfs/vfs-mime-type.hxx"
#include "vfs/vfs-file-monitor.hxx"

#include "vfs/vfs-dir.hxx"

#include "vfs/vfs-utils.hxx"

static std::map<std::string, vfs::mime_type> mime_map;
std::mutex mime_map_lock;

static std::vector<vfs::file_monitor> mime_caches_monitors;

vfs::mime_type
vfs_mime_type_new(const std::string_view type_name)
{
    auto mime_type = std::make_shared<VFSMimeType>(type_name);
    return mime_type;
}

vfs::mime_type
vfs_mime_type_get_from_file(const std::filesystem::path& file_path)
{
    const std::string type = mime_type_get_by_file(file_path);
    return vfs_mime_type_get_from_type(type);
}

vfs::mime_type
vfs_mime_type_get_from_type(const std::string_view type)
{
    std::unique_lock<std::mutex> lock(mime_map_lock);

    if (mime_map.contains(type.data()))
    {
        return mime_map.at(type.data());
    }

    vfs::mime_type mime_type = vfs_mime_type_new(type);
    mime_map.insert({mime_type->type().data(), mime_type});

    return mime_type;
}

static bool
vfs_mime_type_reload()
{
    std::unique_lock<std::mutex> lock(mime_map_lock);
    mime_map.clear();
    // ztd::logger::debug("reload mime-types");
    vfs_dir_mime_type_reload();
    mime_type_regen_all_caches();
    return false;
}

static void
on_mime_cache_changed(const vfs::file_monitor& monitor, vfs::file_monitor_event event,
                      const std::filesystem::path& file_name, void* user_data)
{
    (void)monitor;
    (void)event;
    (void)file_name;
    (void)user_data;

    // ztd::logger::debug("reloading all mime caches");
    std::jthread idle_thread(
        []()
        {
            vfs_mime_type_reload();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

    idle_thread.join();
}

void
vfs_mime_type_init()
{
    mime_type_init();

    /* install file alteration monitor for mime-cache */
    for (const mime_cache_t& cache : mime_type_get_caches())
    {
        // MOD NOTE1  check to see if path exists - otherwise it later tries to
        //  remove nullptr monitor with inotify which caused segfault
        if (!std::filesystem::exists(cache->file_path()))
        {
            continue;
        }

        const vfs::file_monitor monitor =
            vfs_file_monitor_add(cache->file_path(), on_mime_cache_changed, nullptr);

        mime_caches_monitors.emplace_back(monitor);
    }
}

void
vfs_mime_type_finalize()
{
    // remove file alteration monitor for mime-cache
    const auto action_remove = [](const vfs::file_monitor& monitor)
    { vfs_file_monitor_remove(monitor, on_mime_cache_changed, nullptr); };
    std::ranges::for_each(mime_caches_monitors, action_remove);

    mime_type_finalize();

    mime_map.clear();
}

/////////////////////////////////////

VFSMimeType::VFSMimeType(const std::string_view type_name) : type_(type_name)
{
    const auto icon_data = mime_type_get_desc_icon(this->type_);
    this->description_ = icon_data[1];
    if (this->description_.empty() && !ztd::same(this->type_, XDG_MIME_TYPE_UNKNOWN))
    {
        ztd::logger::warn("mime-type {} has no description (comment)", this->type_);
        const auto mime_unknown = vfs_mime_type_get_from_type(XDG_MIME_TYPE_UNKNOWN);
        if (mime_unknown)
        {
            this->description_ = mime_unknown->description();
        }
    }
}

VFSMimeType::~VFSMimeType()
{
    if (this->big_icon_)
    {
        g_object_unref(this->big_icon_);
    }
    if (this->small_icon_)
    {
        g_object_unref(this->small_icon_);
    }
}

GdkPixbuf*
VFSMimeType::icon(bool big) noexcept
{
    i32 icon_size;

    if (big)
    { // big icon
        if (this->icon_size_big_ != app_settings.icon_size_big())
        { // big icon size has changed
            if (this->big_icon_)
            {
                g_object_unref(this->big_icon_);
                this->big_icon_ = nullptr;
            }
        }
        if (this->big_icon_)
        {
            return g_object_ref(this->big_icon_);
        }
        this->icon_size_big_ = app_settings.icon_size_big();
        icon_size = this->icon_size_big_;
    }
    else
    { // small icon
        if (this->icon_size_small_ != app_settings.icon_size_small())
        { // small icon size has changed
            if (this->small_icon_)
            {
                g_object_unref(this->small_icon_);
                this->small_icon_ = nullptr;
            }
        }
        if (this->small_icon_)
        {
            return g_object_ref(this->small_icon_);
        }
        this->icon_size_small_ = app_settings.icon_size_small();
        icon_size = this->icon_size_small_;
    }

    GdkPixbuf* icon = nullptr;

    if (ztd::same(this->type_, XDG_MIME_TYPE_DIRECTORY))
    {
        icon = vfs_load_icon("gtk-directory", icon_size);
        if (!icon)
        {
            icon = vfs_load_icon("gnome-fs-directory", icon_size);
        }
        if (!icon)
        {
            icon = vfs_load_icon("folder", icon_size);
        }
        if (big)
        {
            this->big_icon_ = icon;
        }
        else
        {
            this->small_icon_ = icon;
        }
        return icon ? g_object_ref(icon) : nullptr;
    }

    // get description and icon from freedesktop XML - these are fetched
    // together for performance.
    const auto [mime_icon, mime_desc] = mime_type_get_desc_icon(this->type_);

    if (!mime_icon.empty())
    {
        icon = vfs_load_icon(mime_icon, icon_size);
    }
    if (!mime_desc.empty())
    {
        if (this->description_.empty())
        {
            this->description_ = mime_desc;
        }
    }
    if (this->description_.empty())
    {
        ztd::logger::warn("mime-type {} has no description (comment)", this->type_);
        vfs::mime_type vfs_mime = vfs_mime_type_get_from_type(XDG_MIME_TYPE_UNKNOWN);
        if (vfs_mime)
        {
            this->description_ = vfs_mime->description();
        }
    }

    if (!icon)
    {
        // guess icon
        if (ztd::contains(this->type_, "/"))
        {
            // convert mime-type foo/bar to foo-bar
            const std::string icon_name = ztd::replace(this->type_, "/", "-");

            // is there an icon named foo-bar?
            icon = vfs_load_icon(icon_name, icon_size);
            // fallback try foo-x-generic
            if (!icon)
            {
                const auto mime = ztd::partition(this->type_, "/")[0];
                const std::string generic_icon_name = std::format("{}-x-generic", mime);
                icon = vfs_load_icon(generic_icon_name, icon_size);
            }
        }
    }

    if (!icon)
    {
        /* prevent endless recursion of XDG_MIME_TYPE_UNKNOWN */
        if (!ztd::same(this->type_, XDG_MIME_TYPE_UNKNOWN))
        {
            /* FIXME: fallback to icon of parent mime-type */
            vfs::mime_type unknown = vfs_mime_type_get_from_type(XDG_MIME_TYPE_UNKNOWN);
            icon = unknown->icon(big);
        }
        else /* unknown */
        {
            icon = vfs_load_icon("unknown", icon_size);
        }
    }

    if (big)
    {
        this->big_icon_ = icon;
    }
    else
    {
        this->small_icon_ = icon;
    }
    return icon ? g_object_ref(icon) : nullptr;
}

const std::string_view
VFSMimeType::type() const noexcept
{
    return this->type_;
}

/* Get human-readable description of mime type */
const std::string_view
VFSMimeType::description() noexcept
{
    return this->description_;
}

const std::vector<std::string>
VFSMimeType::actions() const noexcept
{
    return mime_type_get_actions(this->type_);
}

const std::optional<std::string>
VFSMimeType::default_action() const noexcept
{
    auto def = mime_type_get_default_action(this->type_);

    /* FIXME:
     * If default app is not set, choose one from all availble actions.
     * Is there any better way to do this?
     * Should we put this fallback handling here, or at API of higher level?
     */
    if (def)
    {
        return def;
    }

    const std::vector<std::string> actions = mime_type_get_actions(this->type_);
    if (!actions.empty())
    {
        return actions.at(0).data();
    }
    return std::nullopt;
}

/*
 * Set default app.desktop for specified file.
 * app can be the name of the desktop file or a command line.
 */
void
VFSMimeType::set_default_action(const std::string_view desktop_id) noexcept
{
    const auto custom_desktop = this->add_action(desktop_id);

    mime_type_set_default_action(this->type_, custom_desktop.empty() ? desktop_id : custom_desktop);
}

/* If user-custom desktop file is created, it is returned in custom_desktop. */
const std::string
VFSMimeType::add_action(const std::string_view desktop_id) noexcept
{
    // MOD  do not create custom desktop file if desktop_id is not a command
    if (!ztd::endswith(desktop_id, ".desktop"))
    {
        return mime_type_add_action(this->type_, desktop_id);
    }
    return desktop_id.data();
}

const std::optional<std::filesystem::path>
vfs_mime_type_locate_desktop_file(const std::string_view desktop_id)
{
    return mime_type_locate_desktop_file(desktop_id);
}

const std::optional<std::filesystem::path>
vfs_mime_type_locate_desktop_file(const std::filesystem::path& dir,
                                  const std::string_view desktop_id)
{
    return mime_type_locate_desktop_file(dir, desktop_id);
}
