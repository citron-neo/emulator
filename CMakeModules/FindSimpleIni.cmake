# SPDX-FileCopyrightText: 2023 Alexandre Bouvier <contact@amb.tf>
#
# SPDX-License-Identifier: GPL-3.0-or-later

include(FindPackageHandleStandardArgs)

find_package(SimpleIni QUIET CONFIG)
if (SimpleIni_CONSIDERED_CONFIGS)
    find_package_handle_standard_args(SimpleIni CONFIG_MODE)
else()
    find_package(PkgConfig QUIET)
    pkg_search_module(SIMPLEINI QUIET IMPORTED_TARGET simpleini)
    find_package_handle_standard_args(SimpleIni
        REQUIRED_VARS SIMPLEINI_INCLUDEDIR
        VERSION_VAR SIMPLEINI_VERSION
    )
endif()

if (SimpleIni_FOUND AND NOT TARGET SimpleIni::SimpleIni)
    add_library(SimpleIni::SimpleIni ALIAS PkgConfig::SIMPLEINI)
endif()
