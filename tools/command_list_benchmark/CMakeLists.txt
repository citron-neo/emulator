# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.22)
project(command_list_benchmark LANGUAGES CXX)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Supply an existing Boost 1.87 source tree; no emulator or Android build is needed.
set(BOOST_SOURCE_DIR "" CACHE PATH "Boost source tree (modular or installed headers)")
if(NOT IS_DIRECTORY "${BOOST_SOURCE_DIR}")
    message(FATAL_ERROR "Set BOOST_SOURCE_DIR to a Boost 1.87 header/source tree")
endif()
file(GLOB boost_includes "${BOOST_SOURCE_DIR}/libs/*/include")
list(APPEND boost_includes "${BOOST_SOURCE_DIR}" "${BOOST_SOURCE_DIR}/include")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/builders.h")
add_custom_command(OUTPUT "${generated}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" "${generated}"
    DEPENDS generate.py ../../src/video_core/dma_pusher.h
        ../../src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp)
add_executable(command_list_benchmark benchmark.cpp "${generated}")
target_compile_features(command_list_benchmark PRIVATE cxx_std_20)
target_include_directories(command_list_benchmark PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../src" ${boost_includes})
find_package(Threads REQUIRED)
target_link_libraries(command_list_benchmark PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(command_list_benchmark PRIVATE /W4)
else()
    target_compile_options(command_list_benchmark PRIVATE -Wall -Wextra)
endif()
