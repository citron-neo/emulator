// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"

// For FileTimeStampRaw
#include <sys/stat.h>

#ifdef _MSC_VER
#define stat _stat64
#endif

#ifdef ANDROID
#include "common/fs/fs_android.h"
#endif

namespace FileSys {

namespace FS = Common::FS;

namespace {

constexpr size_t MaxOpenFiles = 512;

constexpr FS::FileAccessMode ModeFlagsToFileAccessMode(OpenMode mode) {
    switch (mode) {
    case OpenMode::Read:
        return FS::FileAccessMode::Read;
    case OpenMode::Write:
    case OpenMode::ReadWrite:
    case OpenMode::AllowAppend:
    case OpenMode::All:
        return FS::FileAccessMode::ReadWrite;
    default:
        return {};
    }
}

} // Anonymous namespace

RealVfsFilesystem::RealVfsFilesystem() : VfsFilesystem(nullptr) {}
RealVfsFilesystem::~RealVfsFilesystem() = default;

std::string RealVfsFilesystem::GetName() const {
    return "Real";
}

bool RealVfsFilesystem::IsReadable() const {
    return true;
}

bool RealVfsFilesystem::IsWritable() const {
    return true;
}

VfsEntryType RealVfsFilesystem::GetEntryType(std::string_view path_) const {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    if (!FS::Exists(path)) {
        return VfsEntryType::None;
    }
    if (FS::IsDir(path)) {
        return VfsEntryType::Directory;
    }

    return VfsEntryType::File;
}

VirtualFile RealVfsFilesystem::OpenFileFromEntry(std::string_view path_, std::optional<u64> size,
                                                 OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    std::scoped_lock lk{list_lock};

    if (auto it = cache.find(path); it != cache.end()) {
        if (auto file = it->second.lock(); file) {
            return file;
        }
    }

    if (!size && !FS::IsFile(path)) {
        return nullptr;
    }

    auto reference = std::make_unique<FileReference>();
    this->InsertReferenceIntoListLocked(*reference);

    auto file = std::shared_ptr<RealVfsFile>(
        new RealVfsFile(*this, std::move(reference), path, perms, size));
    cache[path] = file;

    return file;
}

VirtualFile RealVfsFilesystem::OpenFile(std::string_view path_, OpenMode perms) {
    return OpenFileFromEntry(path_, {}, perms);
}

VirtualFile RealVfsFilesystem::CreateFile(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    {
        std::scoped_lock lk{list_lock};
        cache.erase(path);
    }

    // Current usages of CreateFile expect to delete the contents of an existing file.
    if (FS::IsFile(path)) {
        FS::IOFile temp{path, FS::FileAccessMode::Write, FS::FileType::BinaryFile};

        if (!temp.IsOpen()) {
            return nullptr;
        }

        temp.Close();

        return OpenFile(path, perms);
    }

    if (!FS::NewFile(path)) {
        return nullptr;
    }

    return OpenFile(path, perms);
}

VirtualFile RealVfsFilesystem::CopyFile(std::string_view old_path_, std::string_view new_path_) {
    // Unused
    return nullptr;
}

VirtualFile RealVfsFilesystem::MoveFile(std::string_view old_path_, std::string_view new_path_) {
    const auto old_path = FS::SanitizePath(old_path_, FS::DirectorySeparator::PlatformDefault);
    const auto new_path = FS::SanitizePath(new_path_, FS::DirectorySeparator::PlatformDefault);
    {
        std::scoped_lock lk{list_lock};
        cache.erase(old_path);
        cache.erase(new_path);
    }
    if (!FS::RenameFile(old_path, new_path)) {
        return nullptr;
    }
    return OpenFile(new_path, OpenMode::ReadWrite);
}

bool RealVfsFilesystem::DeleteFile(std::string_view path_) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    {
        std::scoped_lock lk{list_lock};
        cache.erase(path);
    }
    return FS::RemoveFile(path);
}

VirtualDir RealVfsFilesystem::OpenDirectory(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    return std::shared_ptr<RealVfsDirectory>(new RealVfsDirectory(*this, path, perms));
}

VirtualDir RealVfsFilesystem::CreateDirectory(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    if (!FS::CreateDirs(path)) {
        return nullptr;
    }
    return std::shared_ptr<RealVfsDirectory>(new RealVfsDirectory(*this, path, perms));
}

VirtualDir RealVfsFilesystem::CopyDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    // Unused
    return nullptr;
}

VirtualDir RealVfsFilesystem::MoveDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    const auto old_path = FS::SanitizePath(old_path_, FS::DirectorySeparator::PlatformDefault);
    const auto new_path = FS::SanitizePath(new_path_, FS::DirectorySeparator::PlatformDefault);

    if (!FS::RenameDir(old_path, new_path)) {
        return nullptr;
    }
    return OpenDirectory(new_path, OpenMode::ReadWrite);
}

bool RealVfsFilesystem::DeleteDirectory(std::string_view path_) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    return FS::RemoveDirRecursively(path);
}

