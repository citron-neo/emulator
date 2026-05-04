# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# CMakeModules/qt_download.cmake — Download Qt pre-built binaries via aqt
#
# Called from CMakeModules/dependencies.cmake when CITRON_USE_CPM=ON and ENABLE_QT=ON.
# Uses aqt (pip install aqtinstall) to fetch the correct Qt variant for the target
# platform.
#
# Target variants:
#   Windows (native or cross-compile target)  →  win64_llvm_mingw
#   Linux native                              →  linux_gcc_64
#
# Cross-compilation (Linux host → Windows target):
#   QT_HOST_PATH is set to a Linux Qt install so moc/rcc/uic run on the host.
#   For native builds QT_HOST_PATH must NOT be set (it would trigger cross-compile mode).
#
# Prerequisites: Python3 + aqt must be installed (build script's ensure_aqt() handles this).

set(CITRON_QT_VERSION  "6.9.3" CACHE STRING "Qt version to download via aqt")
set(CITRON_QT_BASE_DIR "${CMAKE_BINARY_DIR}/externals/qt-cpm" CACHE PATH
    "Base directory for aqt-managed Qt downloads")

# ── Find aqt ──────────────────────────────────────────────────────────────────
find_program(_AQT_EXECUTABLE NAMES aqt
    HINTS "$ENV{HOME}/.local/bin" "${CITRON_QT_BASE_DIR}")

if (NOT _AQT_EXECUTABLE)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if (Python3_FOUND)
        set(_AQT_EXECUTABLE "${Python3_EXECUTABLE}" "-m" "aqt")
    else()
        message(WARNING
            "[Qt] aqt not found and Python3 not available — Qt download skipped.\n"
            "     Pass -DQt6_DIR=... manually or run the build script first.")
        return()
    endif()
endif()

# ── Determine target platform ──────────────────────────────────────────────────
# WIN32 is TRUE both for native MSYS2 builds and for Linux→Windows cross-compile
# because the CMAKE_SYSTEM_NAME is Windows in both cases.
if (WIN32)
    set(_QT_OS        "windows")
    set(_QT_TARGET    "desktop")
    set(_QT_ARCH      "win64_llvm_mingw")
    set(_QT_CMAKE_SUB "lib/cmake/Qt6")
else()
    # Native Linux (or macOS — extend here if needed)
    set(_QT_OS        "linux")
    set(_QT_TARGET    "desktop")
    set(_QT_ARCH      "linux_gcc_64")
    set(_QT_CMAKE_SUB "lib/cmake/Qt6")
endif()

