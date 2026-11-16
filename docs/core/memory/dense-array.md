# Dense array

```cpp
#include "core/memory/dense_array.h"

DenseArray<Particle> particles(1'000'000, MemoryTag::Core);
particles.push_back(x, y, life);        // constructed in place
for (Particle& p : particles.span()) {
    p.life -= dt;
}
particles.swap_remove(i);               // O(1); the last element fills the hole
```

## Concepts

### Why dense

Code that processes many elements of the same kind (move every particle, update every transform) is fastest when the elements sit next to each other in memory: the CPU fetches whole cache lines and predicts the next ones. A dense array has no holes, so a loop over it touches only useful data.

### Swap-remove

Removing from the middle of an array normally shifts every following element, which is O(n). Swap-remove moves the **last** element into the hole instead, which is O(1):

```
before:   [ A ][ B ][ C ][ D ][ E ]
swap_remove(1):   E moves into slot 1
after:    [ A ][ E ][ C ][ D ]
```

The array stays dense, but **the order changes**. That is fine for data whose order does not matter, such as particles or ECS components.

### Growing in place

A `DenseArray` is built on a [`VirtualBuffer`](virtual-memory.md): it reserves its maximum size and commits 64 KB at a time. Unlike a `std::vector`, growing never moves the elements, so pointers and spans stay valid while elements are added.

| | `std::vector` | `DenseArray` |
|---|---|---|
| Growing | Allocates a bigger block, moves every element, briefly needs twice the memory | Commits more pages in place |
| Pointers and spans after growing | Invalid | Still valid |
| Capacity | Unbounded | Fixed maximum; full is fatal |
| Memory counted as | `General` | The owner's tag |
| Removing from the middle | `erase`, O(n), keeps the order | `swap_remove`, O(1), changes the order |

## API

```cpp
template <typename T>
class DenseArray {                              // not copyable or movable
public:
    DenseArray(usize max_elements, MemoryTag tag);
    ~DenseArray();                              // destroys the elements
    template <typename... Args> T& push_back(Args&&... args);
    void pop_back();
    void swap_remove(usize index);
    void clear();                               // destroys the elements, keeps the pages
    T& operator[](usize index); const T& operator[](usize index) const;
    T* data(); T* begin(); T* end();            // and const versions
    std::span<T> span(); std::span<const T> span() const;
    usize size() const; usize capacity() const; bool empty() const; usize committed_bytes() const;
};
```

## Guarantees and restrictions

- **Stable addresses, not stable identity.** The memory of each position never moves, but after a `swap_remove` position `i` holds another element. Anything that needs to find a specific element later uses a handle and a map from handle to index; the ECS will keep such a map.
- **`swap_remove` returns nothing.** The element that was at `size() - 1` is now at `index`; update any index map accordingly.
- **Types with constructors and destructors are allowed.** `T` must be nothrow move-assignable, because `swap_remove` move-assigns the last element into the hole.
- **Out-of-range access asserts.** So does `pop_back` on an empty array; `swap_remove` and `pop_back` then do nothing.
- **A full array is fatal.** The owner chooses the maximum.
- **Threading:** adding, removing and clearing happen on the owner thread. Several threads may read and write **disjoint ranges** of elements at the same time, in a phase where nobody adds or removes. That is how parallel systems will process component columns.

## Pitfalls and advice

- **Do not hold element pointers across `swap_remove`** expecting them to point at the same element.
- **Prefer plain data.** Components and particle attributes should be simple structures; their moves are then plain copies.
- **Keep parallel arrays in step.** Particles stored as one array per attribute (structure of arrays) must apply each `swap_remove` to every array.

## How other engines do it

Bevy and Unity DOTS store components in contiguous columns per type and remove by swapping with the last element, updating the moved entity's location. Aurora's ECS (step 7) builds its columns on `DenseArray`.

## Implementation notes

- `core/include/core/memory/dense_array.h` (header only).
- Elements are constructed with `T(args...)`, so plain structures without constructors can be filled from their fields.
- Tests: `tests/core/dense_array_tests.cpp` (swap-remove order, one million elements with a stable base, non-trivial elements, `clear`, bounds asserts, tag counters, four threads writing disjoint ranges).

## Status

Implemented. Deferred: an order-preserving remove, a structure-of-arrays group that keeps several arrays in step (particles, step 14), `trim()`.
