# SPDX-FileCopyrightText: 2020 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

function(copy_citron_FFmpeg_deps target_dir)
    include(WindowsCopyFiles)
    set(DLL_DEST "$<TARGET_FILE_DIR:${target_dir}>/")

    if(DEFINED FFmpeg_PATH AND EXISTS "${FFmpeg_PATH}/requirements.txt")
        # ── Bundled pre-built FFmpeg (MinGW / Clangtron path) ────────────────
        # The archive ships a requirements.txt with the exact DLL filenames.
        if(NOT DEFINED FFmpeg_LIBRARY_DIR OR NOT EXISTS "${FFmpeg_LIBRARY_DIR}")
            message(WARNING
                "FFmpeg_LIBRARY_DIR ('${FFmpeg_LIBRARY_DIR}') is not set or does not "
                "exist — skipping FFmpeg DLL copy.")
            return()
        endif()
        file(READ "${FFmpeg_PATH}/requirements.txt" FFmpeg_REQUIRED_DLLS)
        string(STRIP "${FFmpeg_REQUIRED_DLLS}" FFmpeg_REQUIRED_DLLS)
        windows_copy_files(${target_dir} ${FFmpeg_LIBRARY_DIR} ${DLL_DEST}
            ${FFmpeg_REQUIRED_DLLS})

    else()
        # ── vcpkg (MSVC) path ────────────────────────────────────────────────
        # DLL names are versioned (e.g. avcodec-62.dll) and differ between
        # release and debug trees only by directory, not filename.
        #
        # The source directory is chosen at *build* time via a generator
        # expression so that a Debug build copies from debug/bin/ and a Release
        # build copies from bin/.  This is why FFmpeg_LIBRARY_DIR_DEBUG is also
        # exported from externals/ffmpeg/CMakeLists.txt.

        if(NOT DEFINED FFmpeg_LIBRARY_DIR OR NOT EXISTS "${FFmpeg_LIBRARY_DIR}")
            message(WARNING
                "FFmpeg_LIBRARY_DIR ('${FFmpeg_LIBRARY_DIR}') is not set or does not "
                "exist — skipping FFmpeg DLL copy.")
            return()
        endif()

        # If no debug dir was exported (shouldn't happen, but be safe) fall
        # back to the release dir so at least Release builds work.
        if(NOT DEFINED FFmpeg_LIBRARY_DIR_DEBUG OR NOT EXISTS "${FFmpeg_LIBRARY_DIR_DEBUG}")
            set(FFmpeg_LIBRARY_DIR_DEBUG "${FFmpeg_LIBRARY_DIR}")
        endif()

        # Collect the versioned DLL names from the release tree at configure
        # time.  The same filenames exist in the debug tree.
        set(_ffmpeg_dll_names "")
        foreach(_comp avcodec avfilter avutil swscale)
            file(GLOB _found LIST_DIRECTORIES false
                "${FFmpeg_LIBRARY_DIR}/${_comp}-*.dll")
            if(_found)
                get_filename_component(_name "${_found}" NAME)
                list(APPEND _ffmpeg_dll_names "${_name}")
            else()
                message(WARNING
                    "FFmpeg DLL for '${_comp}' not found in '${FFmpeg_LIBRARY_DIR}' "
                    "— it will be missing from the output directory.")
            endif()
        endforeach()

        if(NOT _ffmpeg_dll_names)
            return()
        endif()

        # Generator expression selects the correct source tree per config.
        set(_src_dir
            "$<IF:$<CONFIG:Debug>,${FFmpeg_LIBRARY_DIR_DEBUG},${FFmpeg_LIBRARY_DIR}>")

        windows_copy_files(${target_dir} "${_src_dir}" "${DLL_DEST}"
            ${_ffmpeg_dll_names})
    endif()
endfunction(copy_citron_FFmpeg_deps)
