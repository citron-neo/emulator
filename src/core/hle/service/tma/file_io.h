// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::TMA {

class IFileAccessor final : public ServiceFramework<IFileAccessor> {
public:
    explicit IFileAccessor(Core::System& system_);
    ~IFileAccessor() override;

private:
    Result ReadFile(u32 read_option, Out<s64> out_bytes_read, s64 offset,
                   OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result WriteFile(u32 write_option, s64 offset, InBuffer<BufferAttr_HipcMapAlias> buffer);
    Result GetFileSize(Out<s64> out_file_size);
    Result SetFileSize(s64 file_size);
    Result FlushFile();
    Result SetPriorityForFile(s32 priority);
    Result GetPriorityForFile(Out<s32> out_priority);

    s64 file_size{};
    s32 priority{};
};

class IDirectoryAccessor final : public ServiceFramework<IDirectoryAccessor> {
public:
    explicit IDirectoryAccessor(Core::System& system_);
    ~IDirectoryAccessor() override;

private:
    Result GetEntryCount(Out<s64> out_entry_count);
    Result ReadDirectory(Out<s64> out_entries_read, OutBuffer<BufferAttr_HipcMapAlias> out_entry_buffer);
    Result SetPriorityForDirectory(s32 priority);
    Result GetPriorityForDirectory(Out<s32> out_priority);

    s32 priority{};
};

class IFileManager final : public ServiceFramework<IFileManager> {
public:
    explicit IFileManager(Core::System& system_);
    ~IFileManager() override;

private:
    Result OpenFile(OutInterface<IFileAccessor> out_file_accessor,
                   InBuffer<BufferAttr_HipcMapAlias> path, u32 open_mode);
    Result FileExists(Out<bool> out_exists, InBuffer<BufferAttr_HipcMapAlias> path);
    Result DeleteFile(InBuffer<BufferAttr_HipcMapAlias> path);
    Result RenameFile(InBuffer<BufferAttr_HipcMapAlias> src_path,
                     InBuffer<BufferAttr_HipcMapAlias> dst_path);
    Result GetIOType(Out<s32> out_entry_type, InBuffer<BufferAttr_HipcMapAlias> path);
    Result OpenDirectory(OutInterface<IDirectoryAccessor> out_directory_accessor,
                        InBuffer<BufferAttr_HipcMapAlias> path, u32 open_mode);
    Result DirectoryExists(Out<bool> out_exists, InBuffer<BufferAttr_HipcMapAlias> path);
    Result CreateDirectory(InBuffer<BufferAttr_HipcMapAlias> path);
    Result DeleteDirectory(InBuffer<BufferAttr_HipcMapAlias> path, bool is_recursive);
    Result RenameDirectory(InBuffer<BufferAttr_HipcMapAlias> src_path,
                          InBuffer<BufferAttr_HipcMapAlias> dst_path);
    Result CreateFile(InBuffer<BufferAttr_HipcMapAlias> path, s64 size);
    Result GetFileTimeStamp(Out<u64> out_created_time, Out<u64> out_accessed_time,
                           Out<u64> out_modified_time, InBuffer<BufferAttr_HipcMapAlias> path);
    Result GetCaseSensitivePath(OutBuffer<BufferAttr_HipcMapAlias> out_path,
                               InBuffer<BufferAttr_HipcMapAlias> path);
};

} // namespace Service::TMA