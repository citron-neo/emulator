# SPDX-License-Identifier: GPL-2.0-or-later
# Configure-only checks: no network, compiler or source submodules required.
cmake_minimum_required(VERSION 3.22)
if (NOT CHECK_BINARY_DIR)
    message(FATAL_ERROR "CHECK_BINARY_DIR is required")
endif()
get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(READ "${_repo}/externals/CMakeLists.txt" _externals)
string(REGEX MATCH "# Opus\n(.*)# FFMpeg" _match "${_externals}")
set(_opus_block "${CMAKE_MATCH_1}")
string(REGEX MATCH "# Sirit\n(.*)# libusb" _match "${_externals}")
set(_sirit_block "${CMAKE_MATCH_1}")
if (NOT _opus_block OR NOT _sirit_block)
    message(FATAL_ERROR "Dependency fallback blocks could not be located")
endif()

function(check_case name body expected_error)
    set(source "${CHECK_BINARY_DIR}/${name}")
    file(MAKE_DIRECTORY "${source}")
    file(WRITE "${source}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.22)\nproject(ProviderResolution NONE)\n${body}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${source}/build"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if (expected_error)
        if (result EQUAL 0 OR NOT "${output}\n${error}" MATCHES "${expected_error}")
            message(FATAL_ERROR "${name}: expected ${expected_error}\n${output}\n${error}")
        endif()
    elseif (NOT result EQUAL 0)
        message(FATAL_ERROR "${name}: ${output}\n${error}")
    endif()
    message(STATUS "Provider resolution: ${name} passed")
endfunction()

check_case(opus-canonical "add_library(Opus::opus INTERFACE IMPORTED GLOBAL)\n${_opus_block}" "")
check_case(opus-raw "add_library(opus INTERFACE)\n${_opus_block}" "")
file(MAKE_DIRECTORY "${CHECK_BINARY_DIR}/opus-bundled/opus")
file(WRITE "${CHECK_BINARY_DIR}/opus-bundled/opus/CMakeLists.txt" "add_library(opus INTERFACE)\n")
check_case(opus-bundled "${_opus_block}\nif(NOT TARGET opus)\nmessage(FATAL_ERROR \"Fallback not added\")\nendif()" "")
check_case(opus-missing "${_opus_block}" "Opus provider target is missing")
check_case(sirit-missing-headers "${_sirit_block}" "Sirit requires the project SPIRV-Headers target")
check_case(sirit-canonical "add_library(sirit::sirit INTERFACE IMPORTED GLOBAL)\n${_sirit_block}" "")

# A config-only provider must succeed without pkg-config being installed.
file(MAKE_DIRECTORY "${CHECK_BINARY_DIR}/opus-config/config" "${CHECK_BINARY_DIR}/opus-config/modules")
file(WRITE "${CHECK_BINARY_DIR}/opus-config/config/OpusConfig.cmake"
    "add_library(Opus::opus INTERFACE IMPORTED GLOBAL)\n")
file(WRITE "${CHECK_BINARY_DIR}/opus-config/modules/FindPkgConfig.cmake"
    "message(FATAL_ERROR \"Config provider incorrectly searched pkg-config\")\n")
check_case(opus-config "list(APPEND CMAKE_MODULE_PATH \"\${CMAKE_CURRENT_SOURCE_DIR}/modules\" \"${_repo}/CMakeModules\")\nset(Opus_DIR \"\${CMAKE_CURRENT_SOURCE_DIR}/config\")\nfind_package(Opus REQUIRED MODULE)" "")
file(MAKE_DIRECTORY "${CHECK_BINARY_DIR}/opus-no-pkgconfig/modules")
file(WRITE "${CHECK_BINARY_DIR}/opus-no-pkgconfig/modules/FindPkgConfig.cmake" "set(PkgConfig_FOUND FALSE)\n")
check_case(opus-no-pkgconfig "list(APPEND CMAKE_MODULE_PATH \"\${CMAKE_CURRENT_SOURCE_DIR}/modules\" \"${_repo}/CMakeModules\")\nfind_package(Opus QUIET MODULE)\nif(Opus_FOUND)\nmessage(FATAL_ERROR \"Missing provider was reported found\")\nendif()" "")
