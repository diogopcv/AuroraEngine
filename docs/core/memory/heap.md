# Heap

```cpp
#include "core/memory/heap.h"

void* vertices = AURORA_ALLOC(vertex_bytes, 16, MemoryTag::Core);   // file and line recorded in Debug
...
heap::free(vertices, MemoryTag::Core);

heap::TagStats stats = heap::stats(MemoryTag::Core);   // live bytes, live blocks, total allocations
```

## Concepts

The heap is for memory of **variable size and arbitrary lifetime**: asset payloads, IO buffers, mesh data before it is uploaded. Everything with a simpler lifetime has a faster allocator (see [Choosing an allocator](README.md)).

### mimalloc

The heap is [mimalloc](https://github.com/microsoft/mimalloc), a general-purpose allocator with per-thread heaps: each thread allocates from its own pages without locks, and frees from other threads are handed back cheaply. Writing a general-purpose heap teaches little about engine architecture; mimalloc is one of the libraries used for a specialized domain.

### Tags and statistics

Every allocation names a `MemoryTag`. For each tag the heap counts live bytes, live blocks and total allocations, in relaxed atomics, one cache line per tag so threads updating different tags do not slow each other down. Sizes come from mimalloc (`mi_usable_size`), so live bytes count the real block size, which can be slightly larger than requested.

## API

```cpp
namespace aurora::heap {
struct TagStats { usize live_bytes; usize live_blocks; u64 total_allocations; };

[[nodiscard]] void* alloc(usize size, usize alignment, MemoryTag tag, const char* file = nullptr, i32 line = 0);
void     free(void* ptr, MemoryTag tag);
TagStats stats(MemoryTag tag);
void     log_live_callsites();   // Debug: live blocks grouped by file and line
}

#define AURORA_ALLOC(size, alignment, tag)   // heap::alloc with __FILE__ and __LINE__
```

- `alloc` returns `nullptr` when out of memory. The alignment must be a power of two.
- `free` must receive the same tag that `alloc` did. `nullptr` is ignored.

## Global `new` and `delete`

Every engine executable replaces the global `operator new` and `operator delete` (all twenty forms: plain, array, `nothrow`, aligned, sized). They call the heap with tag `General`. That is how standard containers are counted:

```cpp
std::vector<i32> values(1000);   // counted under General
```

Two consequences:

- **Out of memory in `operator new` is fatal.** Engine code has no exceptions, so it cannot throw `std::bad_alloc`. The `nothrow` forms return `nullptr`.
- The operators live in an OBJECT library (`aurora::new_delete`) that `engine_add_executable()` links into every executable. An object file inside a static library would be dropped by the linker, because nothing references it by name.

## When to use what

| You need | Use |
|---|---|
| A variable-size block owned by an engine system | `AURORA_ALLOC` with the system's tag |
| A standard container inside an engine system | It works and is counted, but under `General`. Tagged containers are an open question |
| One object with a constructor and its own lifetime | Usually a [pool](pools.md) or a member of its owner, not `new T` |
| Temporary memory | [`scratch()`](arena.md#scratch-arenas), never the heap |

## Debug callsite registry

In Debug builds, every `heap::alloc` records `{file, line, size, tag}` in a map, protected by a mutex. This enables:

- a list of **live blocks grouped by file and line** in the [memory report](memory-report.md);
- an assert when a block is **freed with the wrong tag** (the counters are corrected with the recorded tag);
- an assert when a pointer **that the heap did not return** is freed (it is then ignored);
- freed memory filled with `0xDD`, so use-after-free shows absurd values.

Only `AURORA_ALLOC` knows the file and line. Blocks from the global `new` appear as `<operator new>`.

The registry takes a lock on every allocation and free, in Debug only. That is a deliberate exception to "no locks on hot paths". The lock is never held while logging or asserting, because a log sink may allocate and would try to take the same lock. The registry is created on first use and never destroyed, because static destructors can free blocks after `main` returns.

## Pitfalls and advice

- **Free with the same tag.** A mismatch skews the statistics in Release; Debug catches it.
- **Do not use the heap for temporaries or per-frame data.** Use arenas: they are faster and cannot leak.
- **Do not rely on `new` being called.** The compiler may remove a `new`-expression paired with its `delete` when the object never escapes (C++14 allows it). To make sure an allocation happens, for example in a test, call `::operator new` or `heap::alloc` directly.
- **Libraries that call `malloc` are not counted**: `malloc` is not replaced. Connect them through their allocation hooks (`STBI_MALLOC`, cgltf's memory options, miniaudio's allocation callbacks, `ImGui::SetAllocatorFunctions`) when they are integrated.

## mimalloc configuration

- v3.6.1, static library, fetched by CMake; linked privately to `core`.
- `MI_OVERRIDE=OFF`: the engine routes `new` and `delete` itself; `malloc` stays the C runtime's.
- mimalloc's own checks: level 2 (API misuse and internal invariants) in Debug, off elsewhere. Setting `MI_DEBUG=ON` would turn them on in Release too.

## How other engines do it

Unreal uses its own binned allocators at runtime (the editor uses mimalloc) and tracks memory by category (LLM). Unity uses a TLSF heap with a lock-free bucket allocator for small allocations. Godot uses the system `malloc`. Aurora's split (a general heap, linear allocators for temporaries, pools for fixed sizes) is closest to Unity's.

## Implementation notes

- `core/src/memory/heap.cpp`: counters, Debug registry (`std::unordered_map` with `mi_stl_allocator`, so its own allocations do not recurse into the registry).
- `core/src/memory/new_delete.cpp`: the replacement operators, with MSVC's SAL annotations to match `<vcruntime_new.h>`.
- Alignments up to 16 bytes use `mi_malloc`; larger ones use `mi_malloc_aligned`.
- Tests: `tests/core/heap_tests.cpp`.

## Status

Implemented. Deferred: level heaps (destroy everything a level allocated at once, using mimalloc heaps), per-tag budgets, tagged standard containers.
