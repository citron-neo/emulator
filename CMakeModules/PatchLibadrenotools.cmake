# SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Keep bundled and CPM sources on the same host integration. Reconfiguration
# must accept an already-applied patch, but must never silently ignore drift.
if (NOT DEFINED LIBADRENOTOOLS_SOURCE_DIR)
    message(FATAL_ERROR "LIBADRENOTOOLS_SOURCE_DIR is required")
endif()

find_package(Git REQUIRED)
set(_adrenotools_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/libadrenotools-runtime-page-size.patch")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${_adrenotools_patch}"
    WORKING_DIRECTORY "${LIBADRENOTOOLS_SOURCE_DIR}"
    RESULT_VARIABLE _can_apply
    OUTPUT_QUIET ERROR_QUIET
)
if (_can_apply EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${_adrenotools_patch}"
        WORKING_DIRECTORY "${LIBADRENOTOOLS_SOURCE_DIR}"
        RESULT_VARIABLE _apply_result
        ERROR_VARIABLE _apply_error
    )
    if (NOT _apply_result EQUAL 0)
        message(FATAL_ERROR "libadrenotools compatibility patch failed: ${_apply_error}")
    endif()
else()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_adrenotools_patch}"
        WORKING_DIRECTORY "${LIBADRENOTOOLS_SOURCE_DIR}"
        RESULT_VARIABLE _already_applied
        OUTPUT_QUIET ERROR_QUIET
    )
    if (NOT _already_applied EQUAL 0)
        message(FATAL_ERROR "libadrenotools sources do not match the compatibility patch")
    endif()
endif()
