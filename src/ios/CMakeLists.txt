# SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(citron-ios SHARED
    emu_window/emu_window.cpp
    emu_window/emu_window.h
    jit_icache_callbacks_ios.cpp
    native.cpp
    native.h
)

target_link_libraries(citron-ios PRIVATE
    audio_core
    common
    core
    frontend_common
    input_common
    Vulkan::Headers
    "-framework CoreMedia"
    "-framework CoreVideo"
    "-framework Foundation"
    "-framework IOSurface"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework VideoToolbox"
    "-liconv"
)

if (TARGET dynarmic)
    target_link_libraries(citron-ios PRIVATE dynarmic)
endif()

target_compile_definitions(citron-ios PRIVATE CITRON_IOS=1 VK_USE_PLATFORM_METAL_EXT=1)
target_include_directories(citron-ios PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(citron-ios PROPERTIES
    OUTPUT_NAME "citron_ios"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
)

if (CMAKE_GENERATOR STREQUAL "Xcode")
    enable_language(Swift)

    set(CITRON_IOS_BUNDLE_IDENTIFIER "org.citron.citron-ios" CACHE STRING
        "CitronIOS PRODUCT_BUNDLE_IDENTIFIER. Official id is org.citron.citron-ios (requires that App ID on your Apple Developer team). Override if your team uses another id."
    )
    set(CITRON_IOS_DEVELOPMENT_TEAM "" CACHE STRING
        "Apple Developer Team ID for CitronIOS (10 chars). After setting: re-run cmake, then build from Terminal with: cmake --build . --config Debug -- -allowProvisioningUpdates"
    )

    add_executable(CitronIOS MACOSX_BUNDLE
        App/CitronBridge.swift
        App/CitronIOSApp.swift
        App/ContentView.swift
        App/GameControllerManager.swift
        App/RenderView.swift
    )

    target_link_libraries(CitronIOS PRIVATE
        citron-ios
        "-framework GameController"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework SwiftUI"
        "-framework UIKit"
        "-framework UniformTypeIdentifiers"
    )

    set_target_properties(CitronIOS PROPERTIES COMPILE_OPTIONS "")

    set_target_properties(CitronIOS PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/CitronIOS.entitlements"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
        XCODE_ATTRIBUTE_INFOPLIST_KEY_UIFileSharingEnabled "YES"
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${CITRON_IOS_BUNDLE_IDENTIFIER}"
        XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    )
    if (CITRON_IOS_DEVELOPMENT_TEAM)
        set_target_properties(CitronIOS PROPERTIES
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${CITRON_IOS_DEVELOPMENT_TEAM}"
        )
    endif()
else()
    message(STATUS "Skipping CitronIOS Swift app target: use the Xcode generator for iOS app builds")
endif()

set(CITRON_IOS_DEFAULT_MOLTENVK_XCFRAMEWORK
    "${PROJECT_SOURCE_DIR}/externals/moltenvk-all/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework")
if (EXISTS "${CITRON_IOS_DEFAULT_MOLTENVK_XCFRAMEWORK}")
    set(CITRON_IOS_DEFAULT_MOLTENVK_XCFRAMEWORK_VALUE "${CITRON_IOS_DEFAULT_MOLTENVK_XCFRAMEWORK}")
else()
    set(CITRON_IOS_DEFAULT_MOLTENVK_XCFRAMEWORK_VALUE "")
endif()

set(CITRON_IOS_MOLTENVK_XCFRAMEWORK "${CITRON_IOS_DEFAULT_MOLTENVK_XCFRAMEWORK_VALUE}" CACHE PATH
    "Optional path to MoltenVK.xcframework for the Citron iOS app target")

if (TARGET CitronIOS AND CITRON_IOS_MOLTENVK_XCFRAMEWORK)
    target_link_libraries(CitronIOS PRIVATE "${CITRON_IOS_MOLTENVK_XCFRAMEWORK}")

    if (CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(CITRON_IOS_MOLTENVK_FRAMEWORK_SLICE
            "${CITRON_IOS_MOLTENVK_XCFRAMEWORK}/ios-arm64_x86_64-simulator/MoltenVK.framework")
    else()
        set(CITRON_IOS_MOLTENVK_FRAMEWORK_SLICE
            "${CITRON_IOS_MOLTENVK_XCFRAMEWORK}/ios-arm64/MoltenVK.framework")
    endif()

    add_custom_command(TARGET CitronIOS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:CitronIOS>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CITRON_IOS_MOLTENVK_FRAMEWORK_SLICE}"
            "$<TARGET_BUNDLE_DIR:CitronIOS>/Frameworks/MoltenVK.framework"
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/codesign_embedded.sh"
            "$<TARGET_BUNDLE_DIR:CitronIOS>/Frameworks/MoltenVK.framework"
        COMMENT "Embedding MoltenVK.framework")
endif()

if (TARGET CitronIOS)
    add_custom_command(TARGET CitronIOS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:CitronIOS>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:citron-ios>"
            "$<TARGET_BUNDLE_DIR:CitronIOS>/Frameworks/$<TARGET_FILE_NAME:citron-ios>"
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/codesign_embedded.sh"
            "$<TARGET_BUNDLE_DIR:CitronIOS>/Frameworks/$<TARGET_FILE_NAME:citron-ios>"
        COMMENT "Embedding citron-ios native library")
endif()
