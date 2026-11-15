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
        summary.committed_bytes += committed;
        if (committed > 0) {
            AURORA_LOG_WARN("memory: tag {} has {} bytes of committed virtual memory", memory_tag_name(tag), committed);
        }
    }

    if (summary.heap_blocks > 0) {
        heap::log_live_callsites();
    }
    return summary;
}

} // namespace aurora::memory