std::unique_lock<std::mutex> RealVfsFilesystem::RefreshReference(const std::string& path,
                                                                 OpenMode perms,
                                                                 FileReference& reference) {
    std::unique_lock lk{list_lock};

    // Temporarily remove from list.
    this->RemoveReferenceFromListLocked(reference);

    // Restore file if needed.
    if (!reference.file) {
        this->EvictSingleReferenceLocked();

        reference.file =
            FS::FileOpen(path, ModeFlagsToFileAccessMode(perms), FS::FileType::BinaryFile);
        if (reference.file) {
            num_open_files++;
        }
    }

    // Reinsert into list.
    this->InsertReferenceIntoListLocked(reference);

    return lk;
}

void RealVfsFilesystem::DropReference(std::unique_ptr<FileReference>&& reference) {
    std::scoped_lock lk{list_lock};

    // Remove from list.
    this->RemoveReferenceFromListLocked(*reference);

    // Close the file.
    if (reference->file) {
        reference->file.reset();
        num_open_files--;
    }
}

void RealVfsFilesystem::EvictSingleReferenceLocked() {
    if (num_open_files < MaxOpenFiles || open_references.empty()) {
        return;
    }

    // Get and remove from list.
    auto& reference = open_references.back();
    this->RemoveReferenceFromListLocked(reference);

    // Close the file.
    if (reference.file) {
        reference.file.reset();
        num_open_files--;
    }

    // Reinsert into closed list.
    this->InsertReferenceIntoListLocked(reference);
}

void RealVfsFilesystem::InsertReferenceIntoListLocked(FileReference& reference) {
    if (reference.file) {
        open_references.push_front(reference);
    } else {
        closed_references.push_front(reference);
    }
}

void RealVfsFilesystem::RemoveReferenceFromListLocked(FileReference& reference) {
    if (reference.file) {
        open_references.erase(open_references.iterator_to(reference));
    } else {
        closed_references.erase(closed_references.iterator_to(reference));
    }
}

RealVfsFile::RealVfsFile(RealVfsFilesystem& base_, std::unique_ptr<FileReference> reference_,
                         const std::string& path_, OpenMode perms_, std::optional<u64> size_)
    : base(base_), reference(std::move(reference_)), path(path_),
      parent_path(FS::GetParentPath(path_)), path_components(FS::SplitPathComponentsCopy(path_)),
      size(size_), perms(perms_) {}

RealVfsFile::~RealVfsFile() {
    base.DropReference(std::move(reference));
}

std::string RealVfsFile::GetName() const {
#ifdef ANDROID
    if (path[0] != '/') {
        return FS::Android::GetFilename(path);
    }
#endif
    return path_components.empty() ? "" : std::string(path_components.back());
}

std::size_t RealVfsFile::GetSize() const {
    if (size) {
        return *size;
    }
    auto lk = base.RefreshReference(path, perms, *reference);
    return reference->file ? reference->file->GetSize() : 0;
}

bool RealVfsFile::Resize(std::size_t new_size) {
    size.reset();
    auto lk = base.RefreshReference(path, perms, *reference);
    return reference->file ? reference->file->SetSize(new_size) : false;
}

VirtualDir RealVfsFile::GetContainingDirectory() const {
    return base.OpenDirectory(parent_path, perms);
}

bool RealVfsFile::IsWritable() const {
    return True(perms & OpenMode::Write);
}

bool RealVfsFile::IsReadable() const {
    return True(perms & OpenMode::Read);
}

std::size_t RealVfsFile::Read(u8* data, std::size_t length, std::size_t offset) const {
    auto lk = base.RefreshReference(path, perms, *reference);
    if (!reference->file || !reference->file->Seek(static_cast<s64>(offset))) {
        return 0;
    }
    return reference->file->ReadSpan(std::span{data, length});
}

std::size_t RealVfsFile::Write(const u8* data, std::size_t length, std::size_t offset) {
    size.reset();
    auto lk = base.RefreshReference(path, perms, *reference);
    if (!reference->file || !reference->file->Seek(static_cast<s64>(offset))) {
        return 0;
    }
    return reference->file->WriteSpan(std::span{data, length});
}

bool RealVfsFile::Rename(std::string_view name) {
    return base.MoveFile(path, parent_path + '/' + std::string(name)) != nullptr;
}

// TODO(DarkLordZach): MSVC would not let me combine the following two functions using 'if
// constexpr' because there is a compile error in the branch not used.

template <>
std::vector<VirtualFile> RealVfsDirectory::IterateEntries<RealVfsFile, VfsFile>() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::vector<VirtualFile> out;

    const FS::DirEntryCallable callback = [this,
                                           &out](const std::filesystem::directory_entry& entry) {
        const auto full_path_string = FS::PathToUTF8String(entry.path());

        out.emplace_back(base.OpenFileFromEntry(full_path_string, entry.file_size(), perms));

        return true;
    };

    FS::IterateDirEntries(path, callback, FS::DirEntryFilter::File);

    return out;
}

