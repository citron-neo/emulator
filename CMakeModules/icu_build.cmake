# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# ICU build script for Linux portability.
# Fetches ICU via CPM and builds it as shared libraries using Autotools.

if (NOT UNIX OR APPLE)
    return()
endif()

if (NOT DEFINED CITRON_ICU_REPO)
    set(CITRON_ICU_REPO "unicode-org/icu")
endif()
if (NOT DEFINED CITRON_ICU_TAG)
    set(CITRON_ICU_TAG "release-73-2")
endif()

CPMAddPackage(
    NAME icu_src
    GITHUB_REPOSITORY "${CITRON_ICU_REPO}"
    GIT_TAG "${CITRON_ICU_TAG}"
    DOWNLOAD_ONLY YES
)

if (icu_src_ADDED)
    set(ICU_PREFIX "${icu_src_SOURCE_DIR}/icu4c")
    string(FIND "${CMAKE_BINARY_DIR}" " " _space_pos)
    if(_space_pos GREATER -1)
        message(STATUS "[ICU] Binary dir has spaces — redirecting build/install to /tmp/citron-icu-${CMAKE_SYSTEM_NAME}")
        set(ICU_BUILD_DIR "/tmp/citron-icu-${CMAKE_SYSTEM_NAME}")
    else()
        set(ICU_BUILD_DIR "${CMAKE_BINARY_DIR}/externals/icu-build")
    endif()
    file(MAKE_DIRECTORY "${ICU_BUILD_DIR}")

    # Identify the shared libraries we expect to produce
    set(_icu_components data i18n io test tu uc)
    set(ICU_LIBRARIES)
    foreach(_comp ${_icu_components})
        list(APPEND ICU_LIBRARIES "${ICU_BUILD_DIR}/lib/libicu${_comp}.so.73")
    endforeach()

    execute_process(COMMAND nproc OUTPUT_VARIABLE SYSTEM_THREADS OUTPUT_STRIP_TRAILING_WHITESPACE)

    set(_icu_work_dir "${ICU_BUILD_DIR}/build")
    file(MAKE_DIRECTORY "${_icu_work_dir}")

    add_custom_command(
        OUTPUT "${_icu_work_dir}/Makefile"
        COMMAND "${ICU_PREFIX}/source/configure"
                --prefix="${ICU_BUILD_DIR}"
                --enable-shared
                --disable-static
                --disable-samples
                --disable-tests
        WORKING_DIRECTORY "${_icu_work_dir}"
        COMMENT "Configuring ICU via Autotools"
    )

    add_custom_command(
        OUTPUT ${ICU_LIBRARIES}
        COMMAND make -j${SYSTEM_THREADS}
        COMMAND make install
        DEPENDS "${_icu_work_dir}/Makefile"
        WORKING_DIRECTORY "${_icu_work_dir}"
        COMMENT "Building and installing ICU"
    )

    add_custom_target(icu-build ALL DEPENDS ${ICU_LIBRARIES})
    
    # Export for CopyCitronQt6Deps
    set(ICU_BINARY_DIR "${ICU_BUILD_DIR}/lib" CACHE INTERNAL "Location of CPM-built ICU libs")
endif()
