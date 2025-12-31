// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <thread>
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/directory_save_data_filesystem.h"

namespace FileSys {

namespace {

constexpr int MaxRetryCount = 10;
constexpr int RetryWaitTimeMs = 100;

} // Anonymous namespace

// Updated constructor to accept mirror_filesystem
DirectorySaveDataFileSystem::DirectorySaveDataFileSystem(VirtualDir base_filesystem,
                                                         VirtualDir backup_filesystem,
                                                         VirtualDir mirror_filesystem)
    : base_fs(std::move(base_filesystem)),
      backup_fs(std::move(backup_filesystem)),
      mirror_fs(std::move(mirror_filesystem)),
      extra_data_accessor(base_fs),
      journaling_enabled(true),
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
        return extra_data_accessor.CommitExtraDataWithTimeStamp(
            std::chrono::system_clock::now().time_since_epoch().count());
    }

    if (open_writable_files > 0) {
        LOG_ERROR(Service_FS, "Cannot commit: {} writable files still open", open_writable_files);
        return ResultWriteModeFileNotClosed;
    }

    auto committed = base_fs->GetSubdirectory(CommittedDirectoryName);
    if (committed != nullptr) {
        if (!committed->Rename(SynchronizingDirectoryName)) {
            return ResultPermissionDenied;
        }
    }

    R_TRY(SynchronizeDirectory(SynchronizingDirectoryName, ModifiedDirectoryName));

    R_TRY(extra_data_accessor.CommitExtraDataWithTimeStamp(
        std::chrono::system_clock::now().time_since_epoch().count()));

    auto sync_dir = base_fs->GetSubdirectory(SynchronizingDirectoryName);
    if (sync_dir == nullptr || !sync_dir->Rename(CommittedDirectoryName)) {
        return ResultPermissionDenied;
    }

    committed_dir = base_fs->GetSubdirectory(CommittedDirectoryName);

    // Now that the NAND is safely updated, we push the changes back to Ryujinx/Eden
    if (mirror_fs != nullptr) {
        LOG_INFO(Service_FS, "Mirroring: Pushing changes back to external source...");

        // Use SmartSyncToMirror instead of CleanSubdirectoryRecursive
        // working_dir contains the data that was just successfully committed
        SmartSyncToMirror(mirror_fs, working_dir);

        LOG_INFO(Service_FS, "Mirroring: External sync successful.");
    }
    // Standard backup only if Mirroring is NOT active
    else if (Settings::values.backup_saves_to_nand.GetValue() && backup_fs != nullptr) {
        LOG_INFO(Service_FS, "Dual-Save: Backing up to NAND...");
        backup_fs->DeleteSubdirectoryRecursive(CommittedDirectoryName);
        auto nand_committed = backup_fs->CreateSubdirectory(CommittedDirectoryName);
        if (nand_committed) {
            CopyDirectoryRecursively(nand_committed, working_dir);
        }
    }

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

Result DirectorySaveDataFileSystem::RetryFinitelyForTargetLocked(std::function<Result()> operation) {
    int remaining_retries = MaxRetryCount;

    while (true) {
        Result result = operation();

        if (result == ResultSuccess) {
            return ResultSuccess;
        }

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

void DirectorySaveDataFileSystem::SmartSyncToMirror(VirtualDir mirror_dest, VirtualDir citron_source) {
    // Citron: Extra safety check for valid pointers and writable permissions
    if (mirror_dest == nullptr || citron_source == nullptr || !mirror_dest->IsWritable()) {
        return;
    }

    // Sync files from Citron back to the Mirror
    for (const auto& c_file : citron_source->GetFiles()) {
        auto m_file = mirror_dest->CreateFile(c_file->GetName());
        if (m_file) {
            m_file->WriteBytes(c_file->ReadAllBytes());
        }
    }

    // Recursively handle subfolders (like 'private', 'extra', etc)
    for (const auto& c_subdir : citron_source->GetSubdirectories()) {
        auto m_subdir = mirror_dest->GetDirectoryRelative(c_subdir->GetName());
        if (m_subdir == nullptr) {
            m_subdir = mirror_dest->CreateSubdirectory(c_subdir->GetName());
        }
        SmartSyncToMirror(m_subdir, c_subdir);
    }
}

} // namespace FileSys
