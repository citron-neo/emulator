// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/tma/file_io.h"

namespace Service::TMA {

IFileAccessor::IFileAccessor(Core::System& system_) : ServiceFramework{system_, "IFileAccessor"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IFileAccessor::ReadFile>, "ReadFile"},
        {1, D<&IFileAccessor::WriteFile>, "WriteFile"},
        {2, D<&IFileAccessor::GetFileSize>, "GetFileSize"},
        {3, D<&IFileAccessor::SetFileSize>, "SetFileSize"},
        {4, D<&IFileAccessor::FlushFile>, "FlushFile"},
        {5, D<&IFileAccessor::SetPriorityForFile>, "SetPriorityForFile"},
        {6, D<&IFileAccessor::GetPriorityForFile>, "GetPriorityForFile"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IFileAccessor::~IFileAccessor() = default;

Result IFileAccessor::ReadFile(u32 read_option, Out<s64> out_bytes_read, s64 offset,
                              OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, read_option={}, offset={}", read_option, offset);

    // No actual file data to read
    *out_bytes_read = 0;

    R_SUCCEED();
}

Result IFileAccessor::WriteFile(u32 write_option, s64 offset, InBuffer<BufferAttr_HipcMapAlias> buffer) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, write_option={}, offset={}, size={}",
                write_option, offset, buffer.size());

    // Update file size if writing beyond current size
    if (offset + static_cast<s64>(buffer.size()) > file_size) {
        file_size = offset + static_cast<s64>(buffer.size());
    }

    R_SUCCEED();
}

Result IFileAccessor::GetFileSize(Out<s64> out_file_size) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    *out_file_size = file_size;

    R_SUCCEED();
}

Result IFileAccessor::SetFileSize(s64 file_size_new) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, file_size={}", file_size_new);

    file_size = file_size_new;

    R_SUCCEED();
}

Result IFileAccessor::FlushFile() {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IFileAccessor::SetPriorityForFile(s32 priority_new) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, priority={}", priority_new);

    priority = priority_new;

    R_SUCCEED();
}

Result IFileAccessor::GetPriorityForFile(Out<s32> out_priority) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    *out_priority = priority;

    R_SUCCEED();
}

IDirectoryAccessor::IDirectoryAccessor(Core::System& system_) : ServiceFramework{system_, "IDirectoryAccessor"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDirectoryAccessor::GetEntryCount>, "GetEntryCount"},
        {1, D<&IDirectoryAccessor::ReadDirectory>, "ReadDirectory"},
        {2, D<&IDirectoryAccessor::SetPriorityForDirectory>, "SetPriorityForDirectory"},
        {3, D<&IDirectoryAccessor::GetPriorityForDirectory>, "GetPriorityForDirectory"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDirectoryAccessor::~IDirectoryAccessor() = default;

Result IDirectoryAccessor::GetEntryCount(Out<s64> out_entry_count) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return 0 entries
    *out_entry_count = 0;

    R_SUCCEED();
}

Result IDirectoryAccessor::ReadDirectory(Out<s64> out_entries_read, OutBuffer<BufferAttr_HipcMapAlias> out_entry_buffer) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // No entries to read
    *out_entries_read = 0;

    R_SUCCEED();
}

Result IDirectoryAccessor::SetPriorityForDirectory(s32 priority_new) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, priority={}", priority_new);

    priority = priority_new;

    R_SUCCEED();
}

Result IDirectoryAccessor::GetPriorityForDirectory(Out<s32> out_priority) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    *out_priority = priority;

    R_SUCCEED();
}

IFileManager::IFileManager(Core::System& system_) : ServiceFramework{system_, "file_io"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IFileManager::OpenFile>, "OpenFile"},
        {1, D<&IFileManager::FileExists>, "FileExists"},
        {2, D<&IFileManager::DeleteFile>, "DeleteFile"},
        {3, D<&IFileManager::RenameFile>, "RenameFile"},
        {4, D<&IFileManager::GetIOType>, "GetIOType"},
        {5, D<&IFileManager::OpenDirectory>, "OpenDirectory"},
        {6, D<&IFileManager::DirectoryExists>, "DirectoryExists"},
        {7, D<&IFileManager::CreateDirectory>, "CreateDirectory"},
        {8, D<&IFileManager::DeleteDirectory>, "DeleteDirectory"},
        {9, D<&IFileManager::RenameDirectory>, "RenameDirectory"},
        {10, D<&IFileManager::CreateFile>, "CreateFile"},
        {11, D<&IFileManager::GetFileTimeStamp>, "GetFileTimeStamp"},
        {12, D<&IFileManager::GetCaseSensitivePath>, "GetCaseSensitivePath"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IFileManager::~IFileManager() = default;

Result IFileManager::OpenFile(OutInterface<IFileAccessor> out_file_accessor,
                             InBuffer<BufferAttr_HipcMapAlias> path, u32 open_mode) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, open_mode={}", open_mode);

    *out_file_accessor = std::make_shared<IFileAccessor>(system);

    R_SUCCEED();
}

Result IFileManager::FileExists(Out<bool> out_exists, InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // No files exist on host
    *out_exists = false;

    R_SUCCEED();
}

Result IFileManager::DeleteFile(InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IFileManager::RenameFile(InBuffer<BufferAttr_HipcMapAlias> src_path,
                               InBuffer<BufferAttr_HipcMapAlias> dst_path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IFileManager::GetIOType(Out<s32> out_entry_type, InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return 0 (does not exist)
    *out_entry_type = 0;

    R_SUCCEED();
}

Result IFileManager::OpenDirectory(OutInterface<IDirectoryAccessor> out_directory_accessor,
                                  InBuffer<BufferAttr_HipcMapAlias> path, u32 open_mode) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, open_mode={}", open_mode);

    *out_directory_accessor = std::make_shared<IDirectoryAccessor>(system);

    R_SUCCEED();
}

Result IFileManager::DirectoryExists(Out<bool> out_exists, InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // No directories exist on host
    *out_exists = false;

    R_SUCCEED();
}

Result IFileManager::CreateDirectory(InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IFileManager::DeleteDirectory(InBuffer<BufferAttr_HipcMapAlias> path, bool is_recursive) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, is_recursive={}", is_recursive);

    R_SUCCEED();
}

Result IFileManager::RenameDirectory(InBuffer<BufferAttr_HipcMapAlias> src_path,
                                    InBuffer<BufferAttr_HipcMapAlias> dst_path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    R_SUCCEED();
}

Result IFileManager::CreateFile(InBuffer<BufferAttr_HipcMapAlias> path, s64 size) {
    LOG_WARNING(Service_TMA, "(STUBBED) called, size={}", size);

    R_SUCCEED();
}

Result IFileManager::GetFileTimeStamp(Out<u64> out_created_time, Out<u64> out_accessed_time,
                                     Out<u64> out_modified_time, InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return 0 timestamps
    *out_created_time = 0;
    *out_accessed_time = 0;
    *out_modified_time = 0;

    R_SUCCEED();
}

Result IFileManager::GetCaseSensitivePath(OutBuffer<BufferAttr_HipcMapAlias> out_path,
                                         InBuffer<BufferAttr_HipcMapAlias> path) {
    LOG_WARNING(Service_TMA, "(STUBBED) called");

    // Return the input path as-is
    size_t copy_size = std::min(out_path.size(), path.size());
    std::memcpy(out_path.data(), path.data(), copy_size);

    // Null terminate if there's space
    if (copy_size < out_path.size()) {
        out_path[copy_size] = 0;
    }

    R_SUCCEED();
}

} // namespace Service::TMA