// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/directory_save_data_filesystem.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/savedata_extra_data_accessor.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/acc/profile_manager.h"

namespace FileSys {

namespace {

// Using a leaked raw pointer for the RealVfsFilesystem singleton.
// This prevents SIGSEGV during shutdown by ensuring the VFS bridge
// outlives all threads that might still be flushing save data.
RealVfsFilesystem* GetPersistentVfs() {
    static RealVfsFilesystem* instance = new RealVfsFilesystem();
    return instance;
}

bool ShouldSaveDataBeAutomaticallyCreated(SaveDataSpaceId space, const SaveDataAttribute& attr) {
    return attr.type == SaveDataType::Cache || attr.type == SaveDataType::Temporary ||
           (space == SaveDataSpaceId::User &&
            (attr.type == SaveDataType::Account || attr.type == SaveDataType::Device) &&
            attr.program_id == 0 && attr.system_save_data_id == 0);
}

std::string GetFutureSaveDataPath(SaveDataSpaceId space_id, SaveDataType type, u64 title_id,
                                  u128 user_id) {
    if (space_id != SaveDataSpaceId::User) {
        return "";
    }

    Common::UUID uuid;
    std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));

    switch (type) {
    case SaveDataType::Account:
        return fmt::format("/user/save/account/{}/{:016X}/0", uuid.RawString(), title_id);
    case SaveDataType::Device:
        return fmt::format("/user/save/device/{:016X}/0", title_id);
    default:
        return "";
    }
}

void BufferedVfsCopy(VirtualFile source, VirtualFile dest) {
    if (!source || !dest) return;
    try {
        std::vector<u8> buffer(0x100000); // 1MB buffer
        dest->Resize(0);
        size_t offset = 0;
        while (offset < source->GetSize()) {
            const size_t to_read = std::min(buffer.size(), source->GetSize() - offset);
            source->Read(buffer.data(), to_read, offset);
            dest->Write(buffer.data(), to_read, offset);
            offset += to_read;
        }
    } catch (...) {
        LOG_ERROR(Service_FS, "Critical error during VFS mirror operation.");
    }
}

} // Anonymous namespace

SaveDataFactory::SaveDataFactory(Core::System& system_, ProgramId program_id_,
                                 VirtualDir save_directory_, VirtualDir backup_directory_)
    : system{system_}, program_id{program_id_}, dir{std::move(save_directory_)},
      backup_dir{std::move(backup_directory_)} {
    dir->DeleteSubdirectoryRecursive("temp");
}

SaveDataFactory::~SaveDataFactory() = default;

VirtualDir SaveDataFactory::Create(SaveDataSpaceId space, const SaveDataAttribute& meta) const {
    const auto save_directory = GetFullPath(program_id, dir, space, meta.type, meta.program_id,
                                            meta.user_id, meta.system_save_data_id);

    auto save_dir = dir->CreateDirectoryRelative(save_directory);
    if (save_dir == nullptr) {
        return nullptr;
    }

    SaveDataExtraDataAccessor accessor(save_dir);
    if (accessor.Initialize(true) == ResultSuccess) {
        SaveDataExtraData initial_data{};
        initial_data.attr = meta;
        initial_data.owner_id = meta.program_id;
        initial_data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        initial_data.flags = static_cast<u32>(SaveDataFlags::None);
        initial_data.available_size = 0;
        initial_data.journal_size = 0;
        initial_data.commit_id = 1;

        accessor.WriteExtraData(initial_data);
        accessor.CommitExtraData();
    }

    return save_dir;
}

VirtualDir SaveDataFactory::Open(SaveDataSpaceId space, const SaveDataAttribute& meta) const {
    const auto save_directory = GetFullPath(program_id, dir, space, meta.type, meta.program_id,
                                            meta.user_id, meta.system_save_data_id);

    auto out = dir->GetDirectoryRelative(save_directory);

    if (out == nullptr && (ShouldSaveDataBeAutomaticallyCreated(space, meta) && auto_create)) {
        return Create(space, meta);
    }

    return out;
}

VirtualDir SaveDataFactory::GetSaveDataSpaceDirectory(SaveDataSpaceId space) const {
    return dir->GetDirectoryRelative(GetSaveDataSpaceIdPath(space));
}

std::string SaveDataFactory::GetSaveDataSpaceIdPath(SaveDataSpaceId space) {
    switch (space) {
    case SaveDataSpaceId::System:
    case SaveDataSpaceId::ProperSystem:
    case SaveDataSpaceId::SafeMode:
        return "/system/";
    case SaveDataSpaceId::User:
        return "/user/";
    case SaveDataSpaceId::Temporary:
        return "/temp/";
    case SaveDataSpaceId::SdSystem:
    case SaveDataSpaceId::SdUser:
        return "/sd/";
    default:
        return "/unrecognized/";
    }
}

