// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/fs/file.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/directory_save_data_filesystem.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/savedata_extra_data_accessor.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/hle/service/acc/profile_manager.h"

// [UNITY-FIX] winbase.h A/W macros shadow C++ method names.
#undef DeleteFile
#undef CreateFile
#undef CopyFile
#undef MoveFile
#undef MoveFileEx
#undef CreateDirectory
#undef RemoveDirectory

namespace FileSys {

namespace {

constexpr u64 Lego2KDriveProgramId = 0x0100739018020000ULL;

constexpr std::array<const char*, 5> Lego2KDriveSeedFiles = {
    "ArtemisDnaInfo",
    "ArtemisDnaLocalCache",
    "ArtemisUserSettings",
    "ArtemisPlayerInfo",
    "DynamicPlayerInfo",
};

void SeedLego2KDriveTemplate(const VirtualDir& save_dir, u64 resolved_program_id,
                             SaveDataType type) {
    if (resolved_program_id != Lego2KDriveProgramId || type != SaveDataType::Account) {
        return;
    }

    VirtualDir working = save_dir->GetSubdirectory("1");
    if (working == nullptr || !working->GetFiles().empty()) {
        return;
    }

#ifdef CITRON_SAVE_SEED_ROOT
    const std::filesystem::path seed_dir =
        std::filesystem::path(CITRON_SAVE_SEED_ROOT) / "0100739018020000";
    if (!std::filesystem::is_directory(seed_dir)) {
        LOG_WARNING(Service_FS, "LEGO 2K Drive save seed directory missing: {}", seed_dir.string());
        return;
    }

    LOG_INFO(Service_FS, "Seeding LEGO 2K Drive account save in journal working dir");

    std::size_t seeded_files = 0;
    for (const char* filename : Lego2KDriveSeedFiles) {
        const auto source_path = seed_dir / filename;
        if (!std::filesystem::is_regular_file(source_path)) {
            LOG_WARNING(Service_FS, "LEGO 2K Drive save seed missing: {}", source_path.string());
            continue;
        }

        Common::FS::IOFile seed_file(source_path, Common::FS::FileAccessMode::Read,
                                     Common::FS::FileType::BinaryFile);
        if (!seed_file.IsOpen()) {
            LOG_WARNING(Service_FS, "LEGO 2K Drive save seed unreadable: {}", source_path.string());
            continue;
        }

        std::vector<u8> data(static_cast<std::size_t>(seed_file.GetSize()));
        if (seed_file.Read(data) != data.size()) {
            continue;
        }

        VirtualFile out_file = working->CreateFile(filename);
        if (out_file == nullptr || out_file->WriteBytes(data) != data.size()) {
            LOG_WARNING(Service_FS, "Failed to seed LEGO 2K Drive save file {}", filename);
            continue;
        }
        ++seeded_files;
    }

    if (seeded_files > 0) {
        LOG_INFO(Service_FS, "Seeded LEGO 2K Drive account save with {} template file(s)",
                 seeded_files);
    }
#endif
}

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
    const size_t source_size = source->GetSize();
    if (source_size == 0) return;

