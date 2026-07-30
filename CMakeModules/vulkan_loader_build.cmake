# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Vulkan loader build script for Linux portability.
#
# citron dlopen()s libvulkan.so.1 by name at runtime (see
# src/video_core/vulkan_common/vulkan_library.cpp) rather than linking it —
# so unlike Qt/ICU/XCB, nothing here is a compile-time dependency of citron
# itself. This exists purely so the AppImage has a specific, reproducible,
# deliberately-chosen loader to bundle instead of whatever the build
# machine's package manager incidentally happens to have installed as a
# side effect of something unrelated. That "incidental" state is exactly
# what caused a real failure: a build machine with an older glibc produced
# a loader too old to satisfy a modern host's own GPU driver, unrelated to
# the loader's own code — see the DEPLOY_GLIBC comment in
# AppImageBuilder/package-citron-linux.sh for the full report. Pinning the
# loader doesn't fix that class of bug by itself (glibc bundling is what
# does), but it does mean every build produces the same loader on purpose,
# not by accident of CI package versions on a given day.

if (NOT UNIX OR APPLE)
    return()
endif()

if (NOT DEFINED CITRON_VULKAN_LOADER_TAG)
    set(CITRON_VULKAN_LOADER_TAG "vulkan-sdk-1.4.350.0")
endif()

string(FIND "${CMAKE_BINARY_DIR}" " " _space_pos)
if(_space_pos GREATER -1)
    message(STATUS "[Vulkan-Loader] Binary dir has spaces — redirecting build/install to /tmp/citron-vulkan-loader-${CMAKE_SYSTEM_NAME}")
    set(VULKAN_LOADER_BUILD_ROOT "/tmp/citron-vulkan-loader-${CMAKE_SYSTEM_NAME}")
else()
    set(VULKAN_LOADER_BUILD_ROOT "${CMAKE_BINARY_DIR}/externals/vulkan-loader-build")
endif()
file(MAKE_DIRECTORY "${VULKAN_LOADER_BUILD_ROOT}")

# Vulkan-Loader's CMake build wants an installed, find_package(CONFIG)-able
# Vulkan-Headers tree (checked directly against its CMakeLists.txt — a plain
# header include dir isn't enough). This is a separate, self-contained fetch
# from whatever Vulkan-Headers citron's own compile uses elsewhere
# (externals/Vulkan-Headers) — reusing that one isn't straightforward since
# it's brought in via add_subdirectory(), not installed, and mixing the two
# purposes isn't worth the coupling. Tag matches CITRON_VULKAN_LOADER_TAG:
# Khronos cuts matching vulkan-sdk-X.Y.Z.W tags across both repos together.
CPMAddPackage(
    NAME vulkan_headers_for_loader_src
    GITHUB_REPOSITORY "KhronosGroup/Vulkan-Headers"
    GIT_TAG "${CITRON_VULKAN_LOADER_TAG}"
    DOWNLOAD_ONLY YES
)

CPMAddPackage(
    NAME vulkan_loader_src
    GITHUB_REPOSITORY "KhronosGroup/Vulkan-Loader"
    GIT_TAG "${CITRON_VULKAN_LOADER_TAG}"
    DOWNLOAD_ONLY YES
)

if (vulkan_headers_for_loader_src_ADDED AND vulkan_loader_src_ADDED)
    set(_vh_build_dir "${VULKAN_LOADER_BUILD_ROOT}/headers-build")
    set(_vh_install_dir "${VULKAN_LOADER_BUILD_ROOT}/headers-install")

    add_custom_command(
        OUTPUT "${_vh_install_dir}/share/cmake/VulkanHeaders/VulkanHeadersConfig.cmake"
        COMMAND ${CMAKE_COMMAND} -S "${vulkan_headers_for_loader_src_SOURCE_DIR}" -B "${_vh_build_dir}"
                -D CMAKE_INSTALL_PREFIX="${_vh_install_dir}"
        COMMAND ${CMAKE_COMMAND} --build "${_vh_build_dir}" --target install
        COMMENT "Configuring and installing Vulkan-Headers (for the loader build only)"
    )

    set(_vl_build_dir "${VULKAN_LOADER_BUILD_ROOT}/loader-build")
    set(_vl_install_dir "${VULKAN_LOADER_BUILD_ROOT}/loader-install")

    # WSI platforms passed explicitly rather than left on their upstream
    # defaults (which today happen to already be XCB/XLIB/XRANDR/Wayland=ON,
    # DirectFB=OFF) — same reasoning as pinning the tag itself: correct by
    # our own choice, not by whatever a future tag's defaults happen to be.
    add_custom_command(
        OUTPUT "${_vl_build_dir}/CMakeCache.txt"
        COMMAND ${CMAKE_COMMAND} -S "${vulkan_loader_src_SOURCE_DIR}" -B "${_vl_build_dir}"
                -D CMAKE_BUILD_TYPE=Release
                -D CMAKE_PREFIX_PATH="${_vh_install_dir}"
                -D CMAKE_INSTALL_PREFIX="${_vl_install_dir}"
                -D BUILD_TESTS=OFF
                -D BUILD_WSI_XCB_SUPPORT=ON
                -D BUILD_WSI_XLIB_SUPPORT=ON
                -D BUILD_WSI_XLIB_XRANDR_SUPPORT=ON
                -D BUILD_WSI_WAYLAND_SUPPORT=ON
                -D BUILD_WSI_DIRECTFB_SUPPORT=OFF
        DEPENDS "${_vh_install_dir}/share/cmake/VulkanHeaders/VulkanHeadersConfig.cmake"
        COMMENT "Configuring Vulkan-Loader ${CITRON_VULKAN_LOADER_TAG}"
    )

    execute_process(COMMAND nproc OUTPUT_VARIABLE SYSTEM_THREADS OUTPUT_STRIP_TRAILING_WHITESPACE)

    add_custom_command(
        OUTPUT "${_vl_install_dir}/lib/libvulkan.so"
        COMMAND ${CMAKE_COMMAND} --build "${_vl_build_dir}" -j${SYSTEM_THREADS}
        COMMAND ${CMAKE_COMMAND} --build "${_vl_build_dir}" --target install
        DEPENDS "${_vl_build_dir}/CMakeCache.txt"
        COMMENT "Building and installing Vulkan-Loader ${CITRON_VULKAN_LOADER_TAG}"
    )

    add_custom_target(vulkan-loader-build ALL DEPENDS "${_vl_install_dir}/lib/libvulkan.so")

    set(VULKAN_LOADER_BINARY_DIR "${_vl_install_dir}/lib" CACHE INTERNAL "Location of the CPM-built, pinned Vulkan loader")
endif()
