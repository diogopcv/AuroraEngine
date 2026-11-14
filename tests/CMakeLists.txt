include(FetchContent)

set(DOCTEST_WITH_TESTS               OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB  OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL               ON  CACHE BOOL "" FORCE)

FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(doctest)
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

add_library(test_main STATIC
    framework/src/main.cpp
    framework/include/test/aurora_test.h)
target_include_directories(test_main PUBLIC framework/include)
target_link_libraries(test_main PUBLIC aurora::core doctest::doctest)

target_compile_options(test_main PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/EHsc>)
engine_configure_target(test_main)
set_target_properties(test_main PROPERTIES FOLDER "tests")

# Creates <layer>_tests from test sources and registers each test case in CTest (D28).
# Test sources are compiled into the executable, never into a library: the linker drops
# unreferenced objects from static libraries, and with them the test registrations.
#
#   engine_add_tests(math
#       SOURCES vec_tests.cpp mat_tests.cpp
#       DEPS    aurora::math)
#
# test_main (and through it aurora::core) is always linked. TEST_SUITE names become CTest labels.
function(engine_add_tests layer)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    set(target ${layer}_tests)
    engine_add_executable(${target}
        SOURCES ${ARG_SOURCES}
        DEPS    test_main ${ARG_DEPS})
    set_target_properties(${target} PROPERTIES FOLDER "tests")
    doctest_discover_tests(${target} TEST_PREFIX "${target}." ADD_LABELS 1)
endfunction()

add_subdirectory(core)