engine_add_executable(sandbox
    SOURCES 
        src/main.cpp
    DEPS
        aurora::core
)

if(ENGINE_BUILD_TESTS)
    # Shutdown memory report: a clean exit reports no leaks; --leak injects one.
    add_test(NAME sandbox.shutdown.clean COMMAND sandbox)
    set_tests_properties(sandbox.shutdown.clean PROPERTIES
        PASS_REGULAR_EXPRESSION "memory: no leaks"
        FAIL_REGULAR_EXPRESSION "LEAKS at shutdown")

    add_test(NAME sandbox.shutdown.leak COMMAND sandbox --leak)
    set_tests_properties(sandbox.shutdown.leak PROPERTIES
        PASS_REGULAR_EXPRESSION "memory: tag Debug has 1 live blocks")
endif()
