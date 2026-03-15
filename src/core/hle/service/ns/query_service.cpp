// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include "common/logging.h"
#include "common/uuid.h"
#include "core/core.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/query_service.h"
#include "core/hle/service/service.h"

namespace Service::NS {

IQueryService::IQueryService(Core::System& system_) : ServiceFramework{system_, "pdm:qry"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "QueryAppletEvent"},
        {1, nullptr, "QueryPlayStatistics"},
        {2, nullptr, "QueryPlayStatisticsByUserAccountId"},
        {3, nullptr, "QueryPlayStatisticsByNetworkServiceAccountId"},
        {4, nullptr, "QueryPlayStatisticsByApplicationId"},
        {5, D<&IQueryService::QueryPlayStatisticsByApplicationIdAndUserAccountId>, "QueryPlayStatisticsByApplicationIdAndUserAccountId"},
        {6, nullptr, "QueryPlayStatisticsByApplicationIdAndNetworkServiceAccountId"},
        {7, nullptr, "QueryLastPlayTimeV0"},
        {8, nullptr, "QueryPlayEvent"},
        {9, nullptr, "GetAvailablePlayEventRange"},
        {10, nullptr, "QueryAccountEvent"},
        {11, nullptr, "QueryAccountPlayEvent"},
        {12, nullptr, "GetAvailableAccountPlayEventRange"},
        {13, nullptr, "QueryApplicationPlayStatisticsForSystemV0"},
        {14, D<&IQueryService::QueryRecentlyPlayedApplication>, "QueryRecentlyPlayedApplication"},
        {15, nullptr, "GetRecentlyPlayedApplicationUpdateEvent"},
        {16, nullptr, "QueryApplicationPlayStatisticsByUserAccountIdForSystemV0"},
        {17, nullptr, "QueryLastPlayTime"},
        {18, D<&IQueryService::QueryApplicationPlayStatisticsForSystem>, "QueryApplicationPlayStatisticsForSystem"},
        {19, D<&IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystem>, "QueryApplicationPlayStatisticsByUserAccountIdForSystem"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IQueryService::~IQueryService() = default;

Result IQueryService::QueryPlayStatisticsByApplicationIdAndUserAccountId(
    Out<PlayStatistics> out_play_statistics, bool unknown, u64 application_id, Uid account_id) {
    // TODO(German77): Read statistics of the game
    *out_play_statistics = {
        .application_id = application_id,
        .total_launches = 1,
    };

    LOG_WARNING(Service_NS, "(STUBBED) called. unknown={}. application_id={:016X}, account_id={}",
                unknown, application_id, account_id.uuid.FormattedString());
    R_SUCCEED();
}

Result IQueryService::QueryRecentlyPlayedApplication(
    Out<s32> out_count, OutArray<u64, BufferAttr_HipcMapAlias> out_applications, Uid user_id) {
    const auto limit = out_applications.size();
    LOG_INFO(Service_NS, "called. user_id={}, limit={}", user_id.uuid.FormattedString(), limit);

    const auto& cache = system.GetContentProviderUnion();
    auto installed_games =
        cache.ListEntriesFilter(std::nullopt, FileSys::ContentRecordType::Program, std::nullopt);

    // Filter and sort
    std::vector<u64> applications;
    for (const auto& entry : installed_games) {
        if (entry.title_id == 0 || entry.title_id < 0x0100000000001FFFull) {
            continue;
        }
        applications.push_back(entry.title_id);
    }

    // Sort by Title ID for now as a stable order
    std::sort(applications.begin(), applications.end());
    applications.erase(std::unique(applications.begin(), applications.end()), applications.end());

    const auto count = std::min(limit, applications.size());
    LOG_INFO(Service_NS, "Returning {} applications out of {} found.", count, applications.size());

    for (size_t i = 0; i < count; i++) {
        LOG_INFO(Service_NS, "  [{}] TitleID: {:016X}", i, applications[i]);
        out_applications[i] = applications[i];
    }

    *out_count = static_cast<s32>(count);
    R_SUCCEED();
}

Result IQueryService::QueryApplicationPlayStatisticsForSystem(
    Out<s32> out_entries, u8 flag,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const size_t count = std::min(out_stats.size(), application_ids.size());
    s32 written = 0;
    for (size_t i = 0; i < count; ++i) {
        const u64 app_id = application_ids[i];
        ApplicationPlayStatistics stats{};
        stats.application_id = app_id;
        stats.play_time_ns = 0; // TODO: Implement play time tracking
        stats.launch_count = 1; // Default to 1 for now
        out_stats[i] = stats;
        ++written;
    }
    *out_entries = written;
    LOG_INFO(Service_NS, "called, entries={} flag={}", written, flag);
    R_SUCCEED();
}

Result IQueryService::QueryApplicationPlayStatisticsByUserAccountIdForSystem(
    Out<s32> out_entries, u8 flag, Common::UUID user_id,
    OutArray<ApplicationPlayStatistics, BufferAttr_HipcMapAlias> out_stats,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    LOG_INFO(Service_NS, "called, user_id={}", user_id.FormattedString());
    return QueryApplicationPlayStatisticsForSystem(out_entries, flag, out_stats, application_ids);
}

} // namespace Service::NS
