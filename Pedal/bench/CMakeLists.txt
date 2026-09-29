# SWARMNESS pedal engine benchmark - builds without JUCE.
#
#   cmake -S Pedal/bench -B build-bench -DCMAKE_BUILD_TYPE=Release && cmake --build build-bench
#   ./build-bench/pedal_bench
#
# On a Raspberry Pi 4 add -DCMAKE_CXX_FLAGS="-mcpu=cortex-a72", on a Pi 5 "-mcpu=cortex-a76".
cmake_minimum_required(VERSION 3.16)
project(SwarmnessPedalBench CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(pedal_bench pedal_bench.cpp)
target_include_directories(pedal_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../Source)
target_compile_definitions(pedal_bench PRIVATE SWARM_NO_JUCE=1)
target_compile_options(pedal_bench PRIVATE -O2)
