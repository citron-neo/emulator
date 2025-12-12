# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Set the RPATH for Qt Libraries
# This must be done before the `citron` target is created
if (CITRON_USE_BUNDLED_QT AND (${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/bin/lib/")
endif()

add_executable(citron
    Info.plist
    about_dialog.cpp
    about_dialog.h
    aboutdialog.ui
    applets/qt_amiibo_settings.cpp
    applets/qt_amiibo_settings.h
    applets/qt_amiibo_settings.ui
    applets/qt_controller.cpp
    applets/qt_controller.h
    applets/qt_controller.ui
    applets/qt_error.cpp
    applets/qt_error.h
    applets/qt_profile_select.cpp
    applets/qt_profile_select.h
    applets/qt_software_keyboard.cpp
    applets/qt_software_keyboard.h
    applets/qt_software_keyboard.ui
    applets/qt_web_browser.cpp
    applets/qt_web_browser.h
    applets/qt_web_browser_scripts.h
    bootmanager.cpp
    bootmanager.h
    controller_overlay.cpp
    controller_overlay.h
    compatdb.ui
    setup_wizard.ui
    compatibility_list.cpp
    compatibility_list.h
    configuration/configuration_shared.cpp
    configuration/configuration_shared.h
    configuration/configure.ui
    configuration/configure_applets.cpp
    configuration/configure_applets.h
    configuration/configure_applets.ui
    configuration/configure_audio.cpp
    configuration/configure_audio.h
    configuration/configure_audio.ui
    configuration/configure_camera.cpp
    configuration/configure_camera.h
    configuration/configure_camera.ui
    configuration/configure_cpu.cpp
    configuration/configure_cpu.h
    configuration/configure_cpu.ui
    configuration/configure_cpu_debug.cpp
    configuration/configure_cpu_debug.h
    configuration/configure_cpu_debug.ui
    configuration/configure_debug.cpp
    configuration/configure_debug.h
    configuration/configure_debug.ui
    configuration/configure_debug_controller.cpp
    configuration/configure_debug_controller.h
    configuration/configure_debug_controller.ui
    configuration/configure_debug_tab.cpp
    configuration/configure_debug_tab.h
    configuration/configure_debug_tab.ui
    configuration/configure_dialog.cpp
    configuration/configure_dialog.h
    configuration/configure_filesystem.cpp
    configuration/configure_filesystem.h
    configuration/configure_filesystem.ui
    configuration/configure_general.cpp
    configuration/configure_general.h
    configuration/configure_general.ui
    configuration/configure_graphics.cpp
    configuration/configure_graphics.h
    configuration/configure_graphics.ui
    configuration/configure_graphics_advanced.cpp
    configuration/configure_graphics_advanced.h
    configuration/configure_graphics_advanced.ui
    configuration/configure_hotkeys.cpp
    configuration/configure_hotkeys.h
    configuration/configure_hotkeys.ui
    configuration/configure_input.cpp
    configuration/configure_input.h
    configuration/configure_input.ui
    configuration/configure_input_advanced.cpp
    configuration/configure_input_advanced.h
    configuration/configure_input_advanced.ui
    configuration/configure_input_per_game.cpp
    configuration/configure_input_per_game.h
    configuration/configure_input_per_game.ui
    configuration/configure_input_player.cpp
    configuration/configure_input_player.h
    configuration/configure_input_player.ui
    configuration/configure_input_player_widget.cpp
    configuration/configure_input_player_widget.h
    configuration/configure_input_profile_dialog.cpp
    configuration/configure_input_profile_dialog.h
    configuration/configure_input_profile_dialog.ui
    configuration/configure_linux_tab.cpp
    configuration/configure_linux_tab.h
    configuration/configure_linux_tab.ui
    configuration/configure_mouse_panning.cpp
    configuration/configure_mouse_panning.h
    configuration/configure_mouse_panning.ui
    configuration/configure_motion_touch.cpp
    configuration/configure_motion_touch.h
    configuration/configure_motion_touch.ui
    configuration/configure_per_game.cpp
    configuration/configure_per_game.h
    configuration/configure_per_game.ui
    configuration/configure_per_game_addons.cpp
    configuration/configure_per_game_addons.h
    configuration/configure_per_game_addons.ui
    configuration/configure_per_game_cheats.cpp
    configuration/configure_per_game_cheats.h
    configuration/configure_per_game_cheats.ui
    configuration/configure_profile_manager.cpp
    configuration/configure_profile_manager.h
    configuration/configure_profile_manager.ui
    configuration/configure_ringcon.cpp
    configuration/configure_ringcon.h
    configuration/configure_ringcon.ui
    configuration/configure_network.cpp
    configuration/configure_network.h
    configuration/configure_network.ui
    configuration/configure_system.cpp
    configuration/configure_system.h
    configuration/configure_system.ui
    configuration/configure_tas.cpp
    configuration/configure_tas.h
    configuration/configure_tas.ui
    configuration/configure_touch_from_button.cpp
    configuration/configure_touch_from_button.h
    configuration/configure_touch_from_button.ui
    configuration/configure_touchscreen_advanced.cpp
    configuration/configure_touchscreen_advanced.h
    configuration/configure_touchscreen_advanced.ui
    configuration/configure_touch_widget.h
    configuration/configure_ui.cpp
    configuration/configure_ui.h
    configuration/configure_ui.ui
    configuration/configure_vibration.cpp
    configuration/configure_vibration.h
    configuration/configure_vibration.ui
    configuration/configure_web.cpp
    configuration/configure_web.h
    configuration/configure_web.ui
    configuration/input_profiles.cpp
    configuration/input_profiles.h
    configuration/shared_translation.cpp
    configuration/shared_translation.h
    configuration/shared_widget.cpp
    configuration/shared_widget.h
    configuration/style_animation_event_filter.cpp
    configuration/style_animation_event_filter.h
    configuration/qt_config.cpp
    configuration/qt_config.h
    debugger/console.cpp
    debugger/console.h
    debugger/controller.cpp
    debugger/controller.h
    debugger/profiler.cpp
    debugger/profiler.h
    debugger/wait_tree.cpp
    debugger/wait_tree.h
    discord.h
    game_list.cpp
    game_list.h
    game_list_p.h
    game_list_worker.cpp
    game_list_worker.h
    hotkeys.cpp
    hotkeys.h
    install_dialog.cpp
    install_dialog.h
    loading_screen.cpp
    loading_screen.h
    loading_screen.ui
    main.cpp
    main.h
    main.ui
    multiplayer/chat_room.cpp
    multiplayer/chat_room.h
    multiplayer/chat_room.ui
    multiplayer/client_room.h
    multiplayer/client_room.cpp
    multiplayer/client_room.ui
    multiplayer/direct_connect.cpp
    multiplayer/direct_connect.h
    multiplayer/direct_connect.ui
    multiplayer/host_room.cpp
    multiplayer/host_room.h
    multiplayer/host_room.ui
    multiplayer/lobby.cpp
    multiplayer/lobby.h
    multiplayer/lobby.ui
    multiplayer/lobby_p.h
    multiplayer/message.cpp
    multiplayer/message.h
    multiplayer/moderation_dialog.cpp
    multiplayer/moderation_dialog.h
    multiplayer/moderation_dialog.ui
    multiplayer/state.cpp
    multiplayer/state.h
    multiplayer/validation.h
    play_time_manager.cpp
    play_time_manager.h
    precompiled_headers.h
    qt_common.cpp
    qt_common.h
    startup_checks.cpp
    startup_checks.h
    theme.h
    uisettings.cpp
    uisettings.h
    util/clickable_label.cpp
    util/clickable_label.h
    util/controller_navigation.cpp
    util/controller_navigation.h
    util/limitable_input_dialog.cpp
    util/limitable_input_dialog.h
    util/overlay_dialog.cpp
    util/overlay_dialog.h
    util/overlay_dialog.ui
    util/performance_overlay.cpp
    util/performance_overlay.h
    util/multiplayer_room_overlay.cpp
    util/multiplayer_room_overlay.h
    util/vram_overlay.cpp
    util/vram_overlay.h
    util/sequence_dialog/sequence_dialog.cpp
    util/sequence_dialog/sequence_dialog.h
    util/title_ids.h
    util/url_request_interceptor.cpp
    util/url_request_interceptor.h
    util/util.cpp
    util/util.h
    vk_device_info.cpp
    vk_device_info.h
    compatdb.cpp
    compatdb.h
    setup_wizard.cpp
    setup_wizard.h
    citron.qrc
    citron.rc
)

set_source_files_properties(controller_overlay.cpp PROPERTIES SKIP_AUTOUIC ON)

if (CITRON_CRASH_DUMPS)
    target_sources(citron PRIVATE
        breakpad.cpp
        breakpad.h
    )

    target_link_libraries(citron PRIVATE libbreakpad_client)
    target_compile_definitions(citron PRIVATE CITRON_CRASH_DUMPS)
endif()

if (CITRON_USE_AUTO_UPDATER)
    target_sources(citron PRIVATE
        updater/updater_service.cpp
        updater/updater_service.h
        updater/updater_dialog.cpp
        updater/updater_dialog.h
        updater/updater_dialog.ui
    )

    target_compile_definitions(citron PRIVATE
        # Ensure _WIN32 is defined for moc processing on Windows
        $<$<BOOL:${WIN32}>:_WIN32>
        CITRON_USE_AUTO_UPDATER
    )

    target_link_libraries(citron PRIVATE Qt6::Network)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_definitions(citron PRIVATE
        $<$<VERSION_LESS:$<CXX_COMPILER_VERSION>,15>:CANNOT_EXPLICITLY_INSTANTIATE>
    )
endif()

file(GLOB COMPAT_LIST
     ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.qrc
     ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json)
file(GLOB_RECURSE ICONS ${PROJECT_SOURCE_DIR}/dist/icons/*)
file(GLOB_RECURSE THEMES ${PROJECT_SOURCE_DIR}/dist/qt_themes/*)

if (ENABLE_QT_TRANSLATION)
    set(CITRON_QT_LANGUAGES "${PROJECT_SOURCE_DIR}/dist/languages" CACHE PATH "Path to the translation bundle for the Qt frontend")
    option(GENERATE_QT_TRANSLATION "Generate en.ts as the translation source file" OFF)
    option(WORKAROUND_BROKEN_LUPDATE "Run lupdate directly through CMake if Qt's convenience wrappers don't work" OFF)

    # Update source TS file if enabled
    if (GENERATE_QT_TRANSLATION)
        get_target_property(SRCS citron SOURCES)
        # these calls to qt_create_translation also creates a rule to generate en.qm which conflicts with providing english plurals
        # so we have to set a OUTPUT_LOCATION so that we don't have multiple rules to generate en.qm
        set_source_files_properties(${CITRON_QT_LANGUAGES}/en.ts PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/translations")
        if (WORKAROUND_BROKEN_LUPDATE)
            add_custom_command(OUTPUT ${CITRON_QT_LANGUAGES}/en.ts
                COMMAND lupdate
                    -source-language en_US
                    -target-language en_US
                    ${SRCS}
                    ${UIS}
                    -ts ${CITRON_QT_LANGUAGES}/en.ts
                DEPENDS
                    ${SRCS}
                    ${UIS}
                WORKING_DIRECTORY
                    ${CMAKE_CURRENT_SOURCE_DIR}
            )
        else()
            qt_create_translation(QM_FILES
                ${SRCS}
                ${UIS}
                ${CITRON_QT_LANGUAGES}/en.ts
            OPTIONS
                -source-language en_US
                -target-language en_US
            )
        endif()

        # Generate plurals into dist/english_plurals/generated_en.ts so it can be used to revise dist/english_plurals/en.ts
        set(GENERATED_PLURALS_FILE ${PROJECT_SOURCE_DIR}/dist/english_plurals/generated_en.ts)
        set_source_files_properties(${GENERATED_PLURALS_FILE} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/plurals")
        if (WORKAROUND_BROKEN_LUPDATE)
            add_custom_command(OUTPUT ${GENERATED_PLURALS_FILE}
                COMMAND lupdate
                    -source-language en_US
                    -target-language en_US
                    ${SRCS}
                    ${UIS}
                    -ts ${GENERATED_PLURALS_FILE}
                DEPENDS
                    ${SRCS}
                    ${UIS}
                WORKING_DIRECTORY
                    ${CMAKE_CURRENT_SOURCE_DIR}
            )
        else()
            qt_create_translation(QM_FILES ${SRCS} ${UIS} ${GENERATED_PLURALS_FILE} OPTIONS -pluralonly -source-language en_US -target-language en_US)
        endif()

        add_custom_target(translation ALL DEPENDS ${CITRON_QT_LANGUAGES}/en.ts ${GENERATED_PLURALS_FILE})
    endif()

    # Find all TS files except en.ts
    file(GLOB_RECURSE LANGUAGES_TS ${CITRON_QT_LANGUAGES}/*.ts)
    list(REMOVE_ITEM LANGUAGES_TS ${CITRON_QT_LANGUAGES}/en.ts)

    # Compile TS files to QM files
    qt_add_translation(LANGUAGES_QM ${LANGUAGES_TS})

    # Compile english plurals TS file to en.qm
    qt_add_translation(LANGUAGES_QM ${PROJECT_SOURCE_DIR}/dist/english_plurals/en.ts)

    # Build a QRC file from the QM file list
    set(LANGUAGES_QRC ${CMAKE_CURRENT_BINARY_DIR}/languages.qrc)
    file(WRITE ${LANGUAGES_QRC} "<RCC><qresource prefix=\"languages\">\n")
    foreach (QM ${LANGUAGES_QM})
        get_filename_component(QM_FILE ${QM} NAME)
        file(APPEND ${LANGUAGES_QRC} "<file>${QM_FILE}</file>\n")
    endforeach (QM)
    file(APPEND ${LANGUAGES_QRC} "</qresource></RCC>")

    # Add the QRC file to package in all QM files
    qt_add_resources(LANGUAGES ${LANGUAGES_QRC})
else()
    set(LANGUAGES)
endif()

target_sources(citron
    PRIVATE
        ${COMPAT_LIST}
        ${ICONS}
        ${LANGUAGES}
        ${THEMES}
)

if (APPLE)
    set(MACOSX_ICON "../../dist/citron.icns")
    set_source_files_properties(${MACOSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(citron PRIVATE ${MACOSX_ICON})
    set_target_properties(citron PROPERTIES MACOSX_BUNDLE TRUE)
    set_target_properties(citron PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)

    if (NOT USE_SYSTEM_MOLTENVK)
        set(MOLTENVK_PLATFORM "macOS")
        set(MOLTENVK_VERSION "v1.4.0")
        download_moltenvk_external(${MOLTENVK_PLATFORM} ${MOLTENVK_VERSION})
    endif()
    find_library(MOLTENVK_LIBRARY MoltenVK REQUIRED)
    message(STATUS "Using MoltenVK at ${MOLTENVK_LIBRARY}.")
    set_source_files_properties(${MOLTENVK_LIBRARY} PROPERTIES MACOSX_PACKAGE_LOCATION Frameworks
                                    XCODE_FILE_ATTRIBUTES "CodeSignOnCopy")
    target_sources(citron PRIVATE ${MOLTENVK_LIBRARY})

elseif(WIN32)
    # compile as a win32 gui application instead of a console application
    target_link_libraries(citron PRIVATE Qt6::EntryPointPrivate)

    # Add Windows-specific libraries for WMI/COM to read hardware sensors
    target_link_libraries(citron PRIVATE ole32 oleaut32 wbemuuid)

    if(MSVC)
        target_link_libraries(citron PRIVATE version.lib)
        set_target_properties(citron PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    elseif(MINGW)
        set_target_properties(citron PROPERTIES LINK_FLAGS_RELEASE "-Wl,--subsystem,windows")
    endif()
endif()

# Robust static linking and linker selection under GCC LTO
if (NOT MSVC AND NOT APPLE)
    # Use GNU ld.bfd for GCC LTO plugin-aware archive resolution
    target_link_options(citron PRIVATE -fuse-ld=bfd)

    # Force-include critical static archives and resolve cycles
    if (ENABLE_WEB_SERVICE)
        target_compile_definitions(citron PRIVATE -DENABLE_WEB_SERVICE)
        target_link_libraries(citron PRIVATE
            "-Wl,--start-group"
            "-Wl,--whole-archive" common core input_common frontend_common network video_core web_service "-Wl,--no-whole-archive"
            "-Wl,--end-group"
        )
    else()
        target_link_libraries(citron PRIVATE
            "-Wl,--start-group"
            "-Wl,--whole-archive" common core input_common frontend_common network video_core "-Wl,--no-whole-archive"
            "-Wl,--end-group"
        )
    endif()
else()
    target_link_libraries(citron PRIVATE common core input_common frontend_common network video_core)
endif()

target_link_libraries(citron PRIVATE Boost::headers glad Qt6::Widgets Qt6::Network)
target_link_libraries(citron PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

# Add libarchive for updater functionality
if (TARGET LibArchive::LibArchive)
    target_link_libraries(citron PRIVATE LibArchive::LibArchive)
    target_compile_definitions(citron PRIVATE CITRON_ENABLE_LIBARCHIVE)
endif()

target_link_libraries(citron PRIVATE Vulkan::Headers)
if (NOT WIN32)
    target_include_directories(citron PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
endif()
if (UNIX AND NOT APPLE)
    target_link_libraries(citron PRIVATE Qt6::DBus)
endif()

target_compile_definitions(citron PRIVATE
    # Use QStringBuilder for string concatenation to reduce
    # the overall number of temporary strings created.
    -DQT_USE_QSTRINGBUILDER

    # Disable implicit conversions from/to C strings
    -DQT_NO_CAST_FROM_ASCII
    -DQT_NO_CAST_TO_ASCII

    # Disable implicit type narrowing in signal/slot connect() calls.
    -DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT

    # Disable unsafe overloads of QProcess' start() function.
    -DQT_NO_PROCESS_COMBINED_ARGUMENT_START

    # Disable implicit QString->QUrl conversions to enforce use of proper resolving functions.
    -DQT_NO_URL_CAST_FROM_STRING
)

if (CITRON_ENABLE_COMPATIBILITY_REPORTING)
    target_compile_definitions(citron PRIVATE -DCITRON_ENABLE_COMPATIBILITY_REPORTING)
endif()

if (USE_DISCORD_PRESENCE)
    target_sources(citron PUBLIC
        discord_impl.cpp
        discord_impl.h
    )
    target_link_libraries(citron PRIVATE DiscordRPC::discord-rpc httplib::httplib Qt6::Network)
    target_compile_definitions(citron PRIVATE -DUSE_DISCORD_PRESENCE)
endif()

if (ENABLE_WEB_SERVICE)
    target_compile_definitions(citron PRIVATE -DENABLE_WEB_SERVICE)
endif()

if (CITRON_USE_QT_MULTIMEDIA)
    target_link_libraries(citron PRIVATE Qt6::Multimedia)
    target_compile_definitions(citron PRIVATE -DCITRON_USE_QT_MULTIMEDIA)
endif ()

if (CITRON_USE_QT_WEB_ENGINE)
    target_link_libraries(citron PRIVATE Qt6::WebEngineCore Qt6::WebEngineWidgets)
    target_compile_definitions(citron PRIVATE -DCITRON_USE_QT_WEB_ENGINE)
endif ()

if (CITRON_ENABLE_PGO_USE)
    target_compile_definitions(citron PRIVATE -DCITRON_ENABLE_PGO_USE)
endif()

if(UNIX AND NOT APPLE)
    install(TARGETS citron)
endif()

if (WIN32 AND QT_VERSION VERSION_GREATER_EQUAL 6)
    set(CITRON_EXE_DIR "$<TARGET_FILE_DIR:citron>")
    add_custom_command(TARGET citron POST_BUILD COMMAND ${WINDEPLOYQT_EXECUTABLE} "${CITRON_EXE_DIR}/citron.exe" --dir "${CITRON_EXE_DIR}" --libdir "${CITRON_EXE_DIR}" --plugindir "${CITRON_EXE_DIR}/plugins" --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --no-translations --verbose 0)

    # Create the "user" directory for portable mode
    add_custom_command(TARGET citron POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CITRON_EXE_DIR}/user"
        COMMENT "Creating portable user directory"
    )
endif()

if (CITRON_USE_BUNDLED_QT)
    include(CopyCitronQt6Deps)
    copy_citron_Qt6_deps(citron)
endif()

if (ENABLE_SDL2)
    target_link_libraries(citron PRIVATE SDL2::SDL2)
    target_compile_definitions(citron PRIVATE HAVE_SDL2)
endif()

if (MSVC)
    include(CopyCitronSDLDeps)
    include(CopyCitronFFmpegDeps)
    copy_citron_SDL_deps(citron)
    copy_citron_FFmpeg_deps(citron)
endif()

if (NOT APPLE AND ENABLE_OPENGL)
    target_compile_definitions(citron PRIVATE HAS_OPENGL)
endif()

if (ARCHITECTURE_x86_64 OR ARCHITECTURE_arm64)
    target_link_libraries(citron PRIVATE dynarmic::dynarmic)
endif()

if (CITRON_USE_PRECOMPILED_HEADERS)
    # Disable PCH for MSVC 14.44+ due to __builtin_assume_aligned conflict
    if (NOT (MSVC AND MSVC_VERSION GREATER_EQUAL 1944))
        target_precompile_headers(citron PRIVATE precompiled_headers.h)
    else()
        message(STATUS "Precompiled headers disabled for citron due to MSVC 14.44+ compatibility issue")
    endif()
endif()

create_target_directory_groups(citron)
