cmake_minimum_required(VERSION 3.31)

project(CopyCoach VERSION 0.1.0 LANGUAGES CXX)

# Toolchain contract (docs/dev/coding-standards.md §4, §13): C++23, warnings are errors, UTF-8 sources.
set(CMAKE_CXX_STANDARD 23)          # CMake 3.31 + MSVC 14.51 emits -std:c++latest (PLAN 3-1: /std:c++23 once 14.52 ships)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF) # no C++ modules; scanning only adds a dyndep step and breaks localized MSVC header tracking
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MSVC)
    message(FATAL_ERROR "CopyCoach v1 targets MSVC (VS 2026 Build Tools); other compilers come with the portability stage.")
endif()

# One interface target carries the compile contract so every cc_* target gets the same flags.
add_library(cc_options INTERFACE)
target_compile_options(cc_options INTERFACE
    /W4 /WX /permissive- /utf-8 /EHsc /Zc:__cplusplus /bigobj
)
target_compile_definitions(cc_options INTERFACE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
)

option(CC_BUILD_TESTS "Build cc_tests (Catch2)" ON)

include(cmake/Generated.cmake)
include(cmake/Sounds.cmake)
include(cmake/Lint.cmake)

enable_testing()
add_subdirectory(core)

if(CC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

cc_add_lint_targets()
