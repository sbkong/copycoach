cmake_minimum_required(VERSION 3.28)
project(spike_verovio CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# VEROVIO_SOURCE: checkout of rism-digital/verovio (tag version-6.3.0).
# Verovio is compiled here as a static library. Upstream's BUILD_AS_LIBRARY path builds a DLL
# without any exports on MSVC (it sets CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS as a preprocessor define,
# not as a CMake variable), so no import library comes out of it.
if(NOT VEROVIO_SOURCE)
    message(FATAL_ERROR "set -DVEROVIO_SOURCE=<verovio checkout>")
endif()
file(TO_CMAKE_PATH "${VEROVIO_SOURCE}" VEROVIO_SOURCE) # backslashes would break the glob results below

file(GLOB verovio_SRC
    "${VEROVIO_SOURCE}/src/*.cpp"
    "${VEROVIO_SOURCE}/libmei/dist/*.cpp"
    "${VEROVIO_SOURCE}/libmei/addons/*.cpp"
    "${VEROVIO_SOURCE}/src/midi/*.cpp"
    "${VEROVIO_SOURCE}/src/crc/*.cpp")
add_library(verovio STATIC
    ${verovio_SRC}
    "${VEROVIO_SOURCE}/src/json/jsonxx.cc"
    "${VEROVIO_SOURCE}/src/pugi/pugixml.cpp")
# All PUBLIC: toolkit.h transitively includes pugixml.hpp, Tunings.h, etc. from these dirs.
target_include_directories(verovio PUBLIC
    "${VEROVIO_SOURCE}/include"
    "${VEROVIO_SOURCE}/include/vrv"
    "${VEROVIO_SOURCE}/libmei/dist"
    "${VEROVIO_SOURCE}/libmei/addons"
    "${VEROVIO_SOURCE}/include/crc"
    "${VEROVIO_SOURCE}/include/midi"
    "${VEROVIO_SOURCE}/include/hum"
    "${VEROVIO_SOURCE}/include/json"
    "${VEROVIO_SOURCE}/include/pugi"
    "${VEROVIO_SOURCE}/include/tuning-library"
    "${VEROVIO_SOURCE}/include/zip"
    "${VEROVIO_SOURCE}/include/win32")
# Same feature set as the CLI build used for the coverage test: no Humdrum/PAE/ABC/DARMS.
target_compile_definitions(verovio PRIVATE
    NO_HUMDRUM_SUPPORT NO_PAE_SUPPORT NO_ABC_SUPPORT NO_DARMS_SUPPORT NO_RUNTIME USE_PAE_OLD_PARSER
    RESOURCE_DIR="${VEROVIO_SOURCE}/data")
# /utf-8: Verovio sources contain UTF-8 literals and fail under code page 949 without it.
target_compile_options(verovio PRIVATE /std:c++20 /bigobj /W2 /wd4244 /utf-8 /EHsc)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg SvgWidgets)

add_executable(spike_verovio spike_verovio.cpp)
target_link_libraries(spike_verovio PRIVATE verovio Qt6::Widgets Qt6::Svg Qt6::SvgWidgets)
target_compile_options(spike_verovio PRIVATE /W4 /utf-8 /EHsc)

# Spike (e): Verovio DeviceContext backed by QPainter, compared against the SVG path above.
add_executable(spike_qtdc spike_qtdc.cpp qt_device_context.cpp qt_device_context.h)
target_link_libraries(spike_qtdc PRIVATE verovio Qt6::Gui Qt6::Svg)
target_compile_options(spike_qtdc PRIVATE /W4 /utf-8 /EHsc /wd4100)
