// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <utility>

#include "common/assert.h"
#include "core/file_sys/vfs/vfs_concat.h"
#include "core/file_sys/vfs/vfs_static.h"

namespace FileSys {

ConcatenatedVfsFile::ConcatenatedVfsFile(std::string&& name_, ConcatenationMap&& concatenation_map_)
    : concatenation_map(std::move(concatenation_map_)), name(std::move(name_)) {
    DEBUG_ASSERT(this->VerifyContinuity());
}

bool ConcatenatedVfsFile::VerifyContinuity() const {
    u64 last_offset = 0;
    for (auto& entry : concatenation_map) {
        if (entry.offset != last_offset) {
            return false;
        }

        last_offset = entry.offset + entry.file->GetSize();
    }

    return true;
}

ConcatenatedVfsFile::~ConcatenatedVfsFile() = default;

VirtualFile ConcatenatedVfsFile::MakeConcatenatedFile(std::string&& name,
                                                      std::vector<VirtualFile>&& files) {
    // Fold trivial cases.
    if (files.empty()) {
        return nullptr;
    }
    if (files.size() == 1) {
        return files.front();
    }

    // Make the concatenation map from the input.
    std::vector<ConcatenationEntry> concatenation_map;
    concatenation_map.reserve(files.size());
    u64 last_offset = 0;

    for (auto& file : files) {
        const auto size = file->GetSize();

        concatenation_map.emplace_back(ConcatenationEntry{
            .offset = last_offset,
            .file = std::move(file),
        });

        last_offset += size;
    }

    return VirtualFile(new ConcatenatedVfsFile(std::move(name), std::move(concatenation_map)));
}

VirtualFile ConcatenatedVfsFile::MakeConcatenatedFile(
    u8 filler_byte, std::string&& name, std::vector<std::pair<u64, VirtualFile>>&& files) {
    // Fold trivial cases.
    if (files.empty()) {
        return nullptr;
    }
    if (files.size() == 1) {
        return files.begin()->second;
    }

    // Make the concatenation map from the input.
    std::vector<ConcatenationEntry> concatenation_map;

    concatenation_map.reserve(files.size());
    u64 last_offset = 0;

    // Iteration of a multimap is ordered, so offset will be strictly non-decreasing.
    for (auto& [offset, file] : files) {
        const auto size = file->GetSize();

        if (offset > last_offset) {
            concatenation_map.emplace_back(ConcatenationEntry{
                .offset = last_offset,
                .file = std::make_shared<StaticVfsFile>(filler_byte, offset - last_offset),
            });
        }

        concatenation_map.emplace_back(ConcatenationEntry{
            .offset = offset,
            .file = std::move(file),
        });

        last_offset = offset + size;
    }

    return VirtualFile(new ConcatenatedVfsFile(std::move(name), std::move(concatenation_map)));
}

std::string ConcatenatedVfsFile::GetName() const {
    if (concatenation_map.empty()) {
        return "";
    }
    if (!name.empty()) {
        return name;
    }
    return concatenation_map.front().file->GetName();
}

std::size_t ConcatenatedVfsFile::GetSize() const {
    if (concatenation_map.empty()) {
        return 0;
    }
    return concatenation_map.back().offset + concatenation_map.back().file->GetSize();
}

bool ConcatenatedVfsFile::Resize(std::size_t new_size) {
    return false;
}

VirtualDir ConcatenatedVfsFile::GetContainingDirectory() const {
    if (concatenation_map.empty()) {
        return nullptr;
    }
    return concatenation_map.front().file->GetContainingDirectory();
}

bool ConcatenatedVfsFile::IsWritable() const {
    return false;
}

bool ConcatenatedVfsFile::IsReadable() const {
    return true;
}

std::size_t ConcatenatedVfsFile::Read(u8* data, std::size_t length, std::size_t offset) const {
    const ConcatenationEntry key{
        .offset = offset,
        .file = nullptr,
    };

    // Read nothing if the map is empty.
    if (concatenation_map.empty()) {
        return 0;
    }

    // Binary search to find the iterator to the first position we can check.
    // It must exist, since we are not empty and are comparing unsigned integers.
    auto it = std::prev(std::upper_bound(concatenation_map.begin(), concatenation_map.end(), key));
    u64 cur_length = length;
    u64 cur_offset = offset;

    while (cur_length > 0 && it != concatenation_map.end()) {
        // Check if we can read the file at this position.
        const auto& file = it->file;
        const u64 map_offset = it->offset;
        const u64 file_size = file->GetSize();

        if (cur_offset > map_offset + file_size) {
            // Entirely out of bounds read.
            break;
        }

        // Read the file at this position.
        const u64 file_seek = cur_offset - map_offset;
        const u64 intended_read_size = std::min<u64>(cur_length, file_size - file_seek);
        const u64 actual_read_size =
            file->Read(data + (cur_offset - offset), intended_read_size, file_seek);

        // Update tracking.
        cur_offset += actual_read_size;
        cur_length -= actual_read_size;
        it++;

        // If we encountered a short read, we're done.
        if (actual_read_size < intended_read_size) {
            break;
        }
    }

    return cur_offset - offset;
}

std::size_t ConcatenatedVfsFile::Write(const u8* data, std::size_t length, std::size_t offset) {
    return 0;
}

bool ConcatenatedVfsFile::Rename(std::string_view new_name) {
    return false;
}

} // namespace FileSys
