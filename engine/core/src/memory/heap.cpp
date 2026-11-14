#include "core/memory/heap.h"

#include "core/assert.h"
#include "core/log.h"
#include "core/memory/align.h"

#include <mimalloc.h>

#include <atomic>
#include <cstring>

#if !defined(AURORA_HEAP_CALLSITES)
    #define AURORA_HEAP_CALLSITES AURORA_DEBUG
#endif

#if AURORA_HEAP_CALLSITES
    #include <algorithm>
    #include <mutex>
    #include <new>
    #include <unordered_map>
    #include <vector>
#endif

namespace aurora::heap {

namespace {

// mi_malloc returns blocks aligned to 16 bytes on 64-bit targets.
constexpr usize natural_alignment = 16;

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4324) // padded on purpose: one cache line per tag
#endif
struct alignas(64) TagCounters {
    std::atomic<usize> live_bytes{0};
    std::atomic<usize> live_blocks{0};
    std::atomic<u64>   total_allocations{0};
};
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

constinit TagCounters g_counters[memory_tag_count];

TagCounters& counters(MemoryTag tag) {
    return g_counters[static_cast<usize>(tag)];
}

#if AURORA_HEAP_CALLSITES

struct Callsite {
    const char* file;
    i32         line;
    usize       bytes;
    MemoryTag   tag;
};

template <typename T>
using MiAllocator = mi_stl_allocator<T>;

using CallsiteMap = std::unordered_map<const void*, Callsite, std::hash<const void*>, std::equal_to<const void*>,
                                       MiAllocator<std::pair<const void* const, Callsite>>>;

struct Registry {
    std::mutex  mutex;
    CallsiteMap blocks;
};

Registry& registry() {
    // Never destroyed: static destructors may free blocks after main returns.
    static Registry* instance = new (mi_malloc(sizeof(Registry))) Registry();
    return *instance;
}

void record(const void* ptr, const Callsite& callsite) {
    Registry& r = registry();
    const std::lock_guard lock(r.mutex);
    r.blocks.insert_or_assign(ptr, callsite);
}

// Returns false if ptr was not returned by alloc. Corrects `tag` to the recorded one.
// Asserts only after unlocking: a log sink may allocate, which locks the registry again.
bool forget(const void* ptr, MemoryTag& tag) {
    MemoryTag recorded = tag;
    {
        Registry& r = registry();
        const std::lock_guard lock(r.mutex);
        const auto it = r.blocks.find(ptr);
        if (it == r.blocks.end()) {
            return false;
        }
        recorded = it->second.tag;
        r.blocks.erase(it);
    }
    AURORA_ASSERT(recorded == tag, "block allocated with tag {} freed with tag {}", memory_tag_name(recorded), memory_tag_name(tag));
    tag = recorded;
    return true;
}

void report_callsites() {
    std::vector<Callsite, MiAllocator<Callsite>> live;
    {
        Registry& r = registry();
        const std::lock_guard lock(r.mutex);
        live.reserve(r.blocks.size());
        for (const auto& entry : r.blocks) {
            live.push_back(entry.second);
        }
    }

    const auto file_of = [](const Callsite& c) { return c.file != nullptr ? c.file : ""; };
    std::sort(live.begin(), live.end(), [&](const Callsite& a, const Callsite& b) {
        if (const int order = std::strcmp(file_of(a), file_of(b)); order != 0) return order < 0;
        if (a.line != b.line) return a.line < b.line;
        return a.tag < b.tag;
    });

    for (usize i = 0; i < live.size();) {
        const Callsite& first = live[i];
        usize blocks = 0;
        usize bytes  = 0;
        for (; i < live.size() && live[i].line == first.line && live[i].tag == first.tag &&
               std::strcmp(file_of(live[i]), file_of(first)) == 0;
             ++i) {
            ++blocks;
            bytes += live[i].bytes;
        }
        const char* file = first.file != nullptr ? first.file : "<operator new>";
        AURORA_LOG_WARN("{}({}): {} blocks, {} bytes [{}]", file, first.line, blocks, bytes, memory_tag_name(first.tag));
    }
}

#endif // AURORA_HEAP_CALLSITES

} // namespace

void* alloc(usize size, usize alignment, MemoryTag tag, [[maybe_unused]] const char* file, [[maybe_unused]] i32 line) {
    AURORA_ASSERT(is_power_of_two(alignment), "alignment {} is not a power of two", alignment);
    AURORA_ASSERT(tag < MemoryTag::Count);
    if (!is_power_of_two(alignment) || tag >= MemoryTag::Count) {
        return nullptr;
    }

    void* ptr = (alignment <= natural_alignment) ? mi_malloc(size) : mi_malloc_aligned(size, alignment);
    if (ptr == nullptr) {
        return nullptr;
    }

    const usize bytes = mi_usable_size(ptr);
    TagCounters& c = counters(tag);
    c.live_bytes.fetch_add(bytes, std::memory_order_relaxed);
    c.live_blocks.fetch_add(1, std::memory_order_relaxed);
    c.total_allocations.fetch_add(1, std::memory_order_relaxed);

#if AURORA_HEAP_CALLSITES
    record(ptr, Callsite{file, line, bytes, tag});
#endif
    return ptr;
}

void free(void* ptr, MemoryTag tag) {
    if (ptr == nullptr) {
        return;
    }

#if AURORA_HEAP_CALLSITES
    const bool known = forget(ptr, tag);
    AURORA_ASSERT(known, "freeing {} which heap::alloc did not return", ptr);
    if (!known) {
        return;
    }
#endif

    const usize bytes = mi_usable_size(ptr);
    TagCounters& c = counters(tag);
    c.live_bytes.fetch_sub(bytes, std::memory_order_relaxed);
    c.live_blocks.fetch_sub(1, std::memory_order_relaxed);

#if AURORA_DEBUG
    std::memset(ptr, 0xDD, bytes);
#endif
    mi_free(ptr);
}

TagStats stats(MemoryTag tag) {
    AURORA_ASSERT(tag < MemoryTag::Count);
    if (tag >= MemoryTag::Count) {
        return {};
    }
    const TagCounters& c = counters(tag);
    return TagStats{
        c.live_bytes.load(std::memory_order_relaxed),
        c.live_blocks.load(std::memory_order_relaxed),
        c.total_allocations.load(std::memory_order_relaxed),
    };
}

usize report_leaks() {
    usize total_blocks = 0;
    for (usize i = 0; i < memory_tag_count; ++i) {
        const MemoryTag tag = static_cast<MemoryTag>(i);
        const TagStats s = stats(tag);
        total_blocks += s.live_blocks;
        if (s.live_blocks > 0) {
            AURORA_LOG_WARN("memory: tag {} has {} live blocks, {} bytes", memory_tag_name(tag), s.live_blocks, s.live_bytes);
        }
    }

#if AURORA_HEAP_CALLSITES
    if (total_blocks > 0) {
        report_callsites();
    }
#endif
    return total_blocks;
}

} // namespace aurora::heap
