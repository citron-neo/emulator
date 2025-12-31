// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"
#include "common/settings.h"
#include "core/file_sys/fssrv/fssrv_sf_path.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_directory.h"
#include "core/hle/service/filesystem/fsp/fs_i_file.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"

namespace Service::FileSystem {

IFileSystem::IFileSystem(Core::System& system_, FileSys::VirtualDir dir_, SizeGetter size_getter_,
                         std::shared_ptr<FileSys::SaveDataFactory> factory_,
                         FileSys::SaveDataSpaceId space_id_, FileSys::SaveDataAttribute attribute_)
    : ServiceFramework{system_, "IFileSystem"},
      backend{std::make_unique<FileSys::Fsa::IFileSystem>(dir_)},
      size_getter{std::move(size_getter_)},
      content_dir{std::move(dir_)},
      save_factory{std::move(factory_)},
      save_space{space_id_},
      save_attr{attribute_} {
    static const FunctionInfo functions[] = {
        {0, D<&IFileSystem::CreateFile>, "CreateFile"},
        {1, D<&IFileSystem::DeleteFile>, "DeleteFile"},
        {2, D<&IFileSystem::CreateDirectory>, "CreateDirectory"},
        {3, D<&IFileSystem::DeleteDirectory>, "DeleteDirectory"},
        {4, D<&IFileSystem::DeleteDirectoryRecursively>, "DeleteDirectoryRecursively"},
        {5, D<&IFileSystem::RenameFile>, "RenameFile"},
        {6, nullptr, "RenameDirectory"},
        {7, D<&IFileSystem::GetEntryType>, "GetEntryType"},
        {8, D<&IFileSystem::OpenFile>, "OpenFile"},
        {9, D<&IFileSystem::OpenDirectory>, "OpenDirectory"},
        {10, D<&IFileSystem::Commit>, "Commit"},
        {11, D<&IFileSystem::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        {12, D<&IFileSystem::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        {13, D<&IFileSystem::CleanDirectoryRecursively>, "CleanDirectoryRecursively"},
        {14, D<&IFileSystem::GetFileTimeStampRaw>, "GetFileTimeStampRaw"},
        {15, nullptr, "QueryEntry"},
        {16, D<&IFileSystem::GetFileSystemAttribute>, "GetFileSystemAttribute"},
    };
    RegisterHandlers(functions);
}

Result IFileSystem::CreateFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                               s32 option, s64 size) {
    LOG_DEBUG(Service_FS, "called. file={}, option=0x{:X}, size=0x{:08X}", path->str, option, size);

    R_RETURN(backend->CreateFile(FileSys::Path(path->str), size));
}

Result IFileSystem::DeleteFile(const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. file={}", path->str);

    R_RETURN(backend->DeleteFile(FileSys::Path(path->str)));
}

Result IFileSystem::CreateDirectory(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. directory={}", path->str);

    R_RETURN(backend->CreateDirectory(FileSys::Path(path->str)));
}

Result IFileSystem::DeleteDirectory(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. directory={}", path->str);

    R_RETURN(backend->DeleteDirectory(FileSys::Path(path->str)));
}

Result IFileSystem::DeleteDirectoryRecursively(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. directory={}", path->str);

    R_RETURN(backend->DeleteDirectoryRecursively(FileSys::Path(path->str)));
}

Result IFileSystem::CleanDirectoryRecursively(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. Directory: {}", path->str);

    R_RETURN(backend->CleanDirectoryRecursively(FileSys::Path(path->str)));
}

Result IFileSystem::RenameFile(
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> old_path,
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> new_path) {
    LOG_DEBUG(Service_FS, "called. file '{}' to file '{}'", old_path->str, new_path->str);

    R_RETURN(backend->RenameFile(FileSys::Path(old_path->str), FileSys::Path(new_path->str)));
}

Result IFileSystem::OpenFile(OutInterface<IFile> out_interface,
                             const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                             u32 mode) {
    LOG_DEBUG(Service_FS, "called. file={}, mode={}", path->str, mode);

    FileSys::VirtualFile vfs_file{};
    R_TRY(backend->OpenFile(&vfs_file, FileSys::Path(path->str),
                            static_cast<FileSys::OpenMode>(mode)));

    *out_interface = std::make_shared<IFile>(system, vfs_file);
    R_SUCCEED();
}

Result IFileSystem::OpenDirectory(OutInterface<IDirectory> out_interface,
                                  const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path,
                                  u32 mode) {
    LOG_DEBUG(Service_FS, "called. directory={}, mode={}", path->str, mode);

    FileSys::VirtualDir vfs_dir{};
    R_TRY(backend->OpenDirectory(&vfs_dir, FileSys::Path(path->str),
                                 static_cast<FileSys::OpenDirectoryMode>(mode)));

    *out_interface = std::make_shared<IDirectory>(system, vfs_dir,
                                                  static_cast<FileSys::OpenDirectoryMode>(mode));
    R_SUCCEED();
}

Result IFileSystem::GetEntryType(
    Out<u32> out_type, const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called. file={}", path->str);

    FileSys::DirectoryEntryType vfs_entry_type{};
    R_TRY(backend->GetEntryType(&vfs_entry_type, FileSys::Path(path->str)));

    *out_type = static_cast<u32>(vfs_entry_type);
    R_SUCCEED();
}

Result IFileSystem::Commit() {
    // 1. Standard Commit
    Result res = backend->Commit();
    if (res != ResultSuccess) return res;

    // Citron: Shutdown Safety Check
    // If the emulator is stopping, the VFS might be invalid. Skip mirroring to prevent SEGV.
    if (system.IsShuttingDown()) {
        return res;
    }

    // 2. (Citron NAND -> External)
    if (save_factory) {
        u64 title_id = save_attr.program_id != 0 ? save_attr.program_id : system.GetApplicationProcessProgramID();
        auto mirror_dir = save_factory->GetMirrorDirectory(title_id);

        if (mirror_dir != nullptr) {
            LOG_INFO(Service_FS, "Mirroring: Pushing Citron NAND data back to external source...");

            // SYNC: Citron NAND Title ID folder -> Selected External Folder Contents
            save_factory->SmartSyncFromSource(content_dir, mirror_dir);

            LOG_INFO(Service_FS, "Mirroring: Push complete.");
        }
        else if (Settings::values.backup_saves_to_nand.GetValue()) {
            save_factory->DoNandBackup(save_space, save_attr, content_dir);
        }
    }

    R_RETURN(res);
}

Result IFileSystem::GetFreeSpaceSize(
    Out<s64> out_size, const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called");

    *out_size = size_getter.get_free_size();
    R_SUCCEED();
}

Result IFileSystem::GetTotalSpaceSize(
    Out<s64> out_size, const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_DEBUG(Service_FS, "called");

    *out_size = size_getter.get_total_size();
    R_SUCCEED();
}

Result IFileSystem::GetFileTimeStampRaw(
    Out<FileSys::FileTimeStampRaw> out_timestamp,
    const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_WARNING(Service_FS, "(Partial Implementation) called. file={}", path->str);

    FileSys::FileTimeStampRaw vfs_timestamp{};
    R_TRY(backend->GetFileTimeStampRaw(&vfs_timestamp, FileSys::Path(path->str)));

    *out_timestamp = vfs_timestamp;
    R_SUCCEED();
}

Result IFileSystem::GetFileSystemAttribute(Out<FileSys::FileSystemAttribute> out_attribute) {
    LOG_WARNING(Service_FS, "(STUBBED) called");

    FileSys::FileSystemAttribute savedata_attribute{};
    savedata_attribute.dir_entry_name_length_max_defined = true;
    savedata_attribute.file_entry_name_length_max_defined = true;
    savedata_attribute.dir_entry_name_length_max = 0x40;
    savedata_attribute.file_entry_name_length_max = 0x40;

    *out_attribute = savedata_attribute;
    R_SUCCEED();
}

} // namespace Service::FileSystem
