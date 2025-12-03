# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-FileCopyrightText: 2025 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.22)

project(citron)

# Define the build type, defaulting to "Nightly" for development and CI builds.
set(CITRON_BUILD_TYPE "Stable" CACHE STRING "The build type for the emulator (e.g., Stable, Nightly)")

# For cmake-gui, this creates a dropdown to select the build type.
set_property(CACHE CITRON_BUILD_TYPE PROPERTY STRINGS Stable Nightly)

# Pass the build type to the C++ source code as a preprocessor definition.
add_definitions("-DCITRON_BUILD_TYPE=\"${CITRON_BUILD_TYPE}\"")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/externals/cmake-modules")

include(DownloadExternals)
include(CMakeDependentOption)
include(CTest)
include(PGO)

# Disable Warnings as Errors for MSVC
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /WX-")
    # Add specific warning suppressions for Qt6 compatibility
    add_compile_options(/wd4996)  # Suppress deprecated function warnings
    add_compile_options(/wd4267)  # Suppress size_t conversion warnings
endif()

# Check if SDL2::SDL2 target exists; if not, create an alias
if (TARGET SDL2::SDL2-static)
    add_library(SDL2::SDL2 ALIAS SDL2::SDL2-static)
elseif (TARGET SDL2::SDL2-shared)
    add_library(SDL2::SDL2 ALIAS SDL2::SDL2-shared)
endif()

# Set bundled sdl2/qt as dependent options.
option(ENABLE_SDL2 "Enable the SDL2 frontend" ON)
CMAKE_DEPENDENT_OPTION(CITRON_USE_BUNDLED_SDL2 "Download bundled SDL2 binaries" ON "ENABLE_SDL2;MSVC" OFF)
CMAKE_DEPENDENT_OPTION(CITRON_USE_EXTERNAL_SDL2 "Compile external SDL2" ON "ENABLE_SDL2;NOT MSVC" OFF)

cmake_dependent_option(ENABLE_LIBUSB "Enable the use of LibUSB" ON "NOT ANDROID" OFF)

option(ENABLE_OPENGL "Enable OpenGL" ON)
mark_as_advanced(FORCE ENABLE_OPENGL)
option(ENABLE_QT "Enable the Qt frontend" ON)

option(ENABLE_QT_TRANSLATION "Enable translations for the Qt frontend" OFF)
CMAKE_DEPENDENT_OPTION(CITRON_USE_BUNDLED_QT "Download bundled Qt binaries" "${MSVC}" "ENABLE_QT" OFF)

option(ENABLE_WEB_SERVICE "Enable web services (telemetry, etc.)" ON)

option(CITRON_USE_BUNDLED_FFMPEG "Download/Build bundled FFmpeg" "${WIN32}")

option(CITRON_USE_EXTERNAL_VULKAN_HEADERS "Use Vulkan-Headers from externals" ON)

option(CITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES "Use Vulkan-Utility-Libraries from externals" ON)

option(CITRON_USE_QT_MULTIMEDIA "Use QtMultimedia for Camera" OFF)

option(CITRON_USE_QT_WEB_ENGINE "Use QtWebEngine for web applet implementation" OFF)

option(CITRON_USE_AUTO_UPDATER "Enable automatic updater functionality" OFF)

option(ENABLE_CUBEB "Enables the cubeb audio backend" ON)

option(ENABLE_OPENAL "Enables the OpenAL audio backend" ON)

option(USE_DISCORD_PRESENCE "Enables Discord Rich Presence" OFF)

option(CITRON_TESTS "Compile tests" "${BUILD_TESTING}")

option(CITRON_USE_PRECOMPILED_HEADERS "Use precompiled headers" ON)

option(CITRON_DOWNLOAD_ANDROID_VVL "Download validation layer binary for android" ON)

CMAKE_DEPENDENT_OPTION(CITRON_ROOM "Compile LDN room server" ON "NOT ANDROID" OFF)

CMAKE_DEPENDENT_OPTION(CITRON_CRASH_DUMPS "Compile crash dump (Minidump) support" OFF "WIN32 OR LINUX" OFF)

option(CITRON_USE_BUNDLED_VCPKG "Use vcpkg for citron dependencies" "${MSVC}")

option(CITRON_CHECK_SUBMODULES "Check if submodules are present" ON)

option(CITRON_ENABLE_LTO "Enable link-time optimization" OFF)

