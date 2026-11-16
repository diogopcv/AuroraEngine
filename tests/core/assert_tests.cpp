#include "test/aurora_test.h"

#include "core/assert.h"

using namespace aurora;

TEST_SUITE("assert") {

TEST_CASE("a failing assert is reported to the handler") {
    bool ok = false;
    CHECK_ASSERTS(AURORA_ASSERT(ok));
}

TEST_CASE("a failing assert with a formatted message is reported") {
    i32 index = 7;
    i32 count = 4;
    CHECK_ASSERTS(AURORA_ASSERT(index < count, "index {} out of bounds [0, {})", index, count));
}

TEST_CASE("a passing assert is not reported") {
    bool ok = true;
    i32  value = 2;
    const u32 before = test::assert_failure_count();
    AURORA_ASSERT(ok);
    AURORA_ASSERT(value == 2, "value is {}", value);
    CHECK(test::assert_failure_count() == before);
}

TEST_CASE("the condition is evaluated only when asserts are enabled") {
    i32 evaluations = 0;
    const auto counted_true = [&evaluations] {
        ++evaluations;
        return true;
    };
    AURORA_ASSERT(counted_true()); // side effect only to observe evaluation; never do this in engine code
    CHECK(evaluations == (AURORA_ASSERTS_ENABLED ? 1 : 0));
}

TEST_CASE("CHECK_ASSERTS reports a statement that does not assert" * doctest::should_fail() * doctest::no_breaks()) {
    if constexpr (AURORA_ASSERTS_ENABLED) {
        bool ok = true;
        CHECK_ASSERTS(AURORA_ASSERT(ok));
    } else {
        FAIL("CHECK_ASSERTS is compiled out when asserts are disabled");
    }
}

} // TEST_SUITE("assert")
