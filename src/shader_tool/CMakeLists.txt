# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(shader_tool
    shader_tool.cpp
)

create_target_directory_groups(shader_tool)

target_link_libraries(shader_tool PRIVATE common shader_recompiler)
target_link_libraries(shader_tool PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS shader_tool)
endif()
