/**
 * Copyright (C) 2007 PCMan <pcman.tw@gmail.com>
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

/* Currently this library is NOT MT-safe */

#include <string>
#include <string_view>

#include <format>

#include <filesystem>

#include <array>
#include <vector>

#include <span>

#include <optional>

#include <algorithm>

#include <memory>

#include <fcntl.h>

#include <pugixml.hpp>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "utils.hxx"

#include "vfs/vfs-user-dirs.hxx"

#include "mime-type/mime-type.hxx"
#include "mime-type/mime-cache.hxx"

/* max extent used to checking text files */
inline constexpr i32 TEXT_MAX_EXTENT = 512;

/* Check if the specified mime_type is the subclass of the specified parent type */
static bool mime_type_is_subclass(const std::string_view type, const std::string_view parent);

static std::vector<mime_cache_t> caches;

/* max magic extent of all caches */
static u32 mime_cache_max_extent = 0;

/* free all mime.cache files on the system */
static void mime_cache_free_all();

static bool mime_type_is_data_plain_text(const std::span<const char8_t> data);

/*
 * Get mime-type of the specified file (quick, but less accurate):
 * Mime-type of the file is determined by cheking the filename only.
 */
static const std::string
mime_type_get_by_filename(const std::filesystem::path& filename,
                          const std::filesystem::file_status& status)
{
    const char* type = nullptr;
    const char* suffix_pos = nullptr;
    const char* prev_suffix_pos = (const char*)-1;

    if (std::filesystem::is_directory(status))
    {
        return XDG_MIME_TYPE_DIRECTORY.data();
    }

    for (const mime_cache_t& cache : caches)
    {
        type = cache->lookup_literal(filename.c_str());
        if (type)
        {
            break;
        }

        const char* _type = cache->lookup_suffix(filename.c_str(), &suffix_pos);
        if (_type && suffix_pos < prev_suffix_pos)
        {
            type = _type;
            prev_suffix_pos = suffix_pos;

            if (type)
            {
                break;
            }
        }
    }

    if (!type) /* glob matching */
    {
        i32 max_glob_len = 0;
        i32 glob_len = 0;
        for (const mime_cache_t& cache : caches)
        {
            const char* matched_type = cache->lookup_glob(filename.c_str(), &glob_len);
            /* according to the mime.cache 1.0 spec, we should use the longest glob matched. */
            if (matched_type && glob_len > max_glob_len)
            {
                type = matched_type;
                max_glob_len = glob_len;
            }

            if (type)
            {
                break;
            }
        }
    }

    if (type && *type)
    {
        return type;
    }

    return XDG_MIME_TYPE_UNKNOWN.data();
}

/*
 * Get mime-type info of the specified file (slow, but more accurate):
 * To determine the mime-type of the file, mime_type_get_by_filename() is
 * tried first.  If the mime-type could not be determined, the content of
 * the file will be checked, which is much more time-consuming.
 * If statbuf is not nullptr, it will be used to determine if the file is a directory,
 * or if the file is an executable file; otherwise, the function will call stat()
 * to gather this info itself. So if you already have stat info of the file,
 * pass it to the function to prevent checking the file stat again.
 * If you have basename of the file, pass it to the function can improve the
 * efifciency, too. Otherwise, the function will try to get the basename of
 * the specified file again.
 */
