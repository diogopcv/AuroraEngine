#include "core/memory/pool.h"

#include "core/assert.h"
#include "core/memory/align.h"
#include "core/memory/virtual_memory.h"

#include <algorithm>
#include <cstring>

namespace aurora {

Pool::Pool(usize block_size, usize block_alignment, usize max_blocks, MemoryTag tag) {
    AURORA_ASSERT(block_size > 0 && max_blocks > 0, "pool needs a block size and a block count");
    AURORA_ASSERT(is_power_of_two(block_alignment), "alignment {} is not a power of two", block_alignment);
    AURORA_ASSERT(block_alignment <= vm::allocation_granularity(), "alignment {} is larger than the reservation alignment", block_alignment);

    const usize alignment = std::max(is_power_of_two(block_alignment) ? block_alignment : usize{16}, alignof(void*));
    block_size_ = block_size;
    stride_     = align_up(std::max(block_size, sizeof(void*)), alignment);
    max_blocks_ = std::max(max_blocks, usize{1});

    if (!blocks_.reserve(stride_ * max_blocks_, tag)) {
        AURORA_FATAL("pool [{}] could not reserve {} blocks of {} bytes", memory_tag_name(tag), max_blocks_, stride_);
    }
    if constexpr (AURORA_DEBUG) {
        if (!live_bits_.reserve(align_up(max_blocks_, 8) / 8, tag)) {
            AURORA_FATAL("pool [{}] could not reserve its debug bitmap", memory_tag_name(tag));
        }
    }
}

Pool::~Pool() {
    AURORA_ASSERT(live_ == 0, "pool [{}] destroyed with {} live blocks", memory_tag_name(tag()), live_);
}

void* Pool::alloc() {
    void* block = nullptr;
    if (free_list_ != nullptr) {
        block      = free_list_;
        free_list_ = *static_cast<void**>(block);
    } else {
        if (high_water_ == max_blocks_) {
            AURORA_FATAL("pool [{}] full: {} blocks of {} bytes", memory_tag_name(tag()), max_blocks_, block_size_);
        }
        grow_to(high_water_ + 1);
        block = blocks_.data() + high_water_ * stride_;
        ++high_water_;
    }

    if constexpr (AURORA_DEBUG) {
        const usize index = index_of(block);
        live_bits_.data()[index / 8] |= static_cast<u8>(1u << (index % 8));
    }
    ++live_;
    return block;
}

void Pool::free(void* block) {
    if (block == nullptr) {
        return;
    }
    const bool belongs = is_block(block);
    AURORA_ASSERT(belongs, "pool [{}]: {} is not a block of this pool", memory_tag_name(tag()), block);
    if (!belongs) {
        return;
    }

    if constexpr (AURORA_DEBUG) {
        const usize index = index_of(block);
        u8& bits = live_bits_.data()[index / 8];
        const u8 mask = static_cast<u8>(1u << (index % 8));
        const bool was_live = (bits & mask) != 0;
        AURORA_ASSERT(was_live, "pool [{}]: double free of block {}", memory_tag_name(tag()), index);
        if (!was_live) {
            return;
        }
        bits = static_cast<u8>(bits & ~mask);
        std::memset(block, 0xDD, stride_);
    }

    *static_cast<void**>(block) = free_list_;
    free_list_ = block;
    --live_;
}

bool Pool::is_block(const void* block) const {
    const uptr base = reinterpret_cast<uptr>(blocks_.data());
    const uptr ptr  = reinterpret_cast<uptr>(block);
    return ptr >= base && ptr < base + high_water_ * stride_ && (ptr - base) % stride_ == 0;
}

usize Pool::index_of(const void* block) const {
    return (reinterpret_cast<uptr>(block) - reinterpret_cast<uptr>(blocks_.data())) / stride_;
}

void Pool::grow_to(usize block_count) {
    const usize bytes = block_count * stride_;
    if (bytes > blocks_.committed()) {
        const usize target = std::min(align_up(bytes, commit_chunk), blocks_.capacity());
        if (!blocks_.ensure_committed(target)) {
            AURORA_FATAL("pool [{}] out of memory: could not commit {} bytes", memory_tag_name(tag()), target);
        }
    }
    if constexpr (AURORA_DEBUG) {
        const usize bitmap_bytes = align_up(block_count, 8) / 8;
        if (bitmap_bytes > live_bits_.committed() && !live_bits_.ensure_committed(bitmap_bytes)) {
            AURORA_FATAL("pool [{}] out of memory for its debug bitmap", memory_tag_name(tag()));
        }
    }
}

} // namespace aurora
