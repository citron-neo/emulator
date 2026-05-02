# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# CPM-managed dependencies for Clangtron (Clang cross-compile) builds.
# This file is included ONLY when CITRON_USE_CPM=ON.
# It replaces git submodules with CPMAddPackage() calls, fetching sources
# at CMake configure time into CPM_SOURCE_CACHE.
#
# Packages NOT managed here:
#   - vcpkg  (submodule; used by MSVC/Linux/macOS/Android builds via CITRON_USE_BUNDLED_VCPKG.
#             Disabled in Clangtron builds: CITRON_USE_CPM=ON sets VCPKG_MANIFEST_MODE=OFF.)
#
# Special handling:
#   - ffmpeg (source downloaded by build script from ffmpeg.org, built static
#             with autotools before cmake runs; CPM entry for reference only)
#   - Qt 6.9.3 (pre-built binaries via aqt, managed in CMakeModules/qt_download.cmake)
#
# Static linking: BUILD_SHARED_LIBS is forced OFF in externals/CMakeLists.txt;
# CPM packages inherit this. All dependencies link statically.

# Ensure CPM is available
include(TzdbWindowsFix)
if (NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake not loaded — include CMakeModules/CPM.cmake before this file")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 0 — vcpkg replacement packages
# ═══════════════════════════════════════════════════════════════════════════════
# These packages replace what vcpkg used to provide.  When CITRON_USE_CPM is ON,
# VCPKG_MANIFEST_MODE is set to OFF so vcpkg doesn't auto-install from vcpkg.json.

# ── Boost (CMake variant, header-only + context lib) ──────────────────────────
# Boost 1.86+ has native CMake support.  BOOST_INCLUDE_LIBRARIES restricts which
# sub-libraries are built, keeping compile time short.
if (NOT TARGET Boost::headers)
    set(BOOST_INCLUDE_LIBRARIES "algorithm;asio;container;context;crc;heap;icl;intrusive;process;range;spirit;test;timer;variant" CACHE STRING "Boost components to build")
    set(BOOST_ENABLE_CMAKE ON CACHE BOOL "Enable Boost CMake")
    set(BUILD_TESTING OFF CACHE BOOL "Disable testing")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Disable shared libs")
    CPMAddPackage(
        NAME Boost
        URL "https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz"
    )
    if (TARGET Boost::headers)
        set(_boost_headers_target "Boost::headers")
        get_target_property(_boost_headers_aliased "${_boost_headers_target}" ALIASED_TARGET)
        if (_boost_headers_aliased)
            set(_boost_headers_target "${_boost_headers_aliased}")
        endif()
        file(GLOB _boost_header_include_dirs LIST_DIRECTORIES true
            "${Boost_SOURCE_DIR}/libs/*/include")
        list(REMOVE_DUPLICATES _boost_header_include_dirs)
        if (_boost_header_include_dirs)
            target_include_directories("${_boost_headers_target}" SYSTEM INTERFACE ${_boost_header_include_dirs})
        endif()
    endif()
endif()

# ── fmt ───────────────────────────────────────────────────────────────────────
if (NOT TARGET fmt::fmt)
    CPMAddPackage(
        NAME fmt
        GITHUB_REPOSITORY fmtlib/fmt
        GIT_TAG 12.1.0
        OPTIONS "FMT_INSTALL OFF"
    )
endif()

# ── lz4 ──────────────────────────────────────────────────────────────────────
if (NOT TARGET lz4::lz4)
    CPMAddPackage(
        NAME lz4
        GITHUB_REPOSITORY lz4/lz4
        GIT_TAG v1.10.0
        SOURCE_SUBDIR build/cmake
        OPTIONS
            "LZ4_BUILD_CLI OFF"
            "LZ4_BUILD_LEGACY_LZ4C OFF"
            "BUILD_SHARED_LIBS OFF"
            "BUILD_STATIC_LIBS ON"
    )
    # lz4 cmake creates lz4_static target; alias to lz4::lz4 for find_package compat
    if (TARGET lz4_static AND NOT TARGET lz4::lz4)
        add_library(lz4::lz4 ALIAS lz4_static)
    endif()
endif()

# ── nlohmann-json (header-only) ───────────────────────────────────────────────
if (NOT TARGET nlohmann_json::nlohmann_json)
    CPMAddPackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY nlohmann/json
        GIT_TAG v3.11.3
        OPTIONS "JSON_BuildTests OFF"
    )
