# SPDX-FileCopyrightText: 2017 Citra Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(citron-room
    precompiled_headers.h
    citron_room.cpp
    citron_room.rc
)

# Robust static linking order using GNU ld group to resolve cycles
if (NOT MSVC AND NOT APPLE)
    # Use GNU ld.bfd for GCC LTO archives (ld.lld cannot consume GCC LTO plugin objects)
    target_link_options(citron-room PRIVATE -fuse-ld=bfd)

    # Robust static linking under LTO: force-include these archives and allow cyclic resolution
    if (ENABLE_WEB_SERVICE)
        target_compile_definitions(citron-room PRIVATE -DENABLE_WEB_SERVICE)
        target_link_libraries(citron-room PRIVATE
            "-Wl,--start-group"
            "-Wl,--whole-archive" common network core web_service "-Wl,--no-whole-archive"
            "-Wl,--end-group"
        )
    else()
        target_link_libraries(citron-room PRIVATE
            "-Wl,--start-group"
            "-Wl,--whole-archive" common network core "-Wl,--no-whole-archive"
            "-Wl,--end-group"
        )
    endif()
else()
    # Fallback for MSVC (no --start-group/--end-group)
    target_link_libraries(citron-room PRIVATE common network core)
    if (ENABLE_WEB_SERVICE)
        target_compile_definitions(citron-room PRIVATE -DENABLE_WEB_SERVICE)
        target_link_libraries(citron-room PRIVATE web_service)
    endif()
endif()

target_link_libraries(citron-room PRIVATE mbedtls mbedcrypto)
if (MSVC)
    target_link_libraries(citron-room PRIVATE getopt)
endif()
target_link_libraries(citron-room PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS citron-room)
endif()

# Create the "user" directory for portable mode on Windows
if(WIN32)
    add_custom_command(TARGET citron-room POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:citron-room>/user"
        COMMENT "Creating portable user directory for citron-room"
    )
endif()

if (CITRON_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(citron-room PRIVATE precompiled_headers.h)
endif()

create_target_directory_groups(citron-room)
