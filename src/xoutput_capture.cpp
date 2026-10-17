/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#include "xoutput_capture.hpp"

#include <unistd.h>

namespace xsingular
{
    output_capture::output_capture()
        : m_out{STDOUT_FILENO, -1, nullptr, std::string()}
        , m_err{STDERR_FILENO, -1, nullptr, std::string()}
        , m_active(true)
    {
        std::fflush(nullptr);
        start(m_out);
        start(m_err);
    }

    output_capture::~output_capture()
    {
        stop();
    }

    void output_capture::stop()
    {
        if (!m_active)
        {
            return;
        }
        m_active = false;
        std::fflush(nullptr);
        finish(m_out);
        finish(m_err);
    }

    const std::string& output_capture::out() const noexcept
    {
        return m_out.text;
    }

    const std::string& output_capture::err() const noexcept
    {
        return m_err.text;
    }

    void output_capture::start(channel& c)
    {
        // On any failure the descriptor is simply left alone: the output then
        // goes wherever it went before (the kernel log, or the browser
        // console), which is never worse than losing the cell.
        c.file = std::tmpfile();
        if (c.file == nullptr)
        {
            return;
        }
        c.saved_fd = dup(c.fd);
        if (c.saved_fd < 0 || dup2(fileno(c.file), c.fd) < 0)
        {
            if (c.saved_fd >= 0)
            {
                close(c.saved_fd);
                c.saved_fd = -1;
            }
            std::fclose(c.file);
            c.file = nullptr;
        }
    }

    void output_capture::finish(channel& c)
    {
        if (c.file == nullptr)
        {
            return;
        }
        dup2(c.saved_fd, c.fd);
        close(c.saved_fd);
        c.saved_fd = -1;

        std::rewind(c.file);
        char buffer[4096];
        std::size_t n;
        while ((n = std::fread(buffer, 1, sizeof(buffer), c.file)) > 0)
        {
            c.text.append(buffer, n);
        }
        std::fclose(c.file);
        c.file = nullptr;
    }
}
