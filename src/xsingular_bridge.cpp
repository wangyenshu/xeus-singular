/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Adapted from jupyter_kernel_singular and PySingular by Sebastian Gutsche *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#include "xsingular_bridge.hpp"

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <signal.h>
#else
#include <sys/stat.h>
#endif

#include <Singular/libsingular.h>
#include <Singular/cntrlc.h>
#include <resources/feResource.h>

#include "xeus-singular/xeus_singular_config.hpp"

// Exported by libSingular but not declared in its installed headers.
// PySingular resets `inerror` the same way; iiArithGetCmd is what Singular's
// own readline completer enumerates commands with.
extern int inerror;
const char* iiArithGetCmd(int nPos);

namespace xsingular
{
    namespace bridge
    {
        namespace
        {
            std::string singular_return;
            std::string singular_error;
            bool singular_initialized = false;

            void print_callback(const char* s)
            {
                singular_return += s;
            }

            // PySingular concatenates the bare messages ("`x` is undefinederror
            // occurred in ..."); format them like Singular's terminal does.
            void error_callback(const char* s)
            {
                singular_error += "   ? ";
                singular_error += s;
                singular_error += '\n';
            }

#ifndef __EMSCRIPTEN__
            // Replaces Singular's own SIGINT handler, which prompts on the
            // terminal ("abort after this command(a), ...?") and blocks on
            // stdin. Setting siCntrlc is what answering `a` does: the
            // interpreter aborts the cell with "abort..." at the end of the
            // current command, and the Groebner basis loops (bba, mora) stop
            // early. Singular 4.4.1 can crash when std() is cut short this
            // way (its own executable crashes too); the kernel then dies and
            // Jupyter restarts it, see restore_default_crash_handlers.
            void sigint_handler(int)
            {
                siCntrlc = TRUE;
            }

            void install_sigint_handler()
            {
                struct sigaction action;
                std::memset(&action, 0, sizeof(action));
                action.sa_handler = sigint_handler;
                sigemptyset(&action.sa_mask);
                action.sa_flags = SA_RESTART;
                sigaction(SIGINT, &action, nullptr);
            }

            // Singular's SIGSEGV/SIGBUS/... handler prints "trying to
            // restart..." and longjmps to the restart point that the Singular
            // executable sets up in its main(). A library has no such point,
            // so the jump lands in an empty jmp_buf. Let the kernel die
            // instead: Jupyter notices and restarts it.
            void restore_default_crash_handlers()
            {
                const int signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
                for (int sig : signals)
                {
                    signal(sig, SIG_DFL);
                }
            }
#else
            void set_default_env(const char* name, const std::string& value)
            {
                // Do not override values set by the user or the frontend.
                setenv(name, value.c_str(), 0);
            }

            // In the browser there is no executable to derive the resource
            // directories from, so point Singular at the unpacked conda
            // environment explicitly (the standalone Singular.js build of
            // emscripten-forge does the same in its wasm_patch.c, with paths
            // matching its own .data bundle).
            void setup_wasm_environment()
            {
                std::string prefix = XEUS_SINGULAR_WASM_PREFIX;
                if (prefix.empty() || prefix.back() != '/')
                {
                    prefix += '/';
                }
                // Singular checks that its BinDir and /tmp exist. jupyterlite
                // does not ship bin/ (the packer skips it), so create both.
                mkdir((prefix + "bin").c_str(), 0755);
                mkdir("/tmp", 0777);

                set_default_env("TMPDIR", "/tmp");
                set_default_env("SINGULAR_EXECUTABLE", prefix + "bin/Singular");
                set_default_env("SINGULAR_BIN_DIR", prefix + "bin");
                set_default_env("SINGULAR_ROOT_DIR", prefix);
                set_default_env("SINGULAR_DATA_DIR", prefix + "share/");
                set_default_env("SINGULARPATH", prefix + "share/singular/LIB");
            }
#endif
        }

        void initialize(const std::string& singular_executable)
        {
            if (singular_initialized)
            {
                return;
            }

#ifdef __EMSCRIPTEN__
            setup_wasm_environment();
#endif

            // Same sequence as PySingular's InitializeSingular.
            std::vector<char> argv0(singular_executable.begin(), singular_executable.end());
            argv0.push_back('\0');
            siInit(argv0.data());
            init_signals();
#ifndef __EMSCRIPTEN__
            install_sigint_handler();
            restore_default_crash_handlers();
#endif
            errorreported = 0;
            WerrorS_callback = error_callback;
            PrintS_callback = print_callback;

            singular_return.clear();
            singular_error.clear();
            singular_initialized = true;
        }

