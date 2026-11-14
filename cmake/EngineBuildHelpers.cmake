# Precompiled header shared by every engine target (D36): only the standard headers that the
# core public headers (types.h, log.h, assert.h) include. Engine headers stay out, so each file
# still includes what it uses; build with ENGINE_USE_PCH=OFF to check that.
set(ENGINE_PCH_HEADERS
    <atomic>
    <cstddef>
    <cstdint>
    <format>
    <limits>
    <string_view>)

# Compilation flags common to all code (third-party libraries do not use this).
function(engine_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_definitions(${target} PUBLIC $<$<CONFIG:Debug>:AURORA_DEBUG=1>)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4             # high level warnings
            /permissive-    # strict standard conformance
            /utf-8          # source code and literals in UTF-8
            /Zc:preprocessor  # standard conforming preprocessors
            /Zc:__cplusplus   # __cplusplus with the real value
            /MP             # compile files in parallel
        )
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(ENGINE_USE_PCH)
        target_precompile_headers(${target} PRIVATE ${ENGINE_PCH_HEADERS})
    endif()
endfunction()

# Creates an engine layer as a static library.
#
#   engine_add_layer(platform
#       SOURCES src/a.cpp src/b.cpp
#       DEPS    engine::core)
#
# Expects the structure:
#   <layer>/include/<layer>/*.h public headers -> #include "<layer>/x.h"
#   <layer>/src/*.cpp implementation and private headers
function(engine_add_layer name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})

    add_library(${name} STATIC ${ARG_SOURCES})
    add_library(aurora::${name} ALIAS ${name})

    target_include_directories(${name}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(${name} PUBLIC ${ARG_DEPS})
    engine_configure_target(${name})
    set_target_properties(${name} PROPERTIES FOLDER "engine")    
endfunction()

# Creates an executable (sandbox, editor, tests) that uses the engine.
function(engine_add_executable name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS})
    engine_configure_target(${name})

    # In Visual Studio, it runs from the executable folder.
    set_target_properties(${name} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:${name}>)
endfunction()