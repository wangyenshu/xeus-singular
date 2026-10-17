/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#ifndef XEUS_SINGULAR_OUTPUT_CAPTURE_HPP
#define XEUS_SINGULAR_OUTPUT_CAPTURE_HPP

#include <cstdio>
#include <string>

namespace xsingular
{
    // Captures everything written to the stdout and stderr file descriptors
    // while it is alive.
    //
    // Most of Singular's output goes through PrintS/WerrorS and is collected
    // by the bridge, but warnings (WarnS), shell commands, `help` and a few
    // kernel routines write to stdout/stderr directly. jupyter_kernel_singular
    // loses that output (it ends up in the Jupyter server log); this class
    // collects it so it can be shown in the notebook, natively and in the
    // browser (emscripten's file system supports dup2 as well).
    class output_capture
    {
    public:

        output_capture();
        ~output_capture();

        output_capture(const output_capture&) = delete;
        output_capture& operator=(const output_capture&) = delete;

        // Restores the file descriptors and collects the captured text.
        void stop();

        const std::string& out() const noexcept;
        const std::string& err() const noexcept;

    private:

        struct channel
        {
            int fd;
            int saved_fd;
            std::FILE* file;
            std::string text;
        };

        static void start(channel& c);
        static void finish(channel& c);

        channel m_out;
        channel m_err;
        bool m_active;
    };
}

#endif
