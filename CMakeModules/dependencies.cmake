# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# CMakeModules/dependencies.cmake
#
# CPM-managed dependencies.  Included when CITRON_USE_CPM=ON, for any platform
# (Linux native, Windows native via MSYS2, Linux→Windows cross-compile).
#
# All packages are fetched from source and built statically.  No system
# packages are assumed or required.  CPM_USE_LOCAL_PACKAGES is intentionally
# left OFF so behaviour is identical regardless of what the host has installed.
#
# Packages NOT managed here:
#   - vcpkg  (submodule; only used by MSVC/Android paths with CITRON_USE_BUNDLED_VCPKG=ON)
#   - Qt     (pre-built binaries via aqt — see CMakeModules/qt_download.cmake)
#
# Static linking: BUILD_SHARED_LIBS is forced OFF in externals/CMakeLists.txt;
# all CPM packages inherit this setting.


# ── Submodule & vcpkg Policy ──────────────────────────────────────────────────
# Force-disable reliance on git submodules and vcpkg. All external dependencies
# must be fetched and managed via CPM to ensure portability and build-time
# environment isolation.
set(CITRON_CHECK_SUBMODULES OFF CACHE BOOL "Force disable submodule presence checks" FORCE)
set(CITRON_USE_BUNDLED_VCPKG OFF CACHE BOOL "Force disable vcpkg usage" FORCE)

if (NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake not loaded — include CMakeModules/CPM.cmake before this file")
endif()

# ── tzdb host capability check ────────────────────────────────────────────────
# tzdb_to_nx builds 'zic' from C source using the host compiler.
# On WIN32 hosts, Windows headers lack POSIX APIs (link, symlink, readlink,
# 2-arg mkdir) so the source build always fails.  Force the pre-built
# release artifact download path instead.
# On POSIX hosts (Linux, macOS) zic builds correctly; use CPMAddPackage.
if (WIN32)
    set(CITRON_DOWNLOAD_TIME_ZONE_DATA ON CACHE BOOL
        "Use pre-built timezone data (forced ON: host lacks POSIX build tools)"
        FORCE)
    set(CITRON_TZDB_USE_CPM FALSE)
    message(STATUS "[tzdb] WIN32 host: using pre-built timezone data")
else()
    set(CITRON_TZDB_USE_CPM TRUE)
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Core dependencies
# ═══════════════════════════════════════════════════════════════════════════════

# ── Boost ─────────────────────────────────────────────────────────────────────
if(CITRON_CLANGCL)
    set(BOOST_CONTEXT_IMPLEMENTATION fcontext CACHE STRING "" FORCE)
    # Use Boost's PE/MS-ABI GAS sources with clang's integrated assembler.
    set(BOOST_CONTEXT_ASSEMBLER clang_gas CACHE STRING "" FORCE)
    set(BOOST_CONTEXT_ASM_SUFFIX .S CACHE STRING "" FORCE)
    set(BOOST_CONTEXT_BINARY_FORMAT pe CACHE STRING "" FORCE)
    set(BOOST_CONTEXT_ABI ms CACHE STRING "" FORCE)
endif()

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
        GIT_TAG e8244777ee1c32df8233c215ac9ff626b2dd2c38
        OPTIONS "FMT_INSTALL OFF"
    )
endif()

# ── lz4 ───────────────────────────────────────────────────────────────────────
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
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
        set(ZLIB_INCLUDE_DIRS "${ZLIB_SOURCE_DIR};${ZLIB_BINARY_DIR}" CACHE PATH "" FORCE)
    endif()
    if (CITRON_CLANGCL AND TARGET zlib)
        # Only zlibstatic is used.
        set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
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

# ── OpenSSL ───────────────────────────────────────────────────────────────────
if (ENABLE_OPENSSL OR ENABLE_WEB_SERVICE)
    include(${CMAKE_SOURCE_DIR}/CMakeModules/openssl_build.cmake)
endif()

# ── Catch2 (test framework) ───────────────────────────────────────────────────
if (CITRON_TESTS AND NOT TARGET Catch2::Catch2)
    CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG 675f9eaeb191c51b9d2ffb2bb198009533895051
        OPTIONS "CATCH_INSTALL_DOCS OFF"
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# Header-only / trivial packages
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

# cpp-jwt removed: JWT RS256 verification is now done directly with OpenSSL EVP
# in web_service/verify_user_jwt.cpp. No external JWT library needed.

# ── cpp-httplib ───────────────────────────────────────────────────────────────
# OpenSSL TLS support via CPPHTTPLIB_OPENSSL_SUPPORT.
# web_backend.cpp instantiates httplib::Client with an https:// host which
# auto-upgrades to TLS through this path.
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
            OpenSSL::SSL
            OpenSSL::Crypto
            $<$<TARGET_EXISTS:Threads::Threads>:Threads::Threads>
            $<$<PLATFORM_ID:Windows>:ws2_32>
        )
    endif()
endif()

# ── xbyak ─────────────────────────────────────────────────────────────────────
if ((ARCHITECTURE_x86 OR ARCHITECTURE_x86_64) AND NOT TARGET xbyak::xbyak)
    CPMAddPackage(
        NAME xbyak
        GITHUB_REPOSITORY herumi/xbyak
        GIT_TAG c506ecd5134122115a981fdd45c2a756f9ce20ac
    )
endif()

# ── Vulkan-Headers ────────────────────────────────────────────────────────────
option(CITRON_USE_VULKAN_STUB "Use pre-generated Vulkan stub instead of fetching Vulkan-Headers" ON)

if (CITRON_USE_EXTERNAL_VULKAN_HEADERS AND NOT TARGET Vulkan::Headers)
    CPMAddPackage(
        NAME Vulkan-Headers
        GITHUB_REPOSITORY KhronosGroup/Vulkan-Headers
        GIT_TAG v1.4.337
    )
endif()

# ── Vulkan-Utility-Libraries ──────────────────────────────────────────────────
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
# Compiled libraries — upstream repos
# ═══════════════════════════════════════════════════════════════════════════════

# ── SPIRV-Headers ─────────────────────────────────────────────────────────────
# Must be declared before sirit.
if (NOT CITRON_CLANGCL AND NOT TARGET SPIRV-Headers)
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
    set(_opus_cpm_patches "")
    if (CITRON_CLANGCL)
        # Opus only applies -msse4.1 under `if (NOT MSVC)`, but CMake's MSVC
        # var is TRUE for clang-cl too, so that branch never fires — hence
        # "requires target feature 'sse4.1'" errors. Can't fix this from here
        # (set_source_files_properties is directory-scoped); must patch opus's
        # own CMakeLists.txt to add an elseif for clang-cl detection.
        list(APPEND _opus_cpm_patches "${CMAKE_SOURCE_DIR}/patches/opus-clangcl-sse4.patch")
    endif()

    CPMAddPackage(
        NAME opus
        GITHUB_REPOSITORY xiph/opus
        GIT_TAG v1.5.2
        OPTIONS
            "OPUS_BUILD_TESTING OFF"
            "OPUS_BUILD_PROGRAMS OFF"
            "OPUS_INSTALL_PKG_CONFIG_MODULE OFF"
            "OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF"
        PATCHES
            ${_opus_cpm_patches}
    )
    unset(_opus_cpm_patches)
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
            "BUNDLE_SPEEX ${CITRON_CLANGCL}"
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
    # SDL3 — runtime backing for sdl2-compat.
    # SYSTEM YES: marks SDL3's include dirs as system headers so Citron's
    # own strict warning flags inherited from CMAKE_C_FLAGS do not apply to
    # SDL3's headers when included transitively by Citron code.
    # SDL_WERROR OFF: prevents SDL3's SDL_AddCommonCompilerFlags from adding /WX.
    # SDL_LIBC ON: SDL3 defaults this ON (SDL_LIBC_DEFAULT ON), but explicit
    # here to avoid the MASM stdlib reimplementation — SDL3 guards its MASM
    # source with `if(MSVC AND NOT SDL_LIBC)`.
    CPMAddPackage(
        NAME SDL3
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG f87239e71e42da91ca317a12eefb82cfbf3393eb # release-3.4.12
        SYSTEM YES
        OPTIONS
            "SDL_SHARED ON"
            "SDL_STATIC OFF"
            "SDL_TEST_LIBRARY OFF"
            "SDL_TESTS OFF"
            "SDL_INSTALL OFF"
            "SDL_HIDAPI ON"
            "SDL_HIDAPI_LIBUSB ON"
            "SDL_WERROR OFF"
            "SDL_LIBC ON"
    )

    # sdl2-compat — SDL2 API compatibility layer on top of SDL3.
    # SYSTEM YES: same rationale as SDL3 above.
    # SDL2COMPAT_WERROR OFF: prevents SDL_AddCommonCompilerFlags from adding /WX.
    # SDL_LIBC ON + patch: sdl2-compat's MASM guard is bare `if(MSVC)` with no
    # SDL_LIBC check, unlike SDL3.  The patch adds `AND NOT SDL_LIBC` so that
    # sdl2_mslibc_x64.masm is skipped when linking the system CRT (/MD).
    # NOTE: if this pin is bumped, check whether sdl2-compat has adopted the
    # SDL_LIBC guard upstream (matching SDL3's behaviour); if so, drop the patch.
    # If not, refresh the patch against the new CMakeLists.txt line numbers.
    CPMAddPackage(
        NAME SDL2
        GITHUB_REPOSITORY libsdl-org/sdl2-compat
        GIT_TAG 1321b6c2857022a016280ecc8ed890d1d64c5213 # release-2.32.56
        SYSTEM YES
        PATCHES
            "${CMAKE_SOURCE_DIR}/patches/sdl2compat-no-masm-with-libc.patch"
        OPTIONS
            "SDL2COMPAT_TESTS OFF"
            "SDL2COMPAT_INSTALL OFF"
            "SDL2COMPAT_STATIC OFF"
            "SDL2COMPAT_WERROR OFF"
            "SDL_LIBC ON"
    )

    # Redirect SDL runtime DLL output to a staging directory separate from
    # bin/.  Both sdl2-compat and SDL3-shared inherit CMAKE_RUNTIME_OUTPUT_DIRECTORY
    # (set to bin/) from the parent project, which means $<TARGET_FILE:SDL2>
    # resolves to bin/SDL2.dll — the same location copy_citron_sdl_runtime
    # would copy it to.  cmake -E copy_if_different from a file to its own
    # directory prints an error and ninja stops.  A dedicated staging dir keeps
    # $<TARGET_FILE:SDL2> distinct from $<TARGET_FILE_DIR:citron>.
    foreach(_sdl_out_target IN ITEMS SDL3-shared SDL2)
        if (TARGET ${_sdl_out_target})
            set_target_properties(${_sdl_out_target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sdl-staging"
            )
        endif()
    endforeach()
    unset(_sdl_out_target)

    if (UNIX AND NOT APPLE AND TARGET SDL2)
        # sdl2-compat's own BUILD_RPATH->SDL3-shared trick (its own
        # CMakeLists.txt) is neutralized by CMAKE_BUILD_WITH_INSTALL_RPATH
        # TRUE above: CMake uses INSTALL_RPATH instead for every target,
        # build tree or not. copy_citron_sdl_runtime() places SDL2 and SDL3
        # side by side, so plain $ORIGIN is enough for SDL2 to dlopen SDL3.
        set_property(TARGET SDL2 PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()

    # Post-configure: strip any inherited -Werror / /WX from SDL3 and SDL2
    # targets.  Citron's clang-cl build passes these flags through
    # CMAKE_C_FLAGS / CMAKE_CXX_FLAGS, and CPM sub-projects inherit them.
    # sdl2_compat.c and SDL_dynapi.c emit hundreds of -Wunsafe-buffer-usage,
    # -Wmissing-prototypes, and related diagnostics that are harmless in
    # third-party code but cause clang-cl to exit non-zero when -Werror is
    # active.  Suppressing on the targets is the correct scope: we want
    # these checks to remain active for Citron's own first-party code.
    foreach(_sdl_target IN ITEMS SDL3-shared SDL2)
        if (TARGET ${_sdl_target})
            if (MSVC)
                # /W0 silences all MSVC-dialect warnings on these third-party targets.
                # /clang:-Wno-* flags suppress Clang front-end diagnostics and are only
                # valid under clang-cl (CMAKE_C_COMPILER_ID == "Clang").  Plain cl.exe
                # does not understand the /clang: prefix and would error.
                target_compile_options(${_sdl_target} PRIVATE /W0)
                if (CMAKE_C_COMPILER_ID STREQUAL "Clang")
                    target_compile_options(${_sdl_target} PRIVATE
                        /clang:-Wno-unsafe-buffer-usage
                        /clang:-Wno-unsafe-pointer-arithmetic
                        /clang:-Wno-missing-prototypes
                        /clang:-Wno-missing-variable-declarations
                        /clang:-Wno-sign-conversion
                        /clang:-Wno-cast-qual
                        /clang:-Wno-cast-align
                        /clang:-Wno-implicit-int-conversion
                        /clang:-Wno-shorten-64-to-32
                        /clang:-Wno-float-conversion
                        /clang:-Wno-double-promotion
                        /clang:-Wno-reserved-identifier
                        /clang:-Wno-reserved-macro-identifier
                        /clang:-Wno-extra-semi
                        /clang:-Wno-extra-semi-stmt
                        /clang:-Wno-switch-default
                        /clang:-Wno-switch-enum
                        /clang:-Wno-pre-c11-compat
                        /clang:-Wno-bad-function-cast
                        /clang:-Wno-strict-prototypes
                        /clang:-Wno-unused-macros
                        /clang:-Wno-nonportable-system-include-path
                        /clang:-Wno-date-time
                    )
                endif()
            else()
                # GCC/Clang on Linux/macOS.
                target_compile_options(${_sdl_target} PRIVATE
                    -Wno-error
                    -Wno-unsafe-buffer-usage
                    -Wno-unsafe-pointer-arithmetic
                    -Wno-missing-prototypes
                    -Wno-missing-variable-declarations
                    -Wno-sign-conversion
                    -Wno-cast-qual
                    -Wno-cast-align
                    -Wno-implicit-int-conversion
                    -Wno-shorten-64-to-32
                    -Wno-float-conversion
                    -Wno-double-promotion
                    -Wno-reserved-identifier
                    -Wno-reserved-macro-identifier
                    -Wno-extra-semi
                    -Wno-extra-semi-stmt
                    -Wno-switch-default
                    -Wno-switch-enum
                    -Wno-unused-macros
                    -Wno-date-time
                )
            endif()
        endif()
    endforeach()
    unset(_sdl_target)

    # sdl2-compat loads SDL3 itself rather than linking it. Make SDL3 an
    # explicit build dependency and publish both shared objects for packagers.
    add_dependencies(SDL2 SDL3-shared)
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/citron-sdl-runtime-libs-$<CONFIG>.txt"
        CONTENT "$<TARGET_FILE:SDL2>\n$<TARGET_FILE:SDL3::SDL3-shared>\n"
    )
endif()

# copy_citron_sdl_runtime(<target>)
# On Windows CPM builds: copies SDL2.dll and SDL3.dll next to <target> after
# build.  sdl2-compat dlopens SDL3, so SDL3 is absent from the PE import table
# and CopyMinGWDeps' import-table scan cannot find it.
# On Linux CPM builds: unlike the old statically-linked SDL2, sdl2-compat is
# shared and citron links SDL2::SDL2 directly, but nothing puts the .so
# anywhere citron's rpath ($ORIGIN/lib) can find it. Stage SONAME-named
# copies of both SDL2 and SDL3 into <target>/lib to match -- SDL2 needs
# SDL3 right next to it too, see the INSTALL_RPATH fix above.
# On all other configurations this is a no-op so it is safe to call
# unconditionally from src/citron/CMakeLists.txt and src/citron_cmd/CMakeLists.txt.
function(copy_citron_sdl_runtime target)
    if (WIN32 AND CITRON_USE_EXTERNAL_SDL2 AND TARGET SDL2 AND TARGET SDL3-shared)
        add_dependencies(${target} SDL2 SDL3-shared)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Deploying sdl2-compat and SDL3 for ${target}"
            VERBATIM
        )
    elseif (UNIX AND NOT APPLE AND CITRON_USE_EXTERNAL_SDL2 AND TARGET SDL2 AND TARGET SDL3-shared)
        add_dependencies(${target} SDL2 SDL3-shared)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_SONAME_FILE:SDL2>" "$<TARGET_FILE_DIR:${target}>/lib"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_SONAME_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:${target}>/lib"
            COMMENT "Deploying sdl2-compat and SDL3 for ${target} (\$ORIGIN/lib)"
            VERBATIM
        )
    endif()
endfunction()

# ── tzdb_to_nx ────────────────────────────────────────────────────────────────
# On POSIX hosts: fetch source via CPM; nx_tzdb/CMakeLists.txt builds zic and
# generates the timezone headers at build time.
# On WIN32 hosts: CITRON_TZDB_USE_CPM=FALSE (set above); nx_tzdb downloads the
# pre-built 221202.zip archive instead.
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
# Forked repos — pinned to exact SHAs
# ═══════════════════════════════════════════════════════════════════════════════

# mbedtls removed: all mbedtls usage replaced with OpenSSL EVP and AES-NI
# intrinsics. OpenSSL is built via openssl_build.cmake (CPM).

# ── oaknut (yuzu-mirror fork) — AArch64 only ──────────────────────────────────
if (ARCHITECTURE_arm64 AND NOT TARGET merry::oaknut)
    CPMAddPackage(
        NAME oaknut
        GITHUB_REPOSITORY yuzu-mirror/oaknut
        GIT_TAG 94c726ce0338b054eb8cb5ea91de8fe6c19f4392
    )
endif()

# ── sirit (yuzu-mirror fork) ──────────────────────────────────────────────────
# sirit needs SPIRV-Headers. CPM already populated it above.
if (NOT TARGET sirit)
    if(CITRON_CLANGCL)
        set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF)
    else()
        set(SIRIT_USE_SYSTEM_SPIRV_HEADERS ON)
    endif()
    CPMAddPackage(
        NAME sirit
        GITHUB_REPOSITORY yuzu-mirror/sirit
        GIT_TAG ab75463999f4f3291976b079d42d52ee91eebf3f
    )
    if (CITRON_CLANGCL AND TARGET sirit)
        get_target_property(_sirit_compile_options sirit COMPILE_OPTIONS)
        if (_sirit_compile_options)
            set(_sirit_filtered_compile_options "")
            foreach(_sirit_option IN LISTS _sirit_compile_options)
                string(TOLOWER "${_sirit_option}" _sirit_option_lower)
                if (NOT _sirit_option_lower MATCHES "/zc:throwingnew")
                    list(APPEND _sirit_filtered_compile_options "${_sirit_option}")
                endif()
            endforeach()
            set_property(TARGET sirit PROPERTY COMPILE_OPTIONS "${_sirit_filtered_compile_options}")
        endif()
    endif()
endif()

# ── dynarmic (xinitrcn1 fork) ─────────────────────────────────────────────────
if ((ARCHITECTURE_x86_64 OR ARCHITECTURE_arm64) AND NOT (MSVC AND ARCHITECTURE_arm64))
    if (NOT TARGET dynarmic::dynarmic)
        CPMAddPackage(
            NAME dynarmic
            GITHUB_REPOSITORY xinitrcn1/dynarmic
            GIT_TAG 7bec834bcadbb8b7def7c552a08ad4ac189d4397
            OPTIONS
                "DYNARMIC_USE_PRECOMPILED_HEADERS ${CITRON_USE_PRECOMPILED_HEADERS}"
                "DYNARMIC_IGNORE_ASSERTS ON"
                "DYNARMIC_TESTS OFF"
        )
        if (TARGET dynarmic AND NOT TARGET dynarmic::dynarmic)
            add_library(dynarmic::dynarmic ALIAS dynarmic)
        endif()
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

# ── breakpad (yuzu-mirror fork) ───────────────────────────────────────────────
# Has no usable CMakeLists of its own.  Fetched with DOWNLOAD_ONLY so the
# source is available; externals/CMakeLists.txt contains the build rules and
# uses ${breakpad_SOURCE_DIR} when CPM is active.
if (CITRON_CRASH_DUMPS AND NOT TARGET libbreakpad_client)
    CPMAddPackage(
        NAME breakpad
        GITHUB_REPOSITORY yuzu-mirror/breakpad
        GIT_TAG c89f9dddc793f19910ef06c13e4fd240da4e7a59
        DOWNLOAD_ONLY YES
    )
endif()

# ── libadrenotools — Android only ─────────────────────────────────────────────
if (ANDROID AND ARCHITECTURE_arm64)
    CPMAddPackage(
        NAME libadrenotools
        GITHUB_REPOSITORY bylaws/libadrenotools
        GIT_TAG 5cd3f5c5ceea6d9e9d435ccdd922d9b99e55d10b
    )
endif()

# ═══════════════════════════════════════════════════════════════════════════════
# libusb wrapper
# ═══════════════════════════════════════════════════════════════════════════════
# libusb lives at externals/libusb/libusb (nested).  The wrapper CMakeLists at
# externals/libusb/ builds it manually using LIBUSB_CPM_SOURCE_DIR as the root.
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
# FFmpeg
# ═══════════════════════════════════════════════════════════════════════════════
# FFmpeg uses autotools, not CMake, so CPM can only download the source.
# The actual build is handled by externals/ffmpeg/CMakeLists.txt at build time
# using autoconf+make.  FFMPEG_CPM_SOURCE_DIR tells it where the source landed.
#
# On Windows: the build script pre-builds static FFmpeg before cmake runs and
# passes CITRON_FFMPEG_STATIC_DIR.  The CPM download is then informational only.