# ── Download target Qt ────────────────────────────────────────────────────────
if (Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    message(STATUS "[Qt] Using target Qt from Qt6_DIR: ${Qt6_DIR}")
else()
    set(_QT_TARGET_DIR   "${CITRON_QT_BASE_DIR}/${CITRON_QT_VERSION}/${_QT_ARCH}")
    set(_QT_TARGET_CMAKE "${_QT_TARGET_DIR}/${_QT_CMAKE_SUB}/Qt6Config.cmake")

    if (NOT EXISTS "${_QT_TARGET_CMAKE}")
        message(STATUS "[Qt] Downloading Qt ${CITRON_QT_VERSION} ${_QT_ARCH} via aqt...")
        file(MAKE_DIRECTORY "${CITRON_QT_BASE_DIR}")

        execute_process(
            COMMAND ${_AQT_EXECUTABLE} install-qt
                    ${_QT_OS} ${_QT_TARGET}
                    ${CITRON_QT_VERSION} ${_QT_ARCH}
                    --outputdir "${CITRON_QT_BASE_DIR}"
            RESULT_VARIABLE _qt_result
            OUTPUT_VARIABLE _qt_output
            ERROR_VARIABLE  _qt_error
        )
        if (NOT _qt_result EQUAL 0)
            message(WARNING
                "[Qt] aqt install failed (exit ${_qt_result}): ${_qt_error}\n"
                "     Pass -DQt6_DIR=... manually or ensure aqt is installed.")
            return()
        endif()
        message(STATUS "[Qt] Qt ${CITRON_QT_VERSION} target downloaded")
    endif()

    # Download additional modules (multimedia, imageformats, svg)
    set(_QT_MM_CMAKE  "${_QT_TARGET_DIR}/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake")
    set(_QT_SVG_CMAKE "${_QT_TARGET_DIR}/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake")
    if (NOT EXISTS "${_QT_MM_CMAKE}" OR NOT EXISTS "${_QT_SVG_CMAKE}")
        message(STATUS "[Qt] Downloading Qt ${CITRON_QT_VERSION} additional modules...")
        execute_process(
            COMMAND ${_AQT_EXECUTABLE} install-qt
                    ${_QT_OS} ${_QT_TARGET}
                    ${CITRON_QT_VERSION} ${_QT_ARCH}
                    --outputdir "${CITRON_QT_BASE_DIR}"
                    --modules qtmultimedia qtimageformats qtsvg
            RESULT_VARIABLE _qt_mm_result
            OUTPUT_QUIET ERROR_QUIET
        )
        if (NOT _qt_mm_result EQUAL 0)
            message(WARNING "[Qt] Additional module install failed — build may fail")
        endif()
    endif()

    if (EXISTS "${_QT_TARGET_CMAKE}")
        get_filename_component(_qt6_dir "${_QT_TARGET_CMAKE}" DIRECTORY)
        set(Qt6_DIR "${_qt6_dir}" CACHE PATH "Path to Qt6Config.cmake (from aqt)" FORCE)
        message(STATUS "[Qt] Qt6_DIR = ${Qt6_DIR}")
    endif()
endif()

# ── Host Qt for cross-compilation (Linux host → Windows target) ───────────────
# Only needed when the host OS differs from the target (CMAKE_CROSSCOMPILING=TRUE
# or when CMAKE_HOST_UNIX is TRUE but we're targeting WIN32).
# For native Linux builds: skip entirely — the target Qt IS the host Qt.
# QT_HOST_PATH must NOT be set for native builds (it triggers cross-compile mode).
if (CMAKE_HOST_UNIX AND WIN32)
    if (QT_HOST_PATH AND EXISTS "${QT_HOST_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
        message(STATUS "[Qt] Using host Qt from QT_HOST_PATH: ${QT_HOST_PATH}")
    else()
        set(_QT_HOST_DIR   "${CITRON_QT_BASE_DIR}/${CITRON_QT_VERSION}/gcc_64")
        set(_QT_HOST_CMAKE "${_QT_HOST_DIR}/lib/cmake/Qt6/Qt6Config.cmake")

        if (NOT EXISTS "${_QT_HOST_CMAKE}")
            message(STATUS "[Qt] Downloading Qt ${CITRON_QT_VERSION} linux_gcc_64 host tools via aqt...")
            execute_process(
                COMMAND ${_AQT_EXECUTABLE} install-qt linux desktop
                        ${CITRON_QT_VERSION} linux_gcc_64
                        --outputdir "${CITRON_QT_BASE_DIR}"
                RESULT_VARIABLE _qt_host_result
                OUTPUT_QUIET ERROR_QUIET
            )
            if (NOT _qt_host_result EQUAL 0)
                message(WARNING "[Qt] Host Qt download failed — cross-compile may fail")
            endif()

            execute_process(
                COMMAND ${_AQT_EXECUTABLE} install-qt linux desktop
                        ${CITRON_QT_VERSION} linux_gcc_64
                        --outputdir "${CITRON_QT_BASE_DIR}"
                        --modules qtmultimedia
                OUTPUT_QUIET ERROR_QUIET
            )
        endif()

        if (EXISTS "${_QT_HOST_CMAKE}")
            set(QT_HOST_PATH "${_QT_HOST_DIR}" CACHE PATH "Host Qt for cross-compile tools" FORCE)
            message(STATUS "[Qt] QT_HOST_PATH = ${QT_HOST_PATH}")
        endif()
    endif()
endif()
