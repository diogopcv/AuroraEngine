#include "test/aurora_test.h"

#include "core/memory/ref_pool.h"

#include <atomic>
#include <new>
#include <thread>
#include <vector>

using namespace aurora;

namespace {

struct Texture {
    static inline std::atomic<i32> alive{0};
    static inline std::atomic<i32> destroyed{0};
    i32 width  = 0;
    i32 height = 0;
    Texture(i32 w, i32 h) : width(w), height(h) { alive.fetch_add(1); }
    ~Texture() {
        alive.fetch_sub(1);
        destroyed.fetch_add(1);
    }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
};

} // namespace

static_assert(sizeof(Ref<Texture>) == 16);

TEST_SUITE("ref_pool") {

TEST_CASE("create returns a Ref to the constructed object") {
    RefPool<Texture> pool(16, MemoryTag::Core);
    Ref<Texture> texture = pool.create(256, 128);
    REQUIRE(texture);
    CHECK(texture->width == 256);
    CHECK((*texture).height == 128);
    CHECK(texture.handle().is_valid());
    CHECK(pool.live_count() == 1);
}

TEST_CASE("the last Ref queues the object and collect destroys it") {
    const i32 destroyed_before = Texture::destroyed.load();
    RefPool<Texture> pool(16, MemoryTag::Core);
    Handle<Texture> weak;
    {
        Ref<Texture> a = pool.create(1, 1);
        Ref<Texture> b = a;
        weak = a.handle();
        a.reset();
        CHECK(pool.collect() == 0); // b still holds it
        CHECK(pool.upgrade(weak));
    }
    CHECK(Texture::destroyed.load() == destroyed_before); // queued, not destroyed yet
    CHECK(pool.live_count() == 1);
    CHECK_FALSE(pool.upgrade(weak));                      // pending: no resurrection

    CHECK(pool.collect() == 1);
    CHECK(Texture::destroyed.load() == destroyed_before + 1);
    CHECK(pool.live_count() == 0);
    CHECK_FALSE(pool.upgrade(weak));
    CHECK(pool.collect() == 0);
}

TEST_CASE("moving a Ref keeps the count") {
    RefPool<Texture> pool(16, MemoryTag::Core);
    Ref<Texture> a = pool.create(2, 2);
    Ref<Texture> b = std::move(a);
    CHECK_FALSE(a);
    CHECK(b);
    a.reset(); // empty: no effect
    CHECK(pool.collect() == 0);
    b = Ref<Texture>{};
    CHECK(pool.collect() == 1);
}

TEST_CASE("assignment releases the previous object") {
    RefPool<Texture> pool(16, MemoryTag::Core);
    Ref<Texture> a = pool.create(1, 1);
    Ref<Texture> b = pool.create(2, 2);
    a = b; // a's first object loses its only reference
    CHECK(a == b);
    CHECK(pool.collect() == 1);
    CHECK(a->width == 2);

    Ref<Texture>& self = a;
    a = self; // self-assignment keeps the count
    CHECK(pool.collect() == 0);
    a.reset();
    b.reset();
    CHECK(pool.collect() == 1);
}

TEST_CASE("upgrade adds a reference while the object is alive") {
    RefPool<Texture> pool(16, MemoryTag::Core);
    Ref<Texture> original = pool.create(4, 4);
    Ref<Texture> upgraded = pool.upgrade(original.handle());
    REQUIRE(upgraded);
    CHECK(upgraded == original);
    original.reset();
    CHECK(pool.collect() == 0);
    CHECK(upgraded->width == 4);
    upgraded.reset();
    CHECK(pool.collect() == 1);
    CHECK_FALSE(pool.upgrade(Handle<Texture>{}));
}

TEST_CASE("the owner mutates through get_mut") {
    RefPool<Texture> pool(16, MemoryTag::Core);
    Ref<Texture> texture = pool.create(8, 8);
    Texture* writable = pool.get_mut(texture.handle());
    REQUIRE(writable != nullptr);
    writable->width = 16; // e.g. a hot reload at a sync point
    CHECK(texture->width == 16);

    const Handle<Texture> weak = texture.handle();
    texture.reset();
    CHECK(pool.get_mut(weak) == nullptr); // pending
    CHECK(pool.collect() == 1);
}

TEST_CASE("destroying the pool collects pending objects") {
    const i32 alive_before = Texture::alive.load();
    {
        RefPool<Texture> pool(16, MemoryTag::Core);
        Ref<Texture> texture = pool.create(1, 1);
    } // the Ref dies first, then the pool collects it
    CHECK(Texture::alive.load() == alive_before);
}

TEST_CASE("destroying the pool with live references asserts") {
    alignas(Ref<Texture>) unsigned char storage[sizeof(Ref<Texture>)];
    CHECK_ASSERTS({
        RefPool<Texture> pool(16, MemoryTag::Core);
        ::new (static_cast<void*>(storage)) Ref<Texture>(pool.create(1, 1)); // never destroyed: it would outlive the pool
    });
}

TEST_CASE("copies from many threads keep the count") {
    constexpr i32 thread_count = 8;
    constexpr i32 iterations   = 20'000;
    const i32 destroyed_before = Texture::destroyed.load();

    RefPool<Texture> pool(16, MemoryTag::Core);
    Ref<Texture> shared = pool.create(1, 1);
    const Handle<Texture> weak = shared.handle();
    {
        std::vector<std::thread> threads;
        for (i32 t = 0; t < thread_count; ++t) {
            threads.emplace_back([seed = shared] {
                std::vector<Ref<Texture>> copies;
                copies.reserve(16);
                for (i32 i = 0; i < iterations; ++i) {
                    copies.push_back(seed);
                    if (copies.size() == 16) {
                        copies.clear();
                    }
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }
    CHECK(pool.collect() == 0);
    CHECK(Texture::destroyed.load() == destroyed_before);
    shared.reset();
    CHECK_FALSE(pool.upgrade(weak));
    CHECK(pool.collect() == 1);
    CHECK(Texture::destroyed.load() == destroyed_before + 1);
}

TEST_CASE("last references dropped on many threads while the owner collects") {
    constexpr u32 object_count = 4'000;
    constexpr i32 thread_count = 8;
    const i32 destroyed_before = Texture::destroyed.load();

    RefPool<Texture> pool(object_count, MemoryTag::Core);
    std::vector<std::vector<Ref<Texture>>> per_thread(thread_count);
    for (u32 i = 0; i < object_count; ++i) {
        Ref<Texture> texture = pool.create(static_cast<i32>(i), 0);
        for (i32 t = 0; t < thread_count; ++t) {
            per_thread[static_cast<usize>(t)].push_back(texture); // every thread holds every object
        }
    }

    std::atomic<i32> finished{0};
    std::vector<std::thread> threads;
    for (i32 t = 0; t < thread_count; ++t) {
        threads.emplace_back([&refs = per_thread[static_cast<usize>(t)], &finished] {
            refs.clear(); // the last thread to drop each object pushes it
            finished.fetch_add(1);
        });
    }

    usize collected = 0;
    while (finished.load() < thread_count) {
        collected += pool.collect(); // the owner keeps collecting while threads push
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    collected += pool.collect();

    CHECK(collected == object_count);
    CHECK(Texture::destroyed.load() == destroyed_before + static_cast<i32>(object_count));
    CHECK(pool.live_count() == 0);
}

} // TEST_SUITE("ref_pool")
