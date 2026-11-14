#define DOCTEST_CONFIG_IMPLEMENT
#include "test/aurora_test.h"

#include <atomic>

namespace aurora::test {

namespace {

constinit std::atomic<u32> g_assert_failures{0};

bool count_assert_failure(const char* /*expression*/, const char* /*file*/, i32 /*line*/, std::string_view /*message*/) {
    g_assert_failures.fetch_add(1, std::memory_order_relaxed);
    return false;
}

} // namespace

u32 assert_failure_count() {
    return g_assert_failures.load(std::memory_order_relaxed);
}

} // namespace aurora::test

int main(int argc, char** argv) {
    aurora::set_assert_handler(&aurora::test::count_assert_failure);

    doctest::Context context(argc, argv);
    return context.run();
}
