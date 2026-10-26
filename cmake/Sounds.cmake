# Bundled metronome sound sets (docs/dev/audio-engine.md section 7): synthesized at build time by
# tools/gen_metronome.py into <build>/sounds/metronome/<id>/{accent,beat,sub}.wav. Build output, never in the tree.
set(CC_SOUND_DIR "${CMAKE_BINARY_DIR}/sounds")
set(CC_METRONOME_SETS click woodblock rim beep)
set(CC_METRONOME_OUT "")
foreach(set_id IN LISTS CC_METRONOME_SETS)
    foreach(role accent beat sub)
        list(APPEND CC_METRONOME_OUT "${CC_SOUND_DIR}/metronome/${set_id}/${role}.wav")
    endforeach()
endforeach()

add_custom_command(
    OUTPUT ${CC_METRONOME_OUT}
    COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/gen_metronome.py" --out "${CC_SOUND_DIR}/metronome"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_metronome.py"
    COMMENT "gen_metronome.py -> sounds/metronome/{click,woodblock,rim,beep}"
    VERBATIM
)
add_custom_target(cc_sounds DEPENDS ${CC_METRONOME_OUT})