option(CITRON_ENABLE_PGO_GENERATE "Build with PGO instrumentation to generate profile data (Stage 1)" OFF)
option(CITRON_ENABLE_PGO_USE "Build using PGO profile data for optimization (Stage 2)" OFF)
set(CITRON_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles" CACHE PATH "Directory to store PGO profile data")

option(CITRON_DOWNLOAD_TIME_ZONE_DATA "Always download time zone binaries" OFF)

option(CITRON_ENABLE_PORTABLE "Allow citron to enable portable mode if a user folder is found in the CWD" ON)

CMAKE_DEPENDENT_OPTION(CITRON_USE_FASTER_LD "Check if a faster linker is available" ON "NOT WIN32" OFF)

CMAKE_DEPENDENT_OPTION(USE_SYSTEM_MOLTENVK "Use the system MoltenVK lib (instead of the bundled one)" OFF "APPLE" OFF)

set(DEFAULT_ENABLE_OPENSSL ON)
if (ANDROID OR WIN32 OR APPLE)
    set(DEFAULT_ENABLE_OPENSSL OFF)
endif()
option(ENABLE_OPENSSL "Enable OpenSSL backend for ISslConnection" ${DEFAULT_ENABLE_OPENSSL})

if (ANDROID AND CITRON_DOWNLOAD_ANDROID_VVL)
    set(vvl_version "vulkan-sdk-1.4.328.1")
    set(vvl_version_plain "1.4.328.1")
    set(vvl_zip_file "${CMAKE_BINARY_DIR}/externals/vvl-android.zip")
    set(vvl_extract_dir "${CMAKE_BINARY_DIR}/externals")

    if (NOT EXISTS "${vvl_zip_file}")
        # Download and extract validation layer release to externals directory
        set(vvl_base_url "https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download")
        file(DOWNLOAD "${vvl_base_url}/${vvl_version}/android-binaries-${vvl_version_plain}.zip"
            "${vvl_zip_file}" SHOW_PROGRESS)
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${vvl_zip_file}"
            WORKING_DIRECTORY "${vvl_extract_dir}")
    endif()

    # Copy the arm64 binary to src/android/app/main/jniLibs
    # New structure: android-binaries-X.X.X.X/android-binaries-X.X.X.X/arm64-v8a/libVkLayer_khronos_validation.so
    set(vvl_lib_path "${CMAKE_CURRENT_SOURCE_DIR}/src/android/app/src/main/jniLibs/arm64-v8a/")
    set(vvl_source_file "${vvl_extract_dir}/android-binaries-${vvl_version_plain}/android-binaries-${vvl_version_plain}/arm64-v8a/libVkLayer_khronos_validation.so")

    if (EXISTS "${vvl_source_file}")
        file(COPY "${vvl_source_file}" DESTINATION "${vvl_lib_path}")
        message(STATUS "Successfully copied VVL ${vvl_version_plain} to ${vvl_lib_path}")
    else()
        message(WARNING "Could not find Vulkan Validation Layer at: ${vvl_source_file}")
        message(STATUS "Please manually download from: ${vvl_base_url}/${vvl_version}/android-binaries-${vvl_version_plain}.zip")
    endif()
endif()

if (ANDROID)
    set(CMAKE_SKIP_INSTALL_RULES ON)
endif()

if (CITRON_USE_BUNDLED_VCPKG)
    if (ANDROID)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
        list(APPEND VCPKG_MANIFEST_FEATURES "android")

        if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
            set(VCPKG_TARGET_TRIPLET "arm64-android")
            # this is to avoid CMake using the host pkg-config to find the host
            # libraries when building for Android targets
            set(PKG_CONFIG_EXECUTABLE "aarch64-none-linux-android-pkg-config" CACHE FILEPATH "" FORCE)
        elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
            set(VCPKG_TARGET_TRIPLET "x64-android")
            set(PKG_CONFIG_EXECUTABLE "x86_64-none-linux-android-pkg-config" CACHE FILEPATH "" FORCE)
        else()
            message(FATAL_ERROR "Unsupported Android architecture ${CMAKE_ANDROID_ARCH_ABI}")
        endif()
    endif()

    if (MSVC)
        set(VCPKG_DOWNLOADS_PATH ${PROJECT_SOURCE_DIR}/externals/vcpkg/downloads)
        set(NASM_VERSION "2.16.03")
        set(NASM_DESTINATION_PATH ${VCPKG_DOWNLOADS_PATH}/nasm-${NASM_VERSION}-win64.zip)
        set(NASM_DOWNLOAD_URL "https://www.nasm.us/pub/nasm/releasebuilds/${NASM_VERSION}/win64/nasm-${NASM_VERSION}-win64.zip")

        if (NOT EXISTS ${NASM_DESTINATION_PATH})
            file(DOWNLOAD ${NASM_DOWNLOAD_URL} ${NASM_DESTINATION_PATH} SHOW_PROGRESS STATUS NASM_STATUS)

            if (NOT NASM_STATUS EQUAL 0)
                # Warn and not fail since vcpkg is supposed to download this package for us in the first place
                message(WARNING "External nasm vcpkg package download from ${NASM_DOWNLOAD_URL} failed with status ${NASM_STATUS}")
            endif()
        endif()
    endif()

    if (CITRON_TESTS)
        list(APPEND VCPKG_MANIFEST_FEATURES "citron-tests")
    endif()
    if (ENABLE_WEB_SERVICE)
        list(APPEND VCPKG_MANIFEST_FEATURES "web-service")
    endif()
    if (ANDROID)
        list(APPEND VCPKG_MANIFEST_FEATURES "android")
    endif()

    include(${CMAKE_SOURCE_DIR}/externals/vcpkg/scripts/buildsystems/vcpkg.cmake)
