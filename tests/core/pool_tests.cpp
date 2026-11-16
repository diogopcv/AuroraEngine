#include "test/aurora_test.h"

#include "core/memory/align.h"
#include "core/memory/pool.h"
#include "core/memory/virtual_buffer.h"

#include <vector>

using namespace aurora;

TEST_SUITE("pool") {

TEST_CASE("alloc returns aligned, distinct blocks") {
    Pool pool(24, 32, 100, MemoryTag::Core);
    CHECK(pool.stride() == 32);
    void* a = pool.alloc();
    void* b = pool.alloc();
    CHECK(a != b);
    CHECK(is_aligned(a, 32));
    CHECK(is_aligned(b, 32));
    CHECK(pool.live_count() == 2);
    pool.free(a);
    pool.free(b);
    CHECK(pool.live_count() == 0);
}

TEST_CASE("tiny blocks still hold the free-list pointer") {
    Pool pool(1, 1, 10, MemoryTag::Core);
    CHECK(pool.stride() >= sizeof(void*));
    void* block = pool.alloc();
    CHECK(is_aligned(block, alignof(void*)));
    pool.free(block);
}

TEST_CASE("a freed block is reused first") {
    Pool pool(64, 16, 10, MemoryTag::Core);
    void* a = pool.alloc();
    void* b = pool.alloc();
    pool.free(a);
    void* c = pool.alloc();
    CHECK(c == a);
    pool.free(b);
    pool.free(c);
}

TEST_CASE("addresses stay stable while the pool grows") {
    constexpr usize count = 100'000;
    Pool pool(64, 16, count, MemoryTag::Core);
    std::vector<u8*> blocks;
    blocks.reserve(count);
    for (usize i = 0; i < count; ++i) {
        auto* block = static_cast<u8*>(pool.alloc());
        block[0] = static_cast<u8>(i);
        blocks.push_back(block);
    }
    bool intact = true;
    for (usize i = 0; i < count; ++i) {
        intact = intact && blocks[i][0] == static_cast<u8>(i);
    }
    CHECK(intact);
    CHECK(pool.live_count() == count);
    for (u8* block : blocks) {
        pool.free(block);
    }
}

TEST_CASE("commit grows in 64 KB chunks and is counted under the tag") {
    const usize before = vm::committed_bytes(MemoryTag::Debug);
    usize during = 0;
    {
        Pool pool(64, 16, 10'000, MemoryTag::Debug);
        void* block = pool.alloc();
        during = vm::committed_bytes(MemoryTag::Debug);
        pool.free(block);
    }
    CHECK(during >= before + Pool::commit_chunk);
    CHECK(vm::committed_bytes(MemoryTag::Debug) == before);
}

TEST_CASE("freeing a pointer from elsewhere asserts and is ignored") {
    Pool pool(64, 16, 10, MemoryTag::Core);
    void* block = pool.alloc();
    u8 outside[64]{};
    CHECK_ASSERTS(pool.free(outside));
    CHECK_ASSERTS(pool.free(static_cast<u8*>(block) + 8));
    CHECK(pool.live_count() == 1);
    pool.free(nullptr);
    CHECK(pool.live_count() == 1);
    pool.free(block);
}

TEST_CASE("a double free asserts in Debug and is ignored") {
    if constexpr (AURORA_DEBUG) {
        Pool pool(64, 16, 10, MemoryTag::Core);
        void* a = pool.alloc();
        void* b = pool.alloc();
        pool.free(a);
        CHECK_ASSERTS(pool.free(a));
        CHECK(pool.live_count() == 1);
        void* c = pool.alloc();
        void* d = pool.alloc();
        CHECK(c != d); // the free list was not corrupted by the double free
        pool.free(b);
        pool.free(c);
        pool.free(d);
    }
}

TEST_CASE("destroying a pool with live blocks asserts") {
    CHECK_ASSERTS({
        Pool pool(64, 16, 10, MemoryTag::Core);
        static_cast<void>(pool.alloc());
    });
}

} // TEST_SUITE("pool")
