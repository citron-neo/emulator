// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <thread>
#include "common/logging/log.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/directory_save_data_filesystem.h"

namespace FileSys {

namespace {

constexpr int MaxRetryCount = 10;
constexpr int RetryWaitTimeMs = 100;

} // Anonymous namespace

DirectorySaveDataFileSystem::DirectorySaveDataFileSystem(VirtualDir base_filesystem)
    : base_fs(std::move(base_filesystem)), extra_data_accessor(base_fs), journaling_enabled(true),
      open_writable_files(0) {}

DirectorySaveDataFileSystem::~DirectorySaveDataFileSystem() = default;

Result DirectorySaveDataFileSystem::Initialize(bool enable_journaling) {
    std::scoped_lock lk{mutex};

    journaling_enabled = enable_journaling;

    // Initialize extra data
    R_TRY(extra_data_accessor.Initialize(true));

    // Get or create the working directory (always needed)
    working_dir = base_fs->GetSubdirectory(ModifiedDirectoryName);
    if (working_dir == nullptr) {
        working_dir = base_fs->CreateSubdirectory(ModifiedDirectoryName);
        if (working_dir == nullptr) {
            return ResultPermissionDenied;
        }
    }

    if (!journaling_enabled) {
        // Non-journaling mode: working directory is all we need
        return ResultSuccess;
    }

    // Get or create the committed directory
    committed_dir = base_fs->GetSubdirectory(CommittedDirectoryName);

    if (committed_dir == nullptr) {
        // Check for synchronizing directory (interrupted commit)
        auto sync_dir = base_fs->GetSubdirectory(SynchronizingDirectoryName);
        if (sync_dir != nullptr) {
            // Finish the interrupted commit
            if (!sync_dir->Rename(CommittedDirectoryName)) {
                return ResultPermissionDenied;
            }
            committed_dir = base_fs->GetSubdirectory(CommittedDirectoryName);
        } else {
            // Create committed directory and sync from working
            committed_dir = base_fs->CreateSubdirectory(CommittedDirectoryName);
            if (committed_dir == nullptr) {
                return ResultPermissionDenied;
            }

            // Initial commit: copy working → committed
            R_TRY(SynchronizeDirectory(CommittedDirectoryName, ModifiedDirectoryName));
        }
    } else {
        // Committed exists - restore working from it (previous run may have crashed)
        R_TRY(SynchronizeDirectory(ModifiedDirectoryName, CommittedDirectoryName));
    }

    return ResultSuccess;
}

VirtualDir DirectorySaveDataFileSystem::GetWorkingDirectory() {
    return working_dir;
}

VirtualDir DirectorySaveDataFileSystem::GetCommittedDirectory() {
    return committed_dir;
}

Result DirectorySaveDataFileSystem::Commit() {
    std::scoped_lock lk{mutex};

    if (!journaling_enabled) {
        // Non-journaling: just commit extra data
        return extra_data_accessor.CommitExtraDataWithTimeStamp(
            std::chrono::system_clock::now().time_since_epoch().count());
    }

    // Check that all writable files are closed
    if (open_writable_files > 0) {
        LOG_ERROR(Service_FS, "Cannot commit: {} writable files still open", open_writable_files);
        return ResultWriteModeFileNotClosed;
    }

    // Atomic commit process (based on LibHac lines 572-622)
    // 1. Rename committed → synchronizing (backup old version)
    auto committed = base_fs->GetSubdirectory(CommittedDirectoryName);
    if (committed != nullptr) {
        if (!committed->Rename(SynchronizingDirectoryName)) {
            return ResultPermissionDenied;
        }
    }

    // 2. Copy working → synchronizing (prepare new commit)
    R_TRY(SynchronizeDirectory(SynchronizingDirectoryName, ModifiedDirectoryName));

    // 3. Commit extra data with updated timestamp
    R_TRY(extra_data_accessor.CommitExtraDataWithTimeStamp(
        std::chrono::system_clock::now().time_since_epoch().count()));

    // 4. Rename synchronizing → committed (make it permanent)
    auto sync_dir = base_fs->GetSubdirectory(SynchronizingDirectoryName);
    if (sync_dir == nullptr) {
        return ResultPathNotFound;
    }

    if (!sync_dir->Rename(CommittedDirectoryName)) {
        return ResultPermissionDenied;
    }

    // Update cached committed_dir reference
    committed_dir = base_fs->GetSubdirectory(CommittedDirectoryName);

    LOG_INFO(Service_FS, "Save data committed successfully");
    return ResultSuccess;
}

Result DirectorySaveDataFileSystem::Rollback() {
    std::scoped_lock lk{mutex};

    if (!journaling_enabled) {
        // Can't rollback without journaling
        return ResultSuccess;
    }

    // Restore working directory from committed
    R_TRY(SynchronizeDirectory(ModifiedDirectoryName, CommittedDirectoryName));

    LOG_INFO(Service_FS, "Save data rolled back to last commit");
    return ResultSuccess;
}

bool DirectorySaveDataFileSystem::HasUncommittedChanges() const {
    // For now, assume any write means uncommitted changes
    // A full implementation would compare directory contents
    return open_writable_files > 0;
}

Result DirectorySaveDataFileSystem::SynchronizeDirectory(const char* dest_name,
                                                          const char* source_name) {
    auto source_dir = base_fs->GetSubdirectory(source_name);
    if (source_dir == nullptr) {
        return ResultPathNotFound;
    }

    // Delete destination if it exists
    auto dest_dir = base_fs->GetSubdirectory(dest_name);
    if (dest_dir != nullptr) {
        if (!base_fs->DeleteSubdirectoryRecursive(dest_name)) {
            return ResultPermissionDenied;
        }
    }

    // Create new destination
    dest_dir = base_fs->CreateSubdirectory(dest_name);
    if (dest_dir == nullptr) {
        return ResultPermissionDenied;
    }

    // Copy contents recursively
    return CopyDirectoryRecursively(dest_dir, source_dir);
}

Result DirectorySaveDataFileSystem::CopyDirectoryRecursively(VirtualDir dest, VirtualDir source) {
    // Copy all files
    for (const auto& file : source->GetFiles()) {
        auto new_file = dest->CreateFile(file->GetName());
        if (new_file == nullptr) {
            return ResultUsableSpaceNotEnough;
        }

        auto data = file->ReadAllBytes();
        if (new_file->WriteBytes(data) != data.size()) {
            return ResultUsableSpaceNotEnough;
        }
    }

    // Copy all subdirectories recursively
    for (const auto& subdir : source->GetSubdirectories()) {
        auto new_subdir = dest->CreateSubdirectory(subdir->GetName());
        if (new_subdir == nullptr) {
            return ResultPermissionDenied;
        }

        R_TRY(CopyDirectoryRecursively(new_subdir, subdir));
    }

    return ResultSuccess;
}

Result DirectorySaveDataFileSystem::RetryFinitelyForTargetLocked(
    std::function<Result()> operation) {
    int remaining_retries = MaxRetryCount;

    while (true) {
        Result result = operation();

        if (result == ResultSuccess) {
            return ResultSuccess;
        }

        // Only retry on TargetLocked error
        if (result != ResultTargetLocked) {
            return result;
        }

        if (remaining_retries <= 0) {
            return result;
        }

        remaining_retries--;
        std::this_thread::sleep_for(std::chrono::milliseconds(RetryWaitTimeMs));
    }
}

} // namespace FileSys
