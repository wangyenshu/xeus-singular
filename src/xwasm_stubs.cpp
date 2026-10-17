/***************************************************************************
* xeus-singular: a Jupyter kernel for Singular, based on xeus              *
*                                                                          *
* Distributed under the terms of the GNU General Public License, version 2 *
* or (at your option) any later version. See the LICENSE file.             *
****************************************************************************/

/*
 * Named POSIX semaphores are referenced by Singular's simple IPC module
 * (Singular/links/semaphore.c) but are not provided by emscripten's libc.
 * They only matter for fork-based parallel links, which cannot work in the
 * browser anyway. Same stubs as emscripten-forge's recipes/singular/wasm_patch.c
 * (whose fix_env() part is replaced by setup_wasm_environment() in
 * xsingular_bridge.cpp).
 *
 * The definitions are weak so that a libc which does provide them wins.
 */

#include <semaphore.h>

extern "C"
{

static sem_t xsingular_fake_sem;

__attribute__((weak)) int sem_unlink(const char* name)
{
    (void)name;
    return 0;
}

__attribute__((weak)) sem_t* sem_open(const char* name, int oflag, ...)
{
    (void)name;
    (void)oflag;
    return &xsingular_fake_sem;
}

__attribute__((weak)) int sem_getvalue(sem_t* sem, int* sval)
{
    (void)sem;
    if (sval)
    {
        *sval = 1;
    }
    return 0;
}

__attribute__((weak)) int sem_close(sem_t* sem)
{
    (void)sem;
    return 0;
}

}
