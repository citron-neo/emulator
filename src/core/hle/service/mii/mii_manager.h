// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "core/hle/result.h"
#include "core/hle/service/mii/mii_database_manager.h"
#include "core/hle/service/mii/mii_types.h"

namespace Service::Mii {
class CharInfo;
class CoreData;
class StoreData;
class Ver3StoreData;

struct CharInfoElement;
struct StoreDataElement;

// The Mii manager is responsible for handling mii operations along with providing an easy interface
// for HLE emulation of the mii service.
class MiiManager {
public:
    MiiManager();
    Result Initialize(DatabaseSessionMetadata& metadata);

    // Auto generated mii
    void BuildDefault(CharInfo& out_char_info, u32 index) const;
    void BuildBase(CharInfo& out_char_info, Gender gender) const;
    void BuildRandom(CharInfo& out_char_info, Age age, Gender gender, Race race) const;

    // Database operations
    bool IsFullDatabase() const;
    void SetInterfaceVersion(DatabaseSessionMetadata& metadata, u32 version) const;
    bool IsUpdated(DatabaseSessionMetadata& metadata, SourceFlag source_flag) const;
    u32 GetCount(const DatabaseSessionMetadata& metadata, SourceFlag source_flag) const;
    Result Move(DatabaseSessionMetadata& metadata, u32 index, const Common::UUID& create_id);
    Result AddOrReplace(DatabaseSessionMetadata& metadata, const StoreData& store_data);
    Result Delete(DatabaseSessionMetadata& metadata, const Common::UUID& create_id);
    s32 FindIndex(const Common::UUID& create_id, bool is_special) const;
    Result GetIndex(const DatabaseSessionMetadata& metadata, const CharInfo& char_info,
                    s32& out_index) const;
    Result Append(DatabaseSessionMetadata& metadata, const CharInfo& char_info);

    // Test database operations
    bool IsBrokenWithClearFlag(DatabaseSessionMetadata& metadata);
    Result DestroyFile(DatabaseSessionMetadata& metadata);
    Result DeleteFile();
    Result Format(DatabaseSessionMetadata& metadata);

    // Mii conversions
    Result ConvertV3ToCharInfo(CharInfo& out_char_info, const Ver3StoreData& mii_v3) const;
    Result ConvertCoreDataToCharInfo(CharInfo& out_char_info, const CoreData& core_data) const;
    Result ConvertCharInfoToCoreData(CoreData& out_core_data, const CharInfo& char_info) const;
    Result UpdateLatest(const DatabaseSessionMetadata& metadata, CharInfo& out_char_info,
                        const CharInfo& char_info, SourceFlag source_flag) const;
    Result UpdateLatest(const DatabaseSessionMetadata& metadata, StoreData& out_store_data,
                        const StoreData& store_data, SourceFlag source_flag) const;

    // Overloaded getters
    Result Get(const DatabaseSessionMetadata& metadata, std::span<CharInfoElement> out_elements,
               u32& out_count, SourceFlag source_flag) const;
    Result Get(const DatabaseSessionMetadata& metadata, std::span<CharInfo> out_char_info,
               u32& out_count, SourceFlag source_flag) const;
    Result Get(const DatabaseSessionMetadata& metadata, std::span<StoreDataElement> out_elements,
               u32& out_count, SourceFlag source_flag) const;
    Result Get(const DatabaseSessionMetadata& metadata, std::span<StoreData> out_store_data,
               u32& out_count, SourceFlag source_flag) const;

private:
    Result BuildDefault(std::span<CharInfoElement> out_elements, u32& out_count,
                        SourceFlag source_flag) const;
    Result BuildDefault(std::span<CharInfo> out_char_info, u32& out_count,
                        SourceFlag source_flag) const;
    Result BuildDefault(std::span<StoreDataElement> out_char_info, u32& out_count,
                        SourceFlag source_flag) const;
    Result BuildDefault(std::span<StoreData> out_char_info, u32& out_count,
                        SourceFlag source_flag) const;

    DatabaseManager database_manager{};

    // This should be a global value
    bool is_broken_with_clear_flag{};
};

}; // namespace Service::Mii
