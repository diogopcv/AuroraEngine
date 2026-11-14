// Replacement global new/delete routed to the engine heap with MemoryTag::General.
// Compiled into the aurora::new_delete OBJECT library, which engine_add_executable links into
// every executable.

#include "core/assert.h"
#include "core/memory/heap.h"

#include <new>

// Match the SAL annotations of <vcruntime_new.h>, or MSVC reports inconsistent annotations.
#if defined(_MSC_VER) && defined(_Ret_notnull_) && defined(_Post_writable_byte_size_)
    #define AURORA_NEW_RESULT(n)         _Ret_notnull_ _Post_writable_byte_size_(n)
    #define AURORA_NEW_NOTHROW_RESULT(n) _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n)
#else
    #define AURORA_NEW_RESULT(n)
    #define AURORA_NEW_NOTHROW_RESULT(n)
#endif

namespace {

constexpr std::size_t default_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

void* allocate(std::size_t size, std::size_t alignment) noexcept {
    return aurora::heap::alloc(size, alignment, aurora::MemoryTag::General);
}

// Engine code has no exceptions: running out of memory in operator new is fatal.
void* allocate_or_die(std::size_t size, std::size_t alignment) noexcept {
    void* ptr = allocate(size, alignment);
    if (ptr == nullptr) [[unlikely]] {
        AURORA_FATAL("out of memory: operator new({} bytes)", size);
    }
    return ptr;
}

void deallocate(void* ptr) noexcept {
    aurora::heap::free(ptr, aurora::MemoryTag::General);
}

} // namespace

AURORA_NEW_RESULT(size) void* operator new(std::size_t size) { return allocate_or_die(size, default_alignment); }
AURORA_NEW_RESULT(size) void* operator new[](std::size_t size) { return allocate_or_die(size, default_alignment); }
AURORA_NEW_RESULT(size) void* operator new(std::size_t size, std::align_val_t alignment) { return allocate_or_die(size, static_cast<std::size_t>(alignment)); }
AURORA_NEW_RESULT(size) void* operator new[](std::size_t size, std::align_val_t alignment) { return allocate_or_die(size, static_cast<std::size_t>(alignment)); }

AURORA_NEW_NOTHROW_RESULT(size) void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size, default_alignment); }
AURORA_NEW_NOTHROW_RESULT(size) void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size, default_alignment); }
AURORA_NEW_NOTHROW_RESULT(size) void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocate(size, static_cast<std::size_t>(alignment)); }
AURORA_NEW_NOTHROW_RESULT(size) void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocate(size, static_cast<std::size_t>(alignment)); }

void operator delete(void* ptr) noexcept { deallocate(ptr); }
void operator delete[](void* ptr) noexcept { deallocate(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { deallocate(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { deallocate(ptr); }
void operator delete(void* ptr, std::align_val_t) noexcept { deallocate(ptr); }
void operator delete[](void* ptr, std::align_val_t) noexcept { deallocate(ptr); }
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept { deallocate(ptr); }
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept { deallocate(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { deallocate(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { deallocate(ptr); }
void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept { deallocate(ptr); }
void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept { deallocate(ptr); }
