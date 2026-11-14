engine_add_tests(core
    SOURCES
        log_tests.cpp
        assert_tests.cpp
        virtual_memory_tests.cpp
        framework_selftests.cpp)

add_test(NAME core_tests.selftest.exit_code
    COMMAND core_tests --no-skip --test-case=selftest*)
set_tests_properties(core_tests.selftest.exit_code PROPERTIES WILL_FAIL TRUE)

add_test(NAME core_tests.selftest.location
    COMMAND core_tests --no-skip --test-case=selftest*)
set_tests_properties(core_tests.selftest.location PROPERTIES
    PASS_REGULAR_EXPRESSION "framework_selftests\\.cpp[:(][0-9]+")