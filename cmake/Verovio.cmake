# Verovio 6.3.0 (LGPL-2.1, submodule third_party/verovio) as a static library: cc_verovio.
# Upstream's BUILD_AS_LIBRARY makes a DLL without exports on MSVC (docs/dev/spikes.md (b)), so the sources are
# globbed here with the same feature set as the spike: no Humdrum / PAE / ABC / DARMS, no runtime clock.
# Resources (fonts, glyph tables) are read at run time from <build>/data/verovio (Toolkit::SetResourcePath),
# never from a compile-time path.
set(CC_VEROVIO_SOURCE "${CMAKE_SOURCE_DIR}/third_party/verovio")
if(NOT EXISTS "${CC_VEROVIO_SOURCE}/include/vrv/toolkit.h")
    message(FATAL_ERROR "third_party/verovio is empty: run `git submodule update --init third_party/verovio`")
endif()

file(GLOB _verovio_src
    "${CC_VEROVIO_SOURCE}/src/*.cpp"
    "${CC_VEROVIO_SOURCE}/libmei/dist/*.cpp"
    "${CC_VEROVIO_SOURCE}/libmei/addons/*.cpp"
    "${CC_VEROVIO_SOURCE}/src/midi/*.cpp"
    "${CC_VEROVIO_SOURCE}/src/crc/*.cpp")
add_library(cc_verovio STATIC
    ${_verovio_src}
    "${CC_VEROVIO_SOURCE}/src/json/jsonxx.cc"
    "${CC_VEROVIO_SOURCE}/src/pugi/pugixml.cpp")
# SYSTEM: toolkit.h pulls pugixml, Tunings.h and friends in, and they do not pass /W4.
target_include_directories(cc_verovio SYSTEM PUBLIC
    "${CC_VEROVIO_SOURCE}/include"
    "${CC_VEROVIO_SOURCE}/include/vrv"
    "${CC_VEROVIO_SOURCE}/libmei/dist"
    "${CC_VEROVIO_SOURCE}/libmei/addons"
    "${CC_VEROVIO_SOURCE}/include/crc"
    "${CC_VEROVIO_SOURCE}/include/midi"
    "${CC_VEROVIO_SOURCE}/include/hum"
    "${CC_VEROVIO_SOURCE}/include/json"
    "${CC_VEROVIO_SOURCE}/include/pugi"
    "${CC_VEROVIO_SOURCE}/include/tuning-library"
    "${CC_VEROVIO_SOURCE}/include/zip"
    "${CC_VEROVIO_SOURCE}/include/win32")
target_compile_definitions(cc_verovio PUBLIC
    NO_HUMDRUM_SUPPORT NO_PAE_SUPPORT NO_ABC_SUPPORT NO_DARMS_SUPPORT NO_RUNTIME USE_PAE_OLD_PARSER)
# NDEBUG in every configuration: Verovio's asserts are upstream development checks (the MusicXML importer
# trips assert(isTablature) on a <fret> under a notation staff and carries on fine in release), and an MSVC
# assert dialog would hang a test run. Our own code keeps its asserts.
target_compile_definitions(cc_verovio PRIVATE RESOURCE_DIR="data/verovio" NDEBUG)
# Verovio is C++20 and has UTF-8 literals (fails under code page 949 without /utf-8); warnings stay at /W2.
target_compile_options(cc_verovio PRIVATE /std:c++20 /bigobj /W2 /wd4244 /utf-8 /EHsc)
set_target_properties(cc_verovio PROPERTIES CXX_STANDARD 20 CXX_SCAN_FOR_MODULES OFF)

set(CC_VEROVIO_DATA_DIR "${CMAKE_BINARY_DIR}/data/verovio")
file(COPY "${CC_VEROVIO_SOURCE}/data/" DESTINATION "${CC_VEROVIO_DATA_DIR}")
file(COPY "${CC_VEROVIO_SOURCE}/COPYING.LESSER" DESTINATION "${CMAKE_BINARY_DIR}/licenses/verovio")
