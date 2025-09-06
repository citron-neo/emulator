// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <fmt/format.h>
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace FileSys {

using SaveDataId = u64;
using SystemSaveDataId = u64;
using SystemBcatSaveDataId = SystemSaveDataId;
using ProgramId = u64;

enum class SaveDataSpaceId : u8 {
    System = 0,
    User = 1,
    SdSystem = 2,
    Temporary = 3,
    SdUser = 4,

    ProperSystem = 100,
    SafeMode = 101,
};

enum class SaveDataType : u8 {
    System = 0,
    Account = 1,
    Bcat = 2,
    Device = 3,
    Temporary = 4,
    Cache = 5,
    SystemBcat = 6,
};

enum class SaveDataRank : u8 {
    Primary = 0,
    Secondary = 1,
};

struct SaveDataSize {
    u64 normal;
    u64 journal;
};
static_assert(sizeof(SaveDataSize) == 0x10, "SaveDataSize has invalid size.");

using UserId = u128;
static_assert(std::is_trivially_copyable_v<UserId>, "Data type must be trivially copyable.");
static_assert(sizeof(UserId) == 0x10, "UserId has invalid size.");

constexpr inline SystemSaveDataId InvalidSystemSaveDataId = 0;
constexpr inline UserId InvalidUserId = {};

enum class SaveDataFlags : u32 {
    None = (0 << 0),
    KeepAfterResettingSystemSaveData = (1 << 0),
    KeepAfterRefurbishment = (1 << 1),
    KeepAfterResettingSystemSaveDataWithoutUserSaveData = (1 << 2),
    NeedsSecureDelete = (1 << 3),
};

enum class SaveDataMetaType : u8 {
    None = 0,
    Thumbnail = 1,
    ExtensionContext = 2,
};

struct SaveDataMetaInfo {
    u32 size;
    SaveDataMetaType type;
    INSERT_PADDING_BYTES(0xB);
};
static_assert(std::is_trivially_copyable_v<SaveDataMetaInfo>,
              "Data type must be trivially copyable.");
static_assert(sizeof(SaveDataMetaInfo) == 0x10, "SaveDataMetaInfo has invalid size.");

struct SaveDataCreationInfo {
    s64 size;
    s64 journal_size;
    s64 block_size;
    u64 owner_id;
    u32 flags;
    SaveDataSpaceId space_id;
    bool pseudo;
    INSERT_PADDING_BYTES(0x1A);
};
static_assert(std::is_trivially_copyable_v<SaveDataCreationInfo>,
              "Data type must be trivially copyable.");
static_assert(sizeof(SaveDataCreationInfo) == 0x40, "SaveDataCreationInfo has invalid size.");

struct SaveDataAttribute {
    ProgramId program_id;
    UserId user_id;
    SystemSaveDataId system_save_data_id;
    SaveDataType type;
    SaveDataRank rank;
    u16 index;
    INSERT_PADDING_BYTES(0x1C);

    static constexpr SaveDataAttribute Make(ProgramId program_id, SaveDataType type, UserId user_id,
                                            SystemSaveDataId system_save_data_id, u16 index,
                                            SaveDataRank rank) {
        return {
            .program_id = program_id,
            .user_id = user_id,
            .system_save_data_id = system_save_data_id,
            .type = type,
            .rank = rank,
            .index = index,
        };
    }

    static constexpr SaveDataAttribute Make(ProgramId program_id, SaveDataType type, UserId user_id,
                                            SystemSaveDataId system_save_data_id, u16 index) {
        return Make(program_id, type, user_id, system_save_data_id, index, SaveDataRank::Primary);
    }

    static constexpr SaveDataAttribute Make(ProgramId program_id, SaveDataType type, UserId user_id,
                                            SystemSaveDataId system_save_data_id) {
        return Make(program_id, type, user_id, system_save_data_id, 0, SaveDataRank::Primary);
    }

    std::string DebugInfo() const {
        return fmt::format(
            "[title_id={:016X}, user_id={:016X}{:016X}, save_id={:016X}, type={:02X}, "
            "rank={}, index={}]",
            program_id, user_id[1], user_id[0], system_save_data_id, static_cast<u8>(type),
            static_cast<u8>(rank), index);
    }
};
static_assert(sizeof(SaveDataAttribute) == 0x40);
static_assert(std::is_trivially_destructible<SaveDataAttribute>::value);

constexpr inline bool operator<(const SaveDataAttribute& lhs, const SaveDataAttribute& rhs) {
    return std::tie(lhs.program_id, lhs.user_id, lhs.system_save_data_id, lhs.index, lhs.rank) <
           std::tie(rhs.program_id, rhs.user_id, rhs.system_save_data_id, rhs.index, rhs.rank);
}

constexpr inline bool operator==(const SaveDataAttribute& lhs, const SaveDataAttribute& rhs) {
    return std::tie(lhs.program_id, lhs.user_id, lhs.system_save_data_id, lhs.type, lhs.rank,
                    lhs.index) == std::tie(rhs.program_id, rhs.user_id, rhs.system_save_data_id,
                                           rhs.type, rhs.rank, rhs.index);
}

constexpr inline bool operator!=(const SaveDataAttribute& lhs, const SaveDataAttribute& rhs) {
    return !(lhs == rhs);
}

struct SaveDataExtraData {
    SaveDataAttribute attr;
    u64 owner_id;
    s64 timestamp;
    u32 flags;
    INSERT_PADDING_BYTES(4);
    s64 available_size;
    s64 journal_size;
    s64 commit_id;
    INSERT_PADDING_BYTES(0x190);
};
static_assert(sizeof(SaveDataExtraData) == 0x200, "SaveDataExtraData has invalid size.");
static_assert(std::is_trivially_copyable_v<SaveDataExtraData>,
              "Data type must be trivially copyable.");

struct SaveDataFilter {
    bool use_program_id;
    bool use_save_data_type;
    bool use_user_id;
    bool use_save_data_id;
    bool use_index;
    SaveDataRank rank;
    SaveDataAttribute attribute;
};
static_assert(sizeof(SaveDataFilter) == 0x48, "SaveDataFilter has invalid size.");
static_assert(std::is_trivially_copyable_v<SaveDataFilter>,
              "Data type must be trivially copyable.");

struct HashSalt {
    static constexpr size_t Size = 32;

    std::array<u8, Size> value;
};
static_assert(std::is_trivially_copyable_v<HashSalt>, "Data type must be trivially copyable.");
static_assert(sizeof(HashSalt) == HashSalt::Size);

} // namespace FileSys
