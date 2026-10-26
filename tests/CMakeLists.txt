find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(Catch)

set(CC_TEST_SOURCES
    core/version_test.cpp
    core/theory/fixtures.h
    core/theory/pitch_test.cpp
    core/theory/shellvoicing_test.cpp
    core/theory/shellloop_table_test.cpp
    core/theory/scaleform_test.cpp
    generated_test.cpp
)
add_executable(cc_tests ${CC_TEST_SOURCES})
target_include_directories(cc_tests PRIVATE core/theory)
target_compile_definitions(cc_tests PRIVATE CC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(cc_tests PRIVATE cc_core cc_generated Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
catch_discover_tests(cc_tests)
cc_lint_sources(${CC_TEST_SOURCES})

# Registry and literal checks (docs/ui/naming.md section 6, checks 1, 3, 4; check 2 runs only when the docs are present).
add_test(NAME check.registry COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/check_registry.py")
add_test(NAME check.literals COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/check_literals.py")
