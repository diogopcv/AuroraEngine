#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#if !defined(AURORA_DEBUG)
    #define AURORA_DEBUG 0
#endif

namespace aurora {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;
using uptr = std::uintptr_t;
using iptr = std::intptr_t;

constexpr u64 operator""_KB(unsigned long long v) { return v * 1024ull; }
constexpr u64 operator""_MB(unsigned long long v) { return v * 1024_KB; }
constexpr u64 operator""_GB(unsigned long long v) { return v * 1024_MB; }

static_assert(sizeof(void*) == 8, "Aurora requires a 64-bit target");
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559, "Aurora requires IEEE 754 floating point");

} // namespace aurora