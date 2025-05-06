# SPDX-FileCopyrightText: 2017 Citra Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(citron-room
    precompiled_headers.h
    citron_room.cpp
    citron_room.rc
)

target_link_libraries(citron-room PRIVATE common network)
if (ENABLE_WEB_SERVICE)
    target_compile_definitions(citron-room PRIVATE -DENABLE_WEB_SERVICE)
    target_link_libraries(citron-room PRIVATE web_service)
endif()

target_link_libraries(citron-room PRIVATE mbedtls mbedcrypto)
if (MSVC)
    target_link_libraries(citron-room PRIVATE getopt)
endif()
target_link_libraries(citron-room PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS citron-room)
endif()

if (CITRON_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(citron-room PRIVATE precompiled_headers.h)
endif()

create_target_directory_groups(citron-room)