std::string SaveDataFactory::GetFullPath(ProgramId program_id, VirtualDir dir,
                                         SaveDataSpaceId space, SaveDataType type, u64 title_id,
                                         u128 user_id, u64 save_id) {
    if ((type == SaveDataType::Account || type == SaveDataType::Device) && title_id == 0) {
        title_id = program_id;
    }

    if (std::string future_path = GetFutureSaveDataPath(space, type, title_id & ~(0xFFULL), user_id);
        !future_path.empty()) {
        if (dir->GetDirectoryRelative(future_path) != nullptr) {
            return future_path;
        }
    }

    std::string out = GetSaveDataSpaceIdPath(space);
    switch (type) {
    case SaveDataType::System:
        return fmt::format("{}save/{:016X}/{:016X}{:016X}", out, save_id, user_id[1], user_id[0]);
    case SaveDataType::Account:
    case SaveDataType::Device:
        return fmt::format("{}save/{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0], title_id);
    case SaveDataType::Temporary:
        return fmt::format("{}{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0], title_id);
    case SaveDataType::Cache:
        return fmt::format("{}save/cache/{:016X}", out, title_id);
    default:
        return fmt::format("{}save/unknown_{:X}/{:016X}", out, static_cast<u8>(type), title_id);
    }
}

std::string SaveDataFactory::GetUserGameSaveDataRoot(u128 user_id, bool future) {
    if (future) {
        Common::UUID uuid;
        std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));
        return fmt::format("/user/save/account/{}", uuid.RawString());
    }
    return fmt::format("/user/save/{:016X}/{:016X}{:016X}", 0, user_id[1], user_id[0]);
}

SaveDataSize SaveDataFactory::ReadSaveDataSize(SaveDataType type, u64 title_id, u128 user_id) const {
    const auto path = GetFullPath(program_id, dir, SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);
    const auto size_file = relative_dir->GetFile(GetSaveDataSizeFileName());
    if (size_file == nullptr || size_file->GetSize() < sizeof(SaveDataSize)) return {0, 0};
    SaveDataSize out;
    if (size_file->ReadObject(&out) != sizeof(SaveDataSize)) return {0, 0};
    return out;
}

void SaveDataFactory::WriteSaveDataSize(SaveDataType type, u64 title_id, u128 user_id, SaveDataSize new_value) const {
    const auto path = GetFullPath(program_id, dir, SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);
    const auto size_file = relative_dir->CreateFile(GetSaveDataSizeFileName());
    if (size_file == nullptr) return;
    size_file->Resize(sizeof(SaveDataSize));
    size_file->WriteObject(new_value);
}

void SaveDataFactory::SetAutoCreate(bool state) {
    auto_create = state;
}

Result SaveDataFactory::ReadSaveDataExtraData(SaveDataExtraData* out_extra_data, SaveDataSpaceId space, const SaveDataAttribute& attribute) const {
    const auto save_directory = GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id, attribute.system_save_data_id);
    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) return ResultPathNotFound;
    SaveDataExtraDataAccessor accessor(save_dir);
    if (accessor.Initialize(false) != ResultSuccess) {
        *out_extra_data = {};
        out_extra_data->attr = attribute;
        return ResultSuccess;
    }
    return accessor.ReadExtraData(out_extra_data);
}

Result SaveDataFactory::WriteSaveDataExtraData(const SaveDataExtraData& extra_data, SaveDataSpaceId space, const SaveDataAttribute& attribute) const {
    const auto save_directory = GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id, attribute.system_save_data_id);
    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) return ResultPathNotFound;
    SaveDataExtraDataAccessor accessor(save_dir);
    R_TRY(accessor.Initialize(true));
    R_TRY(accessor.WriteExtraData(extra_data));
    return accessor.CommitExtraData();
}

Result SaveDataFactory::WriteSaveDataExtraDataWithMask(const SaveDataExtraData& extra_data, const SaveDataExtraData& mask, SaveDataSpaceId space, const SaveDataAttribute& attribute) const {
    const auto save_directory = GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id, attribute.system_save_data_id);
    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) return ResultPathNotFound;
    SaveDataExtraDataAccessor accessor(save_dir);
    R_TRY(accessor.Initialize(true));
    SaveDataExtraData current_data{};
    R_TRY(accessor.ReadExtraData(&current_data));
    const u8* extra_data_bytes = reinterpret_cast<const u8*>(&extra_data);
    const u8* mask_bytes = reinterpret_cast<const u8*>(&mask);
    u8* current_data_bytes = reinterpret_cast<u8*>(&current_data);
    for (size_t i = 0; i < sizeof(SaveDataExtraData); ++i) {
        if (mask_bytes[i] != 0) current_data_bytes[i] = extra_data_bytes[i];
    }
    R_TRY(accessor.WriteExtraData(current_data));
    return accessor.CommitExtraData();
}

