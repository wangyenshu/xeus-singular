/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

// Native kernel (ZeroMQ transport). The browser build uses
// main_emscripten_kernel.cpp instead.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "xeus/xhelper.hpp"
#include "xeus/xkernel.hpp"
#include "xeus/xkernel_configuration.hpp"

#include "xeus-zmq/xserver_zmq.hpp"
#include "xeus-zmq/xzmq_context.hpp"

#include "xeus-singular/xeus_singular_config.hpp"
#include "xeus-singular/xinterpreter.hpp"

int main(int argc, char* argv[])
{
    if (xeus::should_print_version(argc, argv))
    {
        std::clog << "xsingular " << XEUS_SINGULAR_VERSION << std::endl;
        return 0;
    }

    // When started by Jupyter, keep xeus' diagnostics out of the kernel log.
    if (std::getenv("JPY_PARENT_PID") != nullptr)
    {
        std::clog.setstate(std::ios_base::failbit);
    }

    std::unique_ptr<xeus::xcontext> context = xeus::make_zmq_context();
    auto interpreter = std::make_unique<xsingular::interpreter>();
    auto history = xeus::make_in_memory_history_manager();

    // Singular output may contain bytes that are not valid UTF-8; replace
    // them instead of failing to serialize the message.
    const auto error_handler = nl::json::error_handler_t::replace;

    const std::string connection_filename = xeus::extract_filename(argc, argv);

    if (!connection_filename.empty())
    {
        xeus::xconfiguration config = xeus::load_configuration(connection_filename);
        xeus::xkernel kernel(config,
                             xeus::get_user_name(),
                             std::move(context),
                             std::move(interpreter),
                             xeus::make_xserver_default,
                             std::move(history),
                             nullptr,
                             xeus::make_null_debugger,
                             nl::json::object(),
                             error_handler);

        std::clog << "Starting xsingular kernel...\n\n"
                     "If you want to connect to this kernel from an other client, you can use the "
                  << connection_filename << " file." << std::endl;

        kernel.start();
    }
    else
    {
        xeus::xkernel kernel(xeus::get_user_name(),
                             std::move(context),
                             std::move(interpreter),
                             xeus::make_xserver_default,
                             std::move(history),
                             nullptr,
                             xeus::make_null_debugger,
                             nl::json::object(),
                             error_handler);

        const auto& config = kernel.get_config();
        std::clog << "Starting xsingular kernel...\n\n"
                     "If you want to connect to this kernel from an other client, just copy"
                     " and paste the following content inside of a `kernel.json` file. And then run for example:\n\n"
                     "# jupyter console --existing kernel.json\n\n"
                     "kernel.json\n```\n{\n"
                  << "    \"transport\": \"" << config.m_transport << "\",\n"
                  << "    \"ip\": \"" << config.m_ip << "\",\n"
                  << "    \"control_port\": " << config.m_control_port << ",\n"
                  << "    \"shell_port\": " << config.m_shell_port << ",\n"
                  << "    \"stdin_port\": " << config.m_stdin_port << ",\n"
                  << "    \"iopub_port\": " << config.m_iopub_port << ",\n"
                  << "    \"hb_port\": " << config.m_hb_port << ",\n"
                  << "    \"signature_scheme\": \"" << config.m_signature_scheme << "\",\n"
                  << "    \"key\": \"" << config.m_key << "\"\n"
                  << "}\n```" << std::endl;

        kernel.start();
    }

    return 0;
}
