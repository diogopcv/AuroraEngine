#include "test/aurora_test.h"

#include "core/memory/align.h"
#include "core/memory/heap.h"

#include <new>
#include <vector>

using namespace aurora;

// Heap statistics are global and never reset: tests compare values before and after.

namespace {

struct alignas(64) CacheLine {
    u8 bytes[64];
};

} // namespace

TEST_SUITE("heap") {

TEST_CASE("alloc and free update the tag statistics") {
    const heap::TagStats before = heap::stats(MemoryTag::Core);
    void* ptr = AURORA_ALLOC(100, 16, MemoryTag::Core);
    const heap::TagStats during = heap::stats(MemoryTag::Core);
    heap::free(ptr, MemoryTag::Core);
    const heap::TagStats after = heap::stats(MemoryTag::Core);

    REQUIRE(ptr != nullptr);
    CHECK(during.live_blocks == before.live_blocks + 1);
    CHECK(during.live_bytes >= before.live_bytes + 100);
    CHECK(during.total_allocations == before.total_allocations + 1);
    CHECK(after.live_blocks == before.live_blocks);
    CHECK(after.live_bytes == before.live_bytes);
    CHECK(after.total_allocations == during.total_allocations);
}

TEST_CASE("allocations honor the requested alignment") {
    for (const usize alignment : {usize{8}, usize{16}, usize{64}, usize{256}, usize{4096}}) {
        void* ptr = heap::alloc(48, alignment, MemoryTag::Core);
        REQUIRE(ptr != nullptr);
        CHECK(is_aligned(ptr, alignment));
        heap::free(ptr, MemoryTag::Core);
    }
}

TEST_CASE("std::vector allocations are counted under General") {
    const heap::TagStats before = heap::stats(MemoryTag::General);
    heap::TagStats during;
    {
        std::vector<i32> values(1000);
        values.back() = 1;
        during = heap::stats(MemoryTag::General);
    }
    const heap::TagStats after = heap::stats(MemoryTag::General);

    CHECK(during.live_bytes >= before.live_bytes + 1000 * sizeof(i32));
    CHECK(during.total_allocations > before.total_allocations);
    CHECK(after.live_bytes == before.live_bytes);
    CHECK(after.live_blocks == before.live_blocks);
}

TEST_CASE("operator new and delete go through the heap, including over-aligned types") {
    // Explicit operator calls: the compiler may elide a new-expression paired with its delete.
    const heap::TagStats before = heap::stats(MemoryTag::General);
    void* plain   = ::operator new(sizeof(i32));
    void* aligned = ::operator new(sizeof(CacheLine), std::align_val_t{alignof(CacheLine)});
    const heap::TagStats during = heap::stats(MemoryTag::General);
    const bool is_line_aligned = is_aligned(aligned, alignof(CacheLine));
    ::operator delete(aligned, std::align_val_t{alignof(CacheLine)});
    ::operator delete(plain);
    const heap::TagStats after = heap::stats(MemoryTag::General);

    CHECK(during.live_blocks == before.live_blocks + 2);
    CHECK(during.total_allocations == before.total_allocations + 2);
    CHECK(is_line_aligned);
    CHECK(after.live_blocks == before.live_blocks);
}

TEST_CASE("freeing with the wrong tag asserts and keeps the statistics right") {
    if constexpr (AURORA_DEBUG) {
        const heap::TagStats core_before  = heap::stats(MemoryTag::Core);
        const heap::TagStats debug_before = heap::stats(MemoryTag::Debug);
        void* ptr = heap::alloc(32, 16, MemoryTag::Core);
        REQUIRE(ptr != nullptr);
        CHECK_ASSERTS(heap::free(ptr, MemoryTag::Debug));
        CHECK(heap::stats(MemoryTag::Core).live_blocks == core_before.live_blocks);
        CHECK(heap::stats(MemoryTag::Debug).live_blocks == debug_before.live_blocks);
    }
}

} // TEST_SUITE("heap")