elseif(NOT "$ENV{VCPKG_TOOLCHAIN_FILE}" STREQUAL "")
    # Disable manifest mode (use vcpkg classic mode) when using a custom vcpkg installation
    option(VCPKG_MANIFEST_MODE "")
    include("$ENV{VCPKG_TOOLCHAIN_FILE}")
endif()

if (CITRON_USE_PRECOMPILED_HEADERS)
    if (MSVC AND CCACHE)
        # buildcache does not properly cache PCH files, leading to compilation errors.
        # See https://github.com/mbitsnbites/buildcache/discussions/230
        message(WARNING "buildcache does not properly support Precompiled Headers. Disabling PCH")
        set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
        set(CITRON_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
    endif()
endif()
if (CITRON_USE_PRECOMPILED_HEADERS)
    message(STATUS "Using Precompiled Headers.")
    set(CMAKE_PCH_INSTANTIATE_TEMPLATES ON)
endif()


# Default to a Release build
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    message(STATUS "Defaulting to a Release build")
endif()

if(EXISTS ${PROJECT_SOURCE_DIR}/hooks/pre-commit AND NOT EXISTS ${PROJECT_SOURCE_DIR}/.git/hooks/pre-commit)
    if (EXISTS ${PROJECT_SOURCE_DIR}/.git/)
        message(STATUS "Copying pre-commit hook")
        file(COPY hooks/pre-commit DESTINATION ${PROJECT_SOURCE_DIR}/.git/hooks)
    endif()
endif()

# Sanity check : Check that all submodules are present
# =======================================================================

function(check_submodules_present)
    file(READ "${PROJECT_SOURCE_DIR}/.gitmodules" gitmodules)
    string(REGEX MATCHALL "path *= *[^ \t\r\n]*" gitmodules ${gitmodules})
    foreach(module ${gitmodules})
        string(REGEX REPLACE "path *= *" "" module ${module})
        if (NOT EXISTS "${PROJECT_SOURCE_DIR}/${module}/.git")
            message(FATAL_ERROR "Git submodule ${module} not found. "
                    "Please run: \ngit submodule update --init --recursive")
        endif()
    endforeach()
endfunction()

if(EXISTS ${PROJECT_SOURCE_DIR}/.gitmodules AND CITRON_CHECK_SUBMODULES)
    check_submodules_present()
endif()
configure_file(${PROJECT_SOURCE_DIR}/dist/compatibility_list/compatibility_list.qrc
               ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.qrc
               COPYONLY)
if (EXISTS ${PROJECT_SOURCE_DIR}/dist/compatibility_list/compatibility_list.json)
    configure_file("${PROJECT_SOURCE_DIR}/dist/compatibility_list/compatibility_list.json"
                   "${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json"
                   COPYONLY)
endif()
if (ENABLE_COMPATIBILITY_LIST_DOWNLOAD AND NOT EXISTS ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json)
    message(STATUS "Downloading compatibility list for citron...")
    file(DOWNLOAD
        https://api.yuzu-citron-emu.org/gamedb/
        "${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json" SHOW_PROGRESS)
endif()
if (NOT EXISTS ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json)
    file(WRITE ${PROJECT_BINARY_DIR}/dist/compatibility_list/compatibility_list.json "")
endif()

# Detect current compilation architecture and create standard definitions
# =======================================================================

include(CheckSymbolExists)
function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

if (NOT ENABLE_GENERIC)
    if (MSVC)
        detect_architecture("_M_AMD64" x86_64)
        detect_architecture("_M_IX86" x86)
        detect_architecture("_M_ARM" arm)
        detect_architecture("_M_ARM64" arm64)
    else()
        detect_architecture("__x86_64__" x86_64)
        detect_architecture("__i386__" x86)
        detect_architecture("__arm__" arm)
        detect_architecture("__aarch64__" arm64)
    endif()
endif()

if (NOT DEFINED ARCHITECTURE)
    set(ARCHITECTURE "GENERIC")
    set(ARCHITECTURE_GENERIC 1)
    add_definitions(-DARCHITECTURE_GENERIC=1)
endif()
message(STATUS "Target architecture: ${ARCHITECTURE}")

if (UNIX)
    add_definitions(-DCITRON_UNIX=1)
endif()

if (ARCHITECTURE_arm64 AND (ANDROID OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux"))
    set(HAS_NCE 1)
    add_definitions(-DHAS_NCE=1)
endif()

# Configure C++ standard
# ===========================

# boost asio's concept usage doesn't play nicely with some compilers yet.
add_definitions(-DBOOST_ASIO_DISABLE_CONCEPTS)
if (MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/std:c++20>)

    # boost still makes use of deprecated result_of.
    add_definitions(-D_HAS_DEPRECATED_RESULT_OF)

    # Enable Link Time Code Generation for better optimization
    add_link_options(/LTCG)
else()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Output binaries to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# System imported libraries
# =======================================================================

# Enforce the search mode of non-required packages for better and shorter failure messages

# Find Boost and its components.
# 'process' is optional. The build will continue if it's not found.
find_package(Boost 1.79.0 REQUIRED COMPONENTS context OPTIONAL_COMPONENTS process)

# Check if the optional 'process' component was successfully found.
if(Boost_PROCESS_FOUND)
    # Note: We'll set this definition later when the core target is created
    set(HAS_BOOST_PROCESS_DEFINITION "HAS_BOOST_PROCESS")
    message(STATUS "Found optional Boost.Process, enabling advanced debugger pipe.")
else()
    # It was not found. This is not an error.
    message(STATUS "Optional Boost.Process not found, using socket-based debugger pipe.")
endif()

# Find the Boost package with our dynamically built list of required components.
find_package(Boost 1.79.0 REQUIRED ${BOOST_COMPONENTS})
find_package(enet 1.3 MODULE)
find_package(fmt 9 REQUIRED)
find_package(LLVM 17.0.2 MODULE COMPONENTS Demangle)
find_package(lz4 REQUIRED)
find_package(nlohmann_json 3.8 REQUIRED)
find_package(Opus 1.3 MODULE)
find_package(RenderDoc MODULE)
find_package(stb MODULE)
find_package(VulkanMemoryAllocator CONFIG)
find_package(ZLIB 1.2 REQUIRED)
find_package(zstd 1.5 REQUIRED)

if (NOT CITRON_USE_EXTERNAL_VULKAN_HEADERS)
    find_package(VulkanHeaders 1.3.274 REQUIRED)
endif()

if (NOT CITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES)
    find_package(VulkanUtilityLibraries REQUIRED)
endif()

if (ENABLE_LIBUSB)
    find_package(libusb 1.0.24 MODULE)
endif()

if (ARCHITECTURE_x86 OR ARCHITECTURE_x86_64)
    find_package(xbyak 6 CONFIG)
endif()

if (ARCHITECTURE_arm64)
    find_package(oaknut 2.0.1 CONFIG)
endif()

# We create a variable to remember if dynarmic was found
set(CITRON_HAS_DYNARMIC FALSE)

if ((ARCHITECTURE_x86_64 OR ARCHITECTURE_arm64) AND NOT (MSVC AND ARCHITECTURE_arm64))
    find_package(dynarmic 6.4.0 CONFIG)
    if(dynarmic_FOUND)
        message(STATUS "Dynarmic found, enabling dynamic recompilation support.")
        set(CITRON_HAS_DYNARMIC TRUE) # Set our variable to TRUE
    else()
        message(STATUS "Dynarmic not found or disabled for this architecture, skipping.")
    endif()
endif()

if (ENABLE_CUBEB)
    find_package(cubeb CONFIG)
endif()

if (USE_DISCORD_PRESENCE)
    find_package(DiscordRPC MODULE)
endif()

if (ENABLE_WEB_SERVICE)
    find_package(cpp-jwt 1.4 CONFIG)
    find_package(httplib 0.12 MODULE COMPONENTS OpenSSL)
endif()

if (CITRON_TESTS)
    find_package(Catch2 3.0.1 REQUIRED)
endif()

# boost:asio has functions that require AcceptEx et al
if (MINGW)
    find_library(MSWSOCK_LIBRARY mswsock REQUIRED)
endif()

if(ENABLE_OPENSSL)
    find_package(OpenSSL 1.1.1 REQUIRED)
endif()

if (UNIX AND NOT APPLE)
    find_package(gamemode 1.7 MODULE)
endif()

add_subdirectory(externals)
if(USE_DISCORD_PRESENCE)
    message(STATUS "Applying custom patch to submodule's rapidjson for Discord presence...")

    # Define the path to the patch file and the target directory
    set(PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/rapidjson-compiler-fix.patch")
    set(PATCH_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/externals/discord-rpc/thirdparty/rapidjson-1.1.0")

    # Check if the target directory exists before trying to patch
    if(EXISTS "${PATCH_TARGET_DIR}")
        execute_process(
            COMMAND git apply --ignore-whitespace "${PATCH_FILE}"
            WORKING_DIRECTORY "${PATCH_TARGET_DIR}"
            RESULT_VARIABLE PATCH_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT PATCH_RESULT EQUAL 0)
            message(WARNING "Failed to apply rapidjson compiler fix patch! This might be okay if it's already applied.")
        endif()
    else()
        message(WARNING "Could not find rapidjson directory to patch. It may not have been downloaded yet.")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Applying custom patch to submodule's mcl for Clang compatibility...")

    set(MCL_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/mcl_clang_template_fix.patch")
    set(MCL_PATCH_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/externals/dynarmic/externals/mcl")

    if(EXISTS "${MCL_PATCH_TARGET_DIR}")
        execute_process(
            COMMAND git apply --ignore-whitespace "${MCL_PATCH_FILE}"
            WORKING_DIRECTORY "${MCL_PATCH_TARGET_DIR}"
            RESULT_VARIABLE MCL_PATCH_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT MCL_PATCH_RESULT EQUAL 0)
            message(WARNING "Failed to apply mcl Clang compatibility patch! This might be okay if it's already applied.")
        endif()
    else()
        message(WARNING "Could not find mcl directory to patch. It may not have been downloaded yet.")
    endif()
endif()

# Apply a patch to stb_image.h to fix a string overflow warning with modern compilers
message(STATUS "Applying custom patch to stb_image.h...")

set(STB_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/stb_image-overflow-fix.patch")

set(STB_PATCH_WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Check if the file to be patched exists before trying to patch
if(EXISTS "${STB_PATCH_WORKING_DIR}/externals/stb/stb_image.h")
    execute_process(
        COMMAND git apply -p0 --ignore-whitespace "${STB_PATCH_FILE}"
        WORKING_DIRECTORY "${STB_PATCH_WORKING_DIR}"
        RESULT_VARIABLE PATCH_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT PATCH_RESULT EQUAL 0)
        message(WARNING "Failed to apply stb_image.h compiler fix patch! This might be okay if it's already applied.")
    endif()
else()
    message(WARNING "Could not find stb_image.h to patch. It may not have been downloaded yet.")
endif()

# Apply a patch to discord-rpc's CMakeLists.txt to suppress -Wclass-memaccess
message(STATUS "Applying custom patch to submodule's discord-rpc CMakeLists.txt...")

# Define the path to the patch file and the target directory using unique names
set(DISCORD_RPC_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/patches/discord-rpc-wclass-memaccess-fix.patch")
set(DISCORD_RPC_PATCH_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/externals/discord-rpc")

# Check if the target directory exists before trying to patch
if(EXISTS "${DISCORD_RPC_PATCH_TARGET_DIR}")
    execute_process(
        COMMAND git apply -p0 --ignore-whitespace "${DISCORD_RPC_PATCH_FILE}"
        WORKING_DIRECTORY "${DISCORD_RPC_PATCH_TARGET_DIR}"
        RESULT_VARIABLE PATCH_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT PATCH_RESULT EQUAL 0)
        message(WARNING "Failed to apply discord-rpc CMakeLists.txt patch! This might be okay if it's already applied.")
    endif()
else()
    message(WARNING "Could not find discord-rpc directory to patch. It may not have been downloaded yet.")
endif()

if (ENABLE_QT)
    if (NOT USE_SYSTEM_QT)
        download_qt(6.7.3)
    endif()

    find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Concurrent)

    if (UNIX AND NOT APPLE)
        find_package(Qt6 REQUIRED COMPONENTS DBus)
    endif()

    if (ENABLE_QT_TRANSLATION)
        find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
    endif()

    if (NOT DEFINED QT_TARGET_PATH)
        # Try to find Qt6 installation path
        if (EXISTS "${CMAKE_BINARY_DIR}/externals/qt/6.8.3/msvc2019_64")
            set(QT_TARGET_PATH "${CMAKE_BINARY_DIR}/externals/qt/6.8.3/msvc2019_64")
        elseif (EXISTS "${CMAKE_BINARY_DIR}/externals/qt/6.7.3/msvc2019_64")
            set(QT_TARGET_PATH "${CMAKE_BINARY_DIR}/externals/qt/6.7.3/msvc2019_64")
        else()
            get_target_property(qtcore_path Qt6::Core LOCATION_Release)
            string(FIND "${qtcore_path}" "/bin/" qtcore_path_bin_pos REVERSE)
            string(FIND "${qtcore_path}" "/lib/" qtcore_path_lib_pos REVERSE)
            if (qtcore_path_bin_pos GREATER qtcore_path_lib_pos)
                string(SUBSTRING "${qtcore_path}" 0 ${qtcore_path_bin_pos} QT_TARGET_PATH)
            else()
                string(SUBSTRING "${qtcore_path}" 0 ${qtcore_path_lib_pos} QT_TARGET_PATH)
            endif()
        endif()
    endif()

    if (NOT DEFINED QT_HOST_PATH)
        set(QT_HOST_PATH "${QT_TARGET_PATH}")
    endif()

    # Add Qt path to CMAKE_PREFIX_PATH to help find Qt6
    if (QT_TARGET_PATH)
        list(APPEND CMAKE_PREFIX_PATH "${QT_TARGET_PATH}")
    endif()

    message(STATUS "Using target Qt at ${QT_TARGET_PATH}")
    message(STATUS "Using host Qt at ${QT_HOST_PATH}")
endif()

function(set_citron_qt_components)
# Best practice is to ask for all components at once, so they are from the same version
set(CITRON_QT_COMPONENTS2 Core Widgets Concurrent)
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
list(APPEND CITRON_QT_COMPONENTS2 DBus GuiPrivate)
endif()
if (CITRON_USE_QT_MULTIMEDIA)
list(APPEND CITRON_QT_COMPONENTS2 Multimedia)
    endif()
    if (CITRON_USE_QT_WEB_ENGINE)
    list(APPEND CITRON_QT_COMPONENTS2 WebEngineCore WebEngineWidgets)
    endif()
    if (ENABLE_QT_TRANSLATION)
    list(APPEND CITRON_QT_COMPONENTS2 LinguistTools)
    endif()
    if (USE_DISCORD_PRESENCE)
    list(APPEND CITRON_QT_COMPONENTS2 Network)
    endif()
    set(CITRON_QT_COMPONENTS ${CITRON_QT_COMPONENTS2} PARENT_SCOPE)
    endfunction(set_citron_qt_components)

# find SDL2 exports a bunch of variables that are needed, so its easier to do this outside of the citron_find_package
if (ENABLE_SDL2)
    if (CITRON_USE_BUNDLED_SDL2)
        # Detect toolchain and platform
        if (MSVC_VERSION GREATER_EQUAL 1920 AND ARCHITECTURE_x86_64)
            set(SDL2_VER "SDL2-2.28.2")
            set(SDL2_ARCH_DIR "x64")
        elseif (MSVC_VERSION GREATER_EQUAL 1920 AND ARCHITECTURE_arm64)
            set(SDL2_VER "SDL2-2.28.2")
            set(SDL2_ARCH_DIR "arm64")
        else()
            message(FATAL_ERROR "No bundled SDL2 binaries for your toolchain. Disable CITRON_USE_BUNDLED_SDL2 and provide your own.")
        endif()

        if (DEFINED SDL2_VER)
            download_bundled_external("sdl2/" ${SDL2_VER} SDL2_PREFIX)
        endif()

        set(SDL2_FOUND YES)
        set(SDL2_INCLUDE_DIR "${SDL2_PREFIX}/include" CACHE PATH "Path to SDL2 headers")
        set(SDL2_LIBRARY "${SDL2_PREFIX}/lib/${SDL2_ARCH_DIR}/SDL2.lib" CACHE PATH "Path to SDL2 library")
        set(SDL2MAIN_LIBRARY "${SDL2_PREFIX}/lib/${SDL2_ARCH_DIR}/SDL2main.lib" CACHE PATH "Path to SDL2main library")
        set(SDL2_DLL_DIR "${SDL2_PREFIX}/lib/${SDL2_ARCH_DIR}/" CACHE PATH "Path to SDL2.dll")

        add_library(SDL2::SDL2 INTERFACE IMPORTED)
        target_link_libraries(SDL2::SDL2 INTERFACE "${SDL2_LIBRARY}" "${SDL2MAIN_LIBRARY}")
        target_include_directories(SDL2::SDL2 INTERFACE "${SDL2_INCLUDE_DIR}")
    elseif (CITRON_USE_EXTERNAL_SDL2)
        message(STATUS "Using SDL2 from externals.")
    else()
        find_package(SDL2 2.26.4 REQUIRED)
    endif()
endif()

# List of all FFmpeg components required
set(FFmpeg_COMPONENTS
    avcodec
    avfilter
    avutil
    swscale)

if (UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBVA libva)
endif()
if (NOT CITRON_USE_BUNDLED_FFMPEG)
    # Use system installed FFmpeg
    find_package(FFmpeg 4.3 REQUIRED QUIET COMPONENTS ${FFmpeg_COMPONENTS})
endif()

if(ENABLE_QT)
    set_citron_qt_components()
    find_package(Qt6 REQUIRED COMPONENTS ${CITRON_QT_COMPONENTS})
    set(QT_MAJOR_VERSION 6)
    # Qt6 sets cxx_std_17 and we need to undo that
    set_target_properties(Qt6::Platform PROPERTIES INTERFACE_COMPILE_FEATURES "")
endif()

if (WIN32 AND CITRON_CRASH_DUMPS)
    set(BREAKPAD_VER "breakpad-c89f9dd")
    download_bundled_external("breakpad/" ${BREAKPAD_VER} BREAKPAD_PREFIX)

    set(BREAKPAD_CLIENT_INCLUDE_DIR "${BREAKPAD_PREFIX}/include")
    set(BREAKPAD_CLIENT_LIBRARY "${BREAKPAD_PREFIX}/lib/libbreakpad_client.lib")

    add_library(libbreakpad_client INTERFACE IMPORTED)
    target_link_libraries(libbreakpad_client INTERFACE "${BREAKPAD_CLIENT_LIBRARY}")
    target_include_directories(libbreakpad_client INTERFACE "${BREAKPAD_CLIENT_INCLUDE_DIR}")
endif()

# Prefer the -pthread flag on Linux.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Platform-specific library requirements
# ======================================

if (APPLE)
    # Umbrella framework for everything GUI-related
    find_library(COCOA_LIBRARY Cocoa)
    set(PLATFORM_LIBRARIES ${COCOA_LIBRARY} ${IOKIT_LIBRARY} ${COREVIDEO_LIBRARY})
elseif (WIN32)
    # Target Windows 10
    add_definitions(-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00)
    set(PLATFORM_LIBRARIES winmm ws2_32 iphlpapi)
    if (MINGW)
        # PSAPI is the Process Status API
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} psapi imm32 version)
    endif()
elseif (CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU|SunOS)$")
    set(PLATFORM_LIBRARIES rt)
endif()

# Setup a custom clang-format target (if clang-format can be found) that will run
# against all the src files. This should be used before making a pull request.
# =======================================================================

set(CLANG_FORMAT_POSTFIX "-20")
find_program(CLANG_FORMAT
    NAMES clang-format${CLANG_FORMAT_POSTFIX}
          clang-format
    PATHS ${PROJECT_BINARY_DIR}/externals)
# if find_program doesn't find it, try to download from externals
if (NOT CLANG_FORMAT)
    if (WIN32 AND NOT CMAKE_CROSSCOMPILING)
        message(STATUS "Clang format not found! Downloading...")
        set(CLANG_FORMAT "${PROJECT_BINARY_DIR}/externals/clang-format${CLANG_FORMAT_POSTFIX}.exe")
        file(DOWNLOAD
            https://git.citron-emu.org/citron/ext-windows-bin/-/blob/master/clang-format${CLANG_FORMAT_POSTFIX}.exe
            "${CLANG_FORMAT}" SHOW_PROGRESS
            STATUS DOWNLOAD_SUCCESS)
        if (NOT DOWNLOAD_SUCCESS EQUAL 0)
            message(WARNING "Could not download clang format! Disabling the clang format target")
            file(REMOVE ${CLANG_FORMAT})
            unset(CLANG_FORMAT)
        endif()
    else()
        message(WARNING "Clang format not found! Disabling the clang format target")
    endif()
endif()

if (CLANG_FORMAT)
    set(SRCS ${PROJECT_SOURCE_DIR}/src)
    set(CCOMMENT "Running clang format against all the .h and .cpp files in src/")
    if (WIN32)
        add_custom_target(clang-format
            COMMAND powershell.exe -Command "Get-ChildItem '${SRCS}/*' -Include *.cpp,*.h -Recurse | Foreach {&'${CLANG_FORMAT}' -i $_.fullname}"
            COMMENT ${CCOMMENT})
    elseif(MINGW)
        add_custom_target(clang-format
            COMMAND find `cygpath -u ${SRCS}` -iname *.h -o -iname *.cpp | xargs `cygpath -u ${CLANG_FORMAT}` -i
            COMMENT ${CCOMMENT})
    else()
        add_custom_target(clang-format
            COMMAND find ${SRCS} -iname *.h -o -iname *.cpp | xargs ${CLANG_FORMAT} -i
            COMMENT ${CCOMMENT})
    endif()
    unset(SRCS)
    unset(CCOMMENT)
endif()

# Include source code
# ===================

# This function should be passed a list of all files in a target. It will automatically generate
# file groups following the directory hierarchy, so that the layout of the files in IDEs matches the
# one in the filesystem.
function(create_target_directory_groups target_name)
    # Place any files that aren't in the source list in a separate group so that they don't get in
    # the way.
    source_group("Other Files" REGULAR_EXPRESSION ".")

    get_target_property(target_sources "${target_name}" SOURCES)

    foreach(file_name IN LISTS target_sources)
        get_filename_component(dir_name "${file_name}" PATH)
        # Group names use '\' as a separator even though the entire rest of CMake uses '/'...
        string(REPLACE "/" "\\" group_name "${dir_name}")
        source_group("${group_name}" FILES "${file_name}")
    endforeach()
endfunction()

# Helper function to configure LTO for a target with CachyOS-specific compatibility
function(citron_configure_lto target_name)
    if (CITRON_ENABLE_LTO)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

        # Detect CachyOS and apply conservative LTO settings only for CachyOS
        set(IS_CACHYOS FALSE)
        if (EXISTS "/etc/os-release")
            file(READ "/etc/os-release" OS_RELEASE_CONTENT)
            if (OS_RELEASE_CONTENT MATCHES "ID=cachyos")
                set(IS_CACHYOS TRUE)
            endif()
        endif()

        # Alternative detection via kernel name
        if (NOT IS_CACHYOS)
            execute_process(COMMAND uname -r OUTPUT_VARIABLE KERNEL_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (KERNEL_VERSION MATCHES "cachyos")
                set(IS_CACHYOS TRUE)
            endif()
        endif()

        # Apply conservative LTO settings only for CachyOS with GCC 15+
        if (IS_CACHYOS AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15.0")
            message(STATUS "Detected CachyOS with GCC ${CMAKE_CXX_COMPILER_VERSION} - using conservative LTO settings for ${target_name}")
            target_compile_options(${target_name} PRIVATE "-flto=auto" "-ffat-lto-objects")
            target_link_options(${target_name} PRIVATE "-flto=auto" "-ffat-lto-objects")
        endif()
    endif()
endfunction()

# Prevent boost from linking against libs when building
target_link_libraries(Boost::headers INTERFACE Boost::disable_autolinking)
# Adjustments for MSVC + Ninja
if (MSVC AND CMAKE_GENERATOR STREQUAL "Ninja")
    add_compile_options(
        /wd4464 # relative include path contains '..'
        /wd4711 # function 'function' selected for automatic inline expansion
        /wd4820 # 'bytes' bytes padding added after construct 'member_name'
    )
endif()

if (CITRON_USE_FASTER_LD AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    # We will assume that if the compiler is GCC, it will attempt to use ld.bfd by default.
    # Try to pick a faster linker.
    find_program(LLD lld)
    find_program(MOLD mold)

    if (MOLD AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "12.1")
        message(NOTICE "Selecting mold as linker")
        add_link_options("-fuse-ld=mold")
    elseif (LLD)
        message(NOTICE "Selecting lld as linker")
        add_link_options("-fuse-ld=lld")
    endif()
endif()

# Set runtime library to MD/MDd for all configurations
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # Force all projects (including external dependencies) to use the same runtime
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MD")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MDd")

    # Add this to ensure Cubeb uses the same runtime
    add_compile_options(
        $<$<CONFIG:Debug>:/MDd>
        $<$<CONFIG:Release>:/MD>
        $<$<CONFIG:RelWithDebInfo>:/MD>
        $<$<CONFIG:MinSizeRel>:/MD>
    )
endif()

add_subdirectory(src)

# Add the compile definition for Dynarmic
if(CITRON_HAS_DYNARMIC) # This variable is set by the find_package call at the top
    target_compile_definitions(core PRIVATE USE_DYNARMIC)
endif()

# Apply the Boost.Process definition to the core target if it was found
if(DEFINED HAS_BOOST_PROCESS_DEFINITION)
    target_compile_definitions(core PRIVATE ${HAS_BOOST_PROCESS_DEFINITION})
endif()

# Apply PGO configuration to main targets
if(CITRON_ENABLE_PGO_GENERATE OR CITRON_ENABLE_PGO_USE)
    if(TARGET citron)
        citron_configure_pgo(citron)
    endif()
    if(TARGET citron-cmd)
        citron_configure_pgo(citron-cmd)
    endif()
    if(TARGET citron-room)
        citron_configure_pgo(citron-room)
    endif()

    # Print PGO instructions
    citron_print_pgo_instructions()
endif()

# Set citron project or citron-cmd project as default StartUp Project in Visual Studio depending on whether QT is enabled or not
if(ENABLE_QT)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT citron)
else()
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT citron-cmd)
endif()


# Installation instructions
# =========================

# Install freedesktop.org metadata files, following those specifications:
# https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html
# https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html
# https://specifications.freedesktop.org/shared-mime-info-spec/shared-mime-info-spec-latest.html
# https://www.freedesktop.org/software/appstream/docs/
if(ENABLE_QT AND UNIX AND NOT APPLE)
    install(FILES "dist/org.citron_emu.citron.desktop"
            DESTINATION "share/applications")
    install(FILES "dist/citron.svg"
            DESTINATION "share/icons/hicolor/scalable/apps"
            RENAME "org.citron_emu.citron.svg")
    install(FILES "dist/org.citron_emu.citron.xml"
            DESTINATION "share/mime/packages")
    install(FILES "dist/org.citron_emu.citron.metainfo.xml"
            DESTINATION "share/metainfo")
endif()

# Final linker cleanup for Clang LTO.
# This forcefully removes any stray "-fuse-ld=bfd" flags that may have been
# incorrectly added by other parts of the build system.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CITRON_ENABLE_LTO)
    message(STATUS "Performing final linker flag cleanup for Clang LTO...")
    foreach(target citron citron-cmd citron-room)
        if(TARGET ${target})
            get_target_property(link_options ${target} LINK_OPTIONS)
            if(link_options)
                string(REPLACE "-fuse-ld=bfd" "" link_options "${link_options}")
                set_target_properties(${target} PROPERTIES LINK_OPTIONS "${link_options}")
            endif()
        endif()
    endforeach()
endif()
