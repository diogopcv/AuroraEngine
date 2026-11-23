#pragma once

#include "core/assert.h"
#include "core/handle.h"
#include "core/memory/align.h"
#include "core/memory/handle_pool.h"
#include "core/memory/memory_tag.h"
#include "core/memory/virtual_buffer.h"
#include "core/types.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace aurora {

template <typename T>
class RefPool;

// Strong, reference-counted handle to an object of a RefPool. Copying and destroying a Ref
// is safe from any thread (like std::shared_ptr, one Ref instance is not shared between threads).
// Access is const: shared objects are immutable; the owner mutates through RefPool::get_mut.
// A Ref must not outlive its pool.
template <typename T>
class Ref {
public:
    Ref() = default;
    Ref(const Ref& other);
    Ref(Ref&& other) noexcept;
    Ref& operator=(const Ref& other);
    Ref& operator=(Ref&& other) noexcept;
    ~Ref() { reset(); }

    void reset();

    const T* get() const;
    const T* operator->() const { return get(); }
    const T& operator*() const { return *get(); }
    explicit operator bool() const { return pool_ != nullptr; }

    Handle<T> handle() const { return handle_; } // weak handle

    friend bool operator==(const Ref& a, const Ref& b) { return a.pool_ == b.pool_ && a.handle_ == b.handle_; }

private:
    friend class RefPool<T>;
    Ref(RefPool<T>* pool, Handle<T> handle) : pool_(pool), handle_(handle) {} // adopts one count

    RefPool<T>* pool_   = nullptr;
    Handle<T>   handle_ = {};
};

// Reference-counted objects for shared ownership with an unknown last owner: assets only.
// The owner thread creates, upgrades, mutates and collects. When the last Ref is dropped, on any
// thread, the object is queued; collect() destroys queued objects on the owner thread.
template <typename T>
class RefPool {
public:
    explicit RefPool(u32 max_objects, MemoryTag tag) : objects_(max_objects, tag) {
        const usize capacity = objects_.capacity();
        if (!counts_.reserve(capacity * sizeof(u32), tag) || !pending_.reserve(capacity * sizeof(Pending), tag)) {
            AURORA_FATAL("ref pool [{}] could not reserve its counters", memory_tag_name(tag));
        }
    }

    ~RefPool() { collect(); } // objects still referenced are reported by the HandlePool destructor

    RefPool(const RefPool&) = delete;
    RefPool& operator=(const RefPool&) = delete;
    RefPool(RefPool&&) = delete;
    RefPool& operator=(RefPool&&) = delete;

    template <typename... Args>
    [[nodiscard]] Ref<T> create(Args&&... args) {
        const Handle<T> handle = objects_.create(std::forward<Args>(args)...);
        grow_to(handle.index + 1);
        count(handle.index).store(1, std::memory_order_relaxed);
        return Ref<T>(this, handle);
    }

    // Empty if the object is gone or waiting for collect(): a count never comes back from zero.
    Ref<T> upgrade(Handle<T> weak) {
        if (!objects_.contains(weak)) {
            return {};
        }
        std::atomic_ref<u32> c = count(weak.index);
        u32 current = c.load(std::memory_order_relaxed);
        do {
            if (current == 0) {
                return {};
            }
        } while (!c.compare_exchange_weak(current, current + 1, std::memory_order_relaxed));
        return Ref<T>(this, weak);
    }

    // Owner thread, at a point where no other thread reads the object.
    T* get_mut(Handle<T> weak) {
        if (!objects_.contains(weak) || count(weak.index).load(std::memory_order_relaxed) == 0) {
            return nullptr;
        }
        return objects_.get(weak);
    }

    // Destroys every object whose last Ref was dropped. Returns how many.
    usize collect() {
        // Acquire pairs with the release CAS of every push: their writes to the entries are visible.
        u32 index = pending_head_.exchange(end_of_list, std::memory_order_acquire);
        usize destroyed = 0;
        while (index != end_of_list) {
            const Pending entry = pending(index);
            objects_.destroy(Handle<T>{index, entry.generation});
            index = entry.next;
            ++destroyed;
        }
        return destroyed;
    }

