#pragma once

#include "core/types.h"

// Operating-system virtual memory. Sizes must already be aligned: reserve takes a multiple of
// allocation_granularity(), commit and decommit a multiple of page_size(). VirtualBuffer does the rounding.
namespace aurora::vm {

usize page_size();              // commit unit (4 KB on x64 Windows)
usize allocation_granularity(); // reservation alignment and minimum size (64 KB on Windows)

void* reserve(usize size);

[[nodiscard]] bool commit(void* ptr, usize size);

void decommit(void* ptr, usize size);

void release(void* ptr);

} // namespace aurora::vm
