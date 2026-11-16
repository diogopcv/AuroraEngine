# Pools and handles

Two allocators for objects of one size that are freed one by one:

| | `Pool` | `HandlePool<T>` |
|---|---|---|
| Referenced by | A pointer | A `Handle<T>`: index and generation |
| Holds | Raw blocks of a fixed size | Objects of type `T`, constructed and destroyed |
| Detects stale references | No | Yes |
| Use for | Internal structures of one system (tree nodes, job descriptors) | Anything referenced from outside: server resources such as meshes, textures, rigid bodies, voices |

## Handle\<T\>

```cpp
#include "core/handle.h"

template <typename T>
struct Handle {
    u32 index      = 0;
    u32 generation = 0;   // 0 = invalid
    constexpr bool is_valid() const;
    friend constexpr bool operator==(Handle, Handle) = default;
};
```

A handle is how one engine object refers to another. It is 8 bytes and trivially copyable, so it fits in components, frame packets and queues.

### Why not a pointer

A pointer to a destroyed object points at freed memory, or at whatever was allocated there next, and nothing can tell. A handle carries a **generation**. Each slot of a `HandlePool` has a generation that changes when its object is destroyed, so an old handle no longer matches and is rejected:

```
slot 3: generation 1, "rock"     handle {3, 1} -> rock
destroy(rock):  slot 3 generation becomes 2
create("tree"): slot 3 reused, generation 2
                handle {3, 1} -> nullptr (stale)    handle {3, 2} -> tree
```

### Properties

- **One type per `T`.** `Handle<Mesh>` and `Handle<Texture>` cannot be mixed up; the compiler rejects it.
- **`T` may be incomplete.** A public API can expose `Handle<Mesh>` with only `struct Mesh;` declared, without revealing how the server stores meshes.
- **A default handle is never valid**, because generation 0 is never issued.
- **Runtime only.** Indices and generations differ between runs; never save a handle to disk. Scenes and assets use stable IDs.
- **Not every handle comes from a `HandlePool`.** ECS entities, event subscriptions and GPU resources use the same type with their own storage.

## HandlePool

```cpp
#include "core/memory/handle_pool.h"

HandlePool<Mesh> meshes(4096, MemoryTag::Core);
Handle<Mesh> cube = meshes.create(36, 12);   // constructor arguments
if (Mesh* mesh = meshes.get(cube)) { ... }
meshes.destroy(cube);
meshes.get(cube);                           // nullptr: stale

meshes.for_each([](Handle<Mesh> handle, Mesh& mesh) { ... });   // live objects, in index order
```

```cpp
template <typename T, u32 MaxGeneration = 0xFFFF'FFFF>
class HandlePool {                              // one owner; not copyable or movable
public:
    HandlePool(u32 max_objects, MemoryTag tag);
    ~HandlePool();
    template <typename... Args> [[nodiscard]] Handle<T> create(Args&&... args);
    T* get(Handle<T> handle); const T* get(Handle<T> handle) const;
    bool contains(Handle<T> handle) const;
    void destroy(Handle<T> handle);
    template <typename F> void for_each(F&& f);        // f(Handle<T>, T&)
    template <typename F> void for_each(F&& f) const;  // f(Handle<T>, const T&)
    u32 live_count() const; u32 capacity() const;
};
```

### How it works

Two parallel arrays, each in a [`VirtualBuffer`](virtual-memory.md):

```
objects:  [ T0 ][ T1 ][ T2 ][ T3 ] ...        sizeof(T) each
slots:    [g,n ][g,n ][g,n ][g,n ] ...        generation, next free (or "alive")
free list: head -> 2 -> 0 -> end
```

- `create` takes a slot from the free list, or a new one at the end, and constructs `T` in place.
- `destroy` runs `~T()`, increments the slot's generation and pushes the slot onto the free list.
- `get` checks that the slot is alive and that the generations match.
- A slot whose generation reaches its maximum is **retired** instead of reused, so an ancient handle can never become valid again. With 32-bit generations that is one slot per four billion reuses.

