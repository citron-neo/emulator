// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <memory>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/savedata_extra_data_accessor.h"
#include "core/file_sys/savedata_factory.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

namespace {

bool ShouldSaveDataBeAutomaticallyCreated(SaveDataSpaceId space, const SaveDataAttribute& attr) {
    return attr.type == SaveDataType::Cache || attr.type == SaveDataType::Temporary ||
           (space == SaveDataSpaceId::User && ///< Normal Save Data -- Current Title & User
            (attr.type == SaveDataType::Account || attr.type == SaveDataType::Device) &&
            attr.program_id == 0 && attr.system_save_data_id == 0);
}

std::string GetFutureSaveDataPath(SaveDataSpaceId space_id, SaveDataType type, u64 title_id,
                                  u128 user_id) {
    // Only detect nand user saves.
    const auto space_id_path = [space_id]() -> std::string_view {
        switch (space_id) {
        case SaveDataSpaceId::User:
            return "/user/save";
        default:
            return "";
        }
    }();

    if (space_id_path.empty()) {
        return "";
    }

    Common::UUID uuid;
    std::memcpy(uuid.uuid.data(), user_id.data(), sizeof(Common::UUID));

    // Only detect account/device saves from the future location.
    switch (type) {
    case SaveDataType::Account:
        return fmt::format("{}/account/{}/{:016X}/0", space_id_path, uuid.RawString(), title_id);
    case SaveDataType::Device:
        return fmt::format("{}/device/{:016X}/0", space_id_path, title_id);
    default:
        return "";
    }
}

} // Anonymous namespace

SaveDataFactory::SaveDataFactory(Core::System& system_, ProgramId program_id_,
                                 VirtualDir save_directory_)
    : system{system_}, program_id{program_id_}, dir{std::move(save_directory_)} {
    // Delete all temporary storages
    // On hardware, it is expected that temporary storage be empty at first use.
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

    // Initialize ExtraData for new save
    SaveDataExtraDataAccessor accessor(save_dir);
    if (accessor.Initialize(true) != ResultSuccess) {
        LOG_WARNING(Service_FS, "Failed to initialize ExtraData for new save at {}", save_directory);
        // Continue anyway - save is still usable
    } else {
        // Write initial extra data
        SaveDataExtraData initial_data{};
        initial_data.attr = meta;
        initial_data.owner_id = meta.program_id;
        initial_data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        initial_data.flags = static_cast<u32>(SaveDataFlags::None);
        initial_data.available_size = 0; // Will be updated on commit
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
        return "/system/";
    case SaveDataSpaceId::User:
        return "/user/";
    case SaveDataSpaceId::Temporary:
        return "/temp/";
    case SaveDataSpaceId::SdSystem:
    case SaveDataSpaceId::SdUser:
        return "/sd/";
    case SaveDataSpaceId::ProperSystem:
        return "/system/";
    case SaveDataSpaceId::SafeMode:
        return "/system/";
    default:
        ASSERT_MSG(false, "Unrecognized SaveDataSpaceId: {:02X}", static_cast<u8>(space));
        return "/unrecognized/"; ///< To prevent corruption when ignoring asserts.
    }
}

