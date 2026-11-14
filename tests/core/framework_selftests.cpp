#include "test/aurora_test.h"

TEST_CASE("selftest: a failing check reports its location" * doctest::skip() * doctest::no_breaks()) {
    CHECK(1 == 2);
}
