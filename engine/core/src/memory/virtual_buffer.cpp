#include "core/memory/virtual_buffer.h"

#include "core/assert.h"
#include "core/memory/align.h"
#include "core/memory/virtual_memory.h"

#include <utility>

namespace aurora {

VirtualBuffer::~VirtualBuffer() {
    release();
}

VirtualBuffer::VirtualBuffer(VirtualBuffer&& other) noexcept
    : base_(std::exchange(other.base_, nullptr)),
      capacity_(std::exchange(other.capacity_, 0)),
      committed_(std::exchange(other.committed_, 0)) {}

VirtualBuffer& VirtualBuffer::operator=(VirtualBuffer&& other) noexcept {
    if (this != &other) {
        release();
        base_      = std::exchange(other.base_, nullptr);
        capacity_  = std::exchange(other.capacity_, 0);
        committed_ = std::exchange(other.committed_, 0);
    }
    return *this;
}

bool VirtualBuffer::reserve(usize capacity) {
    AURORA_ASSERT(!is_reserved(), "VirtualBuffer is already reserved");
    AURORA_ASSERT(capacity > 0, "reserving zero bytes");
    if (is_reserved() || capacity == 0) {
        return false;
    }

    const usize rounded = align_up(capacity, vm::allocation_granularity());
    void* base = vm::reserve(rounded);
    if (base == nullptr) {
        return false;
    }
    base_      = static_cast<u8*>(base);
    capacity_  = rounded;
    committed_ = 0;
    return true;
}

bool VirtualBuffer::ensure_committed(usize size) {
    AURORA_ASSERT(is_reserved(), "VirtualBuffer is not reserved");
    if (!is_reserved() || size > capacity_) {
        return false;
    }
    if (size <= committed_) {
        return true;
    }

    const usize target = align_up(size, vm::page_size());
    if (!vm::commit(base_ + committed_, target - committed_)) {
        return false;
    }
    committed_ = target;
    return true;
}

void VirtualBuffer::decommit_all() {
    if (committed_ > 0) {
        vm::decommit(base_, committed_);
        committed_ = 0;
    }
}

void VirtualBuffer::release() {
    if (base_ != nullptr) {
        vm::release(base_);
        base_      = nullptr;
        capacity_  = 0;
        committed_ = 0;
    }
}

} // namespace aurora
