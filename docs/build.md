# Building

## Toolchain

- Windows 10 or 11, x64.
- Visual Studio 2026 with the **Desktop development with C++** workload (MSVC, Windows SDK, CMake, Ninja).
- CMake 3.25 or newer for the Ninja preset. The `Visual Studio 18 2026` generator needs CMake 4.2 or newer; the CMake bundled with Visual Studio 2026 qualifies.

The Ninja preset needs the MSVC environment: use a **Developer PowerShell for VS 2026**, or the CMake Tools extension in VS Code, which sets the environment up by itself. A regular terminal does not have MSVC and Ninja on its `PATH`.

## Presets

| Configure preset | Generator | Use it for |
|---|---|---|
| `windows-ninja` | Ninja Multi-Config | Command line, VS Code, Visual Studio "Open Folder" |
| `windows-vs` | Visual Studio 18 2026 | A `.sln` solution |

Build presets: `ninja-debug`, `ninja-release`, `ninja-relwithdebinfo`, `vs-debug`, `vs-release`, `vs-relwithdebinfo`. Test presets: `ninja-debug`, `ninja-release`, `vs-debug`, `vs-release`.

```powershell
cmake --preset windows-ninja
cmake --build --preset ninja-debug
ctest --preset ninja-debug
.\build\windows-ninja\bin\Debug\sandbox.exe
```

In the solution, `sandbox` is the startup project, so F5 runs it.

### When the build environment changes

If Visual Studio is updated while a terminal is open, that terminal keeps paths to the old compiler version, and builds fail with errors such as "cannot open include file 'cstddef'". Close every terminal and editor, open a new Developer PowerShell, delete `build\windows-ninja` and configure again.

## Options

| Option | Default | Effect |
|---|---|---|
| `ENGINE_WARNINGS_AS_ERRORS` | `ON` | Adds `/WX` (MSVC) or `-Werror` |
| `ENGINE_BUILD_TESTS` | `ON` | Builds the tests and enables CTest |
| `ENGINE_USE_PCH` | `ON` | Uses the precompiled header for engine targets |

## Compiler flags

Engine targets (layers, executables, tests) get, through `engine_configure_target()`:

- MSVC: `/W4 /permissive- /utf-8 /Zc:preprocessor /Zc:__cplusplus /MP`, plus `/WX`.
- Other compilers: `-Wall -Wextra -Wpedantic -Wshadow`, plus `-Werror`.
- C++20, without compiler extensions.
- `AURORA_DEBUG=1` in Debug, as a public definition, so every translation unit sees the same value.

Third-party libraries are built with their own flags, and their headers are included as system headers, so our warning level does not apply to them.

## Precompiled header

Every engine target precompiles the standard headers that the core headers include: `<atomic> <cstddef> <cstdint> <format> <limits> <string_view>`. `<format>` alone costs about half a second per file, and every file that asserts or logs includes it, so the precompiled header pays for itself quickly.

There is nothing to include in the source files. CMake generates the header and injects it into every file of the target from the command line (`/FI` and `/Yu` on MSVC). The `#include <format>` that `log.h` still has costs nothing when the header was already loaded.

Two rules follow:

1. **Every file still includes what it uses**, as if there were no precompiled header. Configure with `-DENGINE_USE_PCH=OFF` from time to time to check that.
2. **Keep `<windows.h>` out of the precompiled header.** The header is injected before the first line of each file, so a `#define NOMINMAX` written in a `.cpp` would come too late.

Engine headers are not precompiled, so changing an engine header does not rebuild the precompiled header.

## Adding a layer

```cmake
# engine/platform/CMakeLists.txt
engine_add_layer(platform
    SOURCES
        src/win32/window.cpp
        include/platform/window.h
    DEPS
        aurora::core)
```

The layer must have this structure:

```
engine/<layer>/
├── CMakeLists.txt
├── include/<layer>/   public headers, included as "<layer>/file.h"
└── src/               implementation and private headers
```

Then add `add_subdirectory(<layer>)` to `engine/CMakeLists.txt`. Source lists are explicit: every new file is one line in the layer's `CMakeLists.txt`.

## Adding an executable

```cmake
engine_add_executable(editor
    SOURCES src/main.cpp
    DEPS    aurora::core aurora::render)
```

`engine_add_executable()` also links `aurora::new_delete`, the replacement global `new` and `delete` that route through the engine heap. See [Heap](core/memory/heap.md#global-new-and-delete).

## Third-party libraries

Libraries are fetched by CMake at pinned versions (`FetchContent`, declared `SYSTEM`):

| Library | Version | Used by |
|---|---|---|
| mimalloc | v3.6.1 | `core` (heap); static library, no `malloc` override |
| doctest | v2.5.3 | `tests/` only |

mimalloc's own checks run at level 2 in Debug and are off in the other configurations. See [Heap](core/memory/heap.md#mimalloc-configuration).
