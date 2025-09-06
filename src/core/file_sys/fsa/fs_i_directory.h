// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/fs_directory.h"
#include "core/file_sys/fs_file.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"

namespace FileSys::Fsa {

class IDirectory {
public:
    explicit IDirectory(VirtualDir backend_, OpenDirectoryMode mode)
        : backend(std::move(backend_)) {
        // TODO(DarkLordZach): Verify that this is the correct behavior.
        // Build entry index now to save time later.
        if (True(mode & OpenDirectoryMode::Directory)) {
            BuildEntryIndex(backend->GetSubdirectories(), DirectoryEntryType::Directory);
        }
        if (True(mode & OpenDirectoryMode::File)) {
            BuildEntryIndex(backend->GetFiles(), DirectoryEntryType::File);
        }
    }
    virtual ~IDirectory() {}

    Result Read(s64* out_count, DirectoryEntry* out_entries, s64 max_entries) {
        R_UNLESS(out_count != nullptr, ResultNullptrArgument);
        if (max_entries == 0) {
            *out_count = 0;
            R_SUCCEED();
        }
        R_UNLESS(out_entries != nullptr, ResultNullptrArgument);
        R_UNLESS(max_entries > 0, ResultInvalidArgument);
        R_RETURN(this->DoRead(out_count, out_entries, max_entries));
    }

    Result GetEntryCount(s64* out) {
        R_UNLESS(out != nullptr, ResultNullptrArgument);
        R_RETURN(this->DoGetEntryCount(out));
    }

private:
    Result DoRead(s64* out_count, DirectoryEntry* out_entries, s64 max_entries) {
        const u64 actual_entries =
            std::min(static_cast<u64>(max_entries), entries.size() - next_entry_index);
        const auto* begin = reinterpret_cast<u8*>(entries.data() + next_entry_index);
        const auto* end = reinterpret_cast<u8*>(entries.data() + next_entry_index + actual_entries);
        const auto range_size = static_cast<std::size_t>(std::distance(begin, end));

        next_entry_index += actual_entries;
        *out_count = actual_entries;

        std::memcpy(out_entries, begin, range_size);

        R_SUCCEED();
    }

    Result DoGetEntryCount(s64* out) {
        *out = entries.size() - next_entry_index;
        R_SUCCEED();
    }

    // TODO: Remove this when VFS is gone
    template <typename T>
    void BuildEntryIndex(const std::vector<T>& new_data, DirectoryEntryType type) {
        entries.reserve(entries.size() + new_data.size());

        for (const auto& new_entry : new_data) {
            auto name = new_entry->GetName();

            if (type == DirectoryEntryType::File && name == GetSaveDataSizeFileName()) {
                continue;
            }

            entries.emplace_back(name, static_cast<s8>(type),
                                 type == DirectoryEntryType::Directory ? 0 : new_entry->GetSize());
        }
    }

    VirtualDir backend;
    std::vector<DirectoryEntry> entries;
    u64 next_entry_index = 0;
};

} // namespace FileSys::Fsa
