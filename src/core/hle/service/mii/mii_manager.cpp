// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/mii/mii_database_manager.h"
#include "core/hle/service/mii/mii_manager.h"
#include "core/hle/service/mii/mii_result.h"
#include "core/hle/service/mii/mii_util.h"
#include "core/hle/service/mii/types/char_info.h"
#include "core/hle/service/mii/types/core_data.h"
#include "core/hle/service/mii/types/raw_data.h"
#include "core/hle/service/mii/types/store_data.h"
#include "core/hle/service/mii/types/ver3_store_data.h"

namespace Service::Mii {
constexpr std::size_t DefaultMiiCount{RawData::DefaultMii.size()};

MiiManager::MiiManager() {}

Result MiiManager::Initialize(DatabaseSessionMetadata& metadata) {
    database_manager.MountSaveData();
    database_manager.Initialize(metadata, is_broken_with_clear_flag);
    return ResultSuccess;
}

void MiiManager::BuildDefault(CharInfo& out_char_info, u32 index) const {
    StoreData store_data{};
    store_data.BuildDefault(index);
    out_char_info.SetFromStoreData(store_data);
}

void MiiManager::BuildBase(CharInfo& out_char_info, Gender gender) const {
    StoreData store_data{};
    store_data.BuildBase(gender);
    out_char_info.SetFromStoreData(store_data);
}

void MiiManager::BuildRandom(CharInfo& out_char_info, Age age, Gender gender, Race race) const {
    StoreData store_data{};
    store_data.BuildRandom(age, gender, race);
    out_char_info.SetFromStoreData(store_data);
}

bool MiiManager::IsFullDatabase() const {
    return database_manager.IsFullDatabase();
}

void MiiManager::SetInterfaceVersion(DatabaseSessionMetadata& metadata, u32 version) const {
    metadata.interface_version = version;
}

bool MiiManager::IsUpdated(DatabaseSessionMetadata& metadata, SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return false;
    }

    const u64 metadata_update_counter = metadata.update_counter;
    const u64 database_update_counter = database_manager.GetUpdateCounter();
    metadata.update_counter = database_update_counter;
    return metadata_update_counter != database_update_counter;
}

u32 MiiManager::GetCount(const DatabaseSessionMetadata& metadata, SourceFlag source_flag) const {
    u32 mii_count{};
    if ((source_flag & SourceFlag::Default) != SourceFlag::None) {
        mii_count += DefaultMiiCount;
    }
    if ((source_flag & SourceFlag::Database) != SourceFlag::None) {
        mii_count += database_manager.GetCount(metadata);
    }
    return mii_count;
}

Result MiiManager::Move(DatabaseSessionMetadata& metadata, u32 index,
                        const Common::UUID& create_id) {
    const auto result = database_manager.Move(metadata, index, create_id);

    if (result.IsFailure()) {
        return result;
    }

    if (!database_manager.IsModified()) {
        return ResultNotUpdated;
    }

    return database_manager.SaveDatabase();
}

Result MiiManager::AddOrReplace(DatabaseSessionMetadata& metadata, const StoreData& store_data) {
    const auto result = database_manager.AddOrReplace(metadata, store_data);

    if (result.IsFailure()) {
        return result;
    }

    if (!database_manager.IsModified()) {
        return ResultNotUpdated;
    }

    return database_manager.SaveDatabase();
}

Result MiiManager::Delete(DatabaseSessionMetadata& metadata, const Common::UUID& create_id) {
    const auto result = database_manager.Delete(metadata, create_id);

    if (result.IsFailure()) {
        return result;
    }

    if (!database_manager.IsModified()) {
        return ResultNotUpdated;
    }

    return database_manager.SaveDatabase();
}

s32 MiiManager::FindIndex(const Common::UUID& create_id, bool is_special) const {
    s32 index{};
    const auto result = database_manager.FindIndex(index, create_id, is_special);
    if (result.IsError()) {
        index = -1;
    }
    return index;
}

Result MiiManager::GetIndex(const DatabaseSessionMetadata& metadata, const CharInfo& char_info,
                            s32& out_index) const {
    if (char_info.Verify() != ValidationResult::NoErrors) {
        return ResultInvalidCharInfo;
    }

    s32 index{};
    const bool is_special = metadata.magic == MiiMagic;
    const auto result = database_manager.FindIndex(index, char_info.GetCreateId(), is_special);

    if (result.IsError()) {
        index = -1;
    }

    if (index == -1) {
        return ResultNotFound;
    }

    out_index = index;
    return ResultSuccess;
}

