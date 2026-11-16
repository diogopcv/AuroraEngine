#pragma once

#include "core/memory/memory_tag.h"
#include "core/memory/virtual_buffer.h"
#include "core/types.h"

namespace aurora {

// Fixed-size blocks with stable addresses over a VirtualBuffer. Single owner, not thread-safe.
// Blocks are raw memory: no constructors or destructors run.
class Pool {
public:
    static constexpr usize commit_chunk = 64_KB;

    Pool(usize block_size, usize block_alignment, usize max_blocks, MemoryTag tag); // fatal if the address space is exhausted
    ~Pool();                                                                        // asserts if blocks are still live

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

    // Uninitialized block. Fatal when all max_blocks are live or when out of memory.
    [[nodiscard]] void* alloc();

    // nullptr is ignored. A pointer that is not a block of this pool is rejected.
    void free(void* block);

    usize     live_count() const { return live_; }
    usize     capacity() const { return max_blocks_; }
    usize     block_size() const { return block_size_; }
    usize     stride() const { return stride_; }
    MemoryTag tag() const { return blocks_.tag(); }

private:
    bool is_block(const void* block) const;
    usize index_of(const void* block) const;
    void grow_to(usize block_count);

    VirtualBuffer blocks_;
    VirtualBuffer live_bits_; // Debug: one bit per block, for double-free detection
    usize         block_size_ = 0;
    usize         stride_     = 0;
    usize         max_blocks_ = 0;
    usize         high_water_ = 0; // blocks handed out from the top at least once
    usize         live_       = 0;
    void*         free_list_  = nullptr;
};

} // namespace aurora
