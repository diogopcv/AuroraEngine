#include "core/memory/virtual_buffer.h"

#include "core/assert.h"
#include "core/memory/align.h"
#include "core/memory/virtual_memory.h"

#include <atomic>
#include <utility>

namespace aurora {

namespace {

constinit std::atomic<usize> g_committed[memory_tag_count];

void add_committed(MemoryTag tag, usize bytes) {
    g_committed[static_cast<usize>(tag)].fetch_add(bytes, std::memory_order_relaxed);
}

void remove_committed(MemoryTag tag, usize bytes) {
    g_committed[static_cast<usize>(tag)].fetch_sub(bytes, std::memory_order_relaxed);
}

} // namespace

usize vm::committed_bytes(MemoryTag tag) {
    AURORA_ASSERT(tag < MemoryTag::Count);
    if (tag >= MemoryTag::Count) {
        return 0;
    }
    return g_committed[static_cast<usize>(tag)].load(std::memory_order_relaxed);
}

VirtualBuffer::~VirtualBuffer() {
    release();
}

VirtualBuffer::VirtualBuffer(VirtualBuffer&& other) noexcept
    : base_(std::exchange(other.base_, nullptr)),
      capacity_(std::exchange(other.capacity_, 0)),
      committed_(std::exchange(other.committed_, 0)),
      tag_(other.tag_) {}

VirtualBuffer& VirtualBuffer::operator=(VirtualBuffer&& other) noexcept {
    if (this != &other) {
        release();
        base_      = std::exchange(other.base_, nullptr);
        capacity_  = std::exchange(other.capacity_, 0);
        committed_ = std::exchange(other.committed_, 0);
        tag_       = other.tag_;
    }
    return *this;
}

bool VirtualBuffer::reserve(usize capacity, MemoryTag tag) {
    AURORA_ASSERT(!is_reserved(), "VirtualBuffer is already reserved");
    AURORA_ASSERT(capacity > 0, "reserving zero bytes");
    AURORA_ASSERT(tag < MemoryTag::Count);
    if (is_reserved() || capacity == 0 || tag >= MemoryTag::Count) {
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
    tag_       = tag;
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
    add_committed(tag_, target - committed_);
    committed_ = target;
    return true;
}

void VirtualBuffer::decommit_all() {
    if (committed_ > 0) {
        vm::decommit(base_, committed_);
        remove_committed(tag_, committed_);
        committed_ = 0;
    }
}

void VirtualBuffer::release() {
    if (base_ != nullptr) {
        vm::release(base_);
        remove_committed(tag_, committed_);
        base_      = nullptr;
        capacity_  = 0;
        committed_ = 0;
    }
}

} // namespace aurora
