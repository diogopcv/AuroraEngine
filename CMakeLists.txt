cmake_minimum_required(VERSION 3.25)

project(aurora
    VERSION 0.1.0
    DESCRIPTION "3D Game Engine for learning: C++20, Win32, DirectX 12"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(ENGINE_WARNINGS_AS_ERRORS "Treats warnings as errors" ON)
option(ENGINE_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)

include(cmake/EngineBuildHelpers.cmake)

add_subdirectory(engine)
add_subdirectory(apps/sandbox)

if(ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
