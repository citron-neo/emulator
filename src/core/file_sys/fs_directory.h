// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace FileSys {

constexpr inline size_t EntryNameLengthMax = 0x300;

struct DirectoryEntry {
    DirectoryEntry(std::string_view view, s8 entry_type, u64 entry_size)
        : type{entry_type}, file_size{static_cast<s64>(entry_size)} {
        const std::size_t copy_size = view.copy(name, std::size(name) - 1);
        name[copy_size] = '\0';
    }

    char name[EntryNameLengthMax + 1];
    INSERT_PADDING_BYTES(3);
    s8 type;
    INSERT_PADDING_BYTES(3);
    s64 file_size;
};

static_assert(sizeof(DirectoryEntry) == 0x310,
              "Directory Entry struct isn't exactly 0x310 bytes long!");
static_assert(offsetof(DirectoryEntry, type) == 0x304, "Wrong offset for type in Entry.");
static_assert(offsetof(DirectoryEntry, file_size) == 0x308, "Wrong offset for file_size in Entry.");

struct DirectoryHandle {
    void* handle;
};

} // namespace FileSys
