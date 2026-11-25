#include "core/memory/virtual_memory.h"

#include "core/assert.h"
#include "core/memory/align.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

namespace aurora::vm {

namespace {

struct SystemInfo {
    usize page_size;
    usize allocation_granularity;
};

const SystemInfo& system_info() {
    static const SystemInfo info = [] {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        return SystemInfo{si.dwPageSize, si.dwAllocationGranularity};
    }();
    return info;
}

} // namespace

usize page_size() {
    return system_info().page_size;
}

usize allocation_granularity() {
    return system_info().allocation_granularity;
}

void* reserve(usize size) {
    const bool valid = size > 0 && is_aligned(size, allocation_granularity());
    AURORA_ASSERT(valid, "reserve needs a non-zero multiple of the allocation granularity, got {} bytes", size);
    if (!valid) {
        return nullptr;
    }
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool commit(void* ptr, usize size) {
    AURORA_ASSERT(ptr != nullptr && is_aligned(ptr, page_size()), "commit needs a page-aligned address, got {}", ptr);
    AURORA_ASSERT(is_aligned(size, page_size()), "commit needs a multiple of the page size, got {} bytes", size);
    if (ptr == nullptr || !is_aligned(ptr, page_size()) || !is_aligned(size, page_size())) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void decommit(void* ptr, usize size) {
    AURORA_ASSERT(ptr != nullptr && is_aligned(ptr, page_size()), "decommit needs a page-aligned address, got {}", ptr);
    AURORA_ASSERT(is_aligned(size, page_size()), "decommit needs a multiple of the page size, got {} bytes", size);
    if (ptr == nullptr || !is_aligned(ptr, page_size()) || !is_aligned(size, page_size()) || size == 0) {
        return;
    }
    const BOOL ok = VirtualFree(ptr, size, MEM_DECOMMIT);
    AURORA_ASSERT(ok != 0, "VirtualFree(MEM_DECOMMIT) failed: error {}", GetLastError());
}

void release(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    const BOOL ok = VirtualFree(ptr, 0, MEM_RELEASE);
    AURORA_ASSERT(ok != 0, "VirtualFree(MEM_RELEASE) failed: error {}", GetLastError());
}

} // namespace aurora::vm
