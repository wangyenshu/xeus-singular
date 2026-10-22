# xeus-singular

Jupyter kernel for [Singular](https://www.singular.uni-kl.de/) based on
[xeus](https://github.com/jupyter-xeus/xeus). Runs natively (xeus-zmq) and in
JupyterLite (xeus-lite, emscripten-forge). Based on
[jupyter_kernel_singular](https://github.com/sebasguts/jupyter_kernel_singular)
and [PySingular](https://github.com/sebasguts/PySingular).

## Dependencies

Native (conda-forge): `xeus >=6.0,<7`, `xeus-zmq >=4.0,<5`, `nlohmann_json`,
`singular`, `pkg-config`.

WebAssembly (emscripten-forge, emscripten 4.0.9): `xeus >=6.0,<6.1`,
`xeus-lite >=5.0,<6`, `nlohmann_json`, `singular`, `libflint`, `ntl`, `gmp`,
`mpfr`, `cddlib`, `mathicgb`, `mathic`, `memtailor`. libSingular and its
dependencies are linked statically; `libflint` and `ntl` must match the
versions `singular`.

## Build

Native:

```bash
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_INSTALL_PREFIX=$PREFIX
cmake --build build && cmake --install build
```

WebAssembly:

```bash
emcmake cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_INSTALL_PREFIX=$PREFIX
cmake --build build && cmake --install build
```

The WebAssembly build installs `bin/xsingular.js`, `bin/xsingular.wasm` and
`share/jupyter/kernels/xsingular/`. At run time the kernel needs
`share/singular/LIB` and `share/factory` from `singular`, and
`lib/libxeus.so` from `xeus`.

## CMake options

| Option | Default | Description |
|---|---|---|
| `XEUS_SINGULAR_KERNELSPEC_NAME` | `xsingular` | Kernelspec name (`singular` matches jupyter_kernel_singular notebooks) |
| `XEUS_SINGULAR_DISPLAY_NAME` | `Singular` | Display name |
| `XEUS_SINGULAR_WASM_PREFIX` | `/` | Environment mount point in the browser |
| `XEUS_SINGULAR_WASM_ENVIRONMENT` | `web,worker` | Emscripten `ENVIRONMENT` |
| `XEUS_SINGULAR_WASM_LIBS` | libSingular and dependencies | Static libraries linked in the WebAssembly build |

## Differences from jupyter_kernel_singular

Same execution model (code run as a procedure body via `iiAllStart`), same
message types, kernel info and error format. Changes:

- Errors are reported. With Singular 4.4.1 `iiAllStart` returns success on
  error, so the original kernel shows nothing.
- Output printed before an error is kept.
- Warnings and direct stdout/stderr output are sent as stream messages.
- Completion uses the word at the cursor; includes loaded library procedures,
  file names inside strings and library names after `LIB "`.
- `is_complete` on empty input returns `complete`.
- `@python` / `@widget` cells return an error.
- Interrupt (native) aborts the running command; a segfault terminates the
  kernel instead of jumping to a nonexistent restart point.
- `quit;` / `exit;` return an error in the browser.

## Browser limitations

No `system()`, no external programs (surf, normaliz, 4ti2, topcom), no fork-based
links or interrupt. Single thread, 4 GB memory limit. 

## License

GPL-2.0-or-later.
