# copycoach: the executable. Bundled data (data/, sounds/) is copied next to it so AppPaths finds it.
qt_add_executable(copycoach WIN32 main.cpp)
target_link_libraries(copycoach PRIVATE cc_ui $<TARGET_OBJECTS:cc_allochook>)
cc_lint_sources(main.cpp)

add_custom_command(TARGET copycoach POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:copycoach>/data"
    COMMENT "copy data/ next to copycoach.exe"
    VERBATIM)
add_custom_command(TARGET copycoach POST_BUILD
    COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler "$<TARGET_FILE:copycoach>"
    COMMENT "windeployqt copycoach (Qt DLLs and the platform plugin next to the exe)"
    VERBATIM)
add_dependencies(copycoach cc_sounds)
