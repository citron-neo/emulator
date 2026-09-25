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
#   Windows llvm-mingw                       →  win64_llvm_mingw
#   Windows MSVC/clang-cl                    →  win64_msvc2022_64
#   Linux native x86-64                       →  linux_gcc_64 (via aqt)
#   Linux native aarch64                      →  linux_gcc_arm64 (host: linux_arm64)
#
# Cross-compilation (Linux host → Windows target):
#   QT_HOST_PATH is set to a Linux Qt install so moc/rcc/uic run on the host.
#   For native builds QT_HOST_PATH must NOT be set (it would trigger cross-compile mode).
#
# Prerequisites: Python3 + aqt must be installed (build script's ensure_aqt() handles this).

# The version is defined in dependencies.cmake as a CACHE variable.
if (NOT DEFINED CITRON_QT_VERSION)
    set(CITRON_QT_VERSION "6.9.3")
endif()

if (DEFINED ENV{CPM_SOURCE_CACHE})
    set(_DEFAULT_QT_BASE_DIR "$ENV{CPM_SOURCE_CACHE}/qt-bin")
elseif (DEFINED CPM_SOURCE_CACHE)
    set(_DEFAULT_QT_BASE_DIR "${CPM_SOURCE_CACHE}/qt-bin")
else()
    set(_DEFAULT_QT_BASE_DIR "${CMAKE_BINARY_DIR}/externals/qt-cpm")
endif()

