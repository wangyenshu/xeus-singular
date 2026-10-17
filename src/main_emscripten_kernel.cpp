/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

// Browser kernel, loaded by jupyterlite-xeus.

#include <emscripten/bind.h>

#include "xeus/xembind.hpp"

#include "xeus-singular/xinterpreter.hpp"

EMSCRIPTEN_BINDINGS(my_module)
{
    xeus::export_core();
    using interpreter_type = xsingular::interpreter;
    xeus::export_kernel<interpreter_type>("xkernel");
}
