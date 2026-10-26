find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(cc_tests
    core/version_test.cpp
)
target_link_libraries(cc_tests PRIVATE cc_core Catch2::Catch2WithMain)
catch_discover_tests(cc_tests)
