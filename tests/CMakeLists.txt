find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
include(Catch)

set(CC_TEST_SOURCES
    core/version_test.cpp
    core/theory/fixtures.h
    core/theory/pitch_test.cpp
    core/theory/shellvoicing_test.cpp
    core/theory/secondarydominant_test.cpp
    core/theory/chord_test.cpp
    core/theory/shellloop_table_test.cpp
    core/theory/scaleform_test.cpp
    core/theory/triadcycle_test.cpp
    core/theory/weekdayplan_test.cpp
    core/theory/chordsymbol_test.cpp
    core/model/model_test.cpp
    core/routine/sequencer_test.cpp
    core/routine/routinefiles_test.cpp
    core/routine/timer_test.cpp
    core/stats/sqlitestore_test.cpp
    core/project/project_test.cpp
    core/media/media_test.cpp
    core/audio/testsynth.h
    core/audio/rt_test.cpp
    core/audio/backend_test.cpp
    core/audio/engine_test.cpp
    core/audio/renderworker_test.cpp
    core/audio/soundfiles_test.cpp
    core/audio/fluidsynth_test.cpp
    generated_test.cpp
)
add_executable(cc_tests ${CC_TEST_SOURCES} $<TARGET_OBJECTS:cc_allochook>)
target_include_directories(cc_tests PRIVATE core/theory)
target_compile_definitions(cc_tests PRIVATE
    CC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CC_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    CC_SOUND_DIR="${CC_SOUND_DIR}"
)
target_link_libraries(cc_tests PRIVATE cc_core cc_generated Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
add_dependencies(cc_tests cc_sounds)
catch_discover_tests(cc_tests)
cc_lint_sources(${CC_TEST_SOURCES})

# UI tests: need Qt on PATH and the offscreen platform (no display in CI or over RDP).
if(CC_BUILD_APP)
    set(CC_UI_TEST_SOURCES
        ui/main.cpp
        ui/theme_test.cpp
        ui/settings_test.cpp
        ui/actionregistry_test.cpp
        ui/routinesession_test.cpp
        ui/routinerecorder_test.cpp
        ui/shell_test.cpp
        ui/prefs_test.cpp
        ui/statspanel_test.cpp
        ui/e2e_routine_test.cpp
        ui/palette_test.cpp
        ui/keymap_test.cpp
        ui/audioservice_test.cpp
    )
    qt_add_executable(cc_ui_tests ${CC_UI_TEST_SOURCES} $<TARGET_OBJECTS:cc_allochook>)
    target_compile_definitions(cc_ui_tests PRIVATE
        CC_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
        CC_SOUND_DIR="${CC_SOUND_DIR}"
    )
    target_link_libraries(cc_ui_tests PRIVATE cc_ui Catch2::Catch2 Qt6::Test)
    add_dependencies(cc_ui_tests cc_sounds)
    # Qt DLLs next to the executable (test discovery runs it at build time, before any ctest environment applies).
    add_custom_command(TARGET cc_ui_tests POST_BUILD
        COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --include-plugins qoffscreen "$<TARGET_FILE:cc_ui_tests>"
        COMMENT "windeployqt cc_ui_tests"
        VERBATIM)
    catch_discover_tests(cc_ui_tests
        TEST_PREFIX "ui."
        PROPERTIES ENVIRONMENT_MODIFICATION "QT_QPA_PLATFORM=set:offscreen")
    cc_lint_sources(${CC_UI_TEST_SOURCES})
endif()

# Registry and literal checks (docs/ui/naming.md section 6, checks 1, 3, 4; check 2 runs only when the docs are present).
add_test(NAME check.registry COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/check_registry.py")
add_test(NAME check.literals COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/check_literals.py")
