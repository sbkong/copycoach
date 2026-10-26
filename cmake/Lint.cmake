# Formatting and static analysis targets (coding-standards.md section 13). Both run as ctest entries so the
# ci-release preset fails on drift. CC_SOURCES_FOR_LINT is filled by the subdirectories (cc_core, cc_tests).
find_program(CC_CLANG_FORMAT clang-format PATHS "C:/Program Files/LLVM/bin")
find_program(CC_CLANG_TIDY clang-tidy PATHS "C:/Program Files/LLVM/bin")

set(CC_SOURCES_FOR_LINT "" CACHE INTERNAL "sources checked by cc_format_check / cc_tidy")

function(cc_lint_sources)
    set(abs "")
    foreach(src IN LISTS ARGN)
        get_filename_component(path "${src}" ABSOLUTE)
        list(APPEND abs "${path}")
    endforeach()
    set(CC_SOURCES_FOR_LINT ${CC_SOURCES_FOR_LINT} ${abs} CACHE INTERNAL "sources checked by cc_format_check / cc_tidy")
endfunction()

function(cc_add_lint_targets)
    if(CC_CLANG_FORMAT)
        add_custom_target(cc_format_check
            COMMAND "${CC_CLANG_FORMAT}" --dry-run --Werror ${CC_SOURCES_FOR_LINT}
            COMMENT "clang-format --dry-run (coding-standards section 3)"
            VERBATIM)
        add_custom_target(cc_format
            COMMAND "${CC_CLANG_FORMAT}" -i ${CC_SOURCES_FOR_LINT}
            COMMENT "clang-format -i"
            VERBATIM)
        add_test(NAME lint.format COMMAND "${CC_CLANG_FORMAT}" --dry-run --Werror ${CC_SOURCES_FOR_LINT})
    else()
        message(WARNING "clang-format not found: cc_format_check and lint.format are unavailable")
    endif()
    if(CC_CLANG_TIDY)
        set(tidy_sources ${CC_SOURCES_FOR_LINT})
        list(FILTER tidy_sources INCLUDE REGEX "\\.cpp$")
        add_custom_target(cc_tidy
            COMMAND "${CC_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}" --quiet ${tidy_sources}
            COMMENT "clang-tidy (coding-standards section 4, 8, 16)"
            VERBATIM)
        add_test(NAME lint.tidy COMMAND "${CC_CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}" --quiet ${tidy_sources})
    else()
        message(WARNING "clang-tidy not found: cc_tidy and lint.tidy are unavailable")
    endif()
endfunction()
