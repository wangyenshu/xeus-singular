/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Adapted from jupyter_kernel_singular and PySingular by Sebastian Gutsche *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#include "xeus-singular/xinterpreter.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "xeus/xhelper.hpp"

#include "xeus-singular/xeus_singular_config.hpp"
#include "xoutput_capture.hpp"
#include "xsingular_bridge.hpp"

namespace xsingular
{
    namespace
    {
        bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        // Python's str.rstrip()
        std::string rstrip(const std::string& s)
        {
            std::size_t end = s.size();
            while (end > 0 && is_space(s[end - 1]))
            {
                --end;
            }
            return s.substr(0, end);
        }

        // Python's `not s.strip()`
        bool is_blank(const std::string& s)
        {
            return std::all_of(s.begin(), s.end(), is_space);
        }

        bool contains(const std::string& s, const char* needle)
        {
            return s.find(needle) != std::string::npos;
        }

        // Singular's output is mostly ASCII, but library help texts and user
        // strings can contain Latin-1 bytes. nlohmann_json throws on invalid
        // UTF-8 when the message is serialized, which would kill the kernel,
        // so invalid sequences are replaced by U+FFFD.
        std::string to_valid_utf8(const std::string& s)
        {
            std::string res;
            res.reserve(s.size());
            const auto* p = reinterpret_cast<const unsigned char*>(s.data());
            const std::size_t n = s.size();
            std::size_t i = 0;
            while (i < n)
            {
                const unsigned char c = p[i];
                std::size_t len = 0;
                std::uint32_t min_cp = 0;
                if (c < 0x80) { len = 1; }
                else if ((c & 0xE0) == 0xC0) { len = 2; min_cp = 0x80; }
                else if ((c & 0xF0) == 0xE0) { len = 3; min_cp = 0x800; }
                else if ((c & 0xF8) == 0xF0) { len = 4; min_cp = 0x10000; }

                bool valid = len > 0 && i + len <= n;
                std::uint32_t cp = 0;
                if (valid && len > 1)
                {
                    cp = c & (0xFF >> (len + 1));
                    for (std::size_t k = 1; k < len; ++k)
                    {
                        if ((p[i + k] & 0xC0) != 0x80)
                        {
                            valid = false;
                            break;
                        }
                        cp = (cp << 6) | (p[i + k] & 0x3F);
                    }
                    valid = valid && cp >= min_cp && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
                }

                if (valid)
                {
                    res.append(s, i, len);
                    i += len;
                }
                else
                {
                    res += "\xEF\xBF\xBD";
                    ++i;
                }
            }
            return res;
        }

        // Jupyter cursor positions count unicode code points, not bytes.
        std::size_t byte_offset(const std::string& s, int code_points)
        {
            std::size_t i = 0;
            int count = 0;
            while (i < s.size() && count < code_points)
            {
                ++i;
                while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
                {
                    ++i;
                }
                ++count;
            }
            return i;
        }

        int code_point_count(const std::string& s, std::size_t bytes)
        {
            int count = 0;
            for (std::size_t i = 0; i < bytes && i < s.size(); ++i)
            {
                if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                {
                    ++count;
                }
            }
            return count;
        }

        std::string base64_encode(const std::string& data)
        {
            static const char table[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve(((data.size() + 2) / 3) * 4);
            std::size_t i = 0;
            for (; i + 2 < data.size(); i += 3)
            {
                const std::uint32_t v = (static_cast<unsigned char>(data[i]) << 16)
                                      | (static_cast<unsigned char>(data[i + 1]) << 8)
                                      | static_cast<unsigned char>(data[i + 2]);
                out += table[(v >> 18) & 0x3F];
                out += table[(v >> 12) & 0x3F];
                out += table[(v >> 6) & 0x3F];
                out += table[v & 0x3F];
            }
            const std::size_t rest = data.size() - i;
            if (rest > 0)
            {
                std::uint32_t v = static_cast<unsigned char>(data[i]) << 16;
                if (rest == 2)
                {
                    v |= static_cast<unsigned char>(data[i + 1]) << 8;
                }
                out += table[(v >> 18) & 0x3F];
                out += table[(v >> 12) & 0x3F];
                out += rest == 2 ? table[(v >> 6) & 0x3F] : '=';
                out += '=';
            }
            return out;
        }

        bool is_identifier_char(char c)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_';
        }

