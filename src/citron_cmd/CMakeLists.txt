# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Credits to Samantas5855 and others for this function.
function(create_resource file output filename)
    # Read hex data from file
    file(READ ${file} filedata HEX)
    # Convert hex data for C compatibility
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata ${filedata})
    # Write data to output file
    set(RESOURCES_DIR "${PROJECT_BINARY_DIR}/dist" PARENT_SCOPE)
    file(WRITE "${PROJECT_BINARY_DIR}/dist/${output}" "const unsigned char ${filename}[] = {${filedata}};\nconst unsigned ${filename}_size = sizeof(${filename});\n")
endfunction()

add_executable(citron-cmd
    emu_window/emu_window_sdl2.cpp
    emu_window/emu_window_sdl2.h
    emu_window/emu_window_sdl2_null.cpp
    emu_window/emu_window_sdl2_null.h
    emu_window/emu_window_sdl2_vk.cpp
    emu_window/emu_window_sdl2_vk.h
    precompiled_headers.h
    citron_sdl_config.cpp
    citron_sdl_config.h
    citron.cpp
    citron.rc
)

# Robust static linking and linker selection under GCC LTO
# Linker selection: For Clang, the global configuration defaults to lld.
# This guard ensures we only use BFD for legacy GCC builds.
if (NOT MSVC AND NOT APPLE)
    target_link_options(citron-cmd PRIVATE "$<$<CXX_COMPILER_ID:GNU>:-fuse-ld=bfd>")

    # Force-include critical static archives and resolve cycles
    if (ENABLE_WEB_SERVICE)
        target_compile_definitions(citron-cmd PRIVATE -DENABLE_WEB_SERVICE)
        if (CITRON_ENABLE_TRACY AND TARGET TracyClient)
            target_link_libraries(citron-cmd PRIVATE
                "-Wl,--start-group"
                "-Wl,--whole-archive" common core input_common frontend_common network web_service TracyClient "-Wl,--no-whole-archive"
                "-Wl,--end-group"
            )
        else()
            target_link_libraries(citron-cmd PRIVATE
                "-Wl,--start-group"
                "-Wl,--whole-archive" common core input_common frontend_common network web_service "-Wl,--no-whole-archive"
                "-Wl,--end-group"
            )
        endif()
    else()
        if (CITRON_ENABLE_TRACY AND TARGET TracyClient)
            target_link_libraries(citron-cmd PRIVATE
                "-Wl,--start-group"
                "-Wl,--whole-archive" common core input_common frontend_common network TracyClient "-Wl,--no-whole-archive"
                "-Wl,--end-group"
            )
        else()
            target_link_libraries(citron-cmd PRIVATE
                "-Wl,--start-group"
                "-Wl,--whole-archive" common core input_common frontend_common network "-Wl,--no-whole-archive"
                "-Wl,--end-group"
            )
        endif()
    endif()
else()
    # MSVC-style linkers (real MSVC and clang-cl, both report MSVC=TRUE) and
    # Apple's ld64 don't understand -Wl,--whole-archive. Without an explicit
    # equivalent, TracyClient's static initializer (which spawns the profiler
    # worker thread) gets stripped because nothing in citron-cmd.exe directly
    # references a TracyClient symbol.
    if (MSVC AND CITRON_ENABLE_TRACY AND TARGET TracyClient)
        target_link_options(citron-cmd PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:TracyClient>")
        target_link_libraries(citron-cmd PRIVATE common core input_common frontend_common TracyClient)
    elseif (APPLE AND CITRON_ENABLE_TRACY AND TARGET TracyClient)
        target_link_options(citron-cmd PRIVATE "-Wl,-force_load,$<TARGET_FILE:TracyClient>")
        target_link_libraries(citron-cmd PRIVATE common core input_common frontend_common TracyClient)
    else()
        target_link_libraries(citron-cmd PRIVATE common core input_common frontend_common)
    endif()
endif()

target_link_libraries(citron-cmd PRIVATE glad)
if (MSVC)
    target_link_libraries(citron-cmd PRIVATE getopt)
endif()
target_link_libraries(citron-cmd PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

create_resource("../../dist/citron.bmp" "citron_cmd/citron_icon.h" "citron_icon")
target_include_directories(citron-cmd PRIVATE ${RESOURCES_DIR})

target_link_libraries(citron-cmd PRIVATE SDL2::SDL2 Vulkan::Headers)

if(UNIX AND NOT APPLE)
    install(TARGETS citron-cmd)
endif()

if(WIN32)
    # compile as a win32 gui application instead of a console application
    if(MSVC)
        set_target_properties(citron-cmd PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    elseif(MINGW)
        set_target_properties(citron-cmd PROPERTIES LINK_FLAGS_RELEASE "-Wl,--subsystem,windows")
    endif()
endif()

if(MINGW)
    include(CopyMinGWDeps)
    copy_mingw_deps(citron-cmd SEARCH_PATHS "${FFmpeg_PATH}/bin" "${QT_TARGET_PATH}/bin")
endif()

if (ENABLE_SDL2)
    # Keep SDL3 after CopyMinGWDeps' import-table-only stale-DLL cleanup.
    copy_citron_sdl_runtime(citron-cmd)
endif()

if(WIN32)
    add_custom_command(TARGET citron-cmd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:citron-cmd>/user"
        COMMENT "Creating portable user directory for citron-cmd"
    )
endif()

if (CITRON_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(citron-cmd PRIVATE precompiled_headers.h)
endif()

create_target_directory_groups(citron-cmd)
