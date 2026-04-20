// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "common/uuid.h"
#include "core/file_sys/romfs_factory.h"

namespace Service::NS {

enum class ApplicationEvent : u8 {
    None = 0,
    Installing = 2,
    Installed = 3,
    GameCardNotInserted = 5,
    Archived = 11,
    GameCard = 16,
};

enum ApplicationViewFlags : u32 {
    ApplicationViewFlags_Installed = 1 << 0,
    ApplicationViewFlags_Visible = 1 << 1,
    ApplicationViewFlags_Initialized = 1 << 2,
    ApplicationViewFlags_Accessible = 1 << 3,
};

enum class ApplicationControlSource : u8 {
    CacheOnly = 0,
    Storage = 1,
    StorageOnly = 2,
};

constexpr u32 DefaultApplicationViewFlags = 0x401f17;
constexpr u32 DefaultApplicationVersion = 0x70000;

enum class ApplicationDownloadState : u8 {
    Runnable = 0,
    Suspended,
    NotEnoughSpace,
    Fatal,
    Finished,
    SystemUpdateRequired
};

enum class BackgroundNetworkUpdateState : u8 {
    None,
    InProgress,
    Ready,
};

struct ApplicationRecord {
    u64 application_id;
    ApplicationEvent last_event;
    u8 attributes;
    INSERT_PADDING_BYTES_NOINIT(0x6);
    s64 last_updated;
};
static_assert(sizeof(ApplicationRecord) == 0x18, "ApplicationRecord has incorrect size.");

/// ApplicationDownloadProgress
struct ApplicationDownloadProgress {
    u64 downloaded_size;
    u64 total_size;
    u32 last_result;
    ApplicationDownloadState state;
    bool is_running;
    INSERT_PADDING_BYTES_NOINIT(0x2);
    u64 time;
};
static_assert(sizeof(ApplicationDownloadProgress) == 0x20, "ApplicationDownloadProgress has incorrect size.");

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

struct ApplicationViewV19 {
    u64 application_id;
    u32 version;
    u32 flags;
    ApplicationDownloadProgress download_state;
    ApplicationDownloadProgress download_progress;
};
static_assert(sizeof(ApplicationViewV19) == 0x50, "ApplicationViewV19 has incorrect size.");

struct ApplicationViewV20 {
    u64 application_id;
    u32 version;
    u32 flags;
    u32 unk;
    ApplicationDownloadProgress download_state;
    ApplicationDownloadProgress download_progress;
};
static_assert(sizeof(ApplicationViewV20) == 0x58, "ApplicationViewV20 has incorrect size.");

struct ApplicationViewData {
    u64 application_id{};
    u32 version{};
    u32 flags{};
    u32 unk{};
    ApplicationDownloadProgress download_state{};
    ApplicationDownloadProgress download_progress{};
};

inline size_t WriteApplicationView(void* dst, size_t dst_size, const ApplicationViewData& data,
                                   bool is_fw20) {
    if (is_fw20) {
        if (dst_size < sizeof(ApplicationViewV20)) return 0;
        auto* out = reinterpret_cast<ApplicationViewV20*>(dst);
        out->application_id = data.application_id;
        out->version = data.version;
        out->flags = data.flags;
        out->unk = data.unk;
        out->download_state = data.download_state;
        out->download_progress = data.download_progress;
        return sizeof(ApplicationViewV20);
    } else {
        if (dst_size < sizeof(ApplicationViewV19)) return 0;
        auto* out = reinterpret_cast<ApplicationViewV19*>(dst);
        out->application_id = data.application_id;
        out->version = data.version;
        out->flags = data.flags;
        out->download_state = data.download_state;
        out->download_progress = data.download_progress;
        return sizeof(ApplicationViewV19);
    }
}

struct ApplicationViewWithPromotionData {
    ApplicationViewData view;
    PromotionInfo promotion;
};

inline size_t WriteApplicationViewWithPromotion(void* dst, size_t dst_size,
                                                const ApplicationViewWithPromotionData& data,
                                                bool sdk20_plus) {
    const size_t view_written = WriteApplicationView(dst, dst_size, data.view, sdk20_plus);
    if (view_written == 0) return 0;
    const size_t remaining = dst_size - view_written;
    if (remaining < sizeof(PromotionInfo)) return 0;
    auto* promo_dst = reinterpret_cast<u8*>(dst) + view_written;
    std::memcpy(promo_dst, &data.promotion, sizeof(PromotionInfo));
    return view_written + sizeof(PromotionInfo);
}

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

struct ApplicationDisplayData {
    std::array<char, 0x200> application_name;
    std::array<char, 0x100> developer_name;
};
static_assert(sizeof(ApplicationDisplayData) == 0x300, "ApplicationDisplayData has incorrect size.");

struct LogoPath {
    std::array<char, 0x300> path;
};
static_assert(std::is_trivially_copyable_v<LogoPath>, "LogoPath must be trivially copyable.");

} // namespace Service::NS
