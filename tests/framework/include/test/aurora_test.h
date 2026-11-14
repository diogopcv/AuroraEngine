#pragma once

#include "core/assert.h"
#include "core/types.h"
#include <ostream>
#include <doctest/doctest.h>

namespace aurora::test {

u32 assert_failure_count();

} // namespace aurora::test

#define CHECK_ASSERTS(...)                                                         \
    do {                                                                           \
        if constexpr (AURORA_ASSERTS_ENABLED) {                                    \
            const ::aurora::u32 aurora_asserts_before_ =                           \
                ::aurora::test::assert_failure_count();                            \
            __VA_ARGS__;                                                           \
            CHECK_MESSAGE(::aurora::test::assert_failure_count() >                 \
                              aurora_asserts_before_,                              \
                          "expected an assert: " #__VA_ARGS__);                    \
        }                                                                          \
    } while (0)
