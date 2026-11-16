#pragma once

#include "core/types.h"

namespace aurora {

// One tag per subsystem, added when the subsystem's first consumer exists.
enum class MemoryTag : u8 {
    General, // global new/delete, standard containers
    Core,
    Debug,
    Scratch, // per-thread scratch arenas: reported, never a leak
    Count
};

inline constexpr usize memory_tag_count = static_cast<usize>(MemoryTag::Count);

constexpr const char* memory_tag_name(MemoryTag tag) {
    switch (tag) {
        case MemoryTag::General: return "General";
        case MemoryTag::Core:    return "Core";
        case MemoryTag::Debug:   return "Debug";
        case MemoryTag::Scratch: return "Scratch";
        case MemoryTag::Count:   break;
    }
    return "?";
}

} // namespace aurora
