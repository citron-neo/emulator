# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

include_guard(GLOBAL)

function(citron_build_clangtron_ffmpeg)
    if(NOT DEFINED FFMPEG_CPM_SOURCE_DIR OR
       NOT IS_DIRECTORY "${FFMPEG_CPM_SOURCE_DIR}")
        message(FATAL_ERROR "clangtron build requires CPM FFmpeg source")
    endif()

    set(CITRON_MSYS2_ROOT "" CACHE PATH "MSYS2 install root (auto-detected if empty)")
    if (CITRON_MSYS2_ROOT)
        find_program(BASH_PROGRAM bash
            HINTS "${CITRON_MSYS2_ROOT}/usr/bin" REQUIRED)
        find_program(MAKE_PROGRAM make
            HINTS "${CITRON_MSYS2_ROOT}/usr/bin" REQUIRED)
    else()
        find_program(BASH_PROGRAM bash REQUIRED)
        find_program(MAKE_PROGRAM make REQUIRED)
    endif()
    include(ProcessorCount)
    ProcessorCount(_ffmpeg_jobs)
    if(NOT _ffmpeg_jobs)
        set(_ffmpeg_jobs 4)
    endif()

    set(_source_dir "${FFMPEG_CPM_SOURCE_DIR}")
    if (DEFINED CLANGTRON_FFMPEG_CACHE_DIR AND NOT "${CLANGTRON_FFMPEG_CACHE_DIR}" STREQUAL "")
        set(_build_dir  "${CLANGTRON_FFMPEG_CACHE_DIR}/build")
        set(_install_dir "${CLANGTRON_FFMPEG_CACHE_DIR}/install")
        message(STATUS "[FFmpeg/clangtron] Using global cache dir: ${CLANGTRON_FFMPEG_CACHE_DIR}")
    else()
        set(_build_dir  "${PROJECT_BINARY_DIR}/externals/ffmpeg-clangtron-build")
        set(_install_dir "${PROJECT_BINARY_DIR}/externals/ffmpeg-clangtron-install")
    endif()
    
    get_filename_component(_clangtron_tool_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    set(_rc_compiler_abs "")
    if(CMAKE_RC_COMPILER AND NOT CMAKE_RC_COMPILER STREQUAL "")
        if(IS_ABSOLUTE "${CMAKE_RC_COMPILER}" AND EXISTS "${CMAKE_RC_COMPILER}")
            get_filename_component(_rc_compiler_abs "${CMAKE_RC_COMPILER}" REALPATH)
        else()
            find_program(_rc_compiler_abs NAMES "${CMAKE_RC_COMPILER}"
                HINTS "${_clangtron_tool_dir}")
        endif()
    endif()
    
    if(CMAKE_HOST_WIN32)
        set(_clangtron_ffmpeg_cygpath_command
            "cygpath -am '${_source_dir}' && cygpath -am '${_build_dir}' && cygpath -am '${_install_dir}' && cygpath -au '${_clangtron_tool_dir}' && cygpath -am '${CMAKE_C_COMPILER}' && cygpath -au '${_install_dir}' && cygpath -au '${_build_dir}'")
        if(_rc_compiler_abs)
            string(APPEND _clangtron_ffmpeg_cygpath_command " && cygpath -am '${_rc_compiler_abs}'")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                "${BASH_PROGRAM}" -lc "${_clangtron_ffmpeg_cygpath_command}"
            OUTPUT_VARIABLE _clangtron_ffmpeg_paths
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY
        )
        string(REPLACE "\n" ";" _clangtron_ffmpeg_paths "${_clangtron_ffmpeg_paths}")
        list(GET _clangtron_ffmpeg_paths 0 _source_dir_win)
        list(GET _clangtron_ffmpeg_paths 1 _build_dir_win)
        list(GET _clangtron_ffmpeg_paths 2 _install_dir_win)
        list(GET _clangtron_ffmpeg_paths 3 _clangtron_tool_dir_msys)
        list(GET _clangtron_ffmpeg_paths 4 _c_compiler_win)
        list(GET _clangtron_ffmpeg_paths 5 _install_dir_msys)
        list(GET _clangtron_ffmpeg_paths 6 _build_dir_msys)
        if(_rc_compiler_abs)
            list(GET _clangtron_ffmpeg_paths 7 _rc_compiler_win)
        else()
            set(_rc_compiler_win "")
        endif()
    else()
        set(_source_dir_win "${_source_dir}")
        set(_build_dir_win "${_build_dir}")
        set(_install_dir_win "${_install_dir}")
        set(_clangtron_tool_dir_msys "${_clangtron_tool_dir}")
        set(_c_compiler_win "${CMAKE_C_COMPILER}")
        if(_rc_compiler_abs)
            set(_rc_compiler_win "${_rc_compiler_abs}")
        else()
            set(_rc_compiler_win "")
        endif()
        set(_install_dir_msys "${_install_dir}")
        set(_build_dir_msys "${_build_dir}")
    endif()

    set(_build_stamp "${_install_dir}/.built")
    file(MAKE_DIRECTORY "${_build_dir}" "${_install_dir}")

    # FFmpeg's configure script runs in MSYS Bash and gives the compiler
    # POSIX temporary paths (for example /tmp/ffconf.../test.c).  A Windows
    # C:/... compiler spelling bypasses MSYS path conversion, leaving clang
    # unable to locate those files.  Use the matching MSYS spelling only for
    # the compiler commands executed by that script.
    if(CMAKE_HOST_WIN32)
        get_filename_component(_c_compiler_name "${CMAKE_C_COMPILER}" NAME)
        set(_c_compiler_ffmpeg "${_clangtron_tool_dir_msys}/${_c_compiler_name}")
    else()
        set(_c_compiler_ffmpeg "${_c_compiler_win}")
    endif()

    set(_ffmpeg_extra_cflags "")
    set(_ffmpeg_vulkan_flags "")
    set(_vk_headers_source "")
    foreach(_vk_headers_candidate
            "${Vulkan-Headers_SOURCE_DIR}"
            "${Vulkan_Headers_SOURCE_DIR}"
            "$ENV{MSYSTEM_PREFIX}")
        if(_vk_headers_candidate AND EXISTS "${_vk_headers_candidate}/include/vulkan/vulkan.h")
            set(_vk_headers_source "${_vk_headers_candidate}")
            break()
        endif()
    endforeach()
    if (_vk_headers_source)
        if(CMAKE_HOST_WIN32)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                    "${BASH_PROGRAM}" -lc "cygpath -am '${_vk_headers_source}'"
                OUTPUT_VARIABLE _vk_headers_win
                OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY
            )
        else()
            set(_vk_headers_win "${_vk_headers_source}")
        endif()
        set(_ffmpeg_extra_cflags "${_ffmpeg_extra_cflags} -I${_vk_headers_win}/include")
        set(_ffmpeg_vulkan_flags "--enable-vulkan" "--enable-hwaccel=h264_vulkan" "--enable-hwaccel=vp9_vulkan")
    else()
        set(_ffmpeg_vulkan_flags "--disable-vulkan")
    endif()

    # ── ffnvcodec (NVDEC / CUDA) detection ────────────────────────────────────
    set(_ffnvcodec_inc_dir "")
    set(_ffnvcodec_pc_dir "")
    if (DEFINED ffnvcodec_SOURCE_DIR AND EXISTS "${ffnvcodec_SOURCE_DIR}/include/ffnvcodec/nvEncodeAPI.h")
        set(_ffnvcodec_inc_dir "${ffnvcodec_SOURCE_DIR}/include")
        set(_ffnvcodec_pc_dir "${ffnvcodec_SOURCE_DIR}")
    elseif (DEFINED FFNVCODEC_INCLUDE_DIRS AND EXISTS "${FFNVCODEC_INCLUDE_DIRS}/ffnvcodec/nvEncodeAPI.h")
        set(_ffnvcodec_inc_dir "${FFNVCODEC_INCLUDE_DIRS}")
        if (DEFINED FFNVCODEC_PKGCONFIG_DIR)
            set(_ffnvcodec_pc_dir "${FFNVCODEC_PKGCONFIG_DIR}")
        endif()
    elseif (EXISTS "$ENV{MSYSTEM_PREFIX}/include/ffnvcodec/nvEncodeAPI.h")
        set(_ffnvcodec_inc_dir "$ENV{MSYSTEM_PREFIX}/include")
    endif()

    set(_ffnvcodec_nvdec_flags "")
    set(_ffnvcodec_export_pkgconfig "")
    set(_ffnvcodec_pkg_config_opt "")
    if (_ffnvcodec_inc_dir)
        if(CMAKE_HOST_WIN32)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                    "${BASH_PROGRAM}" -lc "cygpath -am '${_ffnvcodec_inc_dir}'"
                OUTPUT_VARIABLE _ffnvcodec_inc_win
                OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY
            )
            if (_ffnvcodec_pc_dir)
                execute_process(
                    COMMAND "${BASH_PROGRAM}" -lc "cygpath -au '${_ffnvcodec_pc_dir}'"
                    OUTPUT_VARIABLE _ffnvcodec_pc_dir_msys
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                )
            endif()
        else()
            set(_ffnvcodec_inc_win "${_ffnvcodec_inc_dir}")
            set(_ffnvcodec_pc_dir_msys "${_ffnvcodec_pc_dir}")
        endif()

        if (_ffnvcodec_pc_dir_msys)
            set(_ffnvcodec_export_pkgconfig "export PKG_CONFIG_PATH='${_ffnvcodec_pc_dir_msys}':$PKG_CONFIG_PATH &&")
        endif()

        # Provide a pkg-config wrapper in build dir to guarantee configure check_pkg_config succeeds
        # even when cross-compiling (where FFmpeg's --cross-prefix defaults pkg-config to
        # <cross-prefix>pkg-config which doesn't exist) or when pkgconf is missing.
        file(WRITE "${_build_dir_win}/pkg-config"
"#!/bin/sh
case \" \$* \" in
    *ffnvcodec*)
        for arg in \"\$@\"; do
            case \"\$arg\" in
                --exists)
                    exit 0
                    ;;
                --cflags*)
                    echo \"-I${_ffnvcodec_inc_win}\"
                    exit 0
                    ;;
                --libs*)
                    echo \"\"
                    exit 0
                    ;;
                --variable=includedir)
                    echo \"${_ffnvcodec_inc_win}\"
                    exit 0
                    ;;
                --modversion)
                    echo \"12.2.72.0\"
                    exit 0
                    ;;
            esac
        done
        exit 0
        ;;
