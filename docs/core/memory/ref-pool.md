# Reference counting: RefPool and Ref

```cpp
#include "core/memory/ref_pool.h"

RefPool<Texture> textures(1024, MemoryTag::Core);

Ref<Texture> albedo = textures.create(256, 256);
Ref<Texture> copy   = albedo;     // any thread
i32 width = albedo->width;        // read-only access

albedo.reset();
copy.reset();                     // the last reference queues the texture
textures.collect();               // owner thread: ~Texture() runs here
```

## Concepts

### Why reference counting exists here, and only here

Aurora gives almost every object a single owner. Assets are the exception: a texture is used by many materials, and none of them knows whether it is the last one. Reference counting answers "when the last user is gone, free it".

It is used **only for assets** because it has costs and does not solve what people often expect it to solve:

- It **does not prevent leaks**: two objects that reference each other keep each other alive, and a forgotten strong reference keeps an object alive forever.
- It **does not reduce fragmentation**; it only changes when frees happen, often less predictably.
- Every copy is an atomic operation.

### Strong and weak references

- `Ref<T>` is a **strong** reference: while it exists, the object exists.
- `Handle<T>` is a **weak** reference: it identifies the object without keeping it alive. `upgrade(handle)` turns it back into a `Ref`, if the object is still alive. An asset cache that maps paths to assets keeps weak handles, so it does not keep every asset alive forever.

### Deferred release

A `Ref` can be copied and destroyed on any thread. The render thread, for example, receives texture references inside its frame packets. The pool itself has one owner. If the last `Ref` were dropped on the render thread and the texture destroyed right there, the render thread would be changing the pool while its owner uses it.

So the last `Ref` does not destroy the object. It **queues** it:

```
any thread:   last Ref dropped -> push slot onto the pending list (lock-free)
owner thread: collect()         -> take the whole list -> destroy each object
```

Destructors therefore always run on the owner's thread, at a point of its choosing (for example once per frame). The object outlives its last reference until the next `collect()`.

### Shared means immutable

A `Ref` gives `const` access only. An object shared between threads must not be changed by those who merely reference it. The owner changes it through `get_mut`, at a point where no other thread reads it, for example a hot reload between frames.

## API

```cpp
template <typename T>
class Ref {                                     // 16 bytes: pool pointer and handle
public:
    Ref();                                      // empty
    Ref(const Ref&); Ref& operator=(const Ref&);        // +1
    Ref(Ref&&) noexcept; Ref& operator=(Ref&&) noexcept;
    ~Ref();                                     // -1; the last one queues the object
    void reset();
    const T* get() const; const T* operator->() const; const T& operator*() const;
    explicit operator bool() const;
    Handle<T> handle() const;                   // weak handle
};

template <typename T>
class RefPool {                                 // owner thread, except Ref copies and destruction
public:
    RefPool(u32 max_objects, MemoryTag tag);
    ~RefPool();                                 // collects; live references are reported
    template <typename... Args> [[nodiscard]] Ref<T> create(Args&&... args);
    Ref<T> upgrade(Handle<T> weak);             // empty if gone or queued
    T* get_mut(Handle<T> weak);                 // owner, at a sync point; nullptr if queued
    usize collect();                            // destroys queued objects; returns how many
    u32 live_count() const;                     // includes queued objects
};
```

## Thread safety, in detail

| Operation | Thread | Atomic operation | Why |
|---|---|---|---|
| Copy a `Ref` | Any | `fetch_add`, relaxed | The copier already holds a reference; nothing new is published |
| Destroy a `Ref` | Any | `fetch_sub`, acquire-release | The last one must see every use made through the other references before the object is queued |
| Queue (last reference) | Any | CAS on the list head, release | Publishes the entry written just before |
| `collect()` | Owner | `exchange` of the head, acquire | Sees every entry pushed before it |
| `create`, `upgrade`, `get_mut` | Owner | — | The pool's structure has one owner |

The pending list is lock-free and has **no ABA problem**: threads only push, and only the owner removes, taking the whole list at once. ABA appears only when several threads pop from the same list.

**No resurrection:** once a count reaches zero, nobody can get a new `Ref` to that object. `upgrade` increments only from a non-zero count.

## Guarantees and restrictions

- **A `Ref` must not outlive its pool.** The pool's destructor collects what is queued; objects that still have references are reported as an assert.
- **One `Ref` instance is not shared between threads.** Each thread copies its own, as with `std::shared_ptr`.
- **The object stays alive until `collect()`.** Call it regularly on the owner thread.
- **A full pool is fatal.**

## Pitfalls and advice

- **Do not create cycles of `Ref`s.** Two assets that hold strong references to each other are never freed. Use a weak `Handle<T>` for back-references.
- **Do not keep `Ref`s "just in case".** Every strong reference you forget is a leak that the memory report will show only at shutdown.
- **Keep the `Ref` while using the pointer.** `get()` is valid only while some `Ref` keeps the object alive.

## How other engines do it

Godot's `RefCounted` frees the object immediately, on whichever thread drops the last reference, and `std::shared_ptr` does the same. Bevy's asset handles notify the asset storage when they are dropped, and assets are freed later by the asset system; Unreal releases render resources in phases. Aurora follows the deferred model, so destructors run on a known thread.

## Implementation notes

- `RefPool<T>` is a `HandlePool<T>` plus two per-slot arrays: the count (a `u32` accessed through `std::atomic_ref`) and the pending entry (`next`, `generation`).
- `Ref::get` computes the address from the pool's base pointer, which never changes, so it reads no slot data and is safe from any thread while the object is alive.
- Tests: `tests/core/ref_pool_tests.cpp`, including 8 threads copying one `Ref` 20 000 times each, and 8 threads dropping the last references of 4 000 objects while the owner collects. The concurrent tests also pass under ThreadSanitizer, and weakening the orderings to relaxed makes ThreadSanitizer report the race.

## Status

Implemented. Deferred: `upgrade` from any thread, conversion between related types, cycle detection.
