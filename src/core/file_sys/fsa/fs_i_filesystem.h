// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/file_sys/errors.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/fs_path.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/result.h"
#include "core/hle/service/filesystem/filesystem.h"

namespace FileSys::Fsa {

class IFile;
class IDirectory;

enum class QueryId : u32 {
    SetConcatenationFileAttribute = 0,
    UpdateMac = 1,
    IsSignedSystemPartitionOnSdCardValid = 2,
    QueryUnpreparedFileInformation = 3,
};

class IFileSystem {
public:
    explicit IFileSystem(VirtualDir backend_) : backend{std::move(backend_)} {}
    virtual ~IFileSystem() {}

    Result CreateFile(const Path& path, s64 size, CreateOption option) {
        R_UNLESS(size >= 0, ResultOutOfRange);
        R_RETURN(this->DoCreateFile(path, size, static_cast<int>(option)));
    }

    Result CreateFile(const Path& path, s64 size) {
        R_RETURN(this->CreateFile(path, size, CreateOption::None));
    }

    Result DeleteFile(const Path& path) {
        R_RETURN(this->DoDeleteFile(path));
    }

    Result CreateDirectory(const Path& path) {
        R_RETURN(this->DoCreateDirectory(path));
    }

    Result DeleteDirectory(const Path& path) {
        R_RETURN(this->DoDeleteDirectory(path));
    }

    Result DeleteDirectoryRecursively(const Path& path) {
        R_RETURN(this->DoDeleteDirectoryRecursively(path));
    }

    Result RenameFile(const Path& old_path, const Path& new_path) {
        R_RETURN(this->DoRenameFile(old_path, new_path));
    }

    Result RenameDirectory(const Path& old_path, const Path& new_path) {
        R_RETURN(this->DoRenameDirectory(old_path, new_path));
    }

    Result GetEntryType(DirectoryEntryType* out, const Path& path) {
        R_RETURN(this->DoGetEntryType(out, path));
    }

    Result OpenFile(VirtualFile* out_file, const Path& path, OpenMode mode) {
        R_UNLESS(out_file != nullptr, ResultNullptrArgument);
        R_UNLESS(static_cast<u32>(mode & OpenMode::ReadWrite) != 0, ResultInvalidOpenMode);
        R_UNLESS(static_cast<u32>(mode & ~OpenMode::All) == 0, ResultInvalidOpenMode);
        R_RETURN(this->DoOpenFile(out_file, path, mode));
    }

    Result OpenDirectory(VirtualDir* out_dir, const Path& path, OpenDirectoryMode mode) {
        R_UNLESS(out_dir != nullptr, ResultNullptrArgument);
        R_UNLESS(static_cast<u64>(mode & OpenDirectoryMode::All) != 0, ResultInvalidOpenMode);
        R_UNLESS(static_cast<u64>(
                     mode & ~(OpenDirectoryMode::All | OpenDirectoryMode::NotRequireFileSize)) == 0,
                 ResultInvalidOpenMode);
        R_RETURN(this->DoOpenDirectory(out_dir, path, mode));
    }

    Result Commit() {
        R_RETURN(this->DoCommit());
    }

    Result GetFreeSpaceSize(s64* out, const Path& path) {
        R_UNLESS(out != nullptr, ResultNullptrArgument);
        R_RETURN(this->DoGetFreeSpaceSize(out, path));
    }

    Result GetTotalSpaceSize(s64* out, const Path& path) {
        R_UNLESS(out != nullptr, ResultNullptrArgument);
        R_RETURN(this->DoGetTotalSpaceSize(out, path));
    }

    Result CleanDirectoryRecursively(const Path& path) {
        R_RETURN(this->DoCleanDirectoryRecursively(path));
    }

    Result GetFileTimeStampRaw(FileTimeStampRaw* out, const Path& path) {
        R_UNLESS(out != nullptr, ResultNullptrArgument);
        R_RETURN(this->DoGetFileTimeStampRaw(out, path));
    }

    Result QueryEntry(char* dst, size_t dst_size, const char* src, size_t src_size, QueryId query,
                      const Path& path) {
        R_RETURN(this->DoQueryEntry(dst, dst_size, src, src_size, query, path));
    }

    // These aren't accessible as commands
    Result CommitProvisionally(s64 counter) {
        R_RETURN(this->DoCommitProvisionally(counter));
    }

    Result Rollback() {
        R_RETURN(this->DoRollback());
    }

    Result Flush() {
        R_RETURN(this->DoFlush());
    }

private:
    Result DoCreateFile(const Path& path, s64 size, int flags) {
        R_RETURN(backend.CreateFile(path.GetString(), size));
    }

    Result DoDeleteFile(const Path& path) {
        R_RETURN(backend.DeleteFile(path.GetString()));
    }

    Result DoCreateDirectory(const Path& path) {
        R_RETURN(backend.CreateDirectory(path.GetString()));
    }

    Result DoDeleteDirectory(const Path& path) {
        R_RETURN(backend.DeleteDirectory(path.GetString()));
    }

    Result DoDeleteDirectoryRecursively(const Path& path) {
        R_RETURN(backend.DeleteDirectoryRecursively(path.GetString()));
    }

    Result DoRenameFile(const Path& old_path, const Path& new_path) {
        R_RETURN(backend.RenameFile(old_path.GetString(), new_path.GetString()));
    }

    Result DoRenameDirectory(const Path& old_path, const Path& new_path) {
        R_RETURN(backend.RenameDirectory(old_path.GetString(), new_path.GetString()));
    }

    Result DoGetEntryType(DirectoryEntryType* out, const Path& path) {
        R_RETURN(backend.GetEntryType(out, path.GetString()));
    }

    Result DoOpenFile(VirtualFile* out_file, const Path& path, OpenMode mode) {
        R_RETURN(backend.OpenFile(out_file, path.GetString(), mode));
    }

    Result DoOpenDirectory(VirtualDir* out_directory, const Path& path, OpenDirectoryMode mode) {
        R_RETURN(backend.OpenDirectory(out_directory, path.GetString()));
    }

    Result DoCommit() {
        R_THROW(ResultNotImplemented);
    }

    Result DoGetFreeSpaceSize(s64* out, const Path& path) {
        R_THROW(ResultNotImplemented);
    }

    Result DoGetTotalSpaceSize(s64* out, const Path& path) {
        R_THROW(ResultNotImplemented);
    }

    Result DoCleanDirectoryRecursively(const Path& path) {
        R_RETURN(backend.CleanDirectoryRecursively(path.GetString()));
    }

    Result DoGetFileTimeStampRaw(FileTimeStampRaw* out, const Path& path) {
        R_RETURN(backend.GetFileTimeStampRaw(out, path.GetString()));
    }

    Result DoQueryEntry(char* dst, size_t dst_size, const char* src, size_t src_size, QueryId query,
                        const Path& path) {
        R_THROW(ResultNotImplemented);
    }

    // These aren't accessible as commands
    Result DoCommitProvisionally(s64 counter) {
        R_THROW(ResultNotImplemented);
    }

    Result DoRollback() {
        R_THROW(ResultNotImplemented);
    }

    Result DoFlush() {
        R_THROW(ResultNotImplemented);
    }

    Service::FileSystem::VfsDirectoryServiceWrapper backend;
};

} // namespace FileSys::Fsa
