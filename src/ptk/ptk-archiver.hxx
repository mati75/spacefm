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

#include <filesystem>

#include <span>

#include <gtkmm.h>

#include "vfs/vfs-file-info.hxx"

#include "ptk/ptk-file-browser.hxx"

void ptk_archiver_create(PtkFileBrowser* file_browser,
                         const std::span<const vfs::file_info> sel_files);

void ptk_archiver_extract(PtkFileBrowser* file_browser,
                          const std::span<const vfs::file_info> sel_files,
                          const std::filesystem::path& dest_dir);

void ptk_archiver_open(PtkFileBrowser* file_browser,
                       const std::span<const vfs::file_info> sel_files);