# clang-cl builds MSVC ABI FFmpeg below.
if(CITRON_CLANGCL)
    unset(CITRON_FFMPEG_STATIC_DIR CACHE)
    unset(CITRON_FFMPEG_STATIC_DIR)
endif()

if (DEFINED CITRON_FFMPEG_STATIC_DIR)
    # Pre-built static libs supplied externally (Windows build script path).
    set(FFmpeg_PATH "${CITRON_FFMPEG_STATIC_DIR}")
    set(FFmpeg_INCLUDE_DIR "${FFmpeg_PATH}/include")
    set(FFmpeg_LIBRARIES
        "${FFmpeg_PATH}/lib/libavfilter.a"
        "${FFmpeg_PATH}/lib/libswscale.a"
        "${FFmpeg_PATH}/lib/libswresample.a"
        "${FFmpeg_PATH}/lib/libavcodec.a"
        "${FFmpeg_PATH}/lib/libavutil.a"
    )
    if (WIN32)
        find_library(FFmpeg_ICONV_LIBRARY NAMES iconv libiconv)
        if (FFmpeg_ICONV_LIBRARY)
            list(APPEND FFmpeg_LIBRARIES "${FFmpeg_ICONV_LIBRARY}")
        endif()
        list(APPEND FFmpeg_LIBRARIES bcrypt m)
    endif()
    set(FFmpeg_FOUND TRUE)
elseif (CITRON_USE_BUNDLED_FFMPEG)
    # Download source for the autotools build in externals/ffmpeg/CMakeLists.txt.
    CPMAddPackage(
        NAME ffmpeg_src
        GITHUB_REPOSITORY FFmpeg/FFmpeg
        GIT_TAG n8.0
        DOWNLOAD_ONLY YES
    )
    if (ffmpeg_src_ADDED)
        set(FFMPEG_CPM_SOURCE_DIR "${ffmpeg_src_SOURCE_DIR}" CACHE INTERNAL
            "FFmpeg source location for the autotools bundled build")
    endif()
endif()

# ── Dependency Versions (Qt, XCB) ─────────────────────────────────────────
set(CITRON_QT_VERSION "6.9.3" CACHE STRING "Qt version to download via aqt")

set(CITRON_XCB_MACROS_VER "1.20.2" CACHE STRING "XCB util-macros version")
set(CITRON_XCB_PROTO_VER "1.17.0" CACHE STRING "XCB proto version")
set(CITRON_XCB_XAU_VER "1.0.11" CACHE STRING "XCB libXau version")
set(CITRON_XCB_XDMCP_VER "1.1.5" CACHE STRING "XCB libXdmcp version")
set(CITRON_XCB_LIBXCB_VER "1.16" CACHE STRING "XCB libxcb version")
set(CITRON_XCB_UTIL_VER "0.4.1" CACHE STRING "XCB util version")
set(CITRON_XCB_CURSOR_VER "0.1.6" CACHE STRING "XCB cursor version")
set(CITRON_XCB_IMAGE_VER "0.4.1" CACHE STRING "XCB image version")
set(CITRON_XCB_KEYSYMS_VER "0.4.1" CACHE STRING "XCB keysyms version")
set(CITRON_XCB_RENDERUTIL_VER "0.3.10" CACHE STRING "XCB renderutil version")
set(CITRON_XCB_WM_VER "0.4.2" CACHE STRING "XCB wm version")

