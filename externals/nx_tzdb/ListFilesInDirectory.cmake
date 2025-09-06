# SPDX-FileCopyrightText: 2023 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# CMake does not have a way to list the files in a specific directory, 
# so we need this script to do that for us in a platform-agnostic fashion

file(GLOB FILE_LIST LIST_DIRECTORIES false RELATIVE ${CMAKE_SOURCE_DIR} "*")
execute_process(COMMAND ${CMAKE_COMMAND} -E echo "${FILE_LIST};")