    try {
        // Move buffer to heap to prevent stack exhaustion during deep recursion
        auto buffer = std::make_unique<std::vector<u8>>(0x100000); // 1MB
        dest->Resize(0);
        size_t offset = 0;
        while (offset < source_size) {
            const size_t to_read = std::min(buffer->size(), source_size - offset);
            source->Read(buffer->data(), to_read, offset);
            dest->Write(buffer->data(), to_read, offset);
            offset += to_read;
        }
    } catch (...) {
        LOG_ERROR(Service_FS, "Mirroring: Buffer copy failed.");
    }
}

constexpr const char* Lego2KDriveDonorSavePath =
    "user/save/0000000000000000/102681DE8729EB5AF9A5BEE463D78659/0100739018020000";

void PromoteLego2KDriveOfflineSave(const VirtualDir& save_dir, const VirtualDir& nand_dir,
                                   u64 resolved_program_id, SaveDataType type) {
    if (resolved_program_id != Lego2KDriveProgramId || type != SaveDataType::Account) {
        return;
    }

    if (save_dir->GetFile("ArtemisVehicle") != nullptr) {
        return;
    }

    VirtualDir donor_dir = nand_dir->GetDirectoryRelative(Lego2KDriveDonorSavePath);
    if (donor_dir == nullptr) {
        LOG_WARNING(Service_FS, "LEGO 2K Drive offline save donor not found at {}",
                    Lego2KDriveDonorSavePath);
        return;
    }

    std::size_t promoted_files = 0;
    for (const auto& donor_file : donor_dir->GetFiles()) {
        if (!donor_file) {
            continue;
        }

        VirtualFile dest = save_dir->CreateFile(donor_file->GetName());
        if (dest == nullptr) {
            LOG_WARNING(Service_FS, "Failed to promote LEGO 2K Drive save file {}",
                        donor_file->GetName());
            continue;
        }

        BufferedVfsCopy(donor_file, dest);
        ++promoted_files;
    }

    if (promoted_files > 0) {
        LOG_INFO(Service_FS,
                 "Promoted LEGO 2K Drive offline save from donor profile ({} root file(s))",
                 promoted_files);
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

SaveDataAttribute SaveDataFactory::NormalizeAttribute(const SaveDataAttribute& meta) const {
    SaveDataAttribute attr = meta;
    if (attr.program_id == 0 && (attr.type == SaveDataType::Account || attr.type == SaveDataType::Device ||
                                 attr.type == SaveDataType::Cache)) {
        attr.program_id = program_id;
    }
    return attr;
}

SaveDataSize SaveDataFactory::GetResolvedSaveDataSize(SaveDataType type, u64 title_id,
                                                      u128 user_id) const {
    const u64 resolved_title_id = title_id != 0 ? title_id : program_id;
    auto size = ReadSaveDataSize(type, resolved_title_id, user_id);
    if (size.normal != 0 || size.journal != 0) {
        return size;
    }

    const PatchManager pm{resolved_title_id, system.GetFileSystemController(),
                          system.GetContentProvider()};
    const auto metadata = pm.GetControlMetadata();
    if (metadata.first == nullptr) {
        return size;
    }

    switch (type) {
    case SaveDataType::Cache:
        return {metadata.first->GetCacheStorageSize(), metadata.first->GetCacheStorageJournalSize()};
    case SaveDataType::Account:
        return {metadata.first->GetDefaultNormalSaveSize(),
                metadata.first->GetDefaultJournalSaveSize()};
    case SaveDataType::Device:
        return {metadata.first->GetDeviceSaveDataSize(),
                metadata.first->GetDefaultJournalSaveSize()};
    default:
        return size;
    }
}

Result SaveDataFactory::InitializeSaveDataLayout(VirtualDir save_dir) const {
    DirectorySaveDataFileSystem journal_fs(save_dir);
    return journal_fs.Initialize(true);
}

Result SaveDataFactory::SyncExtraDataSizes(VirtualDir save_dir,
                                           const SaveDataAttribute& meta) const {
    if (save_dir == nullptr) {
        return ResultPathNotFound;
    }

    const auto attr = NormalizeAttribute(meta);
    const auto sizes = GetResolvedSaveDataSize(attr.type, attr.program_id, attr.user_id);
    if (sizes.normal == 0 && sizes.journal == 0) {
        return ResultSuccess;
    }

    SaveDataExtraDataAccessor accessor(save_dir);
    R_TRY(accessor.Initialize(true));

    SaveDataExtraData extra_data{};
    R_TRY(accessor.ReadExtraData(&extra_data));

    extra_data.attr = attr;
    if (extra_data.owner_id == 0) {
        extra_data.owner_id = attr.program_id;
    }
    extra_data.available_size = static_cast<s64>(sizes.normal);
    extra_data.journal_size = static_cast<s64>(sizes.journal);

    R_TRY(accessor.WriteExtraData(extra_data));
    return accessor.CommitExtraData();
}

VirtualDir SaveDataFactory::Create(SaveDataSpaceId space, const SaveDataAttribute& meta) const {
    const auto attr = NormalizeAttribute(meta);
    const auto save_directory = GetFullPath(program_id, dir, space, attr.type, attr.program_id,
                                            attr.user_id, attr.system_save_data_id, attr.index);

    auto save_dir = dir->CreateDirectoryRelative(save_directory);
    if (save_dir == nullptr) {
        return nullptr;
    }

    const auto sizes = GetResolvedSaveDataSize(attr.type, attr.program_id, attr.user_id);

    SaveDataExtraDataAccessor accessor(save_dir);
    if (accessor.Initialize(true) == ResultSuccess) {
        SaveDataExtraData initial_data{};
        initial_data.attr = attr;
        initial_data.owner_id = attr.program_id;
        initial_data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        initial_data.flags = static_cast<u32>(SaveDataFlags::None);
        initial_data.available_size = static_cast<s64>(sizes.normal);
        initial_data.journal_size = static_cast<s64>(sizes.journal);
        initial_data.commit_id = 1;

        accessor.WriteExtraData(initial_data);
        accessor.CommitExtraData();
    }

    if (sizes.normal != 0 || sizes.journal != 0) {
        WriteSaveDataSize(attr.type, attr.program_id, attr.user_id, sizes);
    }

    InitializeSaveDataLayout(save_dir);
    PromoteLego2KDriveOfflineSave(save_dir, dir, attr.program_id, attr.type);
    if (!Settings::values.airplane_mode.GetValue()) {
        SeedLego2KDriveTemplate(save_dir, attr.program_id, attr.type);
    }

    return save_dir;
}

VirtualDir SaveDataFactory::Open(SaveDataSpaceId space, const SaveDataAttribute& meta) const {
    const auto attr = NormalizeAttribute(meta);
    const auto save_directory = GetFullPath(program_id, dir, space, attr.type, attr.program_id,
                                            attr.user_id, attr.system_save_data_id, attr.index);

    auto out = dir->GetDirectoryRelative(save_directory);

    if (out == nullptr && (ShouldSaveDataBeAutomaticallyCreated(space, meta) && auto_create)) {
        return Create(space, meta);
    }

    if (out != nullptr) {
        SaveDataExtraDataAccessor accessor(out);
        if (accessor.Initialize(false) == ResultSuccess) {
            SaveDataExtraData extra_data{};
            if (accessor.ReadExtraData(&extra_data) == ResultSuccess &&
                extra_data.journal_size == 0) {
                SyncExtraDataSizes(out, attr);
            }
        }
        // Repair saves that exist but are missing journal working/committed dirs.
        if (out->GetSubdirectory("0") == nullptr || out->GetSubdirectory("1") == nullptr) {
            InitializeSaveDataLayout(out);
        }
        PromoteLego2KDriveOfflineSave(out, dir, attr.program_id, attr.type);
        if (!Settings::values.airplane_mode.GetValue()) {
            SeedLego2KDriveTemplate(out, attr.program_id, attr.type);
        }
    }

    return out;
}

Result SaveDataFactory::DeleteCacheStorage(u16 index) const {
    const auto save_directory = GetFullPath(program_id, dir, SaveDataSpaceId::User,
                                            SaveDataType::Cache, 0, {}, 0, index);

    if (dir->GetDirectoryRelative(save_directory) == nullptr) {
        return ResultSuccess;
    }

    if (!dir->DeleteSubdirectoryRecursive(save_directory)) {
        return ResultPermissionDenied;
    }

    return ResultSuccess;
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
                                         u128 user_id, u64 save_id, u16 index) {
    if ((type == SaveDataType::Account || type == SaveDataType::Device || type == SaveDataType::Cache) &&
        title_id == 0) {
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
        if (index != 0) {
            return fmt::format("{}save/cache/{:016X}/{:04X}", out, title_id, index);
        }
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

void SaveDataFactory::WriteSaveDataSize(SaveDataType type, u64 title_id, u128 user_id,
                                        SaveDataSize new_value) const {
    const auto path = GetFullPath(program_id, dir, SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);
    const auto size_file = relative_dir->CreateFile(GetSaveDataSizeFileName());
    if (size_file == nullptr) {
        return;
    }
    size_file->Resize(sizeof(SaveDataSize));
    size_file->WriteObject(new_value);

    SaveDataAttribute attr{};
    attr.program_id = title_id != 0 ? title_id : program_id;
    attr.user_id = user_id;
    attr.type = type;
    SyncExtraDataSizes(relative_dir, attr);
}

void SaveDataFactory::SetAutoCreate(bool state) {
    auto_create = state;
}

Result SaveDataFactory::ReadSaveDataExtraData(SaveDataExtraData* out_extra_data, SaveDataSpaceId space, const SaveDataAttribute& attribute) const {
    const auto save_directory = GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id, attribute.system_save_data_id, attribute.index);
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
    const auto save_directory = GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id, attribute.system_save_data_id, attribute.index);
    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) return ResultPathNotFound;
    SaveDataExtraDataAccessor accessor(save_dir);
    R_TRY(accessor.Initialize(true));
    R_TRY(accessor.WriteExtraData(extra_data));
    return accessor.CommitExtraData();
}

Result SaveDataFactory::WriteSaveDataExtraDataWithMask(const SaveDataExtraData& extra_data, const SaveDataExtraData& mask, SaveDataSpaceId space, const SaveDataAttribute& attribute) const {
    const auto save_directory = GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id, attribute.system_save_data_id, attribute.index);
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
    if (!source || !dest || system.IsShuttingDown()) {
        return;
    }

    // Sync files
    const auto files = source->GetFiles();
    for (const auto& s_file : files) {
        if (!s_file) continue;
        const std::string name = s_file->GetName();

        if (name == ".lock" || name == ".citron_save_size" || name.find("mirror_backup") != std::string::npos) {
            continue;
        }

        auto d_file = dest->CreateFile(name);
        if (d_file) {
            BufferedVfsCopy(s_file, d_file);
        }
    }

    // Sync subdirectories
    const auto subdirs = source->GetSubdirectories();
    for (const auto& s_subdir : subdirs) {
        if (!s_subdir) continue;
        const std::string sub_name = s_subdir->GetName();

        // Recursion guard for title-id folders
        if (sub_name.find("0100") != std::string::npos) continue;

        auto d_subdir = dest->GetDirectoryRelative(sub_name);
        if (!d_subdir) {
            d_subdir = dest->CreateDirectoryRelative(sub_name);
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
