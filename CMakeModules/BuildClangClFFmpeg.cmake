# SPDX-FileCopyrightText: 2026 citron Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

include_guard(GLOBAL)

function(citron_build_clangcl_ffmpeg)
    if(NOT DEFINED FFMPEG_CPM_SOURCE_DIR OR
       NOT IS_DIRECTORY "${FFMPEG_CPM_SOURCE_DIR}")
        message(FATAL_ERROR "clang-cl build requires CPM FFmpeg source")
    endif()

    set(CITRON_MSYS2_ROOT "" CACHE PATH "MSYS2 install root (auto-detected if empty)")
    # Find bash and make: prefer the HINTS path if CITRON_MSYS2_ROOT is set,
    # otherwise rely on PATH (populated by the build script's batch file).
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
    # ── Global artifact cache ──────────────────────────────────────────────────
    # When CLANGCL_FFMPEG_CACHE_DIR is set (by build-clangtron-windows.sh), the
    # built FFmpeg install lives there (under CPM_SOURCE_CACHE) rather than in the
    # per-stage cmake binary dir, so all clang-cl stages share one FFmpeg build.
    if (DEFINED CLANGCL_FFMPEG_CACHE_DIR AND NOT "${CLANGCL_FFMPEG_CACHE_DIR}" STREQUAL "")
        set(_build_dir  "${CLANGCL_FFMPEG_CACHE_DIR}/build")
        set(_install_dir "${CLANGCL_FFMPEG_CACHE_DIR}/install")
        message(STATUS "[FFmpeg/clang-cl] Using global cache dir: ${CLANGCL_FFMPEG_CACHE_DIR}")
    else()
        set(_build_dir  "${PROJECT_BINARY_DIR}/externals/ffmpeg-clangcl-build")
        set(_install_dir "${PROJECT_BINARY_DIR}/externals/ffmpeg-clangcl-install")
    endif()
    get_filename_component(_clangcl_tool_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_linker_tool_dir "${CMAKE_LINKER}" DIRECTORY)
    get_filename_component(_ar_tool_dir "${CMAKE_AR}" DIRECTORY)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
            "${BASH_PROGRAM}" -lc "(_winpath() { cygpath -dos \"$1\" 2>/dev/null || cygpath -am \"$1\"; }; _winpath '${_source_dir}' && _winpath '${_build_dir}' && _winpath '${_install_dir}' && cygpath -au '${_clangcl_tool_dir}' && cygpath -au '${_linker_tool_dir}' && cygpath -au '${_ar_tool_dir}' && cygpath -au '${_install_dir}' && cygpath -au '${_source_dir}' && cygpath -au '${_build_dir}')"
        OUTPUT_VARIABLE _clangcl_ffmpeg_paths
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(REPLACE "\n" ";" _clangcl_ffmpeg_paths "${_clangcl_ffmpeg_paths}")
    list(GET _clangcl_ffmpeg_paths 0 _source_dir_win)
    list(GET _clangcl_ffmpeg_paths 1 _build_dir_win)
    list(GET _clangcl_ffmpeg_paths 2 _install_dir_win)
    list(GET _clangcl_ffmpeg_paths 3 _clangcl_tool_dir_msys)
    list(GET _clangcl_ffmpeg_paths 4 _linker_tool_dir_msys)
    list(GET _clangcl_ffmpeg_paths 5 _ar_tool_dir_msys)
    # MSYS paths for bash commands (cd, mv) — separate from Windows mixed paths
    list(GET _clangcl_ffmpeg_paths 6 _install_dir_msys)
    list(GET _clangcl_ffmpeg_paths 7 _source_dir_msys)
    list(GET _clangcl_ffmpeg_paths 8 _build_dir_msys)
    set(_build_stamp "${_install_dir}/.built")
    file(MAKE_DIRECTORY "${_build_dir}" "${_install_dir}")

    set(_ffmpeg_extra_cflags "/MD")
    if (DEFINED CLANGCL_FFMPEG_EXTRA_CFLAGS AND NOT "${CLANGCL_FFMPEG_EXTRA_CFLAGS}" STREQUAL "")
        # dash-prefixed clang flags from build-clangtron-windows.sh (pgo_flags_dash)
        set(_ffmpeg_extra_cflags "${_ffmpeg_extra_cflags} ${CLANGCL_FFMPEG_EXTRA_CFLAGS}")
    endif()

    # ── Vulkan headers detection ───────────────────────────────────────────────
    # Resolution order (most authoritative first):
    #   1. CPM Vulkan-Headers_SOURCE_DIR (set by dependencies.cmake via CPMAddPackage)
    #   2. MSYS2 system headers ($MSYSTEM_PREFIX/include)
    # We intentionally do NOT use get_target_property(Vulkan::Headers ...) because
    # INTERFACE_INCLUDE_DIRECTORIES may contain generator expressions that cannot
    # be resolved at configure-time for an external shell command.
    set(_vk_inc_dir "")
    if (DEFINED Vulkan-Headers_SOURCE_DIR AND EXISTS "${Vulkan-Headers_SOURCE_DIR}/include/vulkan/vulkan.h")
        set(_vk_inc_dir "${Vulkan-Headers_SOURCE_DIR}/include")
    elseif (DEFINED Vulkan_Headers_SOURCE_DIR AND EXISTS "${Vulkan_Headers_SOURCE_DIR}/include/vulkan/vulkan.h")
        # CPM normalises hyphens to underscores in some versions
        set(_vk_inc_dir "${Vulkan_Headers_SOURCE_DIR}/include")
    elseif (EXISTS "$ENV{MSYSTEM_PREFIX}/include/vulkan/vulkan.h")
        set(_vk_inc_dir "$ENV{MSYSTEM_PREFIX}/include")
    endif()

    set(_ffmpeg_vulkan_flags "")
    if (_vk_inc_dir)
        message(STATUS "[FFmpeg/clang-cl] Vulkan headers found at: ${_vk_inc_dir}")
        # Convert to a Windows-compatible path that clang-cl and FFmpeg's configure can use.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                "${BASH_PROGRAM}" -lc "cygpath -am '${_vk_inc_dir}'"
            OUTPUT_VARIABLE _vk_inc_win
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (_vk_inc_win MATCHES "^[A-Za-z]:/")
            set(_ffmpeg_extra_cflags "${_ffmpeg_extra_cflags} -I${_vk_inc_win}")
            set(_ffmpeg_vulkan_flags
                "--enable-vulkan"
                "--enable-hwaccel=h264_vulkan"
                "--enable-hwaccel=vp9_vulkan"
            )
        else()
            message(STATUS "[FFmpeg/clang-cl] Vulkan include path conversion failed; disabling Vulkan hwaccel")
            set(_ffmpeg_vulkan_flags "--disable-vulkan")
        endif()
    else()
        message(STATUS "[FFmpeg/clang-cl] Vulkan headers not found; disabling Vulkan hwaccel")
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
        message(STATUS "[FFmpeg/clang-cl] ffnvcodec headers found at: ${_ffnvcodec_inc_dir}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                "${BASH_PROGRAM}" -lc "cygpath -am '${_ffnvcodec_inc_dir}'"
            OUTPUT_VARIABLE _ffnvcodec_inc_win
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (_ffnvcodec_pc_dir)
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                    "${BASH_PROGRAM}" -lc "cygpath -au '${_ffnvcodec_pc_dir}'"
                OUTPUT_VARIABLE _ffnvcodec_pc_dir_msys
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if (_ffnvcodec_pc_dir_msys)
                set(_ffnvcodec_export_pkgconfig "export PKG_CONFIG_PATH='${_ffnvcodec_pc_dir_msys}':$PKG_CONFIG_PATH &&")
            endif()
        endif()

        # Provide a pkg-config wrapper in build dir to guarantee configure check_pkg_config succeeds
        # even if pkgconf is not installed in the MSYS2 environment.
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
_pkg_config_wrapper=\"\$(cd -P \"\$(dirname \"\$0\")\" && pwd -P)/\$(basename \"\$0\")\"
for _pkg_config_candidate in pkgconf pkg-config /usr/bin/pkgconf /usr/bin/pkg-config /clang64/bin/pkg-config; do
    case \"\$_pkg_config_candidate\" in
        /*) _pkg_config_delegate=\"\$_pkg_config_candidate\" ;;
        *) _pkg_config_delegate=\"\$(command -v \"\$_pkg_config_candidate\" 2>/dev/null)\" || continue ;;
    esac
    [ -n \"\$_pkg_config_delegate\" ] || continue
    [ -x \"\$_pkg_config_delegate\" ] || continue
    _pkg_config_delegate=\"\$(cd -P \"\$(dirname \"\$_pkg_config_delegate\")\" && pwd -P)/\$(basename \"\$_pkg_config_delegate\")\" || continue
    [ \"\$_pkg_config_delegate\" = \"\$_pkg_config_wrapper\" ] && continue
    exec \"\$_pkg_config_delegate\" \"\$@\"
done
exit 1
")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
                "${BASH_PROGRAM}" -lc "chmod +x '${_build_dir_msys}/pkg-config'"
        )
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

    set(_ffmpeg_configure_command
        "export PATH='${_build_dir_msys}:${_clangcl_tool_dir_msys}:${_linker_tool_dir_msys}:${_ar_tool_dir_msys}':$PATH &&"
        ${_ffnvcodec_export_pkgconfig}
        "'${_source_dir_win}/configure'"
        ${_ffnvcodec_pkg_config_opt}
        "--toolchain=msvc"
        "--cc=clang-cl"
        "--cxx=clang-cl"
        "--ld=lld-link"
        "--ar=llvm-ar"
        "--nm=llvm-nm"
        "--prefix='${_install_dir_win}'"
        "--enable-static"
        "--disable-shared"
        "--disable-pthreads"
        "--enable-w32threads"
        "--disable-avdevice"
        "--disable-avformat"
        "--disable-doc"
        "--disable-everything"
        "--disable-ffmpeg"
        "--disable-ffprobe"
        "--disable-network"
        "--disable-swresample"
        "--disable-x86asm"
        "--disable-vaapi"
        "--disable-vdpau"
        "--enable-decoder=h264"
        "--enable-decoder=vp8"
        "--enable-decoder=vp9"
        "--enable-hwaccel=h264_dxva2"
        "--enable-hwaccel=h264_d3d11va"
        "--enable-hwaccel=h264_d3d11va2"
        "--enable-hwaccel=vp9_dxva2"
        "--enable-hwaccel=vp9_d3d11va"
        "--enable-hwaccel=vp9_d3d11va2"
        ${_ffmpeg_vulkan_flags}
        ${_ffnvcodec_nvdec_flags}
        "--enable-filter=yadif,scale"
        "--enable-dxva2"
        "--enable-d3d11va"
        "--extra-cflags='${_ffmpeg_extra_cflags}'")
    string(JOIN " " _ffmpeg_configure_command ${_ffmpeg_configure_command})

    # Flag sentinel: if recorded configure flags/command differ from current, remove stamp so ninja rebuilds.
    set(_ffmpeg_flags_sentinel "${_install_dir}/.citron-clangcl-extra-cflags")
    set(_ffmpeg_flags_sentinel_content "")
    set(_current_sentinel_hash "${_ffmpeg_configure_command} ${_ffmpeg_extra_cflags}")
    if (EXISTS "${_ffmpeg_flags_sentinel}")
        file(READ "${_ffmpeg_flags_sentinel}" _ffmpeg_flags_sentinel_content)
        string(STRIP "${_ffmpeg_flags_sentinel_content}" _ffmpeg_flags_sentinel_content)
    endif()
    if (EXISTS "${_build_stamp}" AND NOT _ffmpeg_flags_sentinel_content STREQUAL "${_current_sentinel_hash}")
        message(STATUS "[FFmpeg/clang-cl] Configure flags changed; invalidating cache and rebuilding FFmpeg")
        file(REMOVE "${_build_stamp}")
    endif()
    file(WRITE "${_ffmpeg_flags_sentinel}" "${_current_sentinel_hash}")

    add_custom_command(
        OUTPUT "${_build_stamp}"
        BYPRODUCTS
            "${_install_dir}/lib/avfilter.lib"
            "${_install_dir}/lib/swscale.lib"
            "${_install_dir}/lib/avcodec.lib"
            "${_install_dir}/lib/avutil.lib"
        COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
            "${BASH_PROGRAM}" -lc "${_ffmpeg_configure_command}"
        COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
            "${BASH_PROGRAM}" -lc "FFMPEG_SOURCE_DIR_MSYS='${_source_dir_msys}' FFMPEG_SOURCE_DIR_WIN='${_source_dir_win}' FFMPEG_BUILD_DIR_MSYS='${_build_dir_msys}' FFMPEG_BUILD_DIR_WIN='${_build_dir_win}' perl -0pi -e 'my $source_dir_msys = \$ENV{FFMPEG_SOURCE_DIR_MSYS}; my $source_dir_win = \$ENV{FFMPEG_SOURCE_DIR_WIN}; my $build_dir_msys = \$ENV{FFMPEG_BUILD_DIR_MSYS}; my $build_dir_win = \$ENV{FFMPEG_BUILD_DIR_WIN}; if (\$ARGV =~ /config\\.mak$/) { (my $source_dir_make = $source_dir_win) =~ s{ }{\\\\ }g; (my $build_dir_make = $build_dir_win) =~ s{ }{\\\\ }g; s{\\Q$source_dir_msys\\E}{$source_dir_make}g; s{\\Q$build_dir_msys\\E}{$build_dir_make}g; s{^SRC_PATH\\s*:?=\\s*.*$}{SRC_PATH=$source_dir_make}mg; } else { (my $source_dir_sh = $source_dir_win) =~ s{\\x27}{\"\\x27\\\\\\x27\\x27\"}ge; (my $build_dir_sh = $build_dir_win) =~ s{\\x27}{\"\\x27\\\\\\x27\\x27\"}ge; s{\\Q$source_dir_msys\\E}{$source_dir_sh}g; s{\\Q$build_dir_msys\\E}{$build_dir_sh}g; s{^SRC_PATH\\s*:?=\\s*.*$}{\"SRC_PATH=\\x27$source_dir_sh\\x27\"}mge; } s{^(AR|AR_CMD)=llvm-lib}{$1=llvm-ar}mg' '${_build_dir_win}/ffbuild/config.mak' '${_build_dir_win}/ffbuild/config.sh'"
        COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
            "${BASH_PROGRAM}" -lc "export PATH='${_clangcl_tool_dir_msys}:${_linker_tool_dir_msys}:${_ar_tool_dir_msys}':$PATH && '${MAKE_PROGRAM}' -j${_ffmpeg_jobs}"
        COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
            "${BASH_PROGRAM}" -lc "export PATH='${_clangcl_tool_dir_msys}:${_linker_tool_dir_msys}:${_ar_tool_dir_msys}':$PATH && '${MAKE_PROGRAM}' install"
        COMMAND "${CMAKE_COMMAND}" -E env "MSYS2_ARG_CONV_EXCL=*"
            "${BASH_PROGRAM}" -lc
            "cd '${_install_dir_msys}/lib' && for f in avfilter swscale avcodec avutil; do if [ -f \"lib$f.a\" ]; then mv -f \"lib$f.a\" \"$f.lib\" || exit 1; elif [ ! -f \"$f.lib\" ]; then echo \"[FFmpeg/clang-cl] Missing both lib$f.a and $f.lib after make install\" >&2; exit 1; fi; done"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_build_stamp}"
        DEPENDS "${CMAKE_CURRENT_LIST_FILE}" "${_source_dir}/configure"
        WORKING_DIRECTORY "${_build_dir_win}"
        VERBATIM
    )
    add_custom_target(ffmpeg-build ALL DEPENDS "${_build_stamp}")

    # Expose the stamp path so video_core/CMakeLists.txt can set OBJECT_DEPENDS
    # on sources that include FFmpeg headers, preventing them from compiling
    # before make install has written the headers.  Clang-cl path only.
    set(CLANGCL_FFMPEG_BUILD_STAMP "${_build_stamp}" CACHE INTERNAL
        "Stamp file written when clang-cl FFmpeg build+install completes")

    set(_libraries
        "${_install_dir}/lib/avfilter.lib"
        "${_install_dir}/lib/swscale.lib"
        "${_install_dir}/lib/avcodec.lib"
        "${_install_dir}/lib/avutil.lib"
        bcrypt ole32 strmiids mfuuid mfplat uuid d3d11 dxgi dxva2)

    set(FFmpeg_FOUND YES CACHE BOOL "" FORCE)
    set(FFmpeg_INCLUDE_DIR "${_install_dir}/include"
        CACHE PATH "Path to clang-cl FFmpeg headers" FORCE)
    set(FFmpeg_LIBRARIES "${_libraries}"
        CACHE STRING "clang-cl FFmpeg libraries" FORCE)
    set(FFmpeg_LDFLAGS "" CACHE STRING "FFmpeg linker flags" FORCE)
    set(FFmpeg_FOUND YES PARENT_SCOPE)
    set(FFmpeg_INCLUDE_DIR "${_install_dir}/include" PARENT_SCOPE)
    set(FFmpeg_LIBRARIES "${_libraries}" PARENT_SCOPE)
    set(FFmpeg_LDFLAGS "" PARENT_SCOPE)
endfunction()
