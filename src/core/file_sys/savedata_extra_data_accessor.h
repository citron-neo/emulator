// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include "core/file_sys/fs_save_data_types.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"

namespace FileSys {

/// Manages reading and writing SaveDataExtraData with transactional semantics
/// Based on LibHac's DirectorySaveDataFileSystem extra data implementation
class SaveDataExtraDataAccessor {
public:
    explicit SaveDataExtraDataAccessor(VirtualDir save_data_directory);
    ~SaveDataExtraDataAccessor();

    /// Initialize extra data files for this save data
    Result Initialize(bool create_if_missing);

    /// Read the current extra data
    Result ReadExtraData(SaveDataExtraData* out_extra_data);

    /// Write extra data (updates working copy only)
    Result WriteExtraData(const SaveDataExtraData& extra_data);

    /// Commit extra data changes (makes working copy permanent)
    Result CommitExtraData();

    /// Update timestamp and commit ID, then commit
    Result CommitExtraDataWithTimeStamp(s64 timestamp);

    /// Check if extra data exists
    bool ExtraDataExists() const;

private:
    static constexpr const char* CommittedExtraDataFileName = "ExtraData0";
    static constexpr const char* ModifiedExtraDataFileName = "ExtraData1";
    static constexpr const char* SynchronizingExtraDataFileName = "ExtraData_";

    Result ReadExtraDataImpl(SaveDataExtraData* out_extra_data, const char* file_name);
    Result WriteExtraDataImpl(const SaveDataExtraData& extra_data, const char* file_name);
    Result SynchronizeExtraData(const char* dest_file, const char* source_file);
    Result EnsureExtraDataSize(const char* file_name);

    /// Calculate total size of directory contents
    s64 CalculateDirectorySize(VirtualDir directory) const;

    VirtualDir save_directory;
    std::mutex mutex;
    bool is_journaling_enabled;
};

} // namespace FileSys
