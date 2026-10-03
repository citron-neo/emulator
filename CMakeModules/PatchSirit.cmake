# SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(DEPENDENCY_SOURCE_DIR "${SIRIT_SOURCE_DIR}")
set(DEPENDENCY_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/../patches/sirit-provider-target.patch")
include("${CMAKE_CURRENT_LIST_DIR}/ApplyDependencyPatch.cmake")