### Guarantees and restrictions

- **`get` with a stale or invalid handle returns `nullptr`.** That is an expected situation (another system destroyed the object), not an error.
- **`destroy` with a stale handle asserts** and does nothing: destroying twice is a bug.
- **Destroying the pool with live objects asserts** with the count, then destroys them so nothing leaks.
- **No `create` or `destroy` inside `for_each`** (asserted). To destroy while iterating, collect handles into a [scratch arena](arena.md#scratch-arenas) and destroy after the loop.
- **A full pool is fatal.** The owner chooses `max_objects`.
- **Addresses are stable**: an object never moves while it is alive.
- **One owner.** Reads from other threads are safe in a phase where nobody creates or destroys.

## Pool

```cpp
#include "core/memory/pool.h"

Pool nodes(sizeof(Node), alignof(Node), 100'000, MemoryTag::Core);
void* block = nodes.alloc();
Node* node  = new (block) Node{};   // the pool hands out raw memory
...
node->~Node();
nodes.free(node);
```

```cpp
class Pool {                                    // one owner; not copyable or movable
public:
    Pool(usize block_size, usize block_alignment, usize max_blocks, MemoryTag tag);
    ~Pool();                                    // asserts if blocks are still live
    [[nodiscard]] void* alloc();
    void free(void* block);
    usize live_count() const; usize capacity() const; usize block_size() const; usize stride() const;
};
```

### How it works

The pool carves a `VirtualBuffer` into blocks of equal size. A freed block stores the pointer to the next free block inside itself (an **intrusive free list**), so the pool needs no memory of its own to track free blocks. Every block is therefore at least the size of a pointer.

```
[ used ][ free ]->[ free ]->[ used ][ free ]-> null
          ^ head
```

Allocation pops the head; free pushes onto it. Both are constant time. The most recently freed block is reused first, which is good for the cache.

### Guarantees and restrictions

- **Blocks are raw memory.** No constructors or destructors run; use placement `new` and call the destructor yourself, or prefer a `HandlePool` for typed objects.
- **`free` rejects foreign pointers** (outside the pool, or not at the start of a block) in every build: it asserts and ignores them. `nullptr` is ignored.
- **Double frees are detected in Debug** with one bit per block, and freed blocks are filled with `0xDD`.
- **A full pool is fatal.**
- **One owner, no synchronization.**

## When to use which

| Situation | Use |
|---|---|
| Other systems keep references to the object | `HandlePool<T>` |
| Only the owning system knows the object, and holds pointers | `Pool` |
| Objects are iterated in bulk and their order does not matter | [`DenseArray<T>`](dense-array.md) |
| Several owners share the object | [`RefPool<T>`](ref-pool.md) |

## How other engines do it

Bevy's entities are an index plus a generation, the direct source of Aurora's handles. Godot's `RID_Owner` validates IDs in the same way, and its `PagedAllocator` has an optional lock chosen by a template parameter. Unreal's TaskGraph allocates tasks from a lock-free fixed-size allocator with per-thread caches, because tasks are created on one thread and destroyed on another.

## Implementation notes

- `core/handle.h` includes only `types.h`, so any public header can use handles cheaply.
- `HandlePool` keeps slots and objects in separate arrays so no `alignas` structure is needed (MSVC warns about padding caused by `alignas` inside templates). The slot's `next_free` field doubles as its state (alive, end of list, retired), which limits the capacity to `0xFFFF'FFFC` objects.
- `MaxGeneration` is a template parameter only so that tests can reach the retirement case.
- `HandlePool::get_unchecked(index)` returns the object address without reading slot data, for callers that already guarantee the slot is alive ([`RefPool`](ref-pool.md)).
- Tests: `tests/core/pool_tests.cpp`, `tests/core/handle_pool_tests.cpp`.

## Status

Implemented. Deferred: a typed `Pool<T>`, thread-safe pools (with the job system), hashing handles for use as map keys.
