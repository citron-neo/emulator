# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# XCB utility build script for Linux portability.
# Fetches XCB components via CPM and builds them as shared libraries.

if (NOT UNIX OR APPLE)
    return()
endif()

# We need a list of components to fetch and build. 
# 'macros' and 'proto' must come first as others depend on them.
set(_xcb_components macros proto xau xdmcp libxcb util image keysyms renderutil wm cursor)

string(FIND "${CMAKE_BINARY_DIR}" " " _space_pos)
if(_space_pos GREATER -1)
    message(STATUS "[XCB] Binary dir has spaces — redirecting build/install to /tmp/citron-xcb-${CMAKE_SYSTEM_NAME}")
    set(XCB_BUILD_ROOT "/tmp/citron-xcb-${CMAKE_SYSTEM_NAME}")
else()
    set(XCB_BUILD_ROOT "${CMAKE_BINARY_DIR}/externals/xcb-build")
endif()
file(MAKE_DIRECTORY "${XCB_BUILD_ROOT}")
set(XCB_BINARY_DIR "${XCB_BUILD_ROOT}/lib" CACHE INTERNAL "Location of CPM-built XCB libs")

# Define versions for each component to fetch the correct tarball
# These are defined in dependencies.cmake as CACHE variables.
set(_xcb_macros_ver "${CITRON_XCB_MACROS_VER}")
set(_xcb_proto_ver  "${CITRON_XCB_PROTO_VER}")
set(_xcb_xau_ver    "${CITRON_XCB_XAU_VER}")
set(_xcb_xdmcp_ver  "${CITRON_XCB_XDMCP_VER}")
set(_xcb_libxcb_ver "${CITRON_XCB_LIBXCB_VER}")
set(_xcb_cursor_ver "${CITRON_XCB_CURSOR_VER}")
set(_xcb_util_ver "${CITRON_XCB_UTIL_VER}")
set(_xcb_image_ver "${CITRON_XCB_IMAGE_VER}")
set(_xcb_keysyms_ver "${CITRON_XCB_KEYSYMS_VER}")
set(_xcb_renderutil_ver "${CITRON_XCB_RENDERUTIL_VER}")
set(_xcb_wm_ver "${CITRON_XCB_WM_VER}")

set(CITRON_XCB_TARGETS "" CACHE INTERNAL "List of XCB build targets")
foreach(_comp ${_xcb_components})
    # (re-pasting the existing logic to avoid deletion)
    if (_comp STREQUAL "macros")
        set(_url "https://xorg.freedesktop.org/archive/individual/util/util-macros-${_xcb_macros_ver}.tar.xz")
        set(_name "util-macros")
    elseif(_comp STREQUAL "xau")
        set(_url "https://xorg.freedesktop.org/archive/individual/lib/libXau-${_xcb_xau_ver}.tar.xz")
        set(_name "libXau")
    elseif(_comp STREQUAL "xdmcp")
        set(_url "https://xorg.freedesktop.org/archive/individual/lib/libXdmcp-${_xcb_xdmcp_ver}.tar.xz")
        set(_name "libXdmcp")
    elseif(_comp STREQUAL "libxcb")
        set(_url "https://xcb.freedesktop.org/dist/libxcb-${_xcb_libxcb_ver}.tar.xz")
        set(_name "libxcb")
    elseif(_comp STREQUAL "proto")
        set(_url "https://xcb.freedesktop.org/dist/xcb-proto-${_xcb_proto_ver}.tar.xz")
        set(_name "xcb-proto")
    else()
        set(_name "xcb-util")
        if (NOT _comp STREQUAL "util")
            set(_name "xcb-util-${_comp}")
        endif()
        set(_ver "${_xcb_${_comp}_ver}")
        set(_url "https://xcb.freedesktop.org/dist/${_name}-${_ver}.tar.xz")
    endif()
    
    CPMAddPackage(
        NAME xcb_${_comp}_src
        URL "${_url}"
        DOWNLOAD_ONLY YES
    )

    if (xcb_${_comp}_src_ADDED)
        set(_src_dir "${xcb_${_comp}_src_SOURCE_DIR}")
        set(_build_dir "${XCB_BUILD_ROOT}/${_comp}")
        file(MAKE_DIRECTORY "${_build_dir}")

        # Check if it has a CMakeLists.txt (newer versions do) or needs Autotools
        if (EXISTS "${_src_dir}/CMakeLists.txt")
            # Build via CMake
            add_subdirectory("${_src_dir}" "${_build_dir}")
        else()
            # Build via Autotools (standard fallback)
            set(_env_vars 
                "ACLOCAL_PATH=${XCB_BUILD_ROOT}/share/aclocal"
                "PKG_CONFIG_PATH=${XCB_BUILD_ROOT}/lib/pkgconfig:${XCB_BUILD_ROOT}/share/pkgconfig"
                "PYTHON=python3"
                "LD_RUN_PATH=\$\$ORIGIN"
            )

            set(_target_name "xcb-${_comp}-build")
            list(APPEND CITRON_XCB_TARGETS ${_target_name})
            set(CITRON_XCB_TARGETS ${CITRON_XCB_TARGETS} CACHE INTERNAL "List of XCB build targets")

            # We use autoreconf -fi to refresh libtool/aclocal files from the host system
            # to avoid mismatch errors (e.g. 'ltmain.sh is newer').
            add_custom_command(
                OUTPUT "${_build_dir}/Makefile"
                COMMAND ${CMAKE_COMMAND} -E env ${_env_vars} autoreconf -fi "${_src_dir}"
                COMMAND ${CMAKE_COMMAND} -E env ${_env_vars} "${_src_dir}/configure" --prefix="${XCB_BUILD_ROOT}" --enable-shared
                WORKING_DIRECTORY "${_build_dir}"
                COMMENT "Configuring ${_name} via Autotools"
            )
            add_custom_target(${_target_name} ALL 
                COMMAND ${CMAKE_COMMAND} -E env ${_env_vars} make install
                DEPENDS "${_build_dir}/Makefile"
                WORKING_DIRECTORY "${_build_dir}"
                COMMENT "Building ${_name}"
            )
            
            # Enforce sequential build ordering to ensure all dependencies are installed
            # in XCB_BUILD_ROOT before downstream components are configured.
            if (_last_xcb_target)
                add_dependencies(${_target_name} ${_last_xcb_target})
            endif()
            set(_last_xcb_target ${_target_name})
        endif()
    endif()
endforeach()