Result MiiManager::Append(DatabaseSessionMetadata& metadata, const CharInfo& char_info) {
    const auto result = database_manager.Append(metadata, char_info);

    if (result.IsError()) {
        return ResultNotFound;
    }

    if (!database_manager.IsModified()) {
        return ResultNotUpdated;
    }

    return database_manager.SaveDatabase();
}

bool MiiManager::IsBrokenWithClearFlag(DatabaseSessionMetadata& metadata) {
    const bool is_broken = is_broken_with_clear_flag;
    if (is_broken_with_clear_flag) {
        is_broken_with_clear_flag = false;
        database_manager.Format(metadata);
        database_manager.SaveDatabase();
    }
    return is_broken;
}

Result MiiManager::DestroyFile(DatabaseSessionMetadata& metadata) {
    is_broken_with_clear_flag = true;
    return database_manager.DestroyFile(metadata);
}

Result MiiManager::DeleteFile() {
    return database_manager.DeleteFile();
}

Result MiiManager::Format(DatabaseSessionMetadata& metadata) {
    database_manager.Format(metadata);

    if (!database_manager.IsModified()) {
        return ResultNotUpdated;
    }
    return database_manager.SaveDatabase();
}

Result MiiManager::ConvertV3ToCharInfo(CharInfo& out_char_info, const Ver3StoreData& mii_v3) const {
    if (!mii_v3.IsValid()) {
        return ResultInvalidCharInfo;
    }

    StoreData store_data{};
    mii_v3.BuildToStoreData(store_data);
    const auto name = store_data.GetNickname();
    if (!MiiUtil::IsFontRegionValid(store_data.GetFontRegion(), name.data)) {
        store_data.SetInvalidName();
    }

    out_char_info.SetFromStoreData(store_data);
    return ResultSuccess;
}

Result MiiManager::ConvertCoreDataToCharInfo(CharInfo& out_char_info,
                                             const CoreData& core_data) const {
    if (core_data.IsValid() != ValidationResult::NoErrors) {
        return ResultInvalidCharInfo;
    }

    StoreData store_data{};
    store_data.BuildWithCoreData(core_data);
    const auto name = store_data.GetNickname();
    if (!MiiUtil::IsFontRegionValid(store_data.GetFontRegion(), name.data)) {
        store_data.SetInvalidName();
    }

    out_char_info.SetFromStoreData(store_data);
    return ResultSuccess;
}

Result MiiManager::ConvertCharInfoToCoreData(CoreData& out_core_data,
                                             const CharInfo& char_info) const {
    if (char_info.Verify() != ValidationResult::NoErrors) {
        return ResultInvalidCharInfo;
    }

    out_core_data.BuildFromCharInfo(char_info);
    const auto name = out_core_data.GetNickname();
    if (!MiiUtil::IsFontRegionValid(out_core_data.GetFontRegion(), name.data)) {
        out_core_data.SetNickname(out_core_data.GetInvalidNickname());
    }

    return ResultSuccess;
}

Result MiiManager::UpdateLatest(const DatabaseSessionMetadata& metadata, CharInfo& out_char_info,
                                const CharInfo& char_info, SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return ResultNotFound;
    }

    if (metadata.IsInterfaceVersionSupported(1)) {
        if (char_info.Verify() != ValidationResult::NoErrors) {
            return ResultInvalidCharInfo;
        }
    }

    u32 index{};
    Result result = database_manager.FindIndex(metadata, index, char_info.GetCreateId());

    if (result.IsError()) {
        return result;
    }

    StoreData store_data{};
    database_manager.Get(store_data, index, metadata);

    if (store_data.GetType() != char_info.GetType()) {
        return ResultNotFound;
    }

    out_char_info.SetFromStoreData(store_data);

    if (char_info == out_char_info) {
        return ResultNotUpdated;
    }

    return ResultSuccess;
}

Result MiiManager::UpdateLatest(const DatabaseSessionMetadata& metadata, StoreData& out_store_data,
                                const StoreData& store_data, SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return ResultNotFound;
    }

    if (metadata.IsInterfaceVersionSupported(1)) {
        if (store_data.IsValid() != ValidationResult::NoErrors) {
            return ResultInvalidCharInfo;
        }
    }

    u32 index{};
    Result result = database_manager.FindIndex(metadata, index, store_data.GetCreateId());

    if (result.IsError()) {
        return result;
    }

    database_manager.Get(out_store_data, index, metadata);

    if (out_store_data.GetType() != store_data.GetType()) {
        return ResultNotFound;
    }

    if (store_data == out_store_data) {
        return ResultNotUpdated;
    }

    return ResultSuccess;
}

