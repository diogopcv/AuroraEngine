#pragma once

#include "core/assert.h"
#include "core/handle.h"
#include "core/memory/align.h"
#include "core/memory/memory_tag.h"
#include "core/memory/virtual_buffer.h"
#include "core/types.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace aurora {

// Objects referenced by generational handles. Single owner, not thread-safe.
// A slot whose generation reaches MaxGeneration is retired instead of reused.
template <typename T, u32 MaxGeneration = 0xFFFF'FFFFu>
class HandlePool {
    static_assert(MaxGeneration >= 2, "a slot needs at least one reuse");
    static_assert(alignof(T) <= 4096, "over-aligned type for a handle pool");

public:
    static constexpr usize commit_chunk = 64_KB;

    HandlePool(u32 max_objects, MemoryTag tag) {
        AURORA_ASSERT(max_objects > 0 && max_objects <= max_capacity, "handle pool capacity {} out of range", max_objects);
        capacity_ = std::clamp(max_objects, u32{1}, max_capacity);
        if (!objects_.reserve(usize{capacity_} * sizeof(T), tag) || !slots_.reserve(usize{capacity_} * sizeof(Slot), tag)) {
            AURORA_FATAL("handle pool [{}] could not reserve {} objects of {} bytes", memory_tag_name(tag), capacity_, sizeof(T));
        }
    }

    // Live objects are a bug in the owner: assert, then destroy them so nothing leaks.
    ~HandlePool() {
        AURORA_ASSERT(live_ == 0, "handle pool [{}] destroyed with {} live objects", memory_tag_name(tag()), live_);
        for (u32 i = 0; i < high_water_; ++i) {
            if (slot(i).next_free == alive) {
                object(i)->~T();
            }
        }
    }

    HandlePool(const HandlePool&) = delete;
    HandlePool& operator=(const HandlePool&) = delete;
    HandlePool(HandlePool&&) = delete;
    HandlePool& operator=(HandlePool&&) = delete;

    // Fatal when all slots are live or retired, or when out of memory.
    template <typename... Args>
    [[nodiscard]] Handle<T> create(Args&&... args) {
        AURORA_ASSERT(iterating_ == 0, "handle pool [{}]: create during for_each", memory_tag_name(tag()));
        u32 index = 0;
        if (free_head_ != end_of_list) {
            index      = free_head_;
            free_head_ = slot(index).next_free;
        } else {
            if (high_water_ == capacity_) {
                AURORA_FATAL("handle pool [{}] full: {} objects", memory_tag_name(tag()), capacity_);
            }
            index = high_water_;
            grow_to(index + 1);
            ++high_water_;
            slot(index).generation = 1;
        }

        Slot& s = slot(index);
        s.next_free = alive;
        ::new (static_cast<void*>(storage(index))) T(std::forward<Args>(args)...);
        ++live_;
        return Handle<T>{index, s.generation};
    }

    // nullptr for invalid, stale or destroyed handles.
    T* get(Handle<T> handle) {
        return is_live(handle) ? object(handle.index) : nullptr;
    }

    const T* get(Handle<T> handle) const {
        return is_live(handle) ? object(handle.index) : nullptr;
    }

    bool contains(Handle<T> handle) const { return is_live(handle); }

    // No generation check and no slot read: for callers that already guarantee the slot is alive
    // (RefPool's strong references). Reads only the base address, which never changes, so it is
    // safe from any thread while the object is alive.
    T* get_unchecked(u32 index) const { return object(index); }

    // A stale or invalid handle is a bug: assert, then ignore.
    void destroy(Handle<T> handle) {
        AURORA_ASSERT(iterating_ == 0, "handle pool [{}]: destroy during for_each", memory_tag_name(tag()));
        const bool live = is_live(handle);
        AURORA_ASSERT(live, "handle pool [{}]: destroying a stale or invalid handle (index {}, generation {})",
                      memory_tag_name(tag()), handle.index, handle.generation);
        if (!live) {
            return;
        }

        object(handle.index)->~T();
        if constexpr (AURORA_DEBUG) {
            std::memset(storage(handle.index), 0xDD, sizeof(T));
        }
        --live_;

        Slot& s = slot(handle.index);
        if (s.generation == MaxGeneration) {
            s.next_free = retired;
            return;
        }
        ++s.generation;
        s.next_free = free_head_;
        free_head_  = handle.index;
    }

    // f(Handle<T>, T&) for each live object, in index order. No create or destroy inside f.
    template <typename F>
    void for_each(F&& f) {
        ++iterating_;
        const u32 end = high_water_;
        for (u32 i = 0; i < end; ++i) {
            if (slot(i).next_free == alive) {
                f(Handle<T>{i, slot(i).generation}, *object(i));
            }
        }
        --iterating_;
    }

    template <typename F>
    void for_each(F&& f) const {
        ++iterating_;
        const u32 end = high_water_;
        for (u32 i = 0; i < end; ++i) {
            if (slot(i).next_free == alive) {
                f(Handle<T>{i, slot(i).generation}, static_cast<const T&>(*object(i)));
            }
        }
        --iterating_;
    }

    u32       live_count() const { return live_; }
    u32       capacity() const { return capacity_; }
    MemoryTag tag() const { return objects_.tag(); }

private:
    struct Slot {
        u32 generation;
        u32 next_free; // index of the next free slot, or one of the markers below
    };

    static constexpr u32 alive        = 0xFFFF'FFFFu;
    static constexpr u32 end_of_list  = 0xFFFF'FFFEu;
    static constexpr u32 retired      = 0xFFFF'FFFDu;
    static constexpr u32 max_capacity = 0xFFFF'FFFCu;

    bool is_live(Handle<T> handle) const {
        if (handle.generation == 0 || handle.index >= high_water_) {
            return false;
        }
        const Slot& s = slot(handle.index);
        return s.next_free == alive && s.generation == handle.generation;
    }

    void grow_to(u32 count) {
        commit(objects_, usize{count} * sizeof(T));
        commit(slots_, usize{count} * sizeof(Slot));
    }

    void commit(VirtualBuffer& buffer, usize bytes) {
        if (bytes > buffer.committed()) {
            const usize target = std::min(align_up(bytes, commit_chunk), buffer.capacity());
            if (!buffer.ensure_committed(target)) {
                AURORA_FATAL("handle pool [{}] out of memory: could not commit {} bytes", memory_tag_name(tag()), target);
            }
        }
    }

    u8* storage(u32 index) const { return objects_.data() + usize{index} * sizeof(T); }
    T*  object(u32 index) const { return std::launder(reinterpret_cast<T*>(storage(index))); }

    Slot&       slot(u32 index) { return reinterpret_cast<Slot*>(slots_.data())[index]; }
    const Slot& slot(u32 index) const { return reinterpret_cast<const Slot*>(slots_.data())[index]; }

    VirtualBuffer objects_;
    VirtualBuffer slots_;
    u32           capacity_   = 0;
    u32           high_water_ = 0;
    u32           live_       = 0;
    u32           free_head_  = end_of_list;
    mutable u32   iterating_  = 0;
};

} // namespace aurora
