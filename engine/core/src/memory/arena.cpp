#include "core/memory/arena.h"

#include "core/assert.h"
#include "core/memory/align.h"

#include <algorithm>
#include <cstring>

namespace aurora {

Arena::Arena(usize reserve_bytes, MemoryTag tag) {
    if (!buffer_.reserve(reserve_bytes, tag)) {
        AURORA_FATAL("arena [{}] could not reserve {} bytes of address space", memory_tag_name(tag), reserve_bytes);
    }
}

void* Arena::push(usize size, usize alignment) {
    AURORA_ASSERT(is_power_of_two(alignment), "alignment {} is not a power of two", alignment);
    if (!is_power_of_two(alignment)) {
        alignment = default_alignment;
    }

    const uptr  base     = reinterpret_cast<uptr>(buffer_.data());
    const usize start    = align_up(base + position_, alignment) - base;
    const usize capacity = buffer_.capacity();
    if (start > capacity || size > capacity - start) {
        AURORA_FATAL("arena [{}] exhausted: capacity {} bytes, used {}, requested {} (alignment {})",
                     memory_tag_name(tag()), capacity, position_, size, alignment);
    }

    const usize end = start + size;
    if (end > buffer_.committed()) {
        const usize target = std::min(align_up(end, commit_chunk), capacity);
        if (!buffer_.ensure_committed(target)) {
            AURORA_FATAL("arena [{}] out of memory: could not commit {} bytes", memory_tag_name(tag()), target);
        }
    }

    position_   = end;
    high_water_ = std::max(high_water_, end);
    return buffer_.data() + start;
}

void Arena::rewind(ArenaMark mark) {
    AURORA_ASSERT(mark.position <= position_, "rewinding forward: mark {} is past position {}", mark.position, position_);
    if (mark.position > position_) {
        return;
    }
    if constexpr (AURORA_DEBUG) {
        std::memset(buffer_.data() + mark.position, 0xDD, position_ - mark.position);
    }
    position_ = mark.position;
}

ScratchScope scratch(const Arena* conflict) {
    thread_local Arena arenas[2] = {
        Arena(scratch_reserve_bytes, MemoryTag::Core),
        Arena(scratch_reserve_bytes, MemoryTag::Core),
    };
    return ScratchScope(&arenas[0] == conflict ? arenas[1] : arenas[0]);
}

} // namespace aurora