Result MiiManager::Get(const DatabaseSessionMetadata& metadata,
                       std::span<CharInfoElement> out_elements, u32& out_count,
                       SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return BuildDefault(out_elements, out_count, source_flag);
    }

    const auto mii_count = database_manager.GetCount(metadata);

    for (std::size_t index = 0; index < mii_count; ++index) {
        if (out_elements.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        StoreData store_data{};
        database_manager.Get(store_data, index, metadata);

        out_elements[out_count].source = Source::Database;
        out_elements[out_count].char_info.SetFromStoreData(store_data);
        out_count++;
    }

    // Include default Mii at the end of the list
    return BuildDefault(out_elements, out_count, source_flag);
}

Result MiiManager::Get(const DatabaseSessionMetadata& metadata, std::span<CharInfo> out_char_info,
                       u32& out_count, SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return BuildDefault(out_char_info, out_count, source_flag);
    }

    const auto mii_count = database_manager.GetCount(metadata);

    for (std::size_t index = 0; index < mii_count; ++index) {
        if (out_char_info.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        StoreData store_data{};
        database_manager.Get(store_data, index, metadata);

        out_char_info[out_count].SetFromStoreData(store_data);
        out_count++;
    }

    // Include default Mii at the end of the list
    return BuildDefault(out_char_info, out_count, source_flag);
}

Result MiiManager::Get(const DatabaseSessionMetadata& metadata,
                       std::span<StoreDataElement> out_elements, u32& out_count,
                       SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return BuildDefault(out_elements, out_count, source_flag);
    }

    const auto mii_count = database_manager.GetCount(metadata);

    for (std::size_t index = 0; index < mii_count; ++index) {
        if (out_elements.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        StoreData store_data{};
        database_manager.Get(store_data, index, metadata);

        out_elements[out_count].store_data = store_data;
        out_elements[out_count].source = Source::Database;
        out_count++;
    }

    // Include default Mii at the end of the list
    return BuildDefault(out_elements, out_count, source_flag);
}

Result MiiManager::Get(const DatabaseSessionMetadata& metadata, std::span<StoreData> out_store_data,
                       u32& out_count, SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Database) == SourceFlag::None) {
        return BuildDefault(out_store_data, out_count, source_flag);
    }

    const auto mii_count = database_manager.GetCount(metadata);

    for (std::size_t index = 0; index < mii_count; ++index) {
        if (out_store_data.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        StoreData store_data{};
        database_manager.Get(store_data, index, metadata);

        out_store_data[out_count] = store_data;
        out_count++;
    }

    // Include default Mii at the end of the list
    return BuildDefault(out_store_data, out_count, source_flag);
}
Result MiiManager::BuildDefault(std::span<CharInfoElement> out_elements, u32& out_count,
                                SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Default) == SourceFlag::None) {
        return ResultSuccess;
    }

    StoreData store_data{};

    for (std::size_t index = 0; index < DefaultMiiCount; ++index) {
        if (out_elements.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        store_data.BuildDefault(static_cast<u32>(index));

        out_elements[out_count].source = Source::Default;
        out_elements[out_count].char_info.SetFromStoreData(store_data);
        out_count++;
    }

    return ResultSuccess;
}

Result MiiManager::BuildDefault(std::span<CharInfo> out_char_info, u32& out_count,
                                SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Default) == SourceFlag::None) {
        return ResultSuccess;
    }

    StoreData store_data{};

    for (std::size_t index = 0; index < DefaultMiiCount; ++index) {
        if (out_char_info.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        store_data.BuildDefault(static_cast<u32>(index));

        out_char_info[out_count].SetFromStoreData(store_data);
        out_count++;
    }

    return ResultSuccess;
}

Result MiiManager::BuildDefault(std::span<StoreDataElement> out_elements, u32& out_count,
                                SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Default) == SourceFlag::None) {
        return ResultSuccess;
    }

    for (std::size_t index = 0; index < DefaultMiiCount; ++index) {
        if (out_elements.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        out_elements[out_count].store_data.BuildDefault(static_cast<u32>(index));
        out_elements[out_count].source = Source::Default;
        out_count++;
    }

    return ResultSuccess;
}

Result MiiManager::BuildDefault(std::span<StoreData> out_char_info, u32& out_count,
                                SourceFlag source_flag) const {
    if ((source_flag & SourceFlag::Default) == SourceFlag::None) {
        return ResultSuccess;
    }

    for (std::size_t index = 0; index < DefaultMiiCount; ++index) {
        if (out_char_info.size() <= static_cast<std::size_t>(out_count)) {
            return ResultInvalidArgumentSize;
        }

        out_char_info[out_count].BuildDefault(static_cast<u32>(index));
        out_count++;
    }

    return ResultSuccess;
}

} // namespace Service::Mii
