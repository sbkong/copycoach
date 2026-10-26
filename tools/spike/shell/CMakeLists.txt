cmake_minimum_required(VERSION 3.28)
project(spike_shell CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(spike_shell spike_shell.cpp)
target_link_libraries(spike_shell PRIVATE Qt6::Widgets)
target_compile_options(spike_shell PRIVATE /W4 /utf-8 /EHsc)
