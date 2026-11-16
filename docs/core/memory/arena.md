# Arena

```cpp
#include "core/memory/arena.h"

Arena arena(64_MB, MemoryTag::Core);
float* weights = arena.push_array<float>(1024);
void*  buffer  = arena.push(4_KB, 64);   // size, alignment

const ArenaMark mark = arena.mark();
...                                      // more pushes
arena.rewind(mark);                      // frees everything pushed after the mark
arena.reset();                           // frees everything
```

## Concepts

An arena (also called a linear, bump or stack allocator) hands out memory by moving a pointer forward. It does not free individual allocations: it frees everything at once, or everything after a **mark**.

```
         mark                      position
          v                           v
[ used | used | used | used | used |   free   ........................ ]
                                    ^ next push starts here (aligned)
rewind(mark) moves `position` back to `mark`; reset() moves it back to 0.
```

This is the cheapest allocation possible: an alignment, an addition and a comparison. No headers, no free lists, no fragmentation, and leaks are impossible inside a scope, because the scope ends by rewinding.

Marks make the arena a stack allocator too: nested scopes each take a mark and rewind to it.

An arena is built on a [`VirtualBuffer`](virtual-memory.md): it reserves its maximum size up front and commits 64 KB at a time as `position` grows, so memory is never moved.

## API

```cpp
struct ArenaMark { usize position; };

class Arena {                                   // one owner; not copyable or movable
public:
    static constexpr usize default_alignment = 16;
    static constexpr usize commit_chunk      = 64_KB;

    Arena(usize reserve_bytes, MemoryTag tag);  // fatal if the address space is exhausted
    [[nodiscard]] void* push(usize size, usize alignment = default_alignment);
    template <typename T> [[nodiscard]] T* push_array(usize count);
    ArenaMark mark() const;
    void rewind(ArenaMark mark);
    void reset();
    usize position() const; usize committed() const; usize capacity() const;
    usize high_water() const; MemoryTag tag() const;
};

class ArenaScope {                              // mark in the constructor, rewind in the destructor
public:
    explicit ArenaScope(Arena& arena);
    Arena& arena() const;
};
```

## Scratch arenas

Each thread has two scratch arenas for temporary memory inside a function:

```cpp
void cull_lights(std::span<const Light> lights) {
    ScratchScope s = scratch();                                 // takes a mark
    u32* visible = s.arena().push_array<u32>(lights.size());
    ...
}                                                               // rewinds: everything is gone
```

Compared with a local `std::vector`, there is no heap call, no lock (even in Debug), and no fragmentation. The pages stay committed for the next call.

### Why two arenas: the conflict parameter

A function that writes its result into an arena given by the caller, and also needs temporary memory, must not use the same arena for both:

```cpp
// Writes the path into `out`; uses temporary memory for the search.
std::span<NodeId> find_path(Arena& out, const Graph& graph, NodeId from, NodeId to) {
    ScratchScope temp = scratch(&out);                  // "any scratch arena except `out`"
    NodeId* open = temp.arena().push_array<NodeId>(graph.node_count());
    ...
    NodeId* path = out.push_array<NodeId>(length);      // result, in the caller's arena
    return {path, length};
}                                                       // rewinds `temp`; `out` keeps the path

void plan_route() {
    ScratchScope s = scratch();
    std::span<NodeId> path = find_path(s.arena(), graph, a, b);
    ...
}
```

With a single scratch arena, `out` and `temp` would be the same arena; `find_path`'s rewind would erase the path it just wrote. With two, `scratch(&out)` returns the other one. Two are always enough, because each function passes the arena it writes into as the conflict, and the other one is free. This pattern comes from the Handmade community.

### Lifetime

The scratch arenas of a thread are created the first time that thread calls `scratch()`: each reserves 64 MB of address space and commits nothing until used. A thread that never calls `scratch()` pays nothing. They are destroyed when the thread ends. For the main thread, that happens after `main` returns, so a memory report at the end of `main` still sees them.

## The frame ring

```cpp
ArenaRing<2> frames(64_MB, MemoryTag::Core);

Arena& frame = frames.current();
auto* draws = frame.push_array<DrawCommand>(count);
...
frames.advance();   // after the GPU fence says that arena's frame is finished
```

`ArenaRing<N>` holds one arena per frame in flight. `advance()` moves to the next arena and resets it. With two frames in flight, the data written for frame *k* stays valid while the GPU still uses it, and is reset only when the ring comes back to that arena.

## When to use an arena

| Use an arena when | Use something else when |
|---|---|
| Everything allocated is freed together | Objects are freed one by one ([pools](pools.md)) |
| The data lives for a scope, a frame or a load | The lifetime is arbitrary ([heap](heap.md)) |
| The types are trivially destructible | Objects need their destructors to run |

## Guarantees and restrictions

- **No destructors run.** `push_array<T>` requires a trivially destructible `T`, checked at compile time. A `std::string` in an arena would never free its own memory.
- **Memory is uninitialized**: fresh pages are zero; rewound memory is `0xDD` in Debug.
- **Exhaustion is fatal**, with the tag, capacity, used and requested sizes in the message. So is failing to commit. The owner chooses the reservation; `high_water()` helps size it.
- **`reset` and `rewind` keep the pages committed**, so a frame arena reuses the same memory every frame.
- **Alignment** applies to the absolute address, so any power of two works. A non-power-of-two alignment asserts and falls back to 16 bytes.
- **Rewinding forward asserts** and leaves the arena unchanged.
- **One owner, no synchronization.** Scratch arenas are `thread_local`.

## Pitfalls and advice

- **Never keep a pointer into scratch memory after its scope ends.** The memory is reused; in Debug it turns into `0xDD`.
- **Never pass scratch memory to another thread.** It belongs to the arena of the thread that pushed it.
- **Pass the output arena as the conflict** whenever a function writes results into an arena it received.
- **Size frame arenas from measurements.** Log `high_water()` for a few runs, then reserve generously above it.

## How other engines do it

Unity's TLS stack allocator (per thread, LIFO, `Allocator.Temp`) and Unreal's `FMemStack` (per thread, with marks) are the same idea as scratch arenas. For data written by several jobs, Unity uses a lock-free shared linear allocator (`Allocator.TempJob`) with a lifetime of a few frames. Aurora will add that kind of arena with the job system.

## Implementation notes

- `core/include/core/memory/arena.h`, `core/src/memory/arena.cpp`.
- Arenas are neither copyable nor movable, so a moved-from arena can never keep a stale position. `ArenaRing` constructs its arenas in place.
- The constructor reserves and is fatal on failure. `VirtualBuffer` reports failure instead, but an arena already treats exhaustion as fatal.
- Tests: `tests/core/arena_tests.cpp` (alignment, commit chunks, mark and rewind, `0xDD` fill, high water, asserts, tag counters, `ArenaScope`, scratch reuse and conflict, scratch per thread, `ArenaRing`).

## Status

Implemented. Deferred: `trim()` to return unused pages to the OS, a shared arena with an atomic push for jobs (step 17), checking that a mark belongs to its arena.