std::string SaveDataFactory::GetFullPath(ProgramId program_id, VirtualDir dir,
                                         SaveDataSpaceId space, SaveDataType type, u64 title_id,
                                         u128 user_id, u64 save_id) {
    // According to switchbrew, if a save is of type SaveData and the title id field is 0, it should
    // be interpreted as the title id of the current process.
    if (type == SaveDataType::Account || type == SaveDataType::Device) {
        if (title_id == 0) {
            title_id = program_id;
        }
    }

    // For compat with a future impl.
    if (std::string future_path =
            GetFutureSaveDataPath(space, type, title_id & ~(0xFFULL), user_id);
        !future_path.empty()) {
        // Check if this location exists, and prefer it over the old.
        if (const auto future_dir = dir->GetDirectoryRelative(future_path); future_dir != nullptr) {
            LOG_INFO(Service_FS, "Using save at new location: {}", future_path);
            return future_path;
        }
    }

    std::string out = GetSaveDataSpaceIdPath(space);

    switch (type) {
    case SaveDataType::System:
        return fmt::format("{}save/{:016X}/{:016X}{:016X}", out, save_id, user_id[1], user_id[0]);
    case SaveDataType::Account:
    case SaveDataType::Device:
        return fmt::format("{}save/{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0],
                           title_id);
    case SaveDataType::Temporary:
        return fmt::format("{}{:016X}/{:016X}{:016X}/{:016X}", out, 0, user_id[1], user_id[0],
                           title_id);
    case SaveDataType::Cache:
        return fmt::format("{}save/cache/{:016X}", out, title_id);
    default:
        ASSERT_MSG(false, "Unrecognized SaveDataType: {:02X}", static_cast<u8>(type));
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

SaveDataSize SaveDataFactory::ReadSaveDataSize(SaveDataType type, u64 title_id,
                                               u128 user_id) const {
    const auto path =
        GetFullPath(program_id, dir, SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);

    const auto size_file = relative_dir->GetFile(GetSaveDataSizeFileName());
    if (size_file == nullptr || size_file->GetSize() < sizeof(SaveDataSize)) {
        return {0, 0};
    }

    SaveDataSize out;
    if (size_file->ReadObject(&out) != sizeof(SaveDataSize)) {
        return {0, 0};
    }

    return out;
}

void SaveDataFactory::WriteSaveDataSize(SaveDataType type, u64 title_id, u128 user_id,
                                        SaveDataSize new_value) const {
    const auto path =
        GetFullPath(program_id, dir, SaveDataSpaceId::User, type, title_id, user_id, 0);
    const auto relative_dir = GetOrCreateDirectoryRelative(dir, path);

    const auto size_file = relative_dir->CreateFile(GetSaveDataSizeFileName());
    if (size_file == nullptr) {
        return;
    }

    size_file->Resize(sizeof(SaveDataSize));
    size_file->WriteObject(new_value);
}

void SaveDataFactory::SetAutoCreate(bool state) {
    auto_create = state;
}

Result SaveDataFactory::ReadSaveDataExtraData(SaveDataExtraData* out_extra_data,
                                               SaveDataSpaceId space,
                                               const SaveDataAttribute& attribute) const {
    const auto save_directory =
        GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id,
                    attribute.system_save_data_id);

    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) {
        return ResultPathNotFound;
    }

    SaveDataExtraDataAccessor accessor(save_dir);

    // Try to initialize (but don't create if missing)
    if (Result result = accessor.Initialize(false); result != ResultSuccess) {
        // ExtraData doesn't exist - return default values
        LOG_DEBUG(Service_FS, "ExtraData not found for save at {}, returning defaults",
                  save_directory);

        // Return zeroed data
        *out_extra_data = {}; // Or: *out_extra_data = SaveDataExtraData{};
        out_extra_data->attr = attribute;
        return ResultSuccess;
    }

    return accessor.ReadExtraData(out_extra_data);
}

Result SaveDataFactory::WriteSaveDataExtraData(const SaveDataExtraData& extra_data,
                                                SaveDataSpaceId space,
                                                const SaveDataAttribute& attribute) const {
    const auto save_directory =
        GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id,
                    attribute.system_save_data_id);

    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) {
        return ResultPathNotFound;
    }

    SaveDataExtraDataAccessor accessor(save_dir);

    // Initialize and create if missing
    R_TRY(accessor.Initialize(true));

    // Write the data
    R_TRY(accessor.WriteExtraData(extra_data));

    // Commit immediately for transactional writes
    R_TRY(accessor.CommitExtraData());

    return ResultSuccess;
}

Result SaveDataFactory::WriteSaveDataExtraDataWithMask(const SaveDataExtraData& extra_data,
                                                        const SaveDataExtraData& mask,
                                                        SaveDataSpaceId space,
                                                        const SaveDataAttribute& attribute) const {
    const auto save_directory =
        GetFullPath(program_id, dir, space, attribute.type, attribute.program_id, attribute.user_id,
                    attribute.system_save_data_id);

    auto save_dir = dir->GetDirectoryRelative(save_directory);
    if (save_dir == nullptr) {
        return ResultPathNotFound;
    }

    SaveDataExtraDataAccessor accessor(save_dir);

    // Initialize and create if missing
    R_TRY(accessor.Initialize(true));

    // Read existing data
    SaveDataExtraData current_data{};
    R_TRY(accessor.ReadExtraData(&current_data));

    // Apply mask: copy only the bytes where mask is non-zero
    const u8* extra_data_bytes = reinterpret_cast<const u8*>(&extra_data);
    const u8* mask_bytes = reinterpret_cast<const u8*>(&mask);
    u8* current_data_bytes = reinterpret_cast<u8*>(&current_data);

    for (size_t i = 0; i < sizeof(SaveDataExtraData); ++i) {
        if (mask_bytes[i] != 0) {
            current_data_bytes[i] = extra_data_bytes[i];
        }
    }

    // Write back the masked data
    R_TRY(accessor.WriteExtraData(current_data));

    // Commit the changes
    R_TRY(accessor.CommitExtraData());

    return ResultSuccess;
}

} // namespace FileSys