    u32       live_count() const { return objects_.live_count(); } // includes objects waiting for collect()
    u32       capacity() const { return objects_.capacity(); }
    MemoryTag tag() const { return objects_.tag(); }

private:
    friend class Ref<T>;

    struct Pending {
        u32 next;
        u32 generation;
    };

    static constexpr u32 end_of_list = 0xFFFF'FFFFu;
    static_assert(std::atomic_ref<u32>::is_always_lock_free);

    void retain(u32 index) {
        // The caller already holds a reference: nothing to publish.
        count(index).fetch_add(1, std::memory_order_relaxed);
    }

    void release(Handle<T> handle) {
        // acq_rel: the last release sees every use made through the other references.
        const u32 previous = count(handle.index).fetch_sub(1, std::memory_order_acq_rel);
        AURORA_ASSERT(previous > 0, "ref pool [{}]: released a reference that was not held", memory_tag_name(tag()));
        if (previous == 1) {
            push_pending(handle);
        }
    }

    // Any thread. Lock-free push; no ABA because only the owner removes, and it takes the whole list.
    void push_pending(Handle<T> handle) {
        Pending& entry   = pending(handle.index);
        entry.generation = handle.generation;
        u32 head = pending_head_.load(std::memory_order_relaxed);
        do {
            entry.next = head;
        } while (!pending_head_.compare_exchange_weak(head, handle.index, std::memory_order_release, std::memory_order_relaxed));
    }

    const T* object(u32 index) const { return objects_.get_unchecked(index); }

    std::atomic_ref<u32> count(u32 index) { return std::atomic_ref<u32>(reinterpret_cast<u32*>(counts_.data())[index]); }
    Pending&             pending(u32 index) { return reinterpret_cast<Pending*>(pending_.data())[index]; }

    void grow_to(u32 count_needed) {
        commit(counts_, usize{count_needed} * sizeof(u32));
        commit(pending_, usize{count_needed} * sizeof(Pending));
    }

    void commit(VirtualBuffer& buffer, usize bytes) {
        if (bytes > buffer.committed()) {
            const usize target = std::min(align_up(bytes, HandlePool<T>::commit_chunk), buffer.capacity());
            if (!buffer.ensure_committed(target)) {
                AURORA_FATAL("ref pool [{}] out of memory: could not commit {} bytes", memory_tag_name(tag()), target);
            }
        }
    }

    HandlePool<T>    objects_;
    VirtualBuffer    counts_;  // u32 per slot, accessed through std::atomic_ref
    VirtualBuffer    pending_; // Pending per slot, written by the releasing thread before its push
    std::atomic<u32> pending_head_{end_of_list};
};

template <typename T>
Ref<T>::Ref(const Ref& other) : pool_(other.pool_), handle_(other.handle_) {
    if (pool_ != nullptr) {
        pool_->retain(handle_.index);
    }
}

template <typename T>
Ref<T>::Ref(Ref&& other) noexcept : pool_(std::exchange(other.pool_, nullptr)), handle_(std::exchange(other.handle_, Handle<T>{})) {}

template <typename T>
Ref<T>& Ref<T>::operator=(const Ref& other) {
    if (this != &other) {
        Ref copy(other);
        std::swap(pool_, copy.pool_);
        std::swap(handle_, copy.handle_);
    }
    return *this;
}

template <typename T>
Ref<T>& Ref<T>::operator=(Ref&& other) noexcept {
    if (this != &other) {
        reset();
        pool_   = std::exchange(other.pool_, nullptr);
        handle_ = std::exchange(other.handle_, Handle<T>{});
    }
    return *this;
}

template <typename T>
void Ref<T>::reset() {
    if (pool_ != nullptr) {
        pool_->release(handle_);
        pool_   = nullptr;
        handle_ = Handle<T>{};
    }
}

template <typename T>
const T* Ref<T>::get() const {
    return pool_ != nullptr ? pool_->object(handle_.index) : nullptr;
}

} // namespace aurora
