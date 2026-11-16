#pragma once

#include "core/types.h"

// Reads the per-tag counters of every memory source and reports what is still live.
namespace aurora::memory {

struct LeakSummary {
    usize heap_blocks             = 0;
    usize heap_bytes              = 0;
    usize committed_bytes         = 0; // virtual memory still committed, all tags except Scratch
    usize scratch_committed_bytes = 0; // per-thread scratch arenas: reported, never a leak

    bool clean() const { return heap_blocks == 0 && committed_bytes == 0; }
};

// Logs live heap memory and committed virtual memory per tag and, in Debug builds,
// live heap blocks grouped by callsite.
LeakSummary report_leaks();

// Call at the end of main, after every system has shut down and every thread has been joined.
// report_leaks(), then "memory: no leaks" or "memory: LEAKS at shutdown". Only logs and returns.
LeakSummary shutdown_report();

} // namespace aurora::memory
