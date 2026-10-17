/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Adapted from jupyter_kernel_singular and PySingular by Sebastian Gutsche *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#ifndef XEUS_SINGULAR_BRIDGE_HPP
#define XEUS_SINGULAR_BRIDGE_HPP

#include <string>
#include <vector>

// Thin layer over libSingular, equivalent to PySingular's
// InitializeSingular / RunSingularCommand / GetSingularCompletion.
//
// Singular's headers define many macros (TRUE, FALSE, loop, VAR, Print, ...)
// that clash with the standard library, xeus and nlohmann_json, so they are
// only ever included from xsingular_bridge.cpp.
namespace xsingular
{
    namespace bridge
    {
        struct result
        {
            // True when Singular reported an error (WerrorS was called).
            bool error;
            // Everything printed through PrintS.
            std::string output;
            // The error messages, formatted like the Singular terminal
            // prints them ("   ? message\n").
            std::string errors;
        };

        void initialize(const std::string& singular_executable);

        // Runs `code` as the body of a procedure, like PySingular does.
        result run(const std::string& code);

        // Reserved words, kernel commands and visible identifiers starting
        // with `prefix` (the candidate list of Singular's readline completer).
        std::vector<std::string> complete(const std::string& prefix);

        // Directories of Singular's library search path (where LIB looks).
        std::vector<std::string> library_dirs();

        // Asks the running computation to stop at the next check point,
        // like answering `a` to Singular's own interrupt prompt.
        // Async-signal-safe.
        void request_interrupt() noexcept;

        // Native only (no-ops in the browser). Singular handles SIGINT
        // (interrupts) and SIGCHLD (system(), fork links). If one of xeus'
        // ZeroMQ threads takes such a signal, its poll() fails with EINTR and
        // xeus-zmq prints "Interrupted system call", which then shows up in
        // the notebook. Block them before xeus starts its threads (the
        // threads inherit the mask) and unblock them on the thread that runs
        // Singular.
        void block_signals_for_new_threads() noexcept;
        void unblock_signals_on_this_thread() noexcept;

        std::string version();
    }
}

#endif
