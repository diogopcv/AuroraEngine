#include "test/aurora_test.h"

#include "core/handle.h"
#include "core/memory/handle_pool.h"

#include <string>
#include <type_traits>
#include <vector>

using namespace aurora;

namespace {

struct Mesh {
    i32 vertex_count = 0;
    i32 index_count  = 0;
};

struct Tracked {
    static inline i32 alive = 0;
    std::string name;
    explicit Tracked(std::string n) : name(std::move(n)) { ++alive; }
    ~Tracked() { --alive; }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
};

struct Texture;
using TextureHandle = Handle<Texture>; // incomplete type: only the handle is needed

} // namespace

static_assert(sizeof(Handle<Mesh>) == 8);
static_assert(std::is_trivially_copyable_v<Handle<Mesh>>);
static_assert(!std::is_same_v<Handle<Mesh>, TextureHandle>);
static_assert(!TextureHandle{}.is_valid());

TEST_SUITE("handle_pool") {

TEST_CASE("create constructs the object and get returns it") {
    HandlePool<Mesh> pool(100, MemoryTag::Core);
    const Handle<Mesh> handle = pool.create(36, 12);
    REQUIRE(handle.is_valid());
    Mesh* mesh = pool.get(handle);
    REQUIRE(mesh != nullptr);
    CHECK(mesh->vertex_count == 36);
    CHECK(mesh->index_count == 12);
    CHECK(pool.contains(handle));
    CHECK(pool.live_count() == 1);
    pool.destroy(handle);
}

TEST_CASE("a default handle is never valid") {
    HandlePool<Mesh> pool(10, MemoryTag::Core);
    const Handle<Mesh> handle = pool.create();
    CHECK(pool.get(Handle<Mesh>{}) == nullptr);
    CHECK_FALSE(pool.contains(Handle<Mesh>{}));
    pool.destroy(handle);
}

TEST_CASE("a stale handle returns nullptr") {
    HandlePool<Mesh> pool(10, MemoryTag::Core);
    const Handle<Mesh> handle = pool.create(1, 2);
    pool.destroy(handle);
    CHECK(pool.get(handle) == nullptr);
    CHECK_FALSE(pool.contains(handle));
    CHECK(pool.live_count() == 0);
}

TEST_CASE("a reused slot gets a new generation") {
    HandlePool<Mesh> pool(10, MemoryTag::Core);
    const Handle<Mesh> old_handle = pool.create(1, 1);
    pool.destroy(old_handle);
    const Handle<Mesh> new_handle = pool.create(2, 2);

    CHECK(new_handle.index == old_handle.index);
    CHECK(new_handle.generation == old_handle.generation + 1);
    CHECK(pool.get(old_handle) == nullptr);
    REQUIRE(pool.get(new_handle) != nullptr);
    CHECK(pool.get(new_handle)->vertex_count == 2);
    pool.destroy(new_handle);
}

TEST_CASE("destructors run on destroy") {
    {
        HandlePool<Tracked> pool(10, MemoryTag::Core);
        const Handle<Tracked> a = pool.create(std::string("a long name that does not fit in a small string"));
        const Handle<Tracked> b = pool.create(std::string("b"));
        CHECK(Tracked::alive == 2);
        REQUIRE(pool.get(a) != nullptr);
        CHECK(pool.get(a)->name.size() > 20);
        pool.destroy(a);
        CHECK(Tracked::alive == 1);
        pool.destroy(b);
    }
    CHECK(Tracked::alive == 0);
}

TEST_CASE("destroying a stale handle asserts and changes nothing") {
    HandlePool<Mesh> pool(10, MemoryTag::Core);
    const Handle<Mesh> stale = pool.create();
    pool.destroy(stale);
    const Handle<Mesh> live = pool.create();
    CHECK_ASSERTS(pool.destroy(stale));
    CHECK(pool.contains(live));
    CHECK(pool.live_count() == 1);
    pool.destroy(live);
}

TEST_CASE("destroying a pool with live objects asserts and destroys them") {
    CHECK_ASSERTS({
        HandlePool<Tracked> pool(10, MemoryTag::Core);
        static_cast<void>(pool.create(std::string("left behind")));
        static_cast<void>(pool.create(std::string("also left behind")));
    });
    CHECK(Tracked::alive == 0);
}

TEST_CASE("a slot at the last generation is retired") {
    HandlePool<Mesh, 3> pool(10, MemoryTag::Core);
    Handle<Mesh> handle = pool.create();
    const u32 first_index = handle.index;
    for (u32 generation = 1; generation < 3; ++generation) {
        CHECK(handle.generation == generation);
        pool.destroy(handle);
        handle = pool.create();
        CHECK(handle.index == first_index);
    }
    CHECK(handle.generation == 3);
    pool.destroy(handle);

    const Handle<Mesh> next = pool.create();
    CHECK(next.index != first_index); // retired: never reused
    CHECK(next.generation == 1);
    CHECK(pool.get(handle) == nullptr);
    pool.destroy(next);
}

TEST_CASE("for_each visits live objects in index order") {
    HandlePool<Mesh> pool(10, MemoryTag::Core);
    const Handle<Mesh> a = pool.create(1, 0);
    const Handle<Mesh> b = pool.create(2, 0);
    const Handle<Mesh> c = pool.create(3, 0);
    pool.destroy(b);

    std::vector<i32> seen;
    std::vector<Handle<Mesh>> handles;
    pool.for_each([&](Handle<Mesh> handle, Mesh& mesh) {
        seen.push_back(mesh.vertex_count);
        handles.push_back(handle);
        mesh.index_count = 7;
    });
    CHECK(seen == std::vector<i32>{1, 3});
    REQUIRE(handles.size() == 2);
    CHECK(handles[0] == a);
    CHECK(handles[1] == c);

    const HandlePool<Mesh>& view = pool;
    i32 total = 0;
    view.for_each([&](Handle<Mesh>, const Mesh& mesh) { total += mesh.index_count; });
    CHECK(total == 14);

    pool.destroy(a);
    pool.destroy(c);
}

TEST_CASE("creating inside for_each asserts") {
    HandlePool<Mesh> pool(10, MemoryTag::Core);
    const Handle<Mesh> first = pool.create();
    Handle<Mesh> created{};
    CHECK_ASSERTS(pool.for_each([&](Handle<Mesh>, Mesh&) { created = pool.create(); }));
    CHECK(pool.live_count() == (AURORA_ASSERTS_ENABLED ? 2u : 1u));
    pool.destroy(first);
    if (pool.contains(created)) {
        pool.destroy(created);
    }
}

TEST_CASE("many objects keep their handles valid") {
    constexpr u32 count = 100'000;
    HandlePool<Mesh> pool(count, MemoryTag::Core);
    std::vector<Handle<Mesh>> handles;
    handles.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        handles.push_back(pool.create(static_cast<i32>(i), 0));
    }
    bool intact = true;
    for (u32 i = 0; i < count; ++i) {
        const Mesh* mesh = pool.get(handles[i]);
        intact = intact && mesh != nullptr && mesh->vertex_count == static_cast<i32>(i);
    }
    CHECK(intact);
    for (const Handle<Mesh>& handle : handles) {
        pool.destroy(handle);
    }
    CHECK(pool.live_count() == 0);
}

} // TEST_SUITE("handle_pool")