// --- MIRRORING TOOLS ---

VirtualDir SaveDataFactory::GetMirrorDirectory(u64 title_id) const {
    auto it = Settings::values.mirrored_save_paths.find(title_id);
    if (it == Settings::values.mirrored_save_paths.end() || it->second.empty()) return nullptr;

    std::filesystem::path host_path(it->second);
    if (!std::filesystem::exists(host_path)) return nullptr;

    // Get the persistent VFS bridge
    auto* vfs = GetPersistentVfs();
    return vfs->OpenDirectory(it->second, OpenMode::ReadWrite);
}

void SaveDataFactory::SmartSyncFromSource(VirtualDir source, VirtualDir dest) const {
    // Citron: Shutdown and null safety
    if (!source || !dest || system.IsShuttingDown()) {
        return;
    }

    // Sync files from Source to Destination
    for (const auto& s_file : source->GetFiles()) {
        if (!s_file) continue;
        std::string name = s_file->GetName();

        // Skip metadata and lock files
        if (name == ".lock" || name == ".citron_save_size" || name.find("mirror_backup") != std::string::npos) {
            continue;
        }

        auto d_file = dest->CreateFile(name);
        if (d_file) {
            BufferedVfsCopy(s_file, d_file);
        }
    }

    // Recurse into subdirectories
    for (const auto& s_subdir : source->GetSubdirectories()) {
        if (!s_subdir) continue;

        // Prevent recursion into title-id-named folders to avoid infinite loops
        if (s_subdir->GetName().find("0100") != std::string::npos) continue;

        auto d_subdir = dest->GetDirectoryRelative(s_subdir->GetName());
        if (!d_subdir) {
            d_subdir = dest->CreateDirectoryRelative(s_subdir->GetName());
        }

        if (d_subdir) {
            SmartSyncFromSource(s_subdir, d_subdir);
        }
    }
}

void SaveDataFactory::PerformStartupMirrorSync() const {
    // If settings are empty or system is shutting down/uninitialized
    if (Settings::values.mirrored_save_paths.empty() || system.IsShuttingDown()) {
        return;
    }

    // Ensure our NAND directory is actually valid
    if (!dir) {
        LOG_ERROR(Service_FS, "Mirroring: Startup Sync aborted. NAND directory is null.");
        return;
    }

    // Attempt to locate the save root with null checks at every step
    VirtualDir user_save_root = nullptr;
    try {
        user_save_root = dir->GetDirectoryRelative("user/save/0000000000000000");
        if (!user_save_root) {
            user_save_root = dir->GetDirectoryRelative("user/save");
        }
    } catch (...) {
        LOG_ERROR(Service_FS, "Mirroring: Critical failure accessing VFS. Filesystem may be stale.");
        return;
    }

    if (!user_save_root) {
        LOG_WARNING(Service_FS, "Mirroring: Could not find user save root in NAND.");
        return;
    }

    LOG_INFO(Service_FS, "Mirroring: Startup Sync initiated.");

    for (const auto& [title_id, host_path] : Settings::values.mirrored_save_paths) {
        if (host_path.empty()) continue;

        auto mirror_source = GetMirrorDirectory(title_id);
        if (!mirror_source) continue;

        std::string title_id_str = fmt::format("{:016X}", title_id);

        for (const auto& profile_dir : user_save_root->GetSubdirectories()) {
            if (!profile_dir) continue;

            auto nand_dest = profile_dir->GetDirectoryRelative(title_id_str);

            if (!nand_dest) {
                for (const auto& sub : profile_dir->GetSubdirectories()) {
                    if (!sub) continue;
                    nand_dest = sub->GetDirectoryRelative(title_id_str);
                    if (nand_dest) break;
                }
            }

            if (nand_dest) {
                LOG_INFO(Service_FS, "Mirroring: Pulling external data for {}", title_id_str);
                SmartSyncFromSource(mirror_source, nand_dest);
            }
        }
    }
}

void SaveDataFactory::DoNandBackup(SaveDataSpaceId space, const SaveDataAttribute& meta, VirtualDir custom_dir) const {
    u64 title_id = (meta.program_id != 0 ? meta.program_id : static_cast<u64>(program_id));
    if (Settings::values.mirrored_save_paths.count(title_id)) return;

    if (!Settings::values.backup_saves_to_nand.GetValue() || backup_dir == nullptr || custom_dir == nullptr) return;

    const auto nand_path = GetFullPath(program_id, backup_dir, space, meta.type, meta.program_id, meta.user_id, meta.system_save_data_id);
    auto nand_out = backup_dir->CreateDirectoryRelative(nand_path);

    if (nand_out) {
        nand_out->CleanSubdirectoryRecursive(".");
        VfsRawCopyD(custom_dir, nand_out);
    }
}

} // namespace FileSys