# ── Tracy (optional profiler) ─────────────────────────────────────────────────
# Fetched only when CITRON_ENABLE_TRACY=ON.  TRACY_ENABLE must be set before
# CPMAddPackage so the client library and application agree on configuration.
if (CITRON_ENABLE_TRACY AND NOT TARGET Tracy::TracyClient)
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy client" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "Collect Tracy data only while a profiler is connected" FORCE)
    set(TRACY_NO_EXIT ON CACHE BOOL "Do not call exit() from the Tracy client" FORCE)
    if (CITRON_ENABLE_LTO)
        set(TRACY_NO_LTO ON CACHE BOOL "Disable LTO on TracyClient when the main project uses LTO" FORCE)
    endif()
    set(_tracy_cpm_options
        "TRACY_ENABLE ON"
        "TRACY_ON_DEMAND ON"
        "TRACY_NO_EXIT ON"
        "TRACY_NO_BROADCAST ON"
        "TRACY_NO_VSYNC_CAPTURE ON"
        "TRACY_NO_FRAME_IMAGE ON"
        "TRACY_FIBERS ON"
        "TRACY_ONLY_IPV4 ON"
        # With /WHOLEARCHIVE forcing TracyClient's translation unit to link in on
        # MSVC/clang-cl, its global static constructor (which spawns the profiler's
        # background thread) now runs eagerly during CRT static init, before main()
        # -- this has been observed to hang process startup on Windows with no log,
        # no window, and no crash dump. TRACY_DELAYED_INIT moves that work out of the
        # static constructor and into a lazy first-call path triggered by the first
        # real zone/instrumentation macro, which by then runs safely after CRT/Qt
        # startup has completed. Whole-archive linking still keeps the translation
        # unit's *functions* reachable regardless, so the port still opens once the
        # first zone fires.
        "TRACY_DELAYED_INIT ON"
        # NOTE: TRACY_CALLSTACK is deliberately NOT set anywhere for this project,
        # globally or otherwise. See the comment further below (right after this
        # OPTIONS list, near the TracyClient PGO/LTO opt-out block) for why: it
        # would silently upgrade every plain ZoneScoped/CITRON_PROFILE_SCOPE zone
        # in the whole codebase into a per-call stack walk.
    )
    if (CITRON_ENABLE_LTO)
        list(APPEND _tracy_cpm_options "TRACY_NO_LTO ON")
    endif()
    CPMAddPackage(
        NAME TracyClient
        GITHUB_REPOSITORY wolfpld/tracy
        GIT_TAG v0.13.1
        OPTIONS
            ${_tracy_cpm_options}
    )
    unset(_tracy_cpm_options)
    if (TARGET TracyClient)
        # Ensure LTO is completely disabled on the TracyClient library to avoid toolchain mismatches during PGO builds
        set_target_properties(TracyClient PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(TracyClient PRIVATE
                "-fno-lto"
                "-fno-profile-generate"
                "-fno-profile-use"
            )
        endif()
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
            target_compile_options(TracyClient PRIVATE
                "-fno-profile-instr-generate"
                "-fno-profile-instr-use"
                "-fno-cs-profile-generate"
            )
        endif()
        # NOTE: We deliberately do NOT define a project-wide TRACY_CALLSTACK depth
        # here. Tracy's ZoneScoped/ZoneScopedN macros (what CITRON_PROFILE_SCOPE
        # expands to) pass TRACY_CALLSTACK straight through as the callstack-capture
        # depth on every single call -- if TRACY_CALLSTACK is defined project-wide,
        # every zone everywhere captures a full stack walk on every invocation
        # instead of a cheap timestamp, regardless of whether that zone's call site
        # ever asked for one. For hot zones (Kernel::Svc::Call, KScheduler::
        # ScheduleImpl, GPU per-command dispatch) this is enormous, unnecessary
        # overhead. Tracy's header already falls back to TRACY_CALLSTACK=0 (no
        # capture) when it's undefined, which is what we want as the default.
        # If callstack capture is genuinely needed for a specific zone, use
        # ZoneScopedNS(name, depth) / CITRON_PROFILE_SCOPE_CS(name, depth) at that
        # call site instead of turning it on globally.
        if (NOT TARGET Tracy::TracyClient)
            add_library(Tracy::TracyClient ALIAS TracyClient)
        endif()
    endif()
    message(STATUS "[Tracy] Profiling enabled (TRACY_ON_DEMAND=ON, callstacks=15, fibers)")
endif()


# ═══════════════════════════════════════════════════════════════════════════════
# Qt
# ═══════════════════════════════════════════════════════════════════════════════
# Qt uses a proprietary distribution model incompatible with CPM source fetches.
# Pre-built binaries are downloaded via aqt (pip install aqtinstall).
# See CMakeModules/qt_download.cmake for details.
if (ENABLE_QT AND NOT USE_SYSTEM_QT)
    include(${CMAKE_SOURCE_DIR}/CMakeModules/qt_download.cmake)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include(${CMAKE_SOURCE_DIR}/CMakeModules/xcb_build.cmake)
    endif()
endif()

message(STATUS "[CPM] All dependency packages configured")
