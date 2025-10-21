// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include "core/file_sys/fs_save_data_types.h"
#include "core/file_sys/savedata_extra_data_accessor.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"

namespace FileSys {

/// A filesystem wrapper that provides transactional commit semantics for save data
/// Based on LibHac's DirectorySaveDataFileSystem implementation
/// Uses /0 (committed) and /1 (working) directories for journaling
class DirectorySaveDataFileSystem {
public:
    explicit DirectorySaveDataFileSystem(VirtualDir base_filesystem);
    ~DirectorySaveDataFileSystem();

    /// Initialize the journaling filesystem
    Result Initialize(bool enable_journaling);

    /// Get the working directory (where changes are made)
    VirtualDir GetWorkingDirectory();

    /// Get the committed directory (stable version)
    VirtualDir GetCommittedDirectory();

    /// Commit all changes (makes working directory the new committed version)
    Result Commit();

    /// Rollback changes (restore working directory from committed)
    Result Rollback();

    /// Check if there are uncommitted changes
    bool HasUncommittedChanges() const;

    /// Get the extra data accessor for this save
    SaveDataExtraDataAccessor& GetExtraDataAccessor() {
        return extra_data_accessor;
    }

private:
    static constexpr const char* CommittedDirectoryName = "0";
    static constexpr const char* ModifiedDirectoryName = "1";
    static constexpr const char* SynchronizingDirectoryName = "_";

    Result SynchronizeDirectory(const char* dest_name, const char* source_name);
    Result CopyDirectoryRecursively(VirtualDir dest, VirtualDir source);
    Result RetryFinitelyForTargetLocked(std::function<Result()> operation);

    VirtualDir base_fs;
    VirtualDir working_dir;
    VirtualDir committed_dir;
    SaveDataExtraDataAccessor extra_data_accessor;
    std::mutex mutex;
    bool journaling_enabled;
    int open_writable_files;
};

} // namespace FileSys
