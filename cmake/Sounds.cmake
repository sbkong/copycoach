# Bundled sounds (docs/dev/audio-engine.md section 7). Nothing here is checked in:
#  - metronome sets are synthesized at build time by tools/gen_metronome.py into <build>/sounds/metronome/<id>/
#  - the soundfont MuseScore_General.sf3 (MIT, 38 MB) is downloaded once into build/_downloads/ (shared by all
#    presets) and copied to <build>/sounds/. CC_DOWNLOAD_SOUNDFONT=OFF skips it (synth tests then skip too).
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

option(CC_DOWNLOAD_SOUNDFONT "Download MuseScore_General.sf3 (MIT) into the build tree" ON)
set(CC_SOUNDFONT_NAME "MuseScore_General.sf3")
set(CC_SOUNDFONT_URL "https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/${CC_SOUNDFONT_NAME}")
set(CC_SOUNDFONT_SHA256 "5b85b6c2c61d10b2b91cddd41efcce7b25cd31c8271d511c73afafbef20b6fa3") # verified 2026-10-26, 39 900 972 bytes
set(CC_SOUNDFONT_LICENSE_URL "https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General_License.md")
set(CC_DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/build/_downloads")

if(CC_DOWNLOAD_SOUNDFONT)
    set(_sf3 "${CC_DOWNLOAD_DIR}/${CC_SOUNDFONT_NAME}")
    if(NOT EXISTS "${_sf3}")
        message(STATUS "Downloading ${CC_SOUNDFONT_NAME} (38 MB, MIT) ...")
        file(DOWNLOAD "${CC_SOUNDFONT_URL}" "${_sf3}" EXPECTED_HASH SHA256=${CC_SOUNDFONT_SHA256} STATUS _status)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE "${_sf3}")
            message(FATAL_ERROR "soundfont download failed: ${_status}. Set CC_DOWNLOAD_SOUNDFONT=OFF to build without it.")
        endif()
        file(DOWNLOAD "${CC_SOUNDFONT_LICENSE_URL}" "${CC_DOWNLOAD_DIR}/MuseScore_General_License.md")
    endif()
    file(COPY "${_sf3}" "${CC_DOWNLOAD_DIR}/MuseScore_General_License.md" DESTINATION "${CC_SOUND_DIR}")
    set(CC_HAVE_SOUNDFONT ON)
else()
    set(CC_HAVE_SOUNDFONT OFF)
endif()
