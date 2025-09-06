# SPDX-FileCopyrightText: 2020 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

function(copy_citron_FFmpeg_deps target_dir)
    include(WindowsCopyFiles)
    set(DLL_DEST "$<TARGET_FILE_DIR:${target_dir}>/")
    file(READ "${FFmpeg_PATH}/requirements.txt" FFmpeg_REQUIRED_DLLS)
    string(STRIP "${FFmpeg_REQUIRED_DLLS}" FFmpeg_REQUIRED_DLLS)
    windows_copy_files(${target_dir} ${FFmpeg_LIBRARY_DIR} ${DLL_DEST} ${FFmpeg_REQUIRED_DLLS})
endfunction(copy_citron_FFmpeg_deps)
