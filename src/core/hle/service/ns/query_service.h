// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

struct PlayStatistics {
    u64 application_id{};
    u32 first_entry_index{};
    u32 first_timestamp_user{};
    u32 first_timestamp_network{};
    u32 last_entry_index{};
    u32 last_timestamp_user{};
    u32 last_timestamp_network{};
    u32 play_time_in_minutes{};
    u32 total_launches{};
};
static_assert(sizeof(PlayStatistics) == 0x28, "PlayStatistics is an invalid size");

struct LastPlayTime {
    INSERT_PADDING_BYTES_NOINIT(0x8); // Likely needs padding for buffer alignment if used
};

struct ApplicationPlayStatistics {
    u64 application_id{};
    u64 play_time_ns{};
    u64 launch_count{};
};
static_assert(sizeof(ApplicationPlayStatistics) == 0x18,
              "ApplicationPlayStatistics is an invalid size");

class IQueryService final : public ServiceFramework<IQueryService> {
public:
    explicit IQueryService(Core::System& system_);
    ~IQueryService() override;

private:
    Result QueryPlayStatisticsByApplicationIdAndUserAccountId(
        Out<PlayStatistics> out_play_statistics, bool unknown, u64 application_id, Uid account_id);

    Result QueryRecentlyPlayedApplication(Out<s32> out_count,
                                          OutArray<u64, BufferAttr_HipcMapAlias> out_applications,
                                          Uid user_id);

    Result QueryApplicationPlayStatisticsForSystem(
        Out<s32> out_entries, u8 flag,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);

    Result QueryApplicationPlayStatisticsByUserAccountIdForSystem(
        Out<s32> out_entries, u8 flag, Common::UUID user_id,
        OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
        InArray<u64, BufferAttr_HipcMapAlias> application_ids);
};

} // namespace Service::NS