esac
if command -v pkgconf >/dev/null 2>&1; then
    exec pkgconf \"\$@\"
elif command -v pkg-config >/dev/null 2>&1; then
    exec pkg-config \"\$@\"
elif command -v /usr/bin/pkg-config >/dev/null 2>&1; then
    exec /usr/bin/pkg-config \"\$@\"
elif command -v /usr/bin/pkgconf >/dev/null 2>&1; then
    exec /usr/bin/pkgconf \"\$@\"
elif command -v /clang64/bin/pkg-config >/dev/null 2>&1; then
    exec /clang64/bin/pkg-config \"\$@\"
fi
exit 1
")
        if (CMAKE_HOST_WIN32)
            execute_process(
                COMMAND "${BASH_PROGRAM}" -lc "chmod +x '${_build_dir_msys}/pkg-config'"
            )
        else()
            execute_process(
                COMMAND chmod +x "${_build_dir_win}/pkg-config"
            )
        endif()
        set(_ffnvcodec_pkg_config_opt "--pkg-config='${_build_dir_msys}/pkg-config'")

        set(_ffnvcodec_nvdec_flags
            "--enable-ffnvcodec"
            "--enable-cuvid"
            "--enable-nvdec"
            "--enable-hwaccel=h264_nvdec"
            "--enable-hwaccel=vp8_nvdec"
            "--enable-hwaccel=vp9_nvdec"
        )
    endif()

    if (DEFINED CLANGTRON_FFMPEG_EXTRA_CFLAGS AND NOT "${CLANGTRON_FFMPEG_EXTRA_CFLAGS}" STREQUAL "")
        set(_ffmpeg_extra_cflags "${_ffmpeg_extra_cflags} ${CLANGTRON_FFMPEG_EXTRA_CFLAGS}")
    endif()

    set(_ffmpeg_configure_command
        "export PATH='${_build_dir_msys}:${_clangtron_tool_dir_msys}':$PATH &&"
        ${_ffnvcodec_export_pkgconfig}
        "'${_source_dir_win}/configure'"
        ${_ffnvcodec_pkg_config_opt}
        "--arch=x86_64"
        "--target-os=mingw32"
        "--cc='${_c_compiler_ffmpeg}'"
        "--ar=llvm-ar"
        "--nm=llvm-nm"
        "--strip=llvm-strip"
        "--ranlib=llvm-ranlib"
        "--prefix='${_install_dir_win}'"
        "--disable-pthreads"
        "--enable-w32threads"
        "--enable-static"
        "--disable-shared"
        "--disable-doc"
        "--disable-programs"
        "--disable-avdevice"
        "--disable-network"
        "--disable-everything"
        "--disable-vaapi"
        "--disable-vdpau"
        "--disable-iconv"
        "--enable-decoder=h264,vp8,vp9,aac,mp3,opus,flac"
        "--enable-demuxer=mov,matroska,ogg"
        "--enable-filter=yadif,scale,aresample"
        "--enable-protocol=file"
        "--enable-dxva2"
        "--enable-d3d11va"
    )
    list(APPEND _ffmpeg_configure_command ${_ffmpeg_vulkan_flags})
    list(APPEND _ffmpeg_configure_command ${_ffnvcodec_nvdec_flags})

    if(_rc_compiler_win AND NOT _rc_compiler_win STREQUAL "")
        list(APPEND _ffmpeg_configure_command "--windres='${_rc_compiler_win}'")
    endif()

    if(NOT CMAKE_HOST_WIN32)
        list(APPEND _ffmpeg_configure_command "--enable-cross-compile" "--cross-prefix=${_clangtron_tool_dir_msys}/x86_64-w64-mingw32-")
    else()
        # clangtron targets MinGW Windows even when CMake itself runs from
        # MSYS2 on Windows. FFmpeg must not try to execute that target probe
        # during configure; it has to use its cross-compilation path instead.
        list(APPEND _ffmpeg_configure_command "--enable-cross-compile")
        list(APPEND _ffmpeg_configure_command "--host-cc='${_c_compiler_ffmpeg}'")
    endif()

    if(NOT "${_ffmpeg_extra_cflags}" STREQUAL "")
        list(APPEND _ffmpeg_configure_command "--extra-cflags='${_ffmpeg_extra_cflags}'")
    endif()

    string(JOIN " " _ffmpeg_configure_command ${_ffmpeg_configure_command})

    # Rebuild when any configure argument changes.  Stage the new value now,
    # but only promote it after configure, build, and install all succeed.
    set(_ffmpeg_flags_sentinel "${_install_dir}/.citron-clangtron-configure-command")
    set(_ffmpeg_flags_sentinel_staged "${_build_dir}/.citron-clangtron-configure-command")
    set(_ffmpeg_flags_sentinel_content "")
    if (EXISTS "${_ffmpeg_flags_sentinel}")
        file(READ "${_ffmpeg_flags_sentinel}" _ffmpeg_flags_sentinel_content)
        string(STRIP "${_ffmpeg_flags_sentinel_content}" _ffmpeg_flags_sentinel_content)
    endif()
    if (EXISTS "${_build_stamp}" AND NOT _ffmpeg_flags_sentinel_content STREQUAL "${_ffmpeg_configure_command}")
        message(STATUS "[FFmpeg/clangtron] Configure command changed; rebuilding FFmpeg")
        file(REMOVE "${_build_stamp}")
    endif()
    file(WRITE "${_ffmpeg_flags_sentinel_staged}" "${_ffmpeg_configure_command}")

    add_custom_command(
        OUTPUT "${_build_stamp}"
        BYPRODUCTS
            "${_install_dir}/lib/libavfilter.a"
            "${_install_dir}/lib/libswscale.a"
            "${_install_dir}/lib/libswresample.a"
            "${_install_dir}/lib/libavcodec.a"
            "${_install_dir}/lib/libavutil.a"
            "${_install_dir}/lib/libavformat.a"
        # Do not set MSYS2_ARG_CONV_EXCL here. FFmpeg launches the native
        # compiler with POSIX paths for its temporary probes, and MSYS must
        # translate those paths when it starts clang.
        COMMAND "${BASH_PROGRAM}" -lc "${_ffmpeg_configure_command}"
        COMMAND "${BASH_PROGRAM}" -lc "export PATH='${_clangtron_tool_dir_msys}':$PATH && '${MAKE_PROGRAM}' -j${_ffmpeg_jobs}"
        COMMAND "${BASH_PROGRAM}" -lc "export PATH='${_clangtron_tool_dir_msys}':$PATH && '${MAKE_PROGRAM}' install"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_ffmpeg_flags_sentinel_staged}" "${_ffmpeg_flags_sentinel}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_build_stamp}"
        DEPENDS "${CMAKE_CURRENT_LIST_FILE}" "${_source_dir}/configure"
        WORKING_DIRECTORY "${_build_dir_win}"
        VERBATIM
    )
    add_custom_target(ffmpeg-build ALL DEPENDS "${_build_stamp}")

    set(CLANGTRON_FFMPEG_BUILD_STAMP "${_build_stamp}" CACHE INTERNAL
        "Stamp file written when clangtron FFmpeg build+install completes")

    set(_libraries
        "${_install_dir}/lib/libavfilter.a"
        "${_install_dir}/lib/libswscale.a"
        "${_install_dir}/lib/libswresample.a"
        "${_install_dir}/lib/libavformat.a"
        "${_install_dir}/lib/libavcodec.a"
        "${_install_dir}/lib/libavutil.a"
        bcrypt ole32 strmiids mfuuid mfplat uuid d3d11 dxgi dxva2)

    set(FFmpeg_FOUND YES CACHE BOOL "" FORCE)
    set(FFmpeg_INCLUDE_DIR "${_install_dir}/include"
        CACHE PATH "Path to clangtron FFmpeg headers" FORCE)
    set(FFmpeg_LIBRARIES "${_libraries}"
        CACHE STRING "clangtron FFmpeg libraries" FORCE)
    set(FFmpeg_LDFLAGS "" CACHE STRING "FFmpeg linker flags" FORCE)
    set(FFmpeg_FOUND YES PARENT_SCOPE)
    set(FFmpeg_INCLUDE_DIR "${_install_dir}/include" PARENT_SCOPE)
    set(FFmpeg_LIBRARIES "${_libraries}" PARENT_SCOPE)
    set(FFmpeg_LDFLAGS "" PARENT_SCOPE)
endfunction()