const std::string
mime_type_get_by_file(const std::filesystem::path& filepath)
{
    const auto status = std::filesystem::status(filepath);

    if (!std::filesystem::exists(status))
    {
        return XDG_MIME_TYPE_UNKNOWN.data();
    }

    if (std::filesystem::is_other(status))
    {
        return XDG_MIME_TYPE_UNKNOWN.data();
    }

    if (std::filesystem::is_directory(status))
    {
        return XDG_MIME_TYPE_DIRECTORY.data();
    }

    const auto basename = filepath.filename();
    const std::string filename_type = mime_type_get_by_filename(basename, status);
    if (!ztd::same(filename_type, XDG_MIME_TYPE_UNKNOWN))
    {
        return filename_type;
    }

    const char* type = nullptr;

    // check for reg or link due to hangs on fifo and chr dev
    const auto file_size = std::filesystem::file_size(filepath);
    if (file_size > 0 &&
        (std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)))
    {
        /* Open the file and map it into memory */
        const i32 fd = open(filepath.c_str(), O_RDONLY, 0);
        if (fd != -1)
        {
            // mime header size
            std::array<char8_t, 512> data;

            const auto length = read(fd, data.data(), data.size());
            if (length == -1)
            {
                return XDG_MIME_TYPE_UNKNOWN.data();
            }

            for (usize i = 0; !type && i < caches.size(); ++i)
            {
                type = caches.at(i)->lookup_magic(data);
            }

            /* Check for executable file */
            if (!type && have_x_access(filepath))
            {
                type = XDG_MIME_TYPE_EXECUTABLE.data();
            }

            /* fallback: check for plain text */
            if (!type)
            {
                if (mime_type_is_data_plain_text(data))
                {
                    type = XDG_MIME_TYPE_PLAIN_TEXT.data();
                }
            }

            close(fd);
        }
    }
    else
    {
        /* empty file can be viewed as text file */
        type = XDG_MIME_TYPE_PLAIN_TEXT.data();
    }

    if (type && *type)
    {
        return type;
    }

    return XDG_MIME_TYPE_UNKNOWN.data();
}

// returns - icon_name, icon_desc
static std::optional<std::array<std::string, 2>>
mime_type_parse_xml_file(const std::filesystem::path& filepath, bool is_local)
{
    // ztd::logger::info("MIME XML = {}", file_path);

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filepath.c_str());

    if (!result)
    {
        ztd::logger::error("XML parsing error: {}", result.description());
        return std::nullopt;
    }

    std::string comment;
    pugi::xml_node mime_type_node = doc.child("mime-type");
    pugi::xml_node comment_node = mime_type_node.child("comment");
    if (comment_node)
    {
        comment = comment_node.child_value();
    }

    std::string icon_name;
    if (is_local)
    {
        pugi::xml_node icon_node = mime_type_node.child("icon");
        if (icon_node)
        {
            pugi::xml_attribute name_attr = icon_node.attribute("name");
            if (name_attr)
            {
                icon_name = name_attr.value();
            }
        }
    }
    else
    {
        pugi::xml_node generic_icon_node = mime_type_node.child("generic-icon");
        if (generic_icon_node)
        {
            pugi::xml_attribute name_attr = generic_icon_node.attribute("name");
            if (name_attr)
            {
                icon_name = name_attr.value();
            }
        }
    }

    // ztd::logger::info("MIME XML | icon_name = {} | comment = {}", icon_name, comment);
    return std::array{icon_name, comment};
}

/* Get human-readable description and icon name of the mime-type
 * If locale is nullptr, current locale will be used.
 * The returned string should be freed when no longer used.
 * The icon_name will only be set if points to nullptr, and must be freed.
 *
 * Note: Spec is not followed for icon.  If icon tag is found in .local
 * xml file, it is used.  Otherwise vfs_mime_type_get_icon guesses the icon.
 * The Freedesktop spec /usr/share/mime/generic-icons is NOT parsed. */
const std::array<std::string, 2>
mime_type_get_desc_icon(const std::string_view type)
{
    /*  //sfm 0.7.7+ FIXED:
     * According to specs on freedesktop.org, user_data_dir has
     * higher priority than system_data_dirs, but in most cases, there was
     * no file, or very few files in user_data_dir, so checking it first will
     * result in many unnecessary open() system calls, yealding bad performance.
     * Since the spec really sucks, we do not follow it here.
     */

    const std::string file_path =
        std::format("{}/mime/{}.xml", vfs::user_dirs->data_dir().string(), type);
    if (faccessat(0, file_path.data(), F_OK, AT_EACCESS) != -1)
    {
        const auto icon_data = mime_type_parse_xml_file(file_path, true);
        if (icon_data)
        {
            return icon_data.value();
        }
    }

    // look in system dirs
    for (const std::string_view sys_dir : vfs::user_dirs->system_data_dirs())
    {
        const std::string sys_file_path = std::format("{}/mime/{}.xml", sys_dir, type);
        if (faccessat(0, sys_file_path.data(), F_OK, AT_EACCESS) != -1)
        {
            const auto icon_data = mime_type_parse_xml_file(sys_file_path, false);
            if (icon_data)
            {
                return icon_data.value();
            }
        }
    }
    return {"", ""};
}

