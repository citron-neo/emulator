# SPDX-FileCopyrightText: 2023 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(NX_TZDB_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")

add_library(nx_tzdb INTERFACE)

find_program(GIT git)
find_program(GNU_MAKE make)
find_program(DATE_PROG date)

set(CAN_BUILD_NX_TZDB true)

if (NOT GIT)
    set(CAN_BUILD_NX_TZDB false)
endif()
if (NOT GNU_MAKE)
    set(CAN_BUILD_NX_TZDB false)
endif()
if (NOT DATE_PROG)
    set(CAN_BUILD_NX_TZDB false)
endif()

if (ANDROID)
    set(CAN_BUILD_NX_TZDB false)
elseif (WIN32)
    # Native Windows builds and MSYS2 builds cannot run the generated host tools.
    # Linux cross-compiling to Windows also falls through here, so it keeps
    # using the downloaded archive instead of trying to execute a target binary.
    set(CAN_BUILD_NX_TZDB false)
endif()

set(NX_TZDB_VERSION "221202")
set(NX_TZDB_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/${NX_TZDB_VERSION}.zip")

set(NX_TZDB_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/nx_tzdb")

if ((NOT CAN_BUILD_NX_TZDB OR CITRON_DOWNLOAD_TIME_ZONE_DATA) AND NOT EXISTS ${NX_TZDB_ROMFS_DIR})
    set(NX_TZDB_DOWNLOAD_URL "https://github.com/lat9nq/tzdb_to_nx/releases/download/${NX_TZDB_VERSION}/${NX_TZDB_VERSION}.zip")

    message(STATUS "Downloading time zone data from ${NX_TZDB_DOWNLOAD_URL}...")
    file(DOWNLOAD ${NX_TZDB_DOWNLOAD_URL} ${NX_TZDB_ARCHIVE}
        STATUS NX_TZDB_DOWNLOAD_STATUS)
    list(GET NX_TZDB_DOWNLOAD_STATUS 0 NX_TZDB_DOWNLOAD_STATUS_CODE)
    if (NOT NX_TZDB_DOWNLOAD_STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Time zone data download failed (status code ${NX_TZDB_DOWNLOAD_STATUS_CODE})")
    endif()

    file(ARCHIVE_EXTRACT
        INPUT
            ${NX_TZDB_ARCHIVE}
        DESTINATION
            ${NX_TZDB_ROMFS_DIR})
elseif (CAN_BUILD_NX_TZDB AND NOT CITRON_DOWNLOAD_TIME_ZONE_DATA)
    if (DEFINED TZDB_TO_NX_SOURCE_DIR AND EXISTS "${TZDB_TO_NX_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${TZDB_TO_NX_SOURCE_DIR}" tzdb_to_nx)
    else()
        message(FATAL_ERROR "tzdb_to_nx source not found. Enable CITRON_USE_CPM so the source can be fetched.")
    endif()
    add_dependencies(nx_tzdb x80e)

    set(NX_TZDB_ROMFS_DIR "${NX_TZDB_DIR}")
endif()

target_include_directories(nx_tzdb
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include
    INTERFACE ${NX_TZDB_INCLUDE_DIR})

function(CreateHeader ZONE_PATH HEADER_NAME)
    set(HEADER_PATH "${NX_TZDB_INCLUDE_DIR}/nx_tzdb/${HEADER_NAME}.h")
    add_custom_command(
        OUTPUT
            ${NX_TZDB_INCLUDE_DIR}/nx_tzdb/${HEADER_NAME}.h
        COMMAND
            ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/NxTzdbCreateHeader.cmake
                ${ZONE_PATH}
                ${HEADER_NAME}
                ${NX_TZDB_INCLUDE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS
            tzdb_template.h.in
            NxTzdbCreateHeader.cmake)

    target_sources(nx_tzdb PRIVATE ${HEADER_PATH})
endfunction()

CreateHeader(${NX_TZDB_ROMFS_DIR} base)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo zoneinfo)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Africa africa)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/America america)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/America/Argentina america_argentina)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/America/Indiana america_indiana)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/America/Kentucky america_kentucky)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/America/North_Dakota america_north_dakota)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Antarctica antarctica)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Arctic arctic)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Asia asia)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Atlantic atlantic)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Australia australia)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Brazil brazil)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Canada canada)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Chile chile)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Etc etc)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Europe europe)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Indian indian)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Mexico mexico)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/Pacific pacific)
CreateHeader(${NX_TZDB_ROMFS_DIR}/zoneinfo/US us)
