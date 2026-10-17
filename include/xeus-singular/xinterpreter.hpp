/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Adapted from jupyter_kernel_singular and PySingular by Sebastian Gutsche *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#ifndef XEUS_SINGULAR_INTERPRETER_HPP
#define XEUS_SINGULAR_INTERPRETER_HPP

#include <string>

#include "nlohmann/json.hpp"
#include "xeus/xinterpreter.hpp"

namespace nl = nlohmann;

namespace xsingular
{
    class interpreter : public xeus::xinterpreter
    {
    public:

        // `singular_executable` is only used by libSingular to locate its
        // resources (the LIB directory, factory's gftables, ...). When empty,
        // it is looked up the same way jupyter_kernel_singular does it: on
        // PATH, then next to the kernel executable.
        explicit interpreter(std::string singular_executable = std::string());
        ~interpreter() override = default;

    private:

        void configure_impl() override;

        void execute_request_impl(send_reply_callback cb,
                                  int execution_counter,
                                  const std::string& code,
                                  xeus::execute_request_config config,
                                  nl::json user_expressions) override;

        nl::json complete_request_impl(const std::string& code, int cursor_pos) override;

        nl::json inspect_request_impl(const std::string& code,
                                      int cursor_pos,
                                      int detail_level) override;

        nl::json is_complete_request_impl(const std::string& code) override;

        nl::json kernel_info_request_impl() override;

        nl::json shutdown_request_impl(bool restart) override;

        nl::json interrupt_request_impl() override;

        std::string publish_plot(int execution_counter, const std::string& output);

        std::string m_singular_executable;
    };
}

#endif
