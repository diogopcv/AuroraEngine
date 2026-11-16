#pragma once

#include "core/assert.h"
#include "core/memory/align.h"
#include "core/memory/memory_tag.h"
#include "core/memory/virtual_buffer.h"
#include "core/types.h"

#include <algorithm>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

namespace aurora {

// Contiguous array that grows in place over a VirtualBuffer (D53): growth never moves elements,
// so pointers and spans stay valid. swap_remove keeps it dense but does not keep the order.
// Structural changes on the owner thread; disjoint ranges may be read and written from several
// threads in a phase without structural changes.
template <typename T>
class DenseArray {
    static_assert(std::is_nothrow_move_assignable_v<T>, "swap_remove needs a nothrow move assignment");
    static_assert(alignof(T) <= 4096, "over-aligned type for a dense array");

public:
    static constexpr usize commit_chunk = 64_KB;

    DenseArray(usize max_elements, MemoryTag tag) {
        AURORA_ASSERT(max_elements > 0, "dense array needs a capacity");
        capacity_ = std::max(max_elements, usize{1});
        if (capacity_ > static_cast<usize>(-1) / sizeof(T) || !buffer_.reserve(capacity_ * sizeof(T), tag)) {
            AURORA_FATAL("dense array [{}] could not reserve {} elements of {} bytes", memory_tag_name(tag), capacity_, sizeof(T));
        }
    }

    ~DenseArray() { clear(); }

    DenseArray(const DenseArray&) = delete;
    DenseArray& operator=(const DenseArray&) = delete;
    DenseArray(DenseArray&&) = delete;
    DenseArray& operator=(DenseArray&&) = delete;

    // Fatal when full or out of memory.
    template <typename... Args>
    T& push_back(Args&&... args) {
        if (size_ == capacity_) {
            AURORA_FATAL("dense array [{}] full: {} elements", memory_tag_name(tag()), capacity_);
        }
        const usize bytes = (size_ + 1) * sizeof(T);
        if (bytes > buffer_.committed()) {
            const usize target = std::min(align_up(bytes, commit_chunk), buffer_.capacity());
            if (!buffer_.ensure_committed(target)) {
                AURORA_FATAL("dense array [{}] out of memory: could not commit {} bytes", memory_tag_name(tag()), target);
            }
        }
        T* element = ::new (static_cast<void*>(storage() + size_ * sizeof(T))) T(std::forward<Args>(args)...);
        ++size_;
        return *element;
    }

    void pop_back() {
        AURORA_ASSERT(size_ > 0, "pop_back on an empty dense array");
        if (size_ == 0) {
            return;
        }
        --size_;
        data()[size_].~T();
    }

    // The last element moves into `index`.
    void swap_remove(usize index) {
        AURORA_ASSERT(index < size_, "swap_remove index {} out of range [0, {})", index, size_);
        if (index >= size_) {
            return;
        }
        const usize last = size_ - 1;
        if (index != last) {
            data()[index] = std::move(data()[last]);
        }
        data()[last].~T();
        --size_;
    }

    void clear() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (usize i = 0; i < size_; ++i) {
                data()[i].~T();
            }
        }
        size_ = 0;
    }

    T& operator[](usize index) {
        AURORA_ASSERT(index < size_, "index {} out of range [0, {})", index, size_);
        return data()[index];
    }

    const T& operator[](usize index) const {
        AURORA_ASSERT(index < size_, "index {} out of range [0, {})", index, size_);
        return data()[index];
    }

    T*       data() { return std::launder(reinterpret_cast<T*>(buffer_.data())); }
    const T* data() const { return std::launder(reinterpret_cast<const T*>(buffer_.data())); }
    T*       begin() { return data(); }
    T*       end() { return data() + size_; }
    const T* begin() const { return data(); }
    const T* end() const { return data() + size_; }

    std::span<T>       span() { return {data(), size_}; }
    std::span<const T> span() const { return {data(), size_}; }

    usize     size() const { return size_; }
    usize     capacity() const { return capacity_; }
    bool      empty() const { return size_ == 0; }
    usize     committed_bytes() const { return buffer_.committed(); }
    MemoryTag tag() const { return buffer_.tag(); }

private:
    u8* storage() { return buffer_.data(); }

    VirtualBuffer buffer_;
    usize         capacity_ = 0;
    usize         size_     = 0;
};

} // namespace aurora