void
mime_type_finalize()
{
    mime_cache_free_all();
}

// load all mime.cache files on the system,
// including /usr/share/mime/mime.cache,
// /usr/local/share/mime/mime.cache,
// and $HOME/.local/share/mime/mime.cache.
void
mime_type_init()
{
    const auto user_mime_cache = vfs::user_dirs->data_dir() / "mime/mime.cache";
    const mime_cache_t cache = std::make_shared<MimeCache>(user_mime_cache);
    caches.emplace_back(cache);

    if (cache->magic_max_extent() > mime_cache_max_extent)
    {
        mime_cache_max_extent = cache->magic_max_extent();
    }

    caches.reserve(vfs::user_dirs->system_data_dirs().size());
    for (const std::string_view dir : vfs::user_dirs->system_data_dirs())
    {
        const auto sys_mime_cache = std::filesystem::path() / dir.data() / "mime/mime.cache";
        const mime_cache_t dir_cache = std::make_shared<MimeCache>(sys_mime_cache);
        caches.emplace_back(dir_cache);

        if (dir_cache->magic_max_extent() > mime_cache_max_extent)
        {
            mime_cache_max_extent = dir_cache->magic_max_extent();
        }
    }
}

/* free all mime.cache files on the system */
static void
mime_cache_free_all()
{
    caches.clear();

    mime_cache_max_extent = 0;
}

void
mime_cache_reload(const mime_cache_t& cache)
{
    cache->reload();

    /* recalculate max magic extent */
    for (const mime_cache_t& mcache : caches)
    {
        if (mcache->magic_max_extent() > mime_cache_max_extent)
        {
            mime_cache_max_extent = mcache->magic_max_extent();
        }
    }
}

static bool
mime_type_is_data_plain_text(const std::span<const char8_t> data)
{
    if (data.size() == 0)
    {
        return false;
    }

    for (const auto d : data)
    {
        if (d == '\0')
        {
            return false;
        }
    }
    return true;
}

bool
mime_type_is_text_file(const std::filesystem::path& file_path, const std::string_view mime_type)
{
    bool ret = false;

    if (!mime_type.empty())
    {
        if (ztd::same(mime_type, "application/pdf"))
        {
            // seems to think this is XDG_MIME_TYPE_PLAIN_TEXT
            return false;
        }
        if (mime_type_is_subclass(mime_type, XDG_MIME_TYPE_PLAIN_TEXT))
        {
            return true;
        }
        if (!ztd::startswith(mime_type, "text/") && !ztd::startswith(mime_type, "application/"))
        {
            return false;
        }
    }

    if (file_path.empty())
    {
        return false;
    }

    const i32 fd = open(file_path.c_str(), O_RDONLY);
    if (fd != -1)
    {
        const auto file_stat = ztd::statx(file_path);
        if (file_stat)
        {
            if (file_stat.is_regular_file())
            {
                std::array<char8_t, TEXT_MAX_EXTENT> data;
                const auto length = read(fd, data.data(), data.size());
                if (length == -1)
                {
                    ztd::logger::error("failed to read {}", file_path.string());
                    ret = false;
                }
                else
                {
                    ret = mime_type_is_data_plain_text(data);
                }
            }
        }
        close(fd);
    }
    return ret;
}