        result run(const std::string& code)
        {
            if (!singular_initialized)
            {
                return { true, std::string(), "   ? Singular is not initialized\n" };
            }

            singular_return.clear();
            singular_error.clear();
            // An interrupt that arrived while the kernel was idle must not
            // abort the next cell.
            siCntrlc = FALSE;

            // Same as PySingular's RunSingularCommand: execute the code as
            // the body of a procedure ending with return().
            static const char return_str[] = "\nreturn();";
            char* ost = static_cast<char*>(omalloc0(code.size() + sizeof(return_str) + 1));
            std::memcpy(ost, code.data(), code.size());
            std::memcpy(ost + code.size(), return_str, sizeof(return_str));
            BOOLEAN err = iiAllStart(NULL, ost, BT_proc, 0);
            inerror = 0;
            errorreported = 0;
            omFree(ost);

            // PySingular only looks at the return value of iiAllStart. With
            // current Singular (checked with 4.4.1) it returns FALSE even when
            // a statement fails, so jupyter_kernel_singular silently drops
            // every error message. Whether WerrorS was called is reliable.
            const bool failed = err || !singular_error.empty();
            return { failed, singular_return, singular_error };
        }

        std::vector<std::string> complete(const std::string& prefix)
        {
            std::set<std::string> matches;
            if (!singular_initialized || prefix.empty())
            {
                return {};
            }

            auto add_if_match = [&](const char* name)
            {
                if (name != NULL && std::strncmp(name, prefix.c_str(), prefix.size()) == 0)
                {
                    matches.insert(name);
                }
            };

            // Same candidates as command_generator in Singular's
            // kernel/oswrapper/feread.cc: reserved words and kernel commands,
            // then (for prefixes of two or more characters) identifiers.
            const char* name;
            for (int i = 1; (name = iiArithGetCmd(i)) != NULL; ++i)
            {
                add_if_match(name);
            }

            if (prefix.size() > 1)
            {
                for (idhdl h = basePack->idroot; h != NULL; h = h->next)
                {
                    add_if_match(IDID(h));
                    // Also look inside the packages created by LIB commands,
                    // so library procedures complete once they are loaded.
                    if (IDTYP(h) == PACKAGE_CMD && IDPACKAGE(h) != basePack)
                    {
                        for (idhdl p = IDPACKAGE(h)->idroot; p != NULL; p = p->next)
                        {
                            if (IDTYP(p) == PROC_CMD && IDPROC(p)->is_static)
                            {
                                continue;
                            }
                            add_if_match(IDID(p));
                        }
                    }
                }
            }

            return std::vector<std::string>(matches.begin(), matches.end());
        }

        std::vector<std::string> library_dirs()
        {
            std::vector<std::string> dirs;
            if (!singular_initialized)
            {
                return dirs;
            }
            const char* search_path = feResource('s', 0);
            if (search_path == NULL)
            {
                return dirs;
            }
            std::string current;
            for (const char* c = search_path; ; ++c)
            {
                if (*c == ':' || *c == ';' || *c == '\0')
                {
                    if (!current.empty())
                    {
                        dirs.push_back(current);
                    }
                    current.clear();
                    if (*c == '\0')
                    {
                        break;
                    }
                }
                else
                {
                    current += *c;
                }
            }
            return dirs;
        }

        void request_interrupt() noexcept
        {
            siCntrlc = TRUE;
        }

#ifndef __EMSCRIPTEN__
        namespace
        {
            sigset_t singular_signals()
            {
                sigset_t set;
                sigemptyset(&set);
                sigaddset(&set, SIGINT);
                sigaddset(&set, SIGCHLD);
                return set;
            }
        }

        void block_signals_for_new_threads() noexcept
        {
            const sigset_t set = singular_signals();
            pthread_sigmask(SIG_BLOCK, &set, nullptr);
        }

        void unblock_signals_on_this_thread() noexcept
        {
            const sigset_t set = singular_signals();
            pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
        }
#else
        void block_signals_for_new_threads() noexcept
        {
        }

        void unblock_signals_on_this_thread() noexcept
        {
        }
#endif

        std::string version()
        {
            return VERSION;
        }
    }
}

#ifdef __EMSCRIPTEN__
// `quit;` and `exit;` call m2_end(1), which calls exit(0). Natively the
// process dies and Jupyter restarts the kernel, which is what
// jupyter_kernel_singular does too. In the browser exit() tears down the
// WebAssembly runtime and leaves a dead kernel, so the linker redirects calls
// to m2_end here (-Wl,--wrap=m2_end): quitting reports an error instead.
//
// The original m2_end is not called (wasm-ld leaves __real_ symbols
// unresolved in a MAIN_MODULE). What it cleans up besides exiting are
// profiling files and fork-based links, neither of which exist here.
extern "C" void __wrap_m2_end(int i)
{
    if (i == 1)
    {
        WerrorS("quit/exit cannot end Singular in the browser; restart the kernel instead");
        return;
    }
    // m2_end(0) means "clean up and return"; other values are fatal paths.
    if (i > 1)
    {
        std::exit(i);
    }
    if (i < 0)
    {
        std::_Exit(0);
    }
}
#endif
