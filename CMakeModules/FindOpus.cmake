# SPDX-FileCopyrightText: 2022 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Prefer provider exports (including vcpkg) before searching pkg-config.
if (NOT TARGET Opus::opus)
    find_package(Opus ${Opus_FIND_VERSION} QUIET CONFIG)
endif()
if (TARGET Opus::opus)
    set(Opus_FOUND TRUE)
    return()
endif()

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_search_module(OPUS QUIET IMPORTED_TARGET opus)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Opus
    REQUIRED_VARS OPUS_LINK_LIBRARIES
    VERSION_VAR OPUS_VERSION
)

if (Opus_FOUND AND NOT TARGET Opus::opus)
    add_library(Opus::opus ALIAS PkgConfig::OPUS)
endif()
