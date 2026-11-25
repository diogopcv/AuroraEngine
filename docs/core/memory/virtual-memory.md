# Virtual memory and VirtualBuffer

```cpp
#include "core/memory/virtual_buffer.h"

VirtualBuffer buffer;
if (!buffer.reserve(1_GB, MemoryTag::Core)) { /* address space exhausted */ }
if (!buffer.ensure_committed(64_MB)) { /* out of memory */ }
u8* data = buffer.data();   // never changes while the buffer lives
```

## Concepts

### Reserving and committing

On a 64-bit OS, a process has a huge **address space** (128 TB on Windows x64) but only as much **memory** as the machine and the page file provide. Windows separates the two:

| Operation | What it does | Cost |
|---|---|---|
| **Reserve** | Claims a range of addresses. Nothing can be read or written there yet | Almost nothing: no memory is used |
| **Commit** | Backs pages of a reserved range with memory, zero-filled | Counts against the system's commit limit |
| **Decommit** | Returns pages to the OS; the addresses stay reserved | Frees memory |
| **Release** | Gives the whole range back | — |

Pages are 4 KB; reservations are aligned to 64 KB (the allocation granularity).

### Why it matters: growing without moving

A `std::vector` that outgrows its capacity allocates a bigger block, copies everything and frees the old block. Every pointer into it becomes invalid, and for a moment it needs twice the memory.

A `VirtualBuffer` reserves the maximum size it might ever need, up front, and commits pages only as it grows:

```
reserve(1 GB):      [ reserved ............................................ ]
ensure_committed:   [ committed | reserved ................................. ]
grows:              [ committed ............ | reserved .................... ]
                    ^ base address: the same from the first byte to the last
```

The base address never changes, so pointers into the buffer stay valid while it grows. This is what lets arenas, pools and dense arrays grow without moving their contents.

## API

Two levels. Most code uses `VirtualBuffer`.

```cpp
namespace aurora::vm {                          // thin wrapper over the OS; sizes must be aligned
usize page_size();                              // 4 KB
usize allocation_granularity();                 // 64 KB
void* reserve(usize size);                      // multiple of the granularity; nullptr: address space exhausted
[[nodiscard]] bool commit(void* ptr, usize size);   // page-aligned address and size; false: out of memory
void decommit(void* ptr, usize size);           // page-aligned address and size
void release(void* ptr);
usize committed_bytes(MemoryTag tag);           // committed through VirtualBuffers with this tag
}

class VirtualBuffer {                           // move-only; the destructor releases
public:
    [[nodiscard]] bool reserve(usize capacity, MemoryTag tag);
    [[nodiscard]] bool ensure_committed(usize size);   // grows only
    void decommit_all();                        // keeps the reservation
    void release();
    u8* data() const; usize capacity() const; usize committed() const;
    bool is_reserved() const; MemoryTag tag() const;
};
```

## Guarantees and restrictions

- **`VirtualBuffer` rounds; `vm::` does not.** `VirtualBuffer` rounds reservations up to 64 KB and commits up to whole pages, and `capacity()` and `committed()` report the rounded values. The `vm::` functions take sizes that are already aligned: a reservation is a multiple of the granularity, a commit or decommit a multiple of the page. An unaligned size asserts and the call does nothing, the same as an unaligned address. The OS would round silently instead, and for a decommit that means losing bytes past the requested size.
- **Committed memory is zero**, both the first time and after decommit and recommit.
- **`ensure_committed` only grows.** Asking for more than the capacity returns `false` and leaves the buffer unchanged.
- **Failure policy.** Running out of address space or memory is an expected failure and returns `nullptr` or `false`; the caller decides. Misuse (an unaligned address or size, reserving twice, a failing `VirtualFree`) asserts and then returns without doing anything.
- **Initialization in two steps** (`reserve` returns `bool`) because engine code has no exceptions, so a constructor could not report failure.
- **One owner, no synchronization.** The OS calls themselves are thread-safe, but a `VirtualBuffer` is not.
- **Counted per tag.** Commits, decommits and releases update a per-tag counter, so every structure built on a `VirtualBuffer` shows up in the [memory report](memory-report.md) without extra code.

## Pitfalls and advice

- **Reserve generously.** Address space is cheap; running out of a reservation is fatal in the structures built on top. Size from the high-water mark those structures report.
- **Use `vm::` directly only when you need to.** It is the mechanism; `VirtualBuffer` holds the rounding and the per-tag counting. Code that calls `vm::` itself must align its sizes and count its own commits.
- **Commit in chunks**, not page by page. Each commit is a system call. Arenas, pools and dense arrays commit in 64 KB steps.
- **Reading or writing past `committed()` crashes** (access violation), even inside the reservation. That is useful: an overrun shows up immediately.
- **The base address is stable, but contents are not protected.** Structures that move elements inside the buffer (such as `DenseArray::swap_remove`) still change what lives at an address.

## How other engines do it

Unreal and Unity both build their specialized allocators on reserved address space and commit on demand; Unity sends very large allocations straight to the virtual memory API. The technique is standard on consoles, where the engine often owns the whole address space.

## Implementation notes

- `core/src/win32/virtual_memory.cpp` is the only Windows-specific file in `core`. It uses `VirtualAlloc` (`MEM_RESERVE` with `PAGE_NOACCESS`, then `MEM_COMMIT` with `PAGE_READWRITE`) and `VirtualFree` (`MEM_DECOMMIT`, `MEM_RELEASE`). Page size and granularity come from `GetSystemInfo`, queried once.
- `<windows.h>` is included only there, with `WIN32_LEAN_AND_MEAN` and `NOMINMAX`.
- `core/src/memory/virtual_buffer.cpp` holds the per-tag counters.
- Tests: `tests/core/virtual_memory_tests.cpp` (reserve 1 GB, grow to 64 MB with a stable base, rounding in `VirtualBuffer`, unaligned sizes rejected by `vm::`, failure past the capacity, zeroed recommit, moves, counters).

## Status

Implemented. Deferred: large pages, guard pages.
