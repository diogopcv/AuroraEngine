#pragma once

#include "core/types.h"

namespace aurora {

constexpr bool is_power_of_two(usize value) {
    return value != 0 && (value & (value - 1)) == 0;
}

constexpr usize align_up(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

constexpr bool is_aligned(usize value, usize alignment) {
    return (value & (alignment - 1)) == 0;
}

inline bool is_aligned(const void* ptr, usize alignment) {
    return is_aligned(reinterpret_cast<uptr>(ptr), alignment);
}

} // namespace aurora
