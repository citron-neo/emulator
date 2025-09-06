# SPDX-FileCopyrightText: 2023 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(PkgConfig QUIET)
pkg_search_module(GAMEMODE QUIET IMPORTED_TARGET gamemode)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(gamemode
    REQUIRED_VARS GAMEMODE_INCLUDEDIR
    VERSION_VAR GAMEMODE_VERSION
)

if (gamemode_FOUND AND NOT TARGET gamemode::headers)
    add_library(gamemode::headers ALIAS PkgConfig::GAMEMODE)
endif()
