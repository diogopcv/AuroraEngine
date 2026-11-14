#include "test/aurora_test.h"

#include "core/memory/align.h"
#include "core/memory/virtual_buffer.h"
#include "core/memory/virtual_memory.h"

#include <utility>

using namespace aurora;

TEST_SUITE("align") {

TEST_CASE("align_up and is_aligned") {
    CHECK(align_up(0, 16) == 0);
    CHECK(align_up(1, 16) == 16);
    CHECK(align_up(16, 16) == 16);
    CHECK(align_up(17, 16) == 32);
    CHECK(is_aligned(usize{64}, 64));
    CHECK_FALSE(is_aligned(usize{65}, 64));
    CHECK(is_power_of_two(4096));
    CHECK_FALSE(is_power_of_two(0));
    CHECK_FALSE(is_power_of_two(3 * 1024));
}

} // TEST_SUITE("align")

TEST_SUITE("virtual_memory") {

TEST_CASE("page size and allocation granularity") {
    const usize page = vm::page_size();
    const usize granularity = vm::allocation_granularity();
    CHECK(is_power_of_two(page));
    CHECK(page >= 4_KB);
    CHECK(is_power_of_two(granularity));
    CHECK(granularity % page == 0);
}

TEST_CASE("reserve, commit, write, decommit and release") {
    void* base = vm::reserve(1_MB);
    REQUIRE(base != nullptr);
    CHECK(is_aligned(base, vm::allocation_granularity()));

    auto* bytes = static_cast<u8*>(base);
    REQUIRE(vm::commit(base, 2 * vm::page_size()));
    CHECK(bytes[0] == 0);
    CHECK(bytes[2 * vm::page_size() - 1] == 0);
    bytes[0] = 0xAB;
    bytes[2 * vm::page_size() - 1] = 0xCD;
    CHECK(bytes[0] == 0xAB);

    vm::decommit(base, 2 * vm::page_size());
    REQUIRE(vm::commit(base, vm::page_size()));
    CHECK(bytes[0] == 0); // recommitted pages are zero again

    vm::release(base);
}

TEST_CASE("commit with an unaligned address asserts and fails") {
    void* base = vm::reserve(64_KB);
    REQUIRE(base != nullptr);
    bool ok = true;
    CHECK_ASSERTS(ok = vm::commit(static_cast<u8*>(base) + 1, 1));
    if constexpr (AURORA_ASSERTS_ENABLED) {
        CHECK_FALSE(ok);
    }
    vm::release(base);
}

} // TEST_SUITE("virtual_memory")

TEST_SUITE("virtual_buffer") {

TEST_CASE("reserve 1 GB, grow by commit, base address never changes") {
    VirtualBuffer buffer;
    REQUIRE(buffer.reserve(1_GB));
    CHECK(buffer.capacity() >= 1_GB);
    CHECK(buffer.committed() == 0);

    u8* const base = buffer.data();
    const usize page = vm::page_size();
    for (usize size = 1_MB; size <= 64_MB; size += 1_MB) {
        REQUIRE(buffer.ensure_committed(size));
        CHECK(buffer.data() == base);
        CHECK(buffer.committed() >= size);
        for (usize offset = size - 1_MB; offset < size; offset += page) {
            base[offset] = static_cast<u8>(offset / page);
        }
    }
    CHECK(base[0] == 0);
    CHECK(base[64_MB - page] == static_cast<u8>((64_MB - page) / page));
}

TEST_CASE("committed size is rounded to pages and never shrinks") {
    VirtualBuffer buffer;
    REQUIRE(buffer.reserve(1_MB));
    REQUIRE(buffer.ensure_committed(1));
    CHECK(buffer.committed() == vm::page_size());
    REQUIRE(buffer.ensure_committed(vm::page_size() + 1));
    CHECK(buffer.committed() == 2 * vm::page_size());
    REQUIRE(buffer.ensure_committed(1));
    CHECK(buffer.committed() == 2 * vm::page_size());
}

TEST_CASE("committing past the capacity fails and leaves the buffer unchanged") {
    VirtualBuffer buffer;
    REQUIRE(buffer.reserve(64_KB));
    REQUIRE(buffer.ensure_committed(4_KB));
    const usize before = buffer.committed();
    CHECK_FALSE(buffer.ensure_committed(buffer.capacity() + 1));
    CHECK(buffer.committed() == before);
}

TEST_CASE("decommit_all keeps the reservation and recommitted memory is zero") {
    VirtualBuffer buffer;
    REQUIRE(buffer.reserve(1_MB));
    REQUIRE(buffer.ensure_committed(8_KB));
    u8* const base = buffer.data();
    base[100] = 42;

    buffer.decommit_all();
    CHECK(buffer.committed() == 0);
    CHECK(buffer.is_reserved());
    CHECK(buffer.data() == base);

    REQUIRE(buffer.ensure_committed(8_KB));
    CHECK(base[100] == 0);
}

TEST_CASE("move transfers the reservation") {
    VirtualBuffer a;
    REQUIRE(a.reserve(1_MB));
    REQUIRE(a.ensure_committed(vm::page_size()));
    u8* const base = a.data();

    VirtualBuffer b = std::move(a);
    CHECK_FALSE(a.is_reserved());
    CHECK(a.committed() == 0);
    CHECK(b.data() == base);
    CHECK(b.committed() == vm::page_size());

    VirtualBuffer c;
    REQUIRE(c.reserve(64_KB));
    c = std::move(b);
    CHECK(c.data() == base);
    CHECK_FALSE(b.is_reserved());
}

TEST_CASE("release empties the buffer, which can be reserved again") {
    VirtualBuffer buffer;
    REQUIRE(buffer.reserve(64_KB));
    buffer.release();
    CHECK_FALSE(buffer.is_reserved());
    CHECK(buffer.capacity() == 0);
    REQUIRE(buffer.reserve(64_KB));
    CHECK(buffer.is_reserved());
}

TEST_CASE("reserving twice asserts and fails") {
    VirtualBuffer buffer;
    REQUIRE(buffer.reserve(64_KB));
    u8* const base = buffer.data();
    bool ok = true;
    CHECK_ASSERTS(ok = buffer.reserve(64_KB));
    if constexpr (AURORA_ASSERTS_ENABLED) {
        CHECK_FALSE(ok);
    }
    CHECK(buffer.data() == base);
}

} // TEST_SUITE("virtual_buffer")
