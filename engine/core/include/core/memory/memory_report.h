#pragma once

#include "core/types.h"

// Reads the per-tag counters of every memory source and reports what is still live.
namespace aurora::memory {

struct LeakSummary {
    usize heap_blocks     = 0;
    usize heap_bytes      = 0;
    usize committed_bytes = 0; // virtual memory still committed, all tags
};

LeakSummary report_leaks();

} // namespace aurora::memory
