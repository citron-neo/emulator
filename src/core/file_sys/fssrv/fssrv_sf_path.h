// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/fs_directory.h"

namespace FileSys::Sf {

struct Path {
    char str[EntryNameLengthMax + 1];

    static constexpr Path Encode(const char* p) {
        Path path = {};
        for (size_t i = 0; i < sizeof(path) - 1; i++) {
            path.str[i] = p[i];
            if (p[i] == '\x00') {
                break;
            }
        }
        return path;
    }

    static constexpr size_t GetPathLength(const Path& path) {
        size_t len = 0;
        for (size_t i = 0; i < sizeof(path) - 1 && path.str[i] != '\x00'; i++) {
            len++;
        }
        return len;
    }
};
static_assert(std::is_trivially_copyable_v<Path>, "Path must be trivially copyable.");

using FspPath = Path;

} // namespace FileSys::Sf