        // Whether position `pos` lies inside a "..." string literal.
        bool inside_string(const std::string& code, std::size_t pos)
        {
            bool in_string = false;
            for (std::size_t i = 0; i < pos && i < code.size(); ++i)
            {
                if (code[i] == '\\' && in_string)
                {
                    ++i;
                }
                else if (code[i] == '"')
                {
                    in_string = !in_string;
                }
            }
            return in_string;
        }

        bool is_directory(const std::string& path)
        {
            struct stat st;
            return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }

        // Inside a string Singular's readline completer completes file names
        // (handy for LIB "..." and read("...")). Same here.
        std::vector<std::string> complete_filename(const std::string& prefix)
        {
            const std::size_t slash = prefix.rfind('/');
            const std::string dir = slash == std::string::npos ? std::string() : prefix.substr(0, slash + 1);
            const std::string base = slash == std::string::npos ? prefix : prefix.substr(slash + 1);

            std::vector<std::string> matches;
            DIR* d = opendir(dir.empty() ? "." : dir.c_str());
            if (d == nullptr)
            {
                return matches;
            }
            while (dirent* entry = readdir(d))
            {
                const std::string name = entry->d_name;
                if (name == "." || name == "..")
                {
                    continue;
                }
                if (name[0] == '.' && (base.empty() || base[0] != '.'))
                {
                    continue;
                }
                if (name.compare(0, base.size(), base) == 0)
                {
                    std::string match = dir + name;
                    if (is_directory(match))
                    {
                        match += '/';
                    }
                    matches.push_back(match);
                }
            }
            closedir(d);
            std::sort(matches.begin(), matches.end());
            return matches;
        }

        // Whether the string literal opening at `quote` is the argument of
        // LIB, e.g. `LIB "prim`.
        bool follows_lib_keyword(const std::string& code, std::size_t quote)
        {
            std::size_t end = quote;
            while (end > 0 && is_space(code[end - 1]))
            {
                --end;
            }
            if (end < 3 || code.compare(end - 3, 3, "LIB") != 0)
            {
                return false;
            }
            return end == 3 || !is_identifier_char(code[end - 4]);
        }

        // Library files from Singular's search path, so `LIB "prim<TAB>`
        // offers primdec.lib and friends.
        std::vector<std::string> complete_library(const std::string& prefix)
        {
            std::vector<std::string> matches;
            for (const auto& dir : bridge::library_dirs())
            {
                DIR* d = opendir(dir.c_str());
                if (d == nullptr)
                {
                    continue;
                }
                while (dirent* entry = readdir(d))
                {
                    const std::string name = entry->d_name;
                    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lib") == 0
                        && name.compare(0, prefix.size(), prefix) == 0)
                    {
                        matches.push_back(name);
                    }
                }
                closedir(d);
            }
            return matches;
        }

#ifndef __EMSCRIPTEN__
        bool is_executable(const std::string& path)
        {
            return !path.empty() && access(path.c_str(), X_OK) == 0 && !is_directory(path);
        }

        std::string current_executable_dir()
        {
            std::string path;
#if defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::vector<char> buffer(size + 1, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) == 0)
            {
                path = buffer.data();
            }
#else
            std::vector<char> buffer(4096, '\0');
            const ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
            if (len > 0)
            {
                path.assign(buffer.data(), static_cast<std::size_t>(len));
            }
#endif
            const std::size_t slash = path.rfind('/');
            return slash == std::string::npos ? std::string() : path.substr(0, slash);
        }

        // jupyter_kernel_singular passes `which("Singular")` to
        // InitializeSingular. Do the same, then fall back to the directory of
        // this executable (xsingular and Singular share bin/ in a conda env).
        std::string find_singular_executable()
        {
            if (const char* env = std::getenv("SINGULAR_EXECUTABLE"))
            {
                if (is_executable(env))
                {
                    return env;
                }
            }
            if (const char* path = std::getenv("PATH"))
            {
                std::stringstream ss(path);
                std::string dir;
                while (std::getline(ss, dir, ':'))
                {
                    const std::string candidate = (dir.empty() ? std::string(".") : dir) + "/Singular";
                    if (is_executable(candidate))
                    {
                        return candidate;
                    }
                }
            }
            const std::string dir = current_executable_dir();
            return dir.empty() ? std::string("Singular") : dir + "/Singular";
        }
#endif

