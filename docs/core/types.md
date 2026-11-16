# Types

`core/types.h` defines the vocabulary every file uses. It is deliberately small: only what almost every file needs.

```cpp
#include "core/types.h"

using namespace aurora;

u32   index = 0;
f32   speed = 2.5f;
usize bytes = 64_MB;
```

## Contents

| Name | Type | Notes |
|---|---|---|
| `u8`, `u16`, `u32`, `u64` | `std::uint8_t` … `std::uint64_t` | Exact-width unsigned integers |
| `i8`, `i16`, `i32`, `i64` | `std::int8_t` … `std::int64_t` | Exact-width signed integers |
| `f32`, `f64` | `float`, `double` | IEEE 754 is checked at compile time |
| `usize` | `std::size_t` | Sizes, counts and indices into memory |
| `uptr`, `iptr` | `std::uintptr_t`, `std::intptr_t` | Pointer arithmetic and address checks |
| `_KB`, `_MB`, `_GB` | `u64` literals | Binary units: `1_KB` is 1024 bytes |
| `AURORA_DEBUG` | macro, `0` or `1` | `1` in Debug builds, set by CMake for every engine target |

## Platform assumptions

Two `static_assert`s state what the engine relies on:

- **64-bit pointers.** Address-space reservations of gigabytes ([virtual memory](memory/virtual-memory.md)) and 64-bit sizes assume it.
- **IEEE 754 floats and doubles.** Math, serialization and GPU data assume it.

A build for a platform that breaks either assumption fails at compile time instead of misbehaving at runtime.

## Advice

- Use `usize` for sizes and indices into memory, `u32` for indices that are stored many times (handles, component indices) where 8 bytes would waste cache.
- Prefer the size literals to bare numbers: `reserve(256_MB)` reads better than `reserve(268435456)`.
- `AURORA_DEBUG` is for code that must differ between builds. For checks, use [assertions](assertions.md), which are already compiled out in Release.

## Why it stays small

Every file includes this header, directly or through `log.h` and `assert.h`. Anything added here costs compile time in every file and grows the vocabulary everyone has to learn. Macros such as `FORCE_INLINE`, `UNUSED` or platform flags were left out on purpose until something needs them.
