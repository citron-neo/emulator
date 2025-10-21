// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include "common/logging/log.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/savedata_extra_data_accessor.h"

namespace FileSys {

SaveDataExtraDataAccessor::SaveDataExtraDataAccessor(VirtualDir save_data_directory)
    : save_directory(std::move(save_data_directory)), is_journaling_enabled(true) {}

SaveDataExtraDataAccessor::~SaveDataExtraDataAccessor() = default;

Result SaveDataExtraDataAccessor::Initialize(bool create_if_missing) {
    std::scoped_lock lk{mutex};

    // Check if modified (working) extra data exists
    auto modified_file = save_directory->GetFile(ModifiedExtraDataFileName);

    if (modified_file == nullptr) {
        if (!create_if_missing) {
            return ResultPathNotFound;
        }

        // Create the modified extra data file
        modified_file = save_directory->CreateFile(ModifiedExtraDataFileName);
        if (modified_file == nullptr) {
            return ResultPermissionDenied;
        }

        if (!modified_file->Resize(sizeof(SaveDataExtraData))) {
            return ResultUsableSpaceNotEnough;
        }

        // Initialize with zeros
        SaveDataExtraData initial_data{};
        modified_file->WriteObject(initial_data);
    }

    // Ensure modified file is correct size
    R_TRY(EnsureExtraDataSize(ModifiedExtraDataFileName));

    // Check for committed extra data (for journaling)
    auto committed_file = save_directory->GetFile(CommittedExtraDataFileName);

    if (committed_file == nullptr) {
        // Check if synchronizing file exists (interrupted commit)
        auto sync_file = save_directory->GetFile(SynchronizingExtraDataFileName);

        if (sync_file != nullptr) {
            // Interrupted commit - finish it
            if (!sync_file->Rename(CommittedExtraDataFileName)) {
                return ResultPermissionDenied;
            }
        } else if (create_if_missing) {
            // Create committed file
            committed_file = save_directory->CreateFile(CommittedExtraDataFileName);
            if (committed_file == nullptr) {
                return ResultPermissionDenied;
            }

            if (!committed_file->Resize(sizeof(SaveDataExtraData))) {
                return ResultUsableSpaceNotEnough;
            }

            // Copy from modified to committed
            R_TRY(SynchronizeExtraData(CommittedExtraDataFileName, ModifiedExtraDataFileName));
        }
    } else {
        // Ensure committed file is correct size
        R_TRY(EnsureExtraDataSize(CommittedExtraDataFileName));

        // If journaling is enabled, sync committed → modified
        if (is_journaling_enabled) {
            R_TRY(SynchronizeExtraData(ModifiedExtraDataFileName, CommittedExtraDataFileName));
        }
    }

    return ResultSuccess;
}

Result SaveDataExtraDataAccessor::ReadExtraData(SaveDataExtraData* out_extra_data) {
    std::scoped_lock lk{mutex};

    // For journaling: read from committed if it exists, otherwise modified
    // For non-journaling: always read from modified
    const char* file_to_read =
        is_journaling_enabled ? CommittedExtraDataFileName : ModifiedExtraDataFileName;

    auto file = save_directory->GetFile(file_to_read);
    if (file == nullptr) {
        // Fallback to modified if committed doesn't exist
        file = save_directory->GetFile(ModifiedExtraDataFileName);
        if (file == nullptr) {
            return ResultPathNotFound;
        }
        file_to_read = ModifiedExtraDataFileName;
    }

    Result result = ReadExtraDataImpl(out_extra_data, file_to_read);
    if (result != ResultSuccess) {
        return result;
    }

    // Update size information based on current directory contents
    out_extra_data->available_size = CalculateDirectorySize(save_directory);

    return ResultSuccess;
}

s64 SaveDataExtraDataAccessor::CalculateDirectorySize(VirtualDir directory) const {
    if (directory == nullptr) {
        return 0;
    }

    s64 total_size = 0;

    // Add file sizes
    for (const auto& file : directory->GetFiles()) {
        total_size += file->GetSize();
    }

    // Add subdirectory sizes recursively (but skip ExtraData files)
    for (const auto& subdir : directory->GetSubdirectories()) {
        total_size += CalculateDirectorySize(subdir);
    }

    return total_size;
}

Result SaveDataExtraDataAccessor::WriteExtraData(const SaveDataExtraData& extra_data) {
    std::scoped_lock lk{mutex};

    return WriteExtraDataImpl(extra_data, ModifiedExtraDataFileName);
}

Result SaveDataExtraDataAccessor::CommitExtraData() {
    std::scoped_lock lk{mutex};

    if (!is_journaling_enabled) {
        // Non-journaling: just write directly to the file
        return ResultSuccess;
    }

    // Journaling: Atomic commit process
    // 1. Rename committed → synchronizing (backup)
    auto committed_file = save_directory->GetFile(CommittedExtraDataFileName);
    if (committed_file != nullptr) {
        if (!committed_file->Rename(SynchronizingExtraDataFileName)) {
            return ResultPermissionDenied;
        }
    }

    // 2. Copy modified → synchronizing
    R_TRY(SynchronizeExtraData(SynchronizingExtraDataFileName, ModifiedExtraDataFileName));

    // 3. Rename synchronizing → committed (make it permanent)
    auto sync_file = save_directory->GetFile(SynchronizingExtraDataFileName);
    if (sync_file == nullptr) {
        return ResultPathNotFound;
    }

    if (!sync_file->Rename(CommittedExtraDataFileName)) {
        return ResultPermissionDenied;
    }

    return ResultSuccess;
}

Result SaveDataExtraDataAccessor::CommitExtraDataWithTimeStamp(s64 timestamp) {
    std::scoped_lock lk{mutex};

    // Read current extra data
    SaveDataExtraData extra_data{};
    R_TRY(ReadExtraDataImpl(&extra_data, ModifiedExtraDataFileName));

    // Update timestamp
    extra_data.timestamp = timestamp;

    // Generate new commit ID (non-zero, different from previous)
    if (extra_data.commit_id == 0) {
        extra_data.commit_id = 1;
    } else {
        extra_data.commit_id++;
    }

    // Write updated data
    R_TRY(WriteExtraDataImpl(extra_data, ModifiedExtraDataFileName));

    // Unlock mutex for commit
    mutex.unlock();
    Result result = CommitExtraData();
    mutex.lock();

    return result;
}

bool SaveDataExtraDataAccessor::ExtraDataExists() const {
    return save_directory->GetFile(ModifiedExtraDataFileName) != nullptr ||
           save_directory->GetFile(CommittedExtraDataFileName) != nullptr;
}

Result SaveDataExtraDataAccessor::ReadExtraDataImpl(SaveDataExtraData* out_extra_data,
                                                     const char* file_name) {
    auto file = save_directory->GetFile(file_name);
    if (file == nullptr) {
        return ResultPathNotFound;
    }

    if (file->GetSize() < sizeof(SaveDataExtraData)) {
        LOG_ERROR(Service_FS, "ExtraData file {} is too small: {} bytes", file_name,
                  file->GetSize());
        return ResultDataCorrupted;
    }

    const auto bytes_read = file->ReadObject(out_extra_data);
    if (bytes_read != sizeof(SaveDataExtraData)) {
        LOG_ERROR(Service_FS, "Failed to read ExtraData from {}: read {} bytes", file_name,
                  bytes_read);
        return ResultDataCorrupted;
    }

    return ResultSuccess;
}

Result SaveDataExtraDataAccessor::WriteExtraDataImpl(const SaveDataExtraData& extra_data,
                                                      const char* file_name) {
    auto file = save_directory->GetFile(file_name);
    if (file == nullptr) {
        // Create the file if it doesn't exist
        file = save_directory->CreateFile(file_name);
        if (file == nullptr) {
            return ResultPermissionDenied;
        }

        if (!file->Resize(sizeof(SaveDataExtraData))) {
            return ResultUsableSpaceNotEnough;
        }
    }

    const auto bytes_written = file->WriteObject(extra_data);
    if (bytes_written != sizeof(SaveDataExtraData)) {
        LOG_ERROR(Service_FS, "Failed to write ExtraData to {}: wrote {} bytes", file_name,
                  bytes_written);
        return ResultUsableSpaceNotEnough;
    }

    return ResultSuccess;
}

Result SaveDataExtraDataAccessor::SynchronizeExtraData(const char* dest_file,
                                                        const char* source_file) {
    // Read from source
    SaveDataExtraData extra_data{};
    R_TRY(ReadExtraDataImpl(&extra_data, source_file));

    // Write to destination
    R_TRY(WriteExtraDataImpl(extra_data, dest_file));

    return ResultSuccess;
}

Result SaveDataExtraDataAccessor::EnsureExtraDataSize(const char* file_name) {
    auto file = save_directory->GetFile(file_name);
    if (file == nullptr) {
        return ResultPathNotFound;
    }

    const auto current_size = file->GetSize();
    if (current_size == sizeof(SaveDataExtraData)) {
        return ResultSuccess;
    }

    LOG_WARNING(Service_FS, "ExtraData file {} has incorrect size: {} bytes, resizing to {}",
                file_name, current_size, sizeof(SaveDataExtraData));

    if (!file->Resize(sizeof(SaveDataExtraData))) {
        return ResultUsableSpaceNotEnough;
    }

    return ResultSuccess;
}

} // namespace FileSys
