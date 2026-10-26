cmake_minimum_required(VERSION 3.28)
project(spike_audio CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(portaudio CONFIG REQUIRED)

add_executable(spike_audio spike_audio.cpp)
target_link_libraries(spike_audio PRIVATE portaudio)
target_compile_options(spike_audio PRIVATE /W4 /utf-8)
