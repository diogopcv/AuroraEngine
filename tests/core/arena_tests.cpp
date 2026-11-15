#include "test/aurora_test.h"

#include "core/memory/align.h"
#include "core/memory/arena.h"
#include "core/memory/virtual_buffer.h"

#include <thread>

using namespace aurora;

TEST_SUITE("arena") {

TEST_CASE("push returns aligned, consecutive memory") {
    Arena arena(1_MB, MemoryTag::Core);
    auto* first = static_cast<u8*>(arena.push(3, 1));
    CHECK(arena.position() == 3);

    for (const usize alignment : {usize{4}, usize{16}, usize{64}, usize{4096}}) {
        void* ptr = arena.push(8, alignment);
        CHECK(is_aligned(ptr, alignment));
        CHECK(static_cast<u8*>(ptr) > first);
    }
    CHECK(arena.position() <= arena.committed());
}

TEST_CASE("push_array returns typed, aligned storage") {
    struct alignas(32) Vec { float values[8]; }; // no padding: MSVC C4324
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(1, 1));
    Vec* vecs = arena.push_array<Vec>(10);
    CHECK(is_aligned(vecs, alignof(Vec)));
    vecs[9].values[7] = 1.0f;
    CHECK(arena.position() >= 1 + 10 * sizeof(Vec));
}

TEST_CASE("commit grows in 64 KB chunks and reset keeps it") {
    Arena arena(1_MB, MemoryTag::Core);
    CHECK(arena.committed() == 0);
    static_cast<void>(arena.push(1));
    CHECK(arena.committed() == Arena::commit_chunk);
    static_cast<void>(arena.push(Arena::commit_chunk));
    CHECK(arena.committed() == 2 * Arena::commit_chunk);

    arena.reset();
    CHECK(arena.position() == 0);
    CHECK(arena.committed() == 2 * Arena::commit_chunk);
}

TEST_CASE("mark and rewind reuse memory") {
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(100));
    const ArenaMark mark = arena.mark();
    void* first = arena.push(64);
    arena.rewind(mark);
    CHECK(arena.position() == mark.position);
    void* again = arena.push(64);
    CHECK(again == first);
}

TEST_CASE("rewound memory is filled with 0xDD in Debug") {
    Arena arena(1_MB, MemoryTag::Core);
    const ArenaMark mark = arena.mark();
    auto* bytes = static_cast<u8*>(arena.push(256));
    for (usize i = 0; i < 256; ++i) {
        bytes[i] = 0x11;
    }
    arena.rewind(mark);
    if constexpr (AURORA_DEBUG) {
        CHECK(bytes[0] == 0xDD);
        CHECK(bytes[255] == 0xDD);
    }
}

TEST_CASE("high water keeps the largest position") {
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(1000));
    arena.reset();
    static_cast<void>(arena.push(10));
    CHECK(arena.high_water() == 1000);
    CHECK(arena.position() == 10);
}

TEST_CASE("rewinding forward asserts and leaves the arena unchanged") {
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(32));
    ArenaMark ahead{arena.position() + 16};
    CHECK_ASSERTS(arena.rewind(ahead));
    CHECK(arena.position() == 32);
}

TEST_CASE("a non-power-of-two alignment asserts and falls back to the default") {
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(1, 1));
    void* ptr = nullptr;
    usize alignment = 24;
    CHECK_ASSERTS(ptr = arena.push(8, alignment));
    if constexpr (AURORA_ASSERTS_ENABLED) {
        CHECK(is_aligned(ptr, Arena::default_alignment));
    }
}

TEST_CASE("committed memory is counted under the arena's tag") {
    const usize before = vm::committed_bytes(MemoryTag::Debug);
    usize during = 0;
    {
        Arena arena(1_MB, MemoryTag::Debug);
        static_cast<void>(arena.push(10));
        during = vm::committed_bytes(MemoryTag::Debug);
        CHECK(arena.tag() == MemoryTag::Debug);
    }
    CHECK(during == before + Arena::commit_chunk);
    CHECK(vm::committed_bytes(MemoryTag::Debug) == before);
}

TEST_CASE("ArenaScope rewinds on exit") {
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(10));
    {
        ArenaScope scope(arena);
        static_cast<void>(scope.arena().push(500));
        CHECK(arena.position() > 10);
    }
    CHECK(arena.position() == 10);
}

} // TEST_SUITE("arena")

TEST_SUITE("scratch") {

TEST_CASE("scratch rewinds on exit and returns the same arena on the same thread") {
    const Arena* first = nullptr;
    usize start = 0;
    {
        ScratchScope s = scratch();
        first = &s.arena();
        start = s.arena().position();
        static_cast<void>(s.arena().push(1000));
    }
    ScratchScope s = scratch();
    CHECK(&s.arena() == first);
    CHECK(s.arena().position() == start);
}

TEST_CASE("scratch avoids the conflicting arena") {
    ScratchScope outer = scratch();
    ScratchScope inner = scratch(&outer.arena());
    CHECK(&inner.arena() != &outer.arena());
    ScratchScope third = scratch(&inner.arena());
    CHECK(&third.arena() == &outer.arena());
}

TEST_CASE("each thread has its own scratch arenas") {
    const Arena* main_arena = &scratch().arena();
    const Arena* other_arena = nullptr;
    std::thread worker([&other_arena] { other_arena = &scratch().arena(); });
    worker.join();
    CHECK(other_arena != nullptr);
    CHECK(other_arena != main_arena);
}

} // TEST_SUITE("scratch")

TEST_SUITE("arena_ring") {

TEST_CASE("advance moves to the next arena and resets it") {
    ArenaRing<3> ring(1_MB, MemoryTag::Core);
    CHECK(ring.index() == 0);
    static_cast<void>(ring.current().push(100));

    ring.advance();
    CHECK(ring.index() == 1);
    CHECK(ring.current().position() == 0);
    static_cast<void>(ring.current().push(200));
    CHECK(ring.at(0).position() == 100); // frame 0 may still be in flight: untouched

    ring.advance();
    ring.advance();
    CHECK(ring.index() == 0);
    CHECK(ring.current().position() == 0); // reset when it came around again
    CHECK(ring.at(1).position() == 200);
}

} // TEST_SUITE("arena_ring")
