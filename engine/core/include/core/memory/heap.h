#pragma once

#include "core/memory/memory_tag.h"
#include "core/types.h"

// General-purpose heap over mimalloc, with per-tag statistics.
namespace aurora::heap {

struct TagStats {
    usize live_bytes        = 0;
    usize live_blocks       = 0;
    u64   total_allocations = 0;
};

// Returns nullptr when out of memory. alignment must be a power of two.
// file and line are recorded in Debug builds; use AURORA_ALLOC to pass them.
[[nodiscard]] void* alloc(usize size, usize alignment, MemoryTag tag, const char* file = nullptr, i32 line = 0);

// ptr must come from alloc with the same tag (asserted in Debug). nullptr is ignored.
void free(void* ptr, MemoryTag tag);

TagStats stats(MemoryTag tag);

// Debug builds: logs live blocks grouped by callsite. No-op otherwise.
// The full report, including other memory sources, is memory::report_leaks (memory_report.h).
void log_live_callsites();

} // namespace aurora::heap

#define AURORA_ALLOC(size, alignment, tag) ::aurora::heap::alloc((size), (alignment), (tag), __FILE__, __LINE__)
