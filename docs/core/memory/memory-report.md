# Memory report

```cpp
#include "core/memory/memory_report.h"

const memory::LeakSummary summary = memory::report_leaks();
```

```
[WARN ] memory: tag General has 72 live blocks, 12189 bytes
[WARN ] memory: tag Debug has 1 live blocks, 64 bytes
[WARN ] memory: tag Core has 65536 bytes of committed virtual memory
[WARN ] <operator new>(0): 72 blocks, 12189 bytes [General]
[WARN ] D:/aurora/apps/sandbox/src/main.cpp(42): 1 blocks, 64 bytes [Debug]
```

## Concepts

Every memory source keeps per-tag counters where it allocates: the [heap](heap.md) counts live blocks and bytes, and every [`VirtualBuffer`](virtual-memory.md) counts committed bytes. The report **only reads** those counters. Nothing in the allocators depends on it.

For each tag with something alive, it logs one line for live heap memory and one for committed virtual memory. In Debug builds it then lists live heap blocks grouped by file and line.

## API

```cpp
namespace aurora::memory {
struct LeakSummary {
    usize heap_blocks     = 0;
    usize heap_bytes      = 0;
    usize committed_bytes = 0;   // all tags
};
LeakSummary report_leaks();
}
```

## When to call it

- **During development**, to see where memory goes at any moment.
- **At shutdown**, after every system has shut down, to find leaks.

## Reading the report

| Line | Meaning |
|---|---|
| `tag X has N live blocks, B bytes` | Heap memory allocated with tag X and not freed |
| `tag X has B bytes of committed virtual memory` | An arena, pool or dense array with tag X still exists |
| `file(line): N blocks` | In Debug, where the live heap blocks were allocated (only through `AURORA_ALLOC`) |
| `<operator new>(0)` | Blocks from the global `new`: standard containers or foreign code |

On MSVC, `file(line)` lines are clickable in the Output window.

## Pitfalls and advice

- **Tests cannot expect a clean report.** The test framework keeps allocations of its own. Tests compare values before and after instead.
- **Static objects and thread-local storage are still alive at the end of `main`**, including the scratch arenas of the main thread.

## Implementation notes

- `core/src/memory/memory_report.cpp`.
- Tests: `tests/core/memory_report_tests.cpp` (a deliberate leak reported with its file and line; a live arena reported as committed memory).

## Status

Implemented. The shutdown report, which decides what counts as a leak at exit, is sub-step 1.10.