template <>
std::vector<VirtualDir> RealVfsDirectory::IterateEntries<RealVfsDirectory, VfsDirectory>() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::vector<VirtualDir> out;

    const FS::DirEntryCallable callback = [this,
                                           &out](const std::filesystem::directory_entry& entry) {
        const auto full_path_string = FS::PathToUTF8String(entry.path());

        out.emplace_back(base.OpenDirectory(full_path_string, perms));

        return true;
    };

    FS::IterateDirEntries(path, callback, FS::DirEntryFilter::Directory);

    return out;
}

RealVfsDirectory::RealVfsDirectory(RealVfsFilesystem& base_, const std::string& path_,
                                   OpenMode perms_)
    : base(base_), path(FS::RemoveTrailingSlash(path_)), parent_path(FS::GetParentPath(path)),
      path_components(FS::SplitPathComponentsCopy(path)), perms(perms_) {
    if (!FS::Exists(path) && True(perms & OpenMode::Write)) {
        void(FS::CreateDirs(path));
    }
}

RealVfsDirectory::~RealVfsDirectory() = default;

VirtualFile RealVfsDirectory::GetFileRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || FS::IsDir(full_path)) {
        return nullptr;
    }
    return base.OpenFile(full_path, perms);
}

VirtualDir RealVfsDirectory::GetDirectoryRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || !FS::IsDir(full_path)) {
        return nullptr;
    }
    return base.OpenDirectory(full_path, perms);
}

VirtualFile RealVfsDirectory::GetFile(std::string_view name) const {
    return GetFileRelative(name);
}

VirtualDir RealVfsDirectory::GetSubdirectory(std::string_view name) const {
    return GetDirectoryRelative(name);
}

VirtualFile RealVfsDirectory::CreateFileRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::CreateParentDirs(full_path)) {
        return nullptr;
    }
    return base.CreateFile(full_path, perms);
}

VirtualDir RealVfsDirectory::CreateDirectoryRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    return base.CreateDirectory(full_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectoryRecursive(std::string_view name) {
    const auto full_path = FS::SanitizePath(this->path + '/' + std::string(name));
    return base.DeleteDirectory(full_path);
}

std::vector<VirtualFile> RealVfsDirectory::GetFiles() const {
    return IterateEntries<RealVfsFile, VfsFile>();
}

FileTimeStampRaw RealVfsDirectory::GetFileTimeStamp(std::string_view path_) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(path_));
    const auto fs_path = std::filesystem::path{FS::ToU8String(full_path)};
    struct stat file_status;

#ifdef _WIN32
    const auto stat_result = _wstat64(fs_path.c_str(), &file_status);
#else
    const auto stat_result = stat(fs_path.c_str(), &file_status);
#endif

    if (stat_result != 0) {
        return {};
    }

    return {
        .created{static_cast<u64>(file_status.st_ctime)},
        .accessed{static_cast<u64>(file_status.st_atime)},
        .modified{static_cast<u64>(file_status.st_mtime)},
    };
}

std::vector<VirtualDir> RealVfsDirectory::GetSubdirectories() const {
    return IterateEntries<RealVfsDirectory, VfsDirectory>();
}

bool RealVfsDirectory::IsWritable() const {
    return True(perms & OpenMode::Write);
}

bool RealVfsDirectory::IsReadable() const {
    return True(perms & OpenMode::Read);
}

std::string RealVfsDirectory::GetName() const {
    return path_components.empty() ? "" : std::string(path_components.back());
}

VirtualDir RealVfsDirectory::GetParentDirectory() const {
    if (path_components.size() <= 1) {
        return nullptr;
    }

    return base.OpenDirectory(parent_path, perms);
}

VirtualDir RealVfsDirectory::CreateSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return base.CreateDirectory(subdir_path, perms);
}

VirtualFile RealVfsDirectory::CreateFile(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.CreateFile(file_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return base.DeleteDirectory(subdir_path);
}

bool RealVfsDirectory::DeleteFile(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.DeleteFile(file_path);
}

bool RealVfsDirectory::Rename(std::string_view name) {
    const std::string new_name = (parent_path + '/').append(name);
    return base.MoveFile(path, new_name) != nullptr;
}

std::string RealVfsDirectory::GetFullPath() const {
    auto out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

std::map<std::string, VfsEntryType, std::less<>> RealVfsDirectory::GetEntries() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::map<std::string, VfsEntryType, std::less<>> out;

    const FS::DirEntryCallable callback = [&out](const std::filesystem::directory_entry& entry) {
        const auto filename = FS::PathToUTF8String(entry.path().filename());
        out.insert_or_assign(filename,
                             entry.is_directory() ? VfsEntryType::Directory : VfsEntryType::File);
        return true;
    };

    FS::IterateDirEntries(path, callback);

    return out;
}

} // namespace FileSys
