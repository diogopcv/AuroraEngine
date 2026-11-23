#pragma once

#include "core/types.h"

namespace aurora {

// Generational reference to an engine object. Generation 0 is invalid, so a
// default-constructed handle is never valid. T only tags the type and may be incomplete.
// Runtime-only: never persisted (scenes and assets use stable IDs).
template <typename T>
struct Handle {
    u32 index      = 0;
    u32 generation = 0;

    constexpr bool is_valid() const { return generation != 0; }

    friend constexpr bool operator==(Handle, Handle) = default;
};

} // namespace aurora
