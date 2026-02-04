// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "common/uuid.h"
#include "core/file_sys/romfs_factory.h"

namespace Service::NS {

enum class ApplicationRecordType : u8 {
    Installing = 2,
    Installed = 3,
    GameCardNotInserted = 5,
    Archived = 11,
    GameCard = 16,
};

enum class ApplicationControlSource : u8 {
    CacheOnly = 0,
    Storage = 1,
    StorageOnly = 2,
};

enum class BackgroundNetworkUpdateState : u8 {
    None,
    InProgress,
    Ready,
};

/// ApplicationDownloadState
struct ApplicationDownloadState {
    u64 downloaded_size;
    u64 total_size;
    u32 unk_x10;
    u8 state;
    u8 unk_x15;
    std::array<u8, 0x2> unk_x16;
    u64 unk_x18;
};
static_assert(sizeof(ApplicationDownloadState) == 0x20,
              "ApplicationDownloadState has incorrect size.");

struct ApplicationRecord {
    u64 application_id;
    ApplicationRecordType type;
    u8 attributes;
    INSERT_PADDING_BYTES_NOINIT(0x6);
    s64 last_updated;
};
static_assert(sizeof(ApplicationRecord) == 0x18, "ApplicationRecord has incorrect size.");

struct ApplicationViewV19 {
    u64 application_id;
    u32 version;
    u32 flags;
    ApplicationDownloadState download_state;
    ApplicationDownloadState download_progress;
};
static_assert(sizeof(ApplicationViewV19) == 0x50, "ApplicationViewV19 has incorrect size.");

struct ApplicationViewV20 {
    u64 application_id;
    u32 version;
    u32 flags;
    u32 unk;
    ApplicationDownloadState download_state;
    ApplicationDownloadState download_progress;
};
static_assert(sizeof(ApplicationViewV20) == 0x58, "ApplicationViewV20 has incorrect size.");

using ApplicationView = ApplicationViewV19;
static_assert(sizeof(ApplicationView) == 0x50, "ApplicationView has incorrect size.");

struct ApplicationRightsOnClient {
    u64 application_id;
    Common::UUID uid;
    u8 flags;
    u8 flags2;
    INSERT_PADDING_BYTES_NOINIT(0x6);
};
static_assert(sizeof(ApplicationRightsOnClient) == 0x20,
              "ApplicationRightsOnClient has incorrect size.");

/// NsPromotionInfo
struct PromotionInfo {
    u64 start_timestamp; ///< POSIX timestamp for the promotion start.
    u64 end_timestamp;   ///< POSIX timestamp for the promotion end.
    s64 remaining_time;  ///< Remaining time until the promotion ends, in nanoseconds
                         ///< ({end_timestamp - current_time} converted to nanoseconds).
    INSERT_PADDING_BYTES_NOINIT(0x4);
    u8 flags; ///< Flags. Bit0: whether the PromotionInfo is valid (including bit1). Bit1 clear:
              ///< remaining_time is set.
    INSERT_PADDING_BYTES_NOINIT(0x3);
};
static_assert(sizeof(PromotionInfo) == 0x20, "PromotionInfo has incorrect size.");

/// NsApplicationViewWithPromotionInfo
struct ApplicationViewWithPromotionInfo {
    ApplicationView view;    ///< \ref NsApplicationView
    PromotionInfo promotion; ///< \ref NsPromotionInfo
};
static_assert(sizeof(ApplicationViewWithPromotionInfo) == 0x70,
              "ApplicationViewWithPromotionInfo has incorrect size.");

struct ApplicationOccupiedSizeEntity {
    FileSys::StorageId storage_id;
    u64 app_size;
    u64 patch_size;
    u64 aoc_size;
};
static_assert(sizeof(ApplicationOccupiedSizeEntity) == 0x20,
              "ApplicationOccupiedSizeEntity has incorrect size.");

struct ApplicationOccupiedSize {
    std::array<ApplicationOccupiedSizeEntity, 4> entities;
};
static_assert(sizeof(ApplicationOccupiedSize) == 0x80,
              "ApplicationOccupiedSize has incorrect size.");

struct ContentPath {
    u8 file_system_proxy_type;
    u64 program_id;
};
static_assert(sizeof(ContentPath) == 0x10, "ContentPath has incorrect size.");

struct Uid {
    alignas(8) Common::UUID uuid;
};
static_assert(sizeof(Uid) == 0x10, "Uid has incorrect size.");

} // namespace Service::NS
