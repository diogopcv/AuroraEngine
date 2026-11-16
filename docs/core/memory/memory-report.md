# Memory report

```cpp
#include "core/memory/memory_report.h"

const memory::LeakSummary summary = memory::report_leaks();   // at any time
memory::shutdown_report();                                     // at the end of main
```

A leak injected with `sandbox --leak`, in a Debug build:

```
[WARN ] memory: tag Debug has 1 live blocks, 64 bytes
[INFO ] memory: thread scratch arenas hold 65536 bytes of committed virtual memory (not a leak)
[WARN ] D:/aurora/apps/sandbox/src/main.cpp(73): 1 blocks, 64 bytes [Debug]
[WARN ] memory: LEAKS at shutdown: 1 heap blocks (64 bytes), 0 bytes of committed virtual memory
```

A clean exit:

```
[INFO ] memory: thread scratch arenas hold 65536 bytes of committed virtual memory (not a leak)
[INFO ] memory: no leaks
```

## Concepts

Every memory source keeps per-tag counters where it allocates: the [heap](heap.md) counts live blocks and bytes, and every [`VirtualBuffer`](virtual-memory.md) counts committed bytes. The report **only reads** those counters; nothing in the allocators depends on it.

For each tag with something alive, it logs one line for live heap memory and one for committed virtual memory. In Debug builds it then lists live heap blocks grouped by file and line.

### What counts as a leak

| Memory | At shutdown it is |
|---|---|
| Live heap blocks, any tag | A leak |
| Committed virtual memory (arenas, pools, dense arrays) | A leak: the structure that owns it still exists |
| Committed memory of **scratch arenas** (tag `Scratch`) | **Not** a leak: it belongs to a thread, and the main thread's scratch arenas are destroyed only after `main` returns |

`LeakSummary::clean()` is `true` when there are no live heap blocks and no committed memory outside `Scratch`.

## API

```cpp
namespace aurora::memory {
struct LeakSummary {
    usize heap_blocks             = 0;
    usize heap_bytes              = 0;
    usize committed_bytes         = 0;   // all tags except Scratch
    usize scratch_committed_bytes = 0;   // reported, never a leak
    bool  clean() const;
};
LeakSummary report_leaks();
LeakSummary shutdown_report();   // report_leaks(), then "memory: no leaks" or "memory: LEAKS at shutdown"
}
```

## When to call it

- **`report_leaks()` during development**, to see where memory goes at any moment.
- **`shutdown_report()` at the end of `main`**, after every system has shut down and every thread has been joined. Today the sandbox calls it; once the platform layer has a main loop, the loop calls it.

Why at the end of `main` and not automatically with `atexit`: objects created before the `atexit` registration (static objects, the test framework, library internals) would still be alive when it runs and would show up as false leaks, and the order of destruction at exit differs between platforms. At the end of `main`, the moment is chosen by the application.

The report only logs and returns. It does not assert and does not change the exit code: a small leak in a third-party library should not stop the engine. The caller decides what to do with the summary.

## Reading the report

| Line | Meaning |
|---|---|
| `tag X has N live blocks, B bytes` | Heap memory allocated with tag X and not freed |
| `tag X has B bytes of committed virtual memory` | An arena, pool or dense array with tag X still exists |
| `thread scratch arenas hold B bytes` | Scratch arenas of the threads still alive; information only |
| `file(line): N blocks` | In Debug, where the live heap blocks were allocated (only through `AURORA_ALLOC`) |
| `<operator new>(0)` | Blocks from the global `new`: standard containers, foreign code, the runtime |

On MSVC, `file(line)` lines are clickable in the Output window.

## Pitfalls and advice

- **Join every thread before the report.** A thread's scratch arenas and `thread_local` objects are destroyed when the thread ends.
- **Destroy static containers before the report**, or keep them out of the engine. A static `std::vector` that still holds memory is reported under `General`.
- **The runtime may allocate through `new`.** MinGW's libstdc++, for example, allocates 32 bytes with `new` to register the destructor of a `thread_local` object, and frees them only when the thread ends; a clean MinGW build therefore reports one `General` block when the main thread used scratch arenas. MSVC's runtime is expected to keep that bookkeeping outside `operator new` (the sandbox test confirms it on each MSVC build).
- **Tests cannot expect a clean report.** The test framework keeps allocations of its own. Tests compare values before and after; the clean and leaking cases are tested by running the sandbox.

## Tests

- `tests/core/memory_report_tests.cpp`: a deliberate leak reported with its file and line; a live arena reported as committed memory; scratch memory reported apart; `clean()`.
- CTest runs the sandbox twice: `sandbox.shutdown.clean` expects `memory: no leaks`, and `sandbox.shutdown.leak` (`sandbox --leak`) expects `memory: tag Debug has 1 live blocks`.

## Status

Implemented.