endif()

# ── zlib ──────────────────────────────────────────────────────────────────────
if (NOT TARGET ZLIB::ZLIB)
    CPMAddPackage(
        NAME ZLIB
        GITHUB_REPOSITORY madler/zlib
        GIT_TAG v1.3.1
        OPTIONS "ZLIB_BUILD_EXAMPLES OFF"
    )
    if (TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        # zlib cmake creates zlibstatic; alias for find_package(ZLIB) compat
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
        set(ZLIB_INCLUDE_DIRS "${ZLIB_SOURCE_DIR};${ZLIB_BINARY_DIR}" CACHE PATH "" FORCE)
    endif()
endif()

# ── zstd ──────────────────────────────────────────────────────────────────────
if (NOT TARGET zstd::libzstd_static)
    CPMAddPackage(
        NAME zstd
        GITHUB_REPOSITORY facebook/zstd
        GIT_TAG v1.5.6
        SOURCE_SUBDIR build/cmake
        OPTIONS
            "ZSTD_BUILD_PROGRAMS OFF"
            "ZSTD_BUILD_SHARED OFF"
            "ZSTD_BUILD_STATIC ON"
            "ZSTD_BUILD_TESTS OFF"
    )
    if (TARGET libzstd_static AND NOT TARGET zstd::libzstd_static)
        add_library(zstd::libzstd_static ALIAS libzstd_static)
        add_library(zstd::zstd ALIAS libzstd_static)
        set(zstd_FOUND TRUE CACHE BOOL "" FORCE)
    endif()
endif()

# ── OpenAL Soft ───────────────────────────────────────────────────────────────
if (NOT TARGET OpenAL::OpenAL)
    CPMAddPackage(
        NAME OpenAL
        GITHUB_REPOSITORY kcat/openal-soft
        GIT_TAG 1.24.3
        OPTIONS
            "ALSOFT_UTILS OFF"
            "ALSOFT_EXAMPLES OFF"
            "ALSOFT_TESTS OFF"
            "ALSOFT_INSTALL OFF"
            "ALSOFT_INSTALL_CONFIG OFF"
            "LIBTYPE STATIC"
    )
    if (TARGET OpenAL AND NOT TARGET OpenAL::OpenAL)
        add_library(OpenAL::OpenAL ALIAS OpenAL)
    endif()
    # Patch OpenAL's vendored fmt to fix undeclared 'malloc' on some toolchains
    if (OpenAL_ADDED AND EXISTS "${OpenAL_SOURCE_DIR}/fmt-11.1.1/include/fmt/format.h")
        file(READ "${OpenAL_SOURCE_DIR}/fmt-11.1.1/include/fmt/format.h" _fmt_content)
        if (NOT _fmt_content MATCHES "#include <stdlib.h>")
            string(REPLACE "#include \"base.h\"" "#include \"base.h\"\n#include <stdlib.h>" _fmt_content "${_fmt_content}")
            file(WRITE "${OpenAL_SOURCE_DIR}/fmt-11.1.1/include/fmt/format.h" "${_fmt_content}")
        endif()
    endif()
endif()

# ── OpenSSL ───────────────────────────────────────────────────────────────────
# OpenSSL doesn't use CMake natively. We build it from source using a cmake
# wrapper module for cross-compilation with llvm-mingw.
if (ENABLE_OPENSSL OR ENABLE_WEB_SERVICE)
    include(${CMAKE_SOURCE_DIR}/CMakeModules/openssl_build.cmake)
endif()


# Phase 1 — Header-only / trivial packages
# ═══════════════════════════════════════════════════════════════════════════════

# ── unordered_dense ───────────────────────────────────────────────────────────
CPMAddPackage(
    NAME unordered_dense
    GITHUB_REPOSITORY martinus/unordered_dense
    GIT_TAG 7b55cab8418da1603496462ce3ccdb4cb1dc3368
    OPTIONS "BUILD_TESTING OFF"
)

# ── simpleini ─────────────────────────────────────────────────────────────────
if (NOT TARGET SimpleIni::SimpleIni)
    CPMAddPackage(
        NAME simpleini
        GITHUB_REPOSITORY brofield/simpleini
        GIT_TAG v4.22
    )
endif()

# ── cpp-jwt ───────────────────────────────────────────────────────────────────
if (ENABLE_WEB_SERVICE AND NOT TARGET cpp-jwt::cpp-jwt)
    # Use CPM to fetch cpp-jwt to ensure it goes into CPM_SOURCE_CACHE.
    # We use DOWNLOAD_ONLY because the library has an install() rule that
    # conflicts with yuzu's own project structure if added as a subdirectory.
    CPMAddPackage(
        NAME cpp-jwt
        GITHUB_REPOSITORY arun11299/cpp-jwt
        GIT_TAG v1.4
        DOWNLOAD_ONLY TRUE
    )
    if (cpp-jwt_ADDED)
        add_library(cpp-jwt::cpp-jwt INTERFACE IMPORTED GLOBAL)
        target_include_directories(cpp-jwt::cpp-jwt INTERFACE "${cpp-jwt_SOURCE_DIR}/include")
    endif()
endif()

# ── cpp-httplib ───────────────────────────────────────────────────────────────
if (ENABLE_WEB_SERVICE AND NOT TARGET httplib::httplib)
    CPMAddPackage(
        NAME httplib
        GITHUB_REPOSITORY yhirose/cpp-httplib
        GIT_TAG v0.18.3
        DOWNLOAD_ONLY TRUE
    )
    if (httplib_ADDED)
        add_library(httplib::httplib INTERFACE IMPORTED GLOBAL)
        target_compile_features(httplib::httplib INTERFACE cxx_std_11)
        target_include_directories(httplib::httplib SYSTEM INTERFACE "${httplib_SOURCE_DIR}")
        target_compile_definitions(httplib::httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
        target_link_libraries(httplib::httplib INTERFACE
            $<$<TARGET_EXISTS:Threads::Threads>:Threads::Threads>
            OpenSSL::SSL
            OpenSSL::Crypto
            $<$<PLATFORM_ID:Windows>:ws2_32>
            $<$<PLATFORM_ID:Windows>:crypt32>
            $<$<PLATFORM_ID:Windows>:cryptui>
        )
    endif()
endif()

# ── xbyak ─────────────────────────────────────────────────────────────────────
if ((ARCHITECTURE_x86 OR ARCHITECTURE_x86_64) AND NOT TARGET xbyak::xbyak)
    CPMAddPackage(
        NAME xbyak
        GITHUB_REPOSITORY herumi/xbyak
        GIT_TAG 560ca671421e47e32d3c8270623aaa74454570f4
    )
endif()

# ── Vulkan-Headers ────────────────────────────────────────────────────────────
# We prefer the pre-generated stub for Clangtron builds to avoid submodule dependencies.
option(CITRON_USE_VULKAN_STUB "Use pre-generated Vulkan stub instead of submodule" ON)

if (CITRON_USE_EXTERNAL_VULKAN_HEADERS AND NOT TARGET Vulkan::Headers)
    if (CITRON_USE_VULKAN_STUB AND
        NOT CITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES AND
        EXISTS "${CMAKE_SOURCE_DIR}/externals/vulkan-stub/include")
        add_library(Vulkan-Headers INTERFACE)
        target_include_directories(Vulkan-Headers SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/externals/vulkan-stub/include")
        # Enable beta extensions to support latest Khronos/vendor features (required by v341+)
        target_compile_definitions(Vulkan-Headers INTERFACE VK_ENABLE_BETA_EXTENSIONS)
        if (NOT TARGET Vulkan::Headers)
            add_library(Vulkan::Headers ALIAS Vulkan-Headers)
        endif()
    else()
        CPMAddPackage(
            NAME Vulkan-Headers
            GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
            GIT_TAG v1.4.337
        )
    endif()
endif()

# ── Vulkan-Utility-Libraries ─────────────────────────────────────────────────
if (CITRON_USE_EXTERNAL_VULKAN_UTILITY_LIBRARIES AND NOT TARGET Vulkan::LayerSettings)
    CPMAddPackage(
        NAME Vulkan-Utility-Libraries
        GITHUB_REPOSITORY KhronosGroup/Vulkan-Utility-Libraries
        GIT_TAG v1.4.337
        OPTIONS "BUILD_TESTS OFF"
    )
endif()

# ── VulkanMemoryAllocator ─────────────────────────────────────────────────────
if (NOT TARGET GPUOpen::VulkanMemoryAllocator)
    CPMAddPackage(
        NAME VulkanMemoryAllocator
        GITHUB_REPOSITORY GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
        GIT_TAG v3.1.0
        SYSTEM YES
        OPTIONS "VMA_BUILD_SAMPLES OFF"
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 2 — Compiled libraries, upstream repos
# ═══════════════════════════════════════════════════════════════════════════════

# ── SPIRV-Headers ─────────────────────────────────────────────────────────────
# Must come before sirit.
if (NOT TARGET SPIRV-Headers)
    CPMAddPackage(
        NAME SPIRV-Headers
        GITHUB_REPOSITORY KhronosGroup/SPIRV-Headers
        GIT_TAG vulkan-sdk-1.4.304.1
        OPTIONS
            "SPIRV_HEADERS_SKIP_EXAMPLES ON"
            "SPIRV_HEADERS_SKIP_INSTALL ON"
    )
endif()

# ── enet ──────────────────────────────────────────────────────────────────────
if (NOT TARGET enet::enet)
    CPMAddPackage(
        NAME enet
        GITHUB_REPOSITORY lsalzman/enet
        GIT_TAG 39a72ab1990014eb399cee9d538fd529df99c6a0
    )
    if (TARGET enet AND NOT TARGET enet::enet)
        target_include_directories(enet INTERFACE
            $<BUILD_INTERFACE:${enet_SOURCE_DIR}/include>)
        add_library(enet::enet ALIAS enet)
    endif()
endif()

# ── opus ──────────────────────────────────────────────────────────────────────
if (NOT TARGET Opus::opus)
    CPMAddPackage(
        NAME opus
        GITHUB_REPOSITORY xiph/opus
        GIT_TAG v1.5.2
        OPTIONS
            "OPUS_BUILD_TESTING OFF"
            "OPUS_BUILD_PROGRAMS OFF"
            "OPUS_INSTALL_PKG_CONFIG_MODULE OFF"
            "OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF"
    )
endif()

# ── cubeb ─────────────────────────────────────────────────────────────────────
if (ENABLE_CUBEB AND NOT TARGET cubeb::cubeb)
    CPMAddPackage(
        NAME cubeb
        GITHUB_REPOSITORY mozilla/cubeb
        GIT_TAG 48689ae7a73caeb747953f9ed664dc71d2f918d8
        OPTIONS
            "BUILD_TESTS OFF"
            "BUILD_TOOLS OFF"
            "BUILD_RUST_LIBS OFF"
    )
    if (TARGET cubeb AND NOT TARGET cubeb::cubeb)
        add_library(cubeb::cubeb ALIAS cubeb)
    endif()
    if (NOT MSVC)
        if (TARGET speex)
            target_compile_options(speex PRIVATE -Wno-sign-compare)
        endif()
        if (TARGET cubeb)
            target_compile_options(cubeb PRIVATE -Wno-implicit-const-int-float-conversion)
        endif()
    endif()
endif()

# ── SDL2 ──────────────────────────────────────────────────────────────────────
if (CITRON_USE_EXTERNAL_SDL2 AND NOT TARGET SDL2::SDL2)
    CPMAddPackage(
        NAME SDL2
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG release-2.32.10
        OPTIONS
            "SDL_SHARED OFF"
            "SDL_STATIC ON"
            "SDL_TEST OFF"
    )
endif()

# ── tzdb_to_nx ────────────────────────────────────────────────────────────────
# nx_tzdb/CMakeLists.txt conditionally calls add_subdirectory(tzdb_to_nx).
# For Windows cross-compile, CAN_BUILD_NX_TZDB is false (host != posix),
# so tzdb_to_nx is never built — it downloads pre-built archives instead.
# We still fetch it via CPM so the submodule isn't required, but the directory
# will only be used if someone does a native Linux/macOS build with CPM.
if (CITRON_TZDB_USE_CPM)
  CPMAddPackage(
      NAME tzdb_to_nx
      GITHUB_REPOSITORY lat9nq/tzdb_to_nx
      GIT_TAG 97929690234f2b4add36b33657fe3fe09bd57dfd
      DOWNLOAD_ONLY YES
  )
endif()
if (tzdb_to_nx_SOURCE_DIR)
    set(TZDB_TO_NX_SOURCE_DIR "${tzdb_to_nx_SOURCE_DIR}")
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 3 — Forked repos (pinned to exact SHAs)
# ═══════════════════════════════════════════════════════════════════════════════

# ── mbedtls (yuzu-mirror fork) ────────────────────────────────────────────────
if (NOT TARGET mbedtls)
    CPMAddPackage(
        NAME mbedtls
        GITHUB_REPOSITORY yuzu-mirror/mbedtls
        GIT_TAG 8c88150ca139e06aa2aae8349df8292a88148ea1
        OPTIONS
            "ENABLE_TESTING OFF"
            "ENABLE_PROGRAMS OFF"
            "MBEDTLS_FATAL_WARNINGS OFF"
    )
    if (TARGET mbedtls)
        target_include_directories(mbedtls PUBLIC ${mbedtls_SOURCE_DIR}/include)
    endif()
    if (TARGET mbedtls AND NOT MSVC)
        target_compile_options(mbedcrypto PRIVATE
            -Wno-unused-but-set-variable
            -Wno-string-concatenation)
    endif()
endif()

# ── oaknut (yuzu-mirror fork) ─────────────────────────────────────────────────
if (ARCHITECTURE_arm64 AND NOT TARGET merry::oaknut)
    CPMAddPackage(
        NAME oaknut
        GITHUB_REPOSITORY yuzu-mirror/oaknut
        GIT_TAG 9d091109deb445bc6e9289c6195a282b7c993d49
    )
endif()

# ── sirit (yuzu-mirror fork) ──────────────────────────────────────────────────
# sirit needs SPIRV-Headers. CPM already populated it above.
if (NOT TARGET sirit)
    set(SIRIT_USE_SYSTEM_SPIRV_HEADERS ON)
    CPMAddPackage(
        NAME sirit
        GITHUB_REPOSITORY yuzu-mirror/sirit
        GIT_TAG ab75463999f4f3291976b079d42d52ee91eebf3f
    )
endif()

# ── dynarmic (xinitrcn1 fork, GPLv3) ─────────────────────────────────────────
if ((ARCHITECTURE_x86_64 OR ARCHITECTURE_arm64) AND NOT (MSVC AND ARCHITECTURE_arm64))
    if (NOT TARGET dynarmic::dynarmic)
        CPMAddPackage(
            NAME dynarmic
            GITHUB_REPOSITORY xinitrcn1/dynarmic
            GIT_TAG b280da7792d247a9910bc6d590af9454810fc64f
            OPTIONS
                "DYNARMIC_USE_PRECOMPILED_HEADERS ${CITRON_USE_PRECOMPILED_HEADERS}"
                "DYNARMIC_IGNORE_ASSERTS ON"
                "DYNARMIC_TESTS OFF"
        )
        if (TARGET dynarmic AND NOT TARGET dynarmic::dynarmic)
            add_library(dynarmic::dynarmic ALIAS dynarmic)
        endif()

        # Apply the mcl Clang patch post-populate
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND dynarmic_ADDED)
            execute_process(
                COMMAND git apply --ignore-whitespace
                        "${CMAKE_SOURCE_DIR}/patches/mcl_clang_template_fix.patch"
                WORKING_DIRECTORY "${dynarmic_SOURCE_DIR}/externals/mcl"
                RESULT_VARIABLE _mcl_patch
                OUTPUT_QUIET ERROR_QUIET
            )
        endif()
    endif()
endif()

# ── discord-rpc (yuzu-mirror fork) ───────────────────────────────────────────
if (USE_DISCORD_PRESENCE AND NOT TARGET DiscordRPC::discord-rpc)
    CPMAddPackage(
        NAME discord-rpc
        GITHUB_REPOSITORY yuzu-mirror/discord-rpc
        GIT_TAG 20cc99aeffa08a4834f156b6ab49ed68618cf94a
        OPTIONS "BUILD_EXAMPLES OFF"
    )
    if (discord-rpc_ADDED)
        # Apply patches post-populate
        execute_process(
            COMMAND git apply --ignore-whitespace
                    "${CMAKE_SOURCE_DIR}/patches/rapidjson-compiler-fix.patch"
            WORKING_DIRECTORY "${discord-rpc_SOURCE_DIR}/thirdparty/rapidjson-1.1.0"
            RESULT_VARIABLE _rj_patch OUTPUT_QUIET ERROR_QUIET
        )
        execute_process(
            COMMAND git apply -p0 --ignore-whitespace
                    "${CMAKE_SOURCE_DIR}/patches/discord-rpc-wclass-memaccess-fix.patch"
            WORKING_DIRECTORY "${discord-rpc_SOURCE_DIR}"
            RESULT_VARIABLE _dr_patch OUTPUT_QUIET ERROR_QUIET
        )
    endif()
    if (TARGET discord-rpc AND NOT TARGET DiscordRPC::discord-rpc)
        target_include_directories(discord-rpc INTERFACE
            $<BUILD_INTERFACE:${discord-rpc_SOURCE_DIR}/include>)
        add_library(DiscordRPC::discord-rpc ALIAS discord-rpc)
    endif()
endif()

# ── breakpad (yuzu-mirror fork) ───────────────────────────────────────────────
# breakpad is built manually in externals/CMakeLists.txt — it has no usable
# CMakeLists of its own. We fetch via CPM with DOWNLOAD_ONLY so the source is
# available but we don't try to add_subdirectory it. The existing breakpad
# build logic in externals/CMakeLists.txt will use ${breakpad_SOURCE_DIR}
# instead of the submodule path when CPM is active.
if (CITRON_CRASH_DUMPS AND NOT TARGET libbreakpad_client)
    CPMAddPackage(
        NAME breakpad
        GITHUB_REPOSITORY yuzu-mirror/breakpad
        GIT_TAG c89f9dddc793f19910ef06c13e4fd240da4e7a59
        DOWNLOAD_ONLY YES
    )
endif()

# ── libadrenotools ────────────────────────────────────────────────────────────
if (ANDROID AND ARCHITECTURE_arm64)
    CPMAddPackage(
        NAME libadrenotools
        GITHUB_REPOSITORY bylaws/libadrenotools
        GIT_TAG 5cd3f5c5ceea6d9e9d435ccdd922d9b99e55d10b
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 4 — libusb wrapper
# ═══════════════════════════════════════════════════════════════════════════════

# libusb lives at externals/libusb/libusb (nested). The wrapper CMakeLists
# at externals/libusb/ builds it manually. We fetch with DOWNLOAD_ONLY
# so the wrapper can use libusb_src_SOURCE_DIR as its source root.
# NOTE: For Clangtron Windows builds, ENABLE_LIBUSB is OFF, so this is a no-op.
if (ENABLE_LIBUSB AND NOT TARGET libusb::usb)
    CPMAddPackage(
        NAME libusb_src
        GITHUB_REPOSITORY libusb/libusb
        GIT_TAG v1.0.27
        DOWNLOAD_ONLY YES
    )
    if (libusb_src_ADDED)
        set(LIBUSB_CPM_SOURCE_DIR ${libusb_src_SOURCE_DIR} CACHE INTERNAL "")
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 5 — FFmpeg (source download for static autotools build)
# ═══════════════════════════════════════════════════════════════════════════════
#
# FFmpeg uses autotools, not CMake, so CPM can only download the source.
# The actual build is performed by build-clangtron-windows.sh's
# rebuild_ffmpeg_pthread_free() BEFORE cmake configure runs.  The build script
# populates CITRON_FFMPEG_STATIC_DIR with pre-built static .a files + headers.
#
# For Clangtron builds: the build script downloads FFmpeg from ffmpeg.org
# directly (since it runs before cmake), builds static libs with
# --enable-static --disable-shared --disable-pthreads --enable-w32threads
# --enable-dxva2 --enable-d3d11va, and passes CITRON_FFMPEG_STATIC_DIR
# to cmake.  The CPM entry below ensures the source is also available in
# the CPM cache for reference and is skipped if the static dir is already set.
if (DEFINED CITRON_FFMPEG_STATIC_DIR)
    set(FFmpeg_PATH "${CITRON_FFMPEG_STATIC_DIR}")
    set(FFmpeg_INCLUDE_DIR "${FFmpeg_PATH}/include")
    set(FFmpeg_LIBRARIES
        "${FFmpeg_PATH}/lib/libavformat.a"
        "${FFmpeg_PATH}/lib/libavfilter.a"
        "${FFmpeg_PATH}/lib/libavcodec.a"
        "${FFmpeg_PATH}/lib/libswresample.a"
        "${FFmpeg_PATH}/lib/libswscale.a"
        "${FFmpeg_PATH}/lib/libavutil.a"
    )
    if (WIN32)
        # MinGW toolchains may provide libiconv, but some cross toolchains do not.
        find_library(FFmpeg_ICONV_LIBRARY NAMES iconv libiconv)
        if (FFmpeg_ICONV_LIBRARY)
            list(APPEND FFmpeg_LIBRARIES "${FFmpeg_ICONV_LIBRARY}")
        endif()
        list(APPEND FFmpeg_LIBRARIES bcrypt m)
    endif()
    set(FFmpeg_FOUND TRUE)
endif()

if (CITRON_USE_BUNDLED_FFMPEG AND NOT DEFINED CITRON_FFMPEG_STATIC_DIR)
    CPMAddPackage(
        NAME ffmpeg_src
        GITHUB_REPOSITORY FFmpeg/FFmpeg
        GIT_TAG n8.0
        DOWNLOAD_ONLY YES
    )
    if (ffmpeg_src_ADDED)
        set(FFMPEG_CPM_SOURCE_DIR ${ffmpeg_src_SOURCE_DIR} CACHE INTERNAL
            "FFmpeg source from CPM (for reference; build handled by build script)")
    endif()
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Phase 6 — Qt 6.9.3 (pre-built binaries via aqt)
# ═══════════════════════════════════════════════════════════════════════════════
#
# Qt uses a proprietary distribution model incompatible with CPM's git-based
# fetching.  We use aqt (pip install aqtinstall) to download pre-built binaries
# at configure time.  The win64_llvm_mingw Qt variant is built with llvm-mingw
# (libc++ based)
#
# Prerequisites: Python3 + aqt must be available (build script installs them).
if (ENABLE_QT AND NOT USE_SYSTEM_QT)
    include(${CMAKE_SOURCE_DIR}/CMakeModules/qt_download.cmake)
endif()

message(STATUS "[CPM] All dependency packages configured")