set(CITRON_QT_BASE_DIR "${_DEFAULT_QT_BASE_DIR}" CACHE PATH
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

# ── Shared Linux host-arch selection ─────────────────────────────────────────
# aqt uses separate host-OS strings for x86-64 ("linux") and arm64
# ("linux_arm64"); the arch token is then linux_gcc_64 or linux_gcc_arm64.
# Computed once here and reused both by the native Linux target case below
# and by the cross-compile host Qt block, so moc/rcc/uic are always fetched
# for the actual build host architecture, not a hardcoded x86-64.
if (CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_QT_HOST_OS       "linux_arm64")
    set(_QT_HOST_ARCH     "linux_gcc_arm64")
    set(_QT_HOST_DIR_NAME "gcc_arm64")
else()
    set(_QT_HOST_OS       "linux")
    set(_QT_HOST_ARCH     "linux_gcc_64")
    set(_QT_HOST_DIR_NAME "gcc_64")
endif()

# ── Determine target platform ──────────────────────────────────────────────────
# WIN32 is TRUE both for native MSYS2 builds and for Linux→Windows cross-compile
# because the CMAKE_SYSTEM_NAME is Windows in both cases.
#
# _QT_WEBENGINE_SUPPORTED tracks whether the qtwebengine module can actually be
# downloaded/used for the resolved target at all. Per Qt's own platform notes
# (https://doc.qt.io/qt-6/qtwebengine-platform-notes.html), WebEngine embeds
# Chromium, which currently does not compile with MinGW on Windows - this is a
# hard Chromium-level limitation, not something aqt or a different module
# selection can work around. It works for MSVC (and therefore clang-cl, which
# is MSVC-ABI-compatible and is what this project's Windows builds actually
# use it with), Linux, and macOS.
set(_QT_WEBENGINE_SUPPORTED TRUE)
if (WIN32)
    set(_QT_OS        "windows")
    set(_QT_TARGET    "desktop")
    if (MSVC)
        set(_QT_ARCH      "win64_msvc2022_64")
        set(_QT_DIR_NAME  "msvc2022_64")
    else()
        set(_QT_ARCH      "win64_llvm_mingw")
        set(_QT_DIR_NAME  "llvm-mingw_64")
        set(_QT_WEBENGINE_SUPPORTED FALSE)
    endif()
    set(_QT_CMAKE_SUB "lib/cmake/Qt6")
else()
    if (APPLE)
        set(_QT_OS        "mac")
        set(_QT_TARGET    "desktop")
        if (ARCHITECTURE_arm64)
            set(_QT_ARCH  "mac_arm64")
        else()
            set(_QT_ARCH  "mac_x64")
        endif()
        set(_QT_DIR_NAME  "macos")
        set(_QT_CMAKE_SUB "lib/cmake/Qt6")
    else()
        # Native Linux — reuse the shared host-arch selection above so
        # moc/rcc/uic (which run on the build host) are the correct ELF arch.
        set(_QT_OS        "${_QT_HOST_OS}")
        set(_QT_TARGET    "desktop")
        set(_QT_ARCH      "${_QT_HOST_ARCH}")
        set(_QT_DIR_NAME  "${_QT_HOST_DIR_NAME}")
        set(_QT_CMAKE_SUB "lib/cmake/Qt6")
    endif()
endif()

# ── Download target Qt ────────────────────────────────────────────────────────
# _QT_TARGET_DIR/_QT_TARGET_CMAKE are computed up front so Qt6_DIR can be
# compared against the exact aqt-managed path. This matters because Qt6_DIR is
# a FORCEd CACHE variable: on subsequent configures the shortcut below skips the
# additional-modules check, so toggling CITRON_USE_QT_WEB_ENGINE in an existing
# build dir would silently skip the qtwebengine download without this comparison.
# An externally-provided Qt6_DIR that doesn't match this path is left untouched.
set(_QT_TARGET_DIR   "${CITRON_QT_BASE_DIR}/${CITRON_QT_VERSION}/${_QT_DIR_NAME}")
set(_QT_TARGET_CMAKE "${_QT_TARGET_DIR}/${_QT_CMAKE_SUB}/Qt6Config.cmake")
get_filename_component(_QT_AQT_CMAKE_DIR "${_QT_TARGET_CMAKE}" DIRECTORY)

if (Qt6_DIR AND EXISTS "${Qt6_DIR}/Qt6Config.cmake" AND NOT ("${Qt6_DIR}" STREQUAL "${_QT_AQT_CMAKE_DIR}"))
    message(STATUS "[Qt] Using target Qt from Qt6_DIR: ${Qt6_DIR}")
    if (NOT QT_TARGET_PATH)
        get_filename_component(_tmp_path "${Qt6_DIR}/../../.." ABSOLUTE)
        set(QT_TARGET_PATH "${_tmp_path}" CACHE PATH "Path to Qt6 target root" FORCE)
    endif()
else()
    # Modules to request via aqt's -m flag.
    # qtimageformats is a normal addon for 6.9.3 on all targets.
    # qtsvg and qttools are NOT addon modules in 6.9.x -- they ship with the base
    # install; requesting them via -m makes aqt fail. qtmultimedia is intentionally
    # excluded as it is not used by citron-neo on Qt6+.
    set(_QT_ADDL_MODULES qtimageformats)
    set(_QT_WEBENGINE_CMAKE "${_QT_TARGET_DIR}/lib/cmake/Qt6WebEngineCore/Qt6WebEngineCoreConfig.cmake")
    if (CITRON_USE_QT_WEB_ENGINE)
        if (_QT_WEBENGINE_SUPPORTED)
            list(APPEND _QT_ADDL_MODULES qtwebengine qtpositioning qtwebchannel)
        else()
            message(WARNING
                "[Qt] CITRON_USE_QT_WEB_ENGINE is ON, but qtwebengine is not available for "
                "${_QT_ARCH} (Qt WebEngine embeds Chromium, which does not compile with MinGW "
                "on Windows - this is a Qt/Chromium limitation, not an aqt packaging gap). "
                "Use the MSVC/clang-cl toolchain instead if you need the web applet frontend; "
                "the build will proceed without it and CITRON_USE_QT_WEB_ENGINE will have no "
                "effect for this target.")
            # Keep later component selection consistent with the modules available for this
            # target, otherwise find_package still requests unavailable WebEngine components.
            set(CITRON_USE_QT_WEB_ENGINE OFF CACHE BOOL
                "Use QtWebEngine for web applet implementation" FORCE)
        endif()
    endif()
    # Svg/LinguistTools ship with the base install now (see note above), so
    # these are no longer a gate for an additional aqt call — just a
    # post-install sanity check further down that the base install actually
    # delivered them.
    set(_QT_SVG_CMAKE  "${_QT_TARGET_DIR}/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake")
    set(_QT_TOOL_CMAKE "${_QT_TARGET_DIR}/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake")

    if (NOT EXISTS "${_QT_TARGET_CMAKE}")
        # Fresh install: request base and all additional modules in a single aqt
        # invocation. A separate modules-only follow-up call has been observed to
        # fail after a cache wipe even with correct argv.
        message(STATUS "[Qt] Downloading Qt ${CITRON_QT_VERSION} ${_QT_ARCH} via aqt (modules: ${_QT_ADDL_MODULES})...")
        file(MAKE_DIRECTORY "${CITRON_QT_BASE_DIR}")

        execute_process(
            COMMAND ${_AQT_EXECUTABLE} install-qt
                    ${_QT_OS} ${_QT_TARGET}
                    ${CITRON_QT_VERSION} ${_QT_ARCH}
                    --outputdir "${CITRON_QT_BASE_DIR}"
                    --modules ${_QT_ADDL_MODULES}
            RESULT_VARIABLE _qt_result
            OUTPUT_VARIABLE _qt_output
            ERROR_VARIABLE  _qt_error
        )
        if (NOT _qt_result EQUAL 0)
            # FATAL_ERROR not WARNING: return() only unwinds this include()'d file.
            # A WARNING leaves Qt6_DIR unset, allowing a later find_package to silently
            # pick up whatever Qt6 is on CMAKE_PREFIX_PATH (e.g. MSYS2's Qt), producing
            # a link failure hundreds of steps later instead of a clear configure error.
            message(FATAL_ERROR
                "[Qt] aqt install failed (exit ${_qt_result}): ${_qt_error}\n"
                "     Pass -DQt6_DIR=... manually or ensure aqt is installed.")
        endif()
        message(STATUS "[Qt] Qt ${CITRON_QT_VERSION} target and additional modules downloaded")
    else()
        # Check each additional module independently -- gating this whole block on
        # webengine's marker alone meant a missing qtimageformats (webengine already
        # present, or CITRON_USE_QT_WEB_ENGINE off entirely) never got repaired.
        set(_qt_missing_modules "")
        file(GLOB _qt_imageformats_files "${_QT_TARGET_DIR}/plugins/imageformats/*")
        if (NOT _qt_imageformats_files)
            list(APPEND _qt_missing_modules qtimageformats)
        endif()
        if (CITRON_USE_QT_WEB_ENGINE AND _QT_WEBENGINE_SUPPORTED AND NOT EXISTS "${_QT_WEBENGINE_CMAKE}")
            list(APPEND _qt_missing_modules qtwebengine qtpositioning qtwebchannel)
        endif()

        if (_qt_missing_modules)
            message(STATUS "[Qt] Downloading Qt ${CITRON_QT_VERSION} additional modules (${_qt_missing_modules})...")
            execute_process(
                COMMAND ${_AQT_EXECUTABLE} install-qt
                        ${_QT_OS} ${_QT_TARGET}
                        ${CITRON_QT_VERSION} ${_QT_ARCH}
                        --outputdir "${CITRON_QT_BASE_DIR}"
                        --modules ${_qt_missing_modules}
                RESULT_VARIABLE _qt_addl_result
                OUTPUT_VARIABLE _qt_addl_output
                ERROR_VARIABLE  _qt_addl_error
            )
            if (NOT _qt_addl_result EQUAL 0)
                message(WARNING
                    "[Qt] Additional module install failed (exit ${_qt_addl_result}) for "
                    "(${_qt_missing_modules}): ${_qt_addl_error}\n"
                    "     Build may fail on missing Qt components.")
            endif()
        endif()
    endif()

    if (EXISTS "${_QT_TARGET_CMAKE}")
        get_filename_component(_qt6_dir "${_QT_TARGET_CMAKE}" DIRECTORY)
        set(Qt6_DIR "${_qt6_dir}" CACHE PATH "Path to Qt6Config.cmake (from aqt)" FORCE)
        set(QT_TARGET_PATH "${_QT_TARGET_DIR}" CACHE PATH "Path to Qt6 target root" FORCE)

        message(STATUS "[Qt] Qt6_DIR = ${Qt6_DIR}")
        message(STATUS "[Qt] QT_TARGET_PATH = ${QT_TARGET_PATH}")

        # Svg/LinguistTools are expected to ship with the base install (see
        # notes above) with no separate -m fallback if they're missing —
        # surface that clearly here rather than letting a much later,
        # unrelated-looking find_package(Qt6 COMPONENTS Svg) failure be the
        # first sign something's wrong.
        if (NOT EXISTS "${_QT_SVG_CMAKE}")
            message(WARNING
                "[Qt] Qt6Svg not found under ${_QT_TARGET_DIR} — expected it to ship with "
                "base Qt ${CITRON_QT_VERSION}; find_package(Qt6 COMPONENTS Svg) will fail.")
        endif()
        if (NOT EXISTS "${_QT_TOOL_CMAKE}")
            message(WARNING
                "[Qt] Qt6LinguistTools not found under ${_QT_TARGET_DIR} — expected it to ship "
                "with base Qt ${CITRON_QT_VERSION}.")
        endif()
    endif()
endif()

# Prepend target path so internal dependencies (like Qt6CoreTools) are found here first
if (QT_TARGET_PATH)
    list(PREPEND CMAKE_PREFIX_PATH "${QT_TARGET_PATH}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE PATH "Search path for Qt and other dependencies" FORCE)
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
        set(_QT_HOST_DIR   "${CITRON_QT_BASE_DIR}/${CITRON_QT_VERSION}/${_QT_HOST_DIR_NAME}")
        set(_QT_HOST_CMAKE "${_QT_HOST_DIR}/lib/cmake/Qt6/Qt6Config.cmake")

        if (NOT EXISTS "${_QT_HOST_CMAKE}")
            message(STATUS "[Qt] Downloading Qt ${CITRON_QT_VERSION} ${_QT_HOST_ARCH} host tools via aqt...")
            execute_process(
                COMMAND ${_AQT_EXECUTABLE} install-qt ${_QT_HOST_OS} desktop
                        ${CITRON_QT_VERSION} ${_QT_HOST_ARCH}
                        --outputdir "${CITRON_QT_BASE_DIR}"
                RESULT_VARIABLE _qt_host_result
                OUTPUT_QUIET ERROR_QUIET
            )
            if (NOT _qt_host_result EQUAL 0)
                message(WARNING "[Qt] Host Qt download failed — cross-compile may fail")
            endif()
        endif()

        if (EXISTS "${_QT_HOST_CMAKE}")
            set(QT_HOST_PATH "${_QT_HOST_DIR}" CACHE PATH "Host Qt for cross-compile tools" FORCE)
            message(STATUS "[Qt] QT_HOST_PATH = ${QT_HOST_PATH}")
        endif()
    endif()
endif()

# Prepend host path for cross-compile tool discovery
if (QT_HOST_PATH)
    list(PREPEND CMAKE_PREFIX_PATH "${QT_HOST_PATH}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" CACHE PATH "Search path for Qt and other dependencies" FORCE)
endif()