        nl::json text_data(const std::string& text)
        {
            nl::json data = nl::json::object();
            data["text/plain"] = to_valid_utf8(text);
            return data;
        }
    }

    interpreter::interpreter(std::string singular_executable)
        : m_singular_executable(std::move(singular_executable))
    {
        xeus::register_interpreter(this);
    }

    void interpreter::configure_impl()
    {
        if (m_singular_executable.empty())
        {
#ifdef __EMSCRIPTEN__
            m_singular_executable = std::string(XEUS_SINGULAR_WASM_PREFIX) + "bin/Singular";
#else
            m_singular_executable = find_singular_executable();
#endif
        }
        bridge::initialize(m_singular_executable);
        // configure() runs while the kernel is being constructed, before
        // xeus-zmq starts its IOPUB and heartbeat threads.
        bridge::block_signals_for_new_threads();
    }

    // Port of SingularKernel.do_execute from jupyter_kernel_singular: same
    // outputs, in the same message types.
    void interpreter::execute_request_impl(send_reply_callback cb,
                                           int execution_counter,
                                           const std::string& code,
                                           xeus::execute_request_config config,
                                           nl::json /*user_expressions*/)
    {
        if (is_blank(code))
        {
            cb(xeus::create_successful_reply());
            return;
        }

        // jupyter_kernel_singular exec()s these cells as Python inside the
        // kernel process. There is no Python interpreter here.
        if (contains(code, "@python") || contains(code, "@widget"))
        {
            const std::string evalue = "@python and @widget cells run Python code, "
                                       "which xeus-singular cannot execute";
            publish_execution_error("NotImplementedError", evalue, {});
            cb(xeus::create_error_reply("NotImplementedError", evalue));
            return;
        }

        const std::string code_stripped = rstrip(code);

        // Interrupts and child-process signals belong to the thread running
        // Singular (see block_signals_for_new_threads).
        bridge::unblock_signals_on_this_thread();

        output_capture capture;
        const bridge::result output = bridge::run(code_stripped);
        capture.stop();

        // Text written straight to stdout/stderr: warnings ("// ** ..."),
        // shell commands, help. jupyter_kernel_singular leaves it on the
        // kernel's own stdout (the Jupyter server log, invisible in the
        // notebook); it is shown as stream output here.
        if (!config.silent)
        {
            if (!capture.out().empty())
            {
                publish_stream("stdout", to_valid_utf8(capture.out()));
            }
            if (!capture.err().empty())
            {
                publish_stream("stderr", to_valid_utf8(capture.err()));
            }
        }

        if (!output.error)
        {
            if (!config.silent)
            {
                if (contains(code_stripped, "plot_jupyter"))
                {
                    const std::string plot_error = publish_plot(execution_counter, output.output);
                    if (!plot_error.empty())
                    {
                        publish_execution_error("FileNotFoundError", plot_error, {});
                        cb(xeus::create_error_reply("FileNotFoundError", plot_error));
                        return;
                    }
                }
                else if (!is_blank(output.output))
                {
                    publish_execution_result(execution_counter, text_data(output.output), nl::json::object());
                }
            }
            cb(xeus::create_successful_reply());
            return;
        }

        // Singular keeps going after a failing statement at the top level, so
        // the cell may also have printed results. PySingular discards them;
        // they are shown first here.
        if (!config.silent && !is_blank(output.output))
        {
            publish_execution_result(execution_counter, text_data(output.output), nl::json::object());
        }

        // The error itself is reported exactly like jupyter_kernel_singular
        // does: "Error:" and the messages as results, then an error message
        // with an empty traceback.
        const std::string evalue = to_valid_utf8(output.errors);
        publish_execution_result(execution_counter, text_data("Error:"), nl::json::object());
        publish_execution_result(execution_counter, text_data(evalue), nl::json::object());
        publish_execution_error("", evalue, {});
        cb(xeus::create_error_reply("", evalue, nl::json::array()));
    }

    // surf_jupyter.lib's plot_jupyter() prints the base name of the image
    // that the external `surf` program rendered (natively only: the browser
    // cannot run surf). Returns an error message, empty on success.
    std::string interpreter::publish_plot(int execution_counter, const std::string& output)
    {
        const std::string filename_image = rstrip(output) + ".jpg";
        if (filename_image == ".jpg")
        {
            publish_execution_result(execution_counter,
                                     text_data("Error: No image produced - Did you forget to load surf_jupyter.lib?"),
                                     nl::json::object());
            return std::string();
        }

        std::ifstream image_file(filename_image, std::ios::binary);
        if (!image_file)
        {
            return "No such file: '" + to_valid_utf8(filename_image) + "'";
        }
        const std::string bytes((std::istreambuf_iterator<char>(image_file)), std::istreambuf_iterator<char>());

        nl::json data = nl::json::object();
        data["image/jpeg"] = base64_encode(bytes);
        nl::json metadata = nl::json::object();
        metadata["image/jpeg"] = { { "width", 400 }, { "height", 400 } };
        display_data(std::move(data), std::move(metadata), nl::json::object());
        return std::string();
    }

    nl::json interpreter::complete_request_impl(const std::string& code, int cursor_pos)
    {
        const std::size_t end = byte_offset(code, cursor_pos);
        std::size_t start = end;
        nl::json matches = nl::json::array();

        if (inside_string(code, end))
        {
            start = code.rfind('"', end - 1) + 1;
            const std::string prefix = code.substr(start, end - start);
            std::vector<std::string> candidates = complete_filename(prefix);
            if (follows_lib_keyword(code, start - 1) && prefix.find('/') == std::string::npos)
            {
                const auto libraries = complete_library(prefix);
                candidates.insert(candidates.end(), libraries.begin(), libraries.end());
                std::sort(candidates.begin(), candidates.end());
                candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            }
            for (const auto& m : candidates)
            {
                matches.push_back(to_valid_utf8(m));
            }
        }
        else
        {
            while (start > 0 && is_identifier_char(code[start - 1]))
            {
                --start;
            }
            for (const auto& m : bridge::complete(code.substr(start, end - start)))
            {
                matches.push_back(to_valid_utf8(m));
            }
        }

        return xeus::create_complete_reply(matches, code_point_count(code, start), code_point_count(code, end));
    }

    nl::json interpreter::inspect_request_impl(const std::string& /*code*/,
                                               int /*cursor_pos*/,
                                               int /*detail_level*/)
    {
        // jupyter_kernel_singular does not implement inspection either.
        return xeus::create_inspect_reply();
    }

    nl::json interpreter::is_complete_request_impl(const std::string& code)
    {
        const std::string stripped = rstrip(code);
        if (stripped.empty() || stripped.back() == ';')
        {
            return xeus::create_is_complete_reply("complete");
        }
        return xeus::create_is_complete_reply("incomplete", "  ");
    }

    nl::json interpreter::kernel_info_request_impl()
    {
        // Same language_info, banner and help links as
        // jupyter_kernel_singular, so existing notebooks keep their metadata.
        nl::json result;
        result["status"] = "ok";
        result["implementation"] = "xeus-singular";
        result["implementation_version"] = XEUS_SINGULAR_VERSION;
        result["banner"] = "Singular Jupyter Kernel";
        result["language_info"] = {
            { "name", "Singular" },
            { "version", bridge::version() },
            { "codemirror_mode", "singular" },
            { "mimetype", "text/x-singular" },
            { "file_extension", ".singular" }
        };
        result["help_links"] = nl::json::array({
            { { "text", "Singular manual" }, { "url", "http://www.singular.uni-kl.de/Manual/4-1-0/index.htm" } },
            { { "text", "Singular examples" }, { "url", "http://www.singular.uni-kl.de/Manual/4-1-0/sing_786.htm" } },
            { { "text", "Singular library" }, { "url", "http://www.singular.uni-kl.de/Manual/4-1-0/sing_870.htm" } },
            { { "text", "Singular index" }, { "url", "http://www.singular.uni-kl.de/Manual/4-1-0/sing_2557.htm" } }
        });
        return result;
    }

    nl::json interpreter::shutdown_request_impl(bool restart)
    {
        return xeus::create_shutdown_reply(restart);
    }

    nl::json interpreter::interrupt_request_impl()
    {
        // Natively Jupyter interrupts with SIGINT (handled in the bridge). An
        // interrupt_request only gets here between executions (the default
        // xeus-zmq server polls control and shell on one thread, and the
        // browser kernel is single-threaded); run() clears the flag, so this
        // is harmless.
        bridge::request_interrupt();
        return xeus::create_interrupt_reply();
    }
}
