#pragma once

#include "core/assert.h"
#include "core/memory/memory_tag.h"
#include "core/memory/virtual_buffer.h"
#include "core/types.h"

#include <type_traits>
#include <utility>

namespace aurora {

struct ArenaMark {
    usize position = 0;
};

// Linear allocator over a VirtualBuffer. Single owner, not thread-safe.
// No destructors run: only trivially destructible types belong here.
class Arena {
public:
    static constexpr usize default_alignment = 16;
    static constexpr usize commit_chunk      = 64_KB;

    Arena(usize reserve_bytes, MemoryTag tag); // fatal if the address space is exhausted

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = delete;
    Arena& operator=(Arena&&) = delete;

    // Uninitialized memory. Fatal past the reservation or when out of memory.
    [[nodiscard]] void* push(usize size, usize alignment = default_alignment);

    template <typename T>
    [[nodiscard]] T* push_array(usize count) {
        static_assert(std::is_trivially_destructible_v<T>, "arenas never run destructors");
        if (count > static_cast<usize>(-1) / sizeof(T)) {
            AURORA_FATAL("arena [{}] push_array of {} elements overflows", memory_tag_name(tag()), count);
        }
        return static_cast<T*>(push(sizeof(T) * count, alignof(T)));
    }

    ArenaMark mark() const { return ArenaMark{position_}; }
    void rewind(ArenaMark mark);
    void reset() { rewind(ArenaMark{}); }

    usize     position() const { return position_; }
    usize     committed() const { return buffer_.committed(); }
    usize     capacity() const { return buffer_.capacity(); }
    usize     high_water() const { return high_water_; }
    MemoryTag tag() const { return buffer_.tag(); }

private:
    VirtualBuffer buffer_;
    usize         position_   = 0;
    usize         high_water_ = 0;
};

// Rewinds the arena to where it was when the scope began.
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) : arena_(arena), mark_(arena.mark()) {}
    ~ArenaScope() { arena_.rewind(mark_); }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;
    ArenaScope(ArenaScope&&) = delete;
    ArenaScope& operator=(ArenaScope&&) = delete;

    Arena& arena() const { return arena_; }

private:
    Arena&    arena_;
    ArenaMark mark_;
};

using ScratchScope = ArenaScope;

inline constexpr usize scratch_reserve_bytes = 64_MB;

// One of the calling thread's two scratch arenas, never `conflict` (pass the arena the caller
// is writing results into). The scope rewinds it on exit.
ScratchScope scratch(const Arena* conflict = nullptr);

// One arena per frame in flight. advance() moves to the next arena and resets it; call it once
// the GPU fence says that arena's frame is finished (owned by the render thread).
template <usize N>
class ArenaRing {
    static_assert(N >= 2, "a ring needs at least two arenas");

public:
    ArenaRing(usize reserve_bytes_each, MemoryTag tag)
        : ArenaRing(reserve_bytes_each, tag, std::make_index_sequence<N>{}) {}

    Arena& current() { return arenas_[index_]; }
    Arena& at(usize i) { return arenas_[i]; }
    usize  index() const { return index_; }
    static constexpr usize size() { return N; }

    void advance() {
        index_ = (index_ + 1) % N;
        arenas_[index_].reset();
    }

private:
    template <usize... I>
    ArenaRing(usize reserve_bytes_each, MemoryTag tag, std::index_sequence<I...>)
        : arenas_{make_arena(reserve_bytes_each, tag, I)...} {}

    // Arena is not movable: each element is initialized in place from this prvalue.
    static Arena make_arena(usize reserve_bytes, MemoryTag tag, usize /*index*/) { return Arena(reserve_bytes, tag); }

    Arena arenas_[N];
    usize index_ = 0;
};

} // namespace aurora
