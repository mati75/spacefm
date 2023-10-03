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

#include <format>

#include <memory>

#include <fmt/format.h>

#include <CLI/CLI.hpp>

#include <nlohmann/json.hpp>

#include <zmqpp/zmqpp.hpp>
#include <zmqpp/socket_options.hpp>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "ipc.hxx"

#include "commandline/socket/subcommands.hxx"

#include "commandline/socket.hxx"

[[noreturn]] void
run_subcommand_socket(const socket_subcommand_data_t& opt)
{
    // connect to server
    zmqpp::context context;
    zmqpp::socket socket(context, zmqpp::socket_type::pair);
    socket.set(zmqpp::socket_option::receive_timeout, 1000);
    try
    {
        socket.connect(std::format("tcp://localhost:{}", ZMQ_PORT));
    }
    catch (const zmqpp::exception& e)
    {
        fmt::print("Failed to connect to server\n");
        std::exit(EXIT_FAILURE);
    }

    // server request
    nlohmann::json server_json;
    // flags
    server_json["window"] = opt->window;
    server_json["panel"] = opt->panel;
    server_json["tab"] = opt->tab;
    // socket data
    server_json["command"] = opt->command;
    server_json["property"] = opt->property;
    server_json["subproperty"] = opt->subproperty;
    server_json["data"] = opt->socket_data;
    // fmt::print("JSON : {}\n", server_json.dump());

    // ztd::logger::debug("Sending message {}\n", server_json.dump());
    const bool sent = socket_send_command(socket, server_json.dump());
    if (!sent)
    {
        fmt::print("Failed to send command to server\n");
        std::exit(EXIT_FAILURE);
    }

    // server response
    const auto server_response = socket_receive_response(socket);
    if (!server_response)
    {
        fmt::print("Failed to receive response from server\n");
        std::exit(EXIT_FAILURE);
    }
    const nlohmann::json response_json = nlohmann::json::parse(server_response.value());
    const i32 ret = response_json["exit"];
    const std::string response = response_json["response"];

    if (!response.empty())
    {
        fmt::print("{}\n", response);
    }
    std::exit(ret);
}

void
setup_subcommand_socket(CLI::App& app)
{
    auto opt = std::make_shared<socket_subcommand_data>();
    CLI::App* sub = app.add_subcommand("socket", "Send a socket command (See subcommand help)");

    // named flags
    sub->add_option("-w,--window", opt->window, "Window to use");
    sub->add_option("-p,--panel", opt->panel, "Panel to use");
    sub->add_option("-t,--tab", opt->tab, "tab to use");

    const auto run_subcommand = [opt]() { run_subcommand_socket(opt); };
    sub->callback(run_subcommand);

    // socket subcommand is used to run other subcommands make sure that they are used.
    sub->require_subcommand();

    setup_subcommand_set(sub, opt);
    setup_subcommand_get(sub, opt);
    setup_subcommand_set_task(sub, opt);
    setup_subcommand_get_task(sub, opt);
    setup_subcommand_run_task(sub, opt);
    setup_subcommand_emit_key(sub, opt);
    setup_subcommand_activate(sub, opt);
    setup_subcommand_add_event(sub, opt);
    setup_subcommand_replace_event(sub, opt);
    setup_subcommand_remove_event(sub, opt);
    setup_subcommand_help(sub, opt);
    setup_subcommand_ping(sub, opt);
}
