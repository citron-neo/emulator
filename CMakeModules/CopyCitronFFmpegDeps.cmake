# SPDX-FileCopyrightText: 2020 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

function(copy_citron_FFmpeg_deps target_dir)
    include(WindowsCopyFiles)
    set(DLL_DEST "$<TARGET_FILE_DIR:${target_dir}>/")

    if(NOT DEFINED FFmpeg_LIBRARY_DIR OR NOT EXISTS "${FFmpeg_LIBRARY_DIR}")
        message(WARNING
            "FFmpeg_LIBRARY_DIR ('${FFmpeg_LIBRARY_DIR}') is not set or does not exist "
            "— skipping FFmpeg DLL copy. The build will succeed but the DLLs will be "
            "absent from the output directory.")
        return()
    endif()

    if(DEFINED FFmpeg_PATH AND EXISTS "${FFmpeg_PATH}/requirements.txt")
        # Bundled pre-built FFmpeg (MinGW / Clangtron path).
        # The archive ships a requirements.txt with the exact DLL filenames.
        file(READ "${FFmpeg_PATH}/requirements.txt" FFmpeg_REQUIRED_DLLS)
        string(STRIP "${FFmpeg_REQUIRED_DLLS}" FFmpeg_REQUIRED_DLLS)
        windows_copy_files(${target_dir} ${FFmpeg_LIBRARY_DIR} ${DLL_DEST} ${FFmpeg_REQUIRED_DLLS})
    else()
        # vcpkg (MSVC) path: DLL names are versioned, e.g. avcodec-61.dll.
        # Glob for each of the four components at configure time; vcpkg has
        # already installed them into FFmpeg_LIBRARY_DIR before CMake runs.
        foreach(_ffmpeg_comp avcodec avfilter avutil swscale)
            file(GLOB _ffmpeg_dll LIST_DIRECTORIES false
                "${FFmpeg_LIBRARY_DIR}/${_ffmpeg_comp}-*.dll")
            if(_ffmpeg_dll)
                get_filename_component(_ffmpeg_dll_name "${_ffmpeg_dll}" NAME)
                windows_copy_files(${target_dir} "${FFmpeg_LIBRARY_DIR}"
                    "${DLL_DEST}" "${_ffmpeg_dll_name}")
            else()
                message(WARNING
                    "FFmpeg DLL for component '${_ffmpeg_comp}' not found in "
                    "'${FFmpeg_LIBRARY_DIR}' — it will be missing from the output directory.")
            endif()
        endforeach()
    endif()
endfunction(copy_citron_FFmpeg_deps)
