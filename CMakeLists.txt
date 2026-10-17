############################################################################
# xeus-singular: a Jupyter kernel for Singular, based on xeus              #
#                                                                          #
# Distributed under the terms of the GNU General Public License, version 2 #
# or (at your option) any later version. See the LICENSE file.             #
############################################################################

cmake_minimum_required(VERSION 3.18)
project(xeus-singular LANGUAGES CXX)

set(XEUS_SINGULAR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Versioning
# ==========

file(STRINGS "${XEUS_SINGULAR_INCLUDE_DIR}/xeus-singular/xeus_singular_config.hpp" version_defines
     REGEX "#define XEUS_SINGULAR_VERSION_(MAJOR|MINOR|PATCH)")
foreach(ver ${version_defines})
    if(ver MATCHES "#define XEUS_SINGULAR_VERSION_(MAJOR|MINOR|PATCH) +([^ ]+)$")
        set(XEUS_SINGULAR_VERSION_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" CACHE INTERNAL "")
    endif()
endforeach()
set(${PROJECT_NAME}_VERSION
    ${XEUS_SINGULAR_VERSION_MAJOR}.${XEUS_SINGULAR_VERSION_MINOR}.${XEUS_SINGULAR_VERSION_PATCH})
message(STATUS "Building xeus-singular v${${PROJECT_NAME}_VERSION}")

# Options
# =======

include(GNUInstallDirs)

set(XEUS_SINGULAR_KERNELSPEC_NAME "xsingular" CACHE STRING
    "Name of the installed kernelspec. Use 'singular' to open notebooks written for jupyter_kernel_singular without picking a kernel.")
set(XEUS_SINGULAR_DISPLAY_NAME "Singular" CACHE STRING "Display name of the kernel")
set(XEUS_SINGULAR_WASM_PREFIX "/" CACHE STRING
    "Browser build: where jupyterlite-xeus unpacks the environment")
set(XEUS_SINGULAR_WASM_ENVIRONMENT "web,worker" CACHE STRING
    "Browser build: emscripten ENVIRONMENT (add ',node' to run it in Node.js)")
set(XEUS_SINGULAR_WASM_LIBS
    Singular polys singular_resources factory omalloc
    mathicgb mathic memtailor cddgmp flint ntl mpfr gmp
    CACHE STRING
    "Browser build: static libraries to link, libSingular and everything it references")

if(NOT DEFINED XEUS_SINGULAR_KERNELSPEC_PATH)
    set(XEUS_SINGULAR_KERNELSPEC_PATH "${CMAKE_INSTALL_FULL_BINDIR}/")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
# ============

if(EMSCRIPTEN)
    # emscripten-forge's xeus exports libxeus.so (a side module, loaded at
    # runtime like for xeus-lua and xeus-octave), which CMake refuses on
    # this platform unless told otherwise.
    set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
    # Emscripten's toolchain file only looks for packages inside its sysroot.
    # Also search CMAKE_PREFIX_PATH (the emscripten-wasm32 environment), as
    # emscripten-forge's CMAKE_ARGS do.
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
endif()

find_package(xeus 6.0 REQUIRED)

if(EMSCRIPTEN)
    find_package(xeus-lite 5.0 REQUIRED)

    # libSingular is a static library in emscripten-forge. Its pkg-config
    # files do not list mathicgb or cddlib and carry -rpath flags, so the
    # libraries are listed explicitly.
    find_path(SINGULAR_INCLUDE_DIR Singular/libsingular.h
              HINTS ${CMAKE_PREFIX_PATH} ${CMAKE_INSTALL_PREFIX}
              PATH_SUFFIXES include/singular
              NO_CMAKE_FIND_ROOT_PATH
              REQUIRED)
    get_filename_component(SINGULAR_BASE_INCLUDE_DIR "${SINGULAR_INCLUDE_DIR}" DIRECTORY)

    set(SINGULAR_LIBRARIES "")
    foreach(lib ${XEUS_SINGULAR_WASM_LIBS})
        find_library(XEUS_SINGULAR_LIB_${lib} NAMES ${lib}
                     HINTS ${CMAKE_PREFIX_PATH} ${CMAKE_INSTALL_PREFIX}
                     PATH_SUFFIXES lib
                     NO_CMAKE_FIND_ROOT_PATH
                     REQUIRED)
        list(APPEND SINGULAR_LIBRARIES ${XEUS_SINGULAR_LIB_${lib}})
    endforeach()

    add_library(xsingular_libsingular INTERFACE)
    target_include_directories(xsingular_libsingular INTERFACE
                               ${SINGULAR_INCLUDE_DIR} ${SINGULAR_BASE_INCLUDE_DIR})
    target_compile_definitions(xsingular_libsingular INTERFACE SING_NDEBUG OM_NDEBUG)
    target_link_libraries(xsingular_libsingular INTERFACE ${SINGULAR_LIBRARIES})
else()
    find_package(xeus-zmq 4.0 REQUIRED)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SINGULAR REQUIRED IMPORTED_TARGET Singular)

    add_library(xsingular_libsingular INTERFACE)
    target_link_libraries(xsingular_libsingular INTERFACE PkgConfig::SINGULAR)
endif()

# Kernel
# ======

set(XEUS_SINGULAR_SRC
    include/xeus-singular/xeus_singular_config.hpp
    include/xeus-singular/xinterpreter.hpp
    src/xinterpreter.cpp
    src/xoutput_capture.cpp
    src/xoutput_capture.hpp
    src/xsingular_bridge.cpp
    src/xsingular_bridge.hpp
)

if(EMSCRIPTEN)
    include(WasmBuildOptions)

    add_executable(xsingular src/main_emscripten_kernel.cpp src/xwasm_stubs.cpp ${XEUS_SINGULAR_SRC})
    target_link_libraries(xsingular PRIVATE xeus-lite xsingular_libsingular)
    target_compile_definitions(xsingular PRIVATE
                               XEUS_SINGULAR_WASM_PREFIX="${XEUS_SINGULAR_WASM_PREFIX}")
    xeus_wasm_compile_options(xsingular)
    xeus_wasm_link_options(xsingular "${XEUS_SINGULAR_WASM_ENVIRONMENT}")
    # Route Singular's m2_end() (called by quit/exit) through
    # __wrap_m2_end in xsingular_bridge.cpp, see there.
    target_link_options(xsingular PRIVATE "SHELL: -Wl,--wrap=m2_end")
else()
    add_executable(xsingular src/main.cpp ${XEUS_SINGULAR_SRC})
    target_link_libraries(xsingular PRIVATE xeus-zmq xsingular_libsingular Threads::Threads)
    set_target_properties(xsingular PROPERTIES
                          INSTALL_RPATH_USE_LINK_PATH TRUE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(xsingular PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endif()

target_include_directories(xsingular PRIVATE
                           ${XEUS_SINGULAR_INCLUDE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Kernelspec
# ==========

set(XEUS_SINGULAR_KERNELSPEC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/share/jupyter/kernels/xsingular)
configure_file(${XEUS_SINGULAR_KERNELSPEC_SRC}/kernel.json.in
               ${CMAKE_CURRENT_BINARY_DIR}/kernelspec/kernel.json)

# Installation
# ============

if(EMSCRIPTEN)
    install(FILES
            "$<TARGET_FILE_DIR:xsingular>/xsingular.js"
            "$<TARGET_FILE_DIR:xsingular>/xsingular.wasm"
            DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
    install(TARGETS xsingular RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/kernelspec/kernel.json
        ${XEUS_SINGULAR_KERNELSPEC_SRC}/logo-32x32.png
        ${XEUS_SINGULAR_KERNELSPEC_SRC}/logo-64x64.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/jupyter/kernels/${XEUS_SINGULAR_KERNELSPEC_NAME})
