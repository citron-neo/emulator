# SPDX-FileCopyrightText: 2023 Alexandre Bouvier <contact@amb.tf>
#
# SPDX-License-Identifier: GPL-3.0-or-later

find_package(LLVM QUIET COMPONENTS CONFIG)
if (LLVM_FOUND)
    separate_arguments(LLVM_DEFINITIONS)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LLVM HANDLE_COMPONENTS CONFIG_MODE)
