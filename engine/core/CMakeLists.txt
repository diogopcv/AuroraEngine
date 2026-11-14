include(FetchContent)

set(MI_OVERRIDE     OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON  CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(MI_DEBUG        OFF CACHE STRING "" FORCE)

FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v3.6.1
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(mimalloc)
set_target_properties(mimalloc-static PROPERTIES FOLDER "third_party")
target_compile_definitions(mimalloc-static PRIVATE $<$<CONFIG:Debug>:MI_DEBUG=2>)

engine_add_layer(core
    SOURCES
        src/log.cpp
        src/assert.cpp
        src/memory/heap.cpp
        src/memory/virtual_buffer.cpp
        src/win32/virtual_memory.cpp
        include/core/types.h
        include/core/log.h
        include/core/assert.h
        include/core/memory/align.h
        include/core/memory/heap.h
        include/core/memory/memory_tag.h
        include/core/memory/virtual_buffer.h
        include/core/memory/virtual_memory.h
)
target_link_libraries(core PRIVATE mimalloc-static)

# Global new/delete. An OBJECT library, so its object always reaches the link of every
# executable created with engine_add_executable.
add_library(core_new_delete OBJECT src/memory/new_delete.cpp)
add_library(aurora::new_delete ALIAS core_new_delete)
target_link_libraries(core_new_delete PRIVATE aurora::core)
engine_configure_target(core_new_delete)
set_target_properties(core_new_delete PROPERTIES FOLDER "engine")