bool
mime_type_is_executable_file(const std::filesystem::path& file_path,
                             const std::string_view mime_type)
{
    std::string file_mime_type;
    if (mime_type.empty())
    {
        file_mime_type = mime_type_get_by_file(file_path);
    }
    else
    {
        file_mime_type = mime_type;
    }

    /*
     * Only executable types can be executale.
     * Since some common types, such as application/x-shellscript,
     * are not in mime database, we have to add them ourselves.
     */
    if (!ztd::same(file_mime_type, XDG_MIME_TYPE_UNKNOWN) &&
        (mime_type_is_subclass(file_mime_type, XDG_MIME_TYPE_EXECUTABLE) ||
         mime_type_is_subclass(file_mime_type, "application/x-shellscript")))
    {
        if (!file_path.empty())
        {
            if (!have_x_access(file_path))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

// Taken from file-roller .desktop file
inline constexpr std::array<std::string_view, 65> archive_mime_types{
    "application/bzip2",
    "application/gzip",
    "application/vnd.android.package-archive",
    "application/vnd.ms-cab-compressed",
    "application/vnd.debian.binary-package",
    "application/x-7z-compressed",
    "application/x-7z-compressed-tar",
    "application/x-ace",
    "application/x-alz",
    "application/x-apple-diskimage",
    "application/x-ar",
    "application/x-archive",
    "application/x-arj",
    "application/x-brotli",
    "application/x-bzip-brotli-tar",
    "application/x-bzip",
    "application/x-bzip-compressed-tar",
    "application/x-bzip1",
    "application/x-bzip1-compressed-tar",
    "application/x-cabinet",
    "application/x-cd-image",
    "application/x-compress",
    "application/x-compressed-tar",
    "application/x-cpio",
    "application/x-chrome-extension",
    "application/x-deb",
    "application/x-ear",
    "application/x-ms-dos-executable",
    "application/x-gtar",
    "application/x-gzip",
    "application/x-gzpostscript",
    "application/x-java-archive",
    "application/x-lha",
    "application/x-lhz",
    "application/x-lrzip",
    "application/x-lrzip-compressed-tar",
    "application/x-lz4",
    "application/x-lzip",
    "application/x-lzip-compressed-tar",
    "application/x-lzma",
    "application/x-lzma-compressed-tar",
    "application/x-lzop",
    "application/x-lz4-compressed-tar",
    "application/x-ms-wim",
    "application/x-rar",
    "application/x-rar-compressed",
    "application/x-rpm",
    "application/x-source-rpm",
    "application/x-rzip",
    "application/x-rzip-compressed-tar",
    "application/x-tar",
    "application/x-tarz",
    "application/x-tzo",
    "application/x-stuffit",
    "application/x-war",
    "application/x-xar",
    "application/x-xz",
    "application/x-xz-compressed-tar",
    "application/x-zip",
    "application/x-zip-compressed",
    "application/x-zstd-compressed-tar",
    "application/x-zoo",
    "application/zip",
    "application/zstd",
};

bool
mime_type_is_archive_file(const std::filesystem::path& file_path, const std::string_view mime_type)
{
    std::string file_mime_type;
    if (mime_type.empty())
    {
        file_mime_type = mime_type_get_by_file(file_path);
    }
    else
    {
        file_mime_type = mime_type;
    }

    return std::ranges::find(archive_mime_types, file_mime_type) != archive_mime_types.cend();
}

/* Check if the specified mime_type is the subclass of the specified parent type */
static bool
mime_type_is_subclass(const std::string_view type, const std::string_view parent)
{
    /* special case, the type specified is identical to the parent type. */
    if (ztd::same(type, parent))
    {
        return true;
    }

    for (const mime_cache_t& cache : caches)
    {
        const std::vector<std::string> parents = cache->lookup_parents(type);
        for (const std::string_view p : parents)
        {
            if (ztd::same(parent, p))
            {
                return true;
            }
        }
    }
    return false;
}

/*
 * Get mime caches
 */
const std::span<const mime_cache_t>
mime_type_get_caches()
{
    return caches;
}

/*
 * Reload all mime caches
 */
void
mime_type_regen_all_caches()
{
    std::ranges::for_each(caches, mime_cache_reload);
}
