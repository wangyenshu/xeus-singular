/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Adapted from jupyter_kernel_singular and PySingular by Sebastian Gutsche *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

#ifndef XEUS_SINGULAR_CONFIG_HPP
#define XEUS_SINGULAR_CONFIG_HPP

#define XEUS_SINGULAR_VERSION_MAJOR 0
#define XEUS_SINGULAR_VERSION_MINOR 1
#define XEUS_SINGULAR_VERSION_PATCH 1

#define XEUS_SINGULAR_STRINGIFY_IMPL(x) #x
#define XEUS_SINGULAR_STRINGIFY(x) XEUS_SINGULAR_STRINGIFY_IMPL(x)

#define XEUS_SINGULAR_VERSION                         \
    XEUS_SINGULAR_STRINGIFY(XEUS_SINGULAR_VERSION_MAJOR) "." \
    XEUS_SINGULAR_STRINGIFY(XEUS_SINGULAR_VERSION_MINOR) "." \
    XEUS_SINGULAR_STRINGIFY(XEUS_SINGULAR_VERSION_PATCH)

// Directory at which jupyterlite-xeus unpacks the conda environment inside
// the browser. Singular's libraries end up in <prefix>share/singular/LIB.
#ifndef XEUS_SINGULAR_WASM_PREFIX
#define XEUS_SINGULAR_WASM_PREFIX "/"
#endif

#endif
