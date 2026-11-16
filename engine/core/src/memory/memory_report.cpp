#include "core/memory/memory_report.h"

#include "core/log.h"
#include "core/memory/heap.h"
#include "core/memory/memory_tag.h"
#include "core/memory/virtual_buffer.h"

namespace aurora::memory {

LeakSummary report_leaks() {
    LeakSummary summary;
    for (usize i = 0; i < memory_tag_count; ++i) {
        const MemoryTag tag = static_cast<MemoryTag>(i);

        const heap::TagStats heap_stats = heap::stats(tag);
        summary.heap_blocks += heap_stats.live_blocks;
        summary.heap_bytes  += heap_stats.live_bytes;
        if (heap_stats.live_blocks > 0) {
            AURORA_LOG_WARN("memory: tag {} has {} live blocks, {} bytes", memory_tag_name(tag), heap_stats.live_blocks, heap_stats.live_bytes);
        }

        const usize committed = vm::committed_bytes(tag);
        if (committed == 0) {
            continue;
        }
        if (tag == MemoryTag::Scratch) {
            summary.scratch_committed_bytes += committed;
            AURORA_LOG_INFO("memory: thread scratch arenas hold {} bytes of committed virtual memory (not a leak)", committed);
        } else {
            summary.committed_bytes += committed;
            AURORA_LOG_WARN("memory: tag {} has {} bytes of committed virtual memory", memory_tag_name(tag), committed);
        }
    }

    if (summary.heap_blocks > 0) {
        heap::log_live_callsites();
    }
    return summary;
}

LeakSummary shutdown_report() {
    const LeakSummary summary = report_leaks();
    if (summary.clean()) {
        AURORA_LOG_INFO("memory: no leaks");
    } else {
        AURORA_LOG_WARN("memory: LEAKS at shutdown: {} heap blocks ({} bytes), {} bytes of committed virtual memory",
                        summary.heap_blocks, summary.heap_bytes, summary.committed_bytes);
    }
    return summary;
}

} // namespace aurora::memory
