find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(cc_tests
    core/version_test.cpp
    generated_test.cpp
)
target_link_libraries(cc_tests PRIVATE cc_core cc_generated Catch2::Catch2WithMain)
catch_discover_tests(cc_tests)

# Registry and literal checks (docs/ui/naming.md section 6, checks 1, 3, 4; check 2 runs only when the docs are present).
add_test(NAME check.registry COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/check_registry.py")
add_test(NAME check.literals COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/check_literals.py")
