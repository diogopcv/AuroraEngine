#include "test/aurora_test.h"

#include "core/memory/dense_array.h"
#include "core/memory/virtual_buffer.h"

#include <string>
#include <thread>
#include <vector>

using namespace aurora;

namespace {

struct Particle {
    float x  = 0.0f;
    float y  = 0.0f;
    float life = 0.0f;
};

struct Named {
    static inline i32 alive = 0;
    std::string name;
    explicit Named(std::string n) : name(std::move(n)) { ++alive; }
    Named(Named&& other) noexcept : name(std::move(other.name)) { ++alive; }
    Named& operator=(Named&& other) noexcept {
        name = std::move(other.name);
        return *this;
    }
    ~Named() { --alive; }
    Named(const Named&) = delete;
    Named& operator=(const Named&) = delete;
};

} // namespace

TEST_SUITE("dense_array") {

TEST_CASE("push_back constructs in place and indexes in order") {
    DenseArray<Particle> particles(100, MemoryTag::Core);
    CHECK(particles.empty());
    Particle& first = particles.push_back(1.0f, 2.0f, 3.0f);
    particles.push_back(4.0f, 5.0f, 6.0f);
    CHECK(particles.size() == 2);
    CHECK(&first == &particles[0]);
    CHECK(particles[1].life == 6.0f);
    CHECK(particles.span().size() == 2);
}

TEST_CASE("swap_remove keeps the array dense") {
    DenseArray<i32> values(16, MemoryTag::Core);
    for (i32 i = 0; i < 5; ++i) {
        values.push_back(i * 10);
    }
    values.swap_remove(1); // 40 moves into slot 1
    CHECK(values.size() == 4);
    CHECK(std::vector<i32>(values.begin(), values.end()) == std::vector<i32>{0, 40, 20, 30});

    values.swap_remove(3); // removing the last element moves nothing
    CHECK(std::vector<i32>(values.begin(), values.end()) == std::vector<i32>{0, 40, 20});

    values.swap_remove(0);
    values.swap_remove(0);
    values.swap_remove(0);
    CHECK(values.empty());
}

TEST_CASE("one million elements without reallocation") {
    constexpr usize count = 1'000'000;
    DenseArray<Particle> particles(count, MemoryTag::Core);
    particles.push_back();
    Particle* const base = particles.data();
    const Particle* const first = &particles[0];

    for (usize i = 1; i < count; ++i) {
        particles.push_back(static_cast<float>(i), 0.0f, 1.0f);
    }
    CHECK(particles.size() == count);
    CHECK(particles.data() == base);
    CHECK(&particles[0] == first);
    CHECK(particles[count - 1].x == static_cast<float>(count - 1));
    CHECK(particles.committed_bytes() >= count * sizeof(Particle));
}

TEST_CASE("non-trivial elements are moved and destroyed") {
    {
        DenseArray<Named> names(16, MemoryTag::Core);
        names.push_back(std::string("first, long enough to need a heap allocation"));
        names.push_back(std::string("second"));
        names.push_back(std::string("third"));
        CHECK(Named::alive == 3);

        names.swap_remove(0);
        CHECK(Named::alive == 2);
        CHECK(names[0].name == "third");
        CHECK(names[1].name == "second");

        names.pop_back();
        CHECK(Named::alive == 1);
    }
    CHECK(Named::alive == 0); // the destructor destroys what is left
}

TEST_CASE("clear destroys the elements and keeps the pages") {
    DenseArray<Named> names(16, MemoryTag::Core);
    names.push_back(std::string("a"));
    names.push_back(std::string("b"));
    const usize committed = names.committed_bytes();
    names.clear();
    CHECK(names.empty());
    CHECK(Named::alive == 0);
    CHECK(names.committed_bytes() == committed);
    names.push_back(std::string("c"));
    CHECK(names[0].name == "c");
}

TEST_CASE("out-of-range access and removal assert") {
    DenseArray<i32> values(4, MemoryTag::Core);
    values.push_back(7);
    usize index = 3;
    CHECK_ASSERTS(values.swap_remove(index));
    CHECK(values.size() == 1);
    CHECK_ASSERTS(static_cast<void>(values[index]));
    values.pop_back();
    CHECK_ASSERTS(values.pop_back());
    CHECK(values.empty());
}

TEST_CASE("memory is counted under the array's tag") {
    const usize before = vm::committed_bytes(MemoryTag::Debug);
    usize during = 0;
    {
        DenseArray<u64> values(100'000, MemoryTag::Debug);
        values.push_back(1u);
        during = vm::committed_bytes(MemoryTag::Debug);
    }
    CHECK(during == before + DenseArray<u64>::commit_chunk);
    CHECK(vm::committed_bytes(MemoryTag::Debug) == before);
}

TEST_CASE("disjoint ranges can be written from several threads") {
    constexpr usize count = 400'000;
    constexpr usize thread_count = 4;
    DenseArray<Particle> particles(count, MemoryTag::Core);
    for (usize i = 0; i < count; ++i) {
        particles.push_back(0.0f, 0.0f, static_cast<float>(i));
    }

    std::vector<std::thread> threads;
    const std::span<Particle> all = particles.span();
    for (usize t = 0; t < thread_count; ++t) {
        const std::span<Particle> range = all.subspan(t * (count / thread_count), count / thread_count);
        threads.emplace_back([range] {
            for (Particle& particle : range) {
                particle.x = particle.life * 2.0f;
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    bool correct = true;
    for (usize i = 0; i < count; ++i) {
        correct = correct && particles[i].x == static_cast<float>(i) * 2.0f;
    }
    CHECK(correct);
}

} // TEST_SUITE("dense_array")
