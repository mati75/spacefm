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

#include <string_view>

#include <filesystem>

#include <map>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "scripts.hxx"

// #define SPACEFM_USER_SCRIPT_OVERRIDE

using namespace std::literals::string_view_literals;

static const std::map<spacefm::script, const std::string_view> script_map{
    {spacefm::script::config_update, "config-update"sv},
    {spacefm::script::config_update_git, "config-update-git"sv},
};

bool
script_exists(spacefm::script script) noexcept
{
    const auto script_name = get_script_path(script);

    if (!std::filesystem::exists(script_name))
    {
        ztd::logger::error("Missing script: {}", script_name.string());
        return false;
    }
    return true;
}

bool
script_exists(const std::filesystem::path& script) noexcept
{
    if (!std::filesystem::exists(script))
    {
        ztd::logger::error("Missing script {}", script.string());
        return false;
    }
    return true;
}

const std::filesystem::path
get_script_path(spacefm::script script) noexcept
{
    return std::filesystem::path() / PACKAGE_SCRIPTS_PATH / script_map.at(script);
}
