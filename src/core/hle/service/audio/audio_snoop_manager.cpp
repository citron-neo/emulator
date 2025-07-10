// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/audio/audio_snoop_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include <cstring>
#include <algorithm>

namespace Service::Audio {

IAudioSnoopManager::IAudioSnoopManager(Core::System& system_)
    : ServiceFramework{system_, "auddev"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioSnoopManager::EnableDspUsageMeasurement>, "EnableDspUsageMeasurement"}, // [6.0.0-16.1.0] / [17.0.0+] GetDspStatistics
        {1, D<&IAudioSnoopManager::DisableDspUsageMeasurement>, "DisableDspUsageMeasurement"}, // [6.0.0-16.1.0] / [20.0.0+] GetAppletStateSummaries
        {2, D<&IAudioSnoopManager::SetDspStatisticsParameter>, "SetDspStatisticsParameter"}, // [20.0.0+]
        {3, D<&IAudioSnoopManager::GetDspStatisticsParameter>, "GetDspStatisticsParameter"}, // [20.0.0+]
        {6, D<&IAudioSnoopManager::GetDspUsage>, "GetDspUsage"}, // [6.0.0-16.1.0]
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioSnoopManager::~IAudioSnoopManager() = default;

Result IAudioSnoopManager::EnableDspUsageMeasurement() {
    LOG_INFO(Service_Audio, "called");

    // [6.0.0-16.1.0] EnableDspUsageMeasurement
    dsp_usage_measurement_enabled = true;

    R_SUCCEED();
}

Result IAudioSnoopManager::GetDspStatistics(Out<u64> out_statistics) {
    LOG_INFO(Service_Audio, "called");

    // [17.0.0+] GetDspStatistics
    *out_statistics = dsp_statistics;

    R_SUCCEED();
}

Result IAudioSnoopManager::DisableDspUsageMeasurement() {
    LOG_INFO(Service_Audio, "called");

    // [6.0.0-16.1.0] DisableDspUsageMeasurement
    dsp_usage_measurement_enabled = false;

    R_SUCCEED();
}

Result IAudioSnoopManager::GetAppletStateSummaries(OutLargeData<std::array<u8, 0x1000>, BufferAttr_HipcMapAlias> out_summaries) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");

    // [20.0.0+] GetAppletStateSummaries
    // This function returns applet state summaries in a buffer
    // Since we don't have real applet state tracking, return empty data
    std::memset(out_summaries->data(), 0, out_summaries->size());

    R_SUCCEED();
}

Result IAudioSnoopManager::SetDspStatisticsParameter(InLargeData<std::array<u8, 0x100>, BufferAttr_HipcMapAlias> parameter) {
    LOG_INFO(Service_Audio, "called with parameter size {}", parameter->size());

    // [20.0.0+] SetDspStatisticsParameter
    // Copy the parameter data to our internal buffer
    const size_t copy_size = std::min(parameter->size(), dsp_statistics_parameter.size());
    std::memcpy(dsp_statistics_parameter.data(), parameter->data(), copy_size);

    R_SUCCEED();
}

Result IAudioSnoopManager::GetDspStatisticsParameter(OutLargeData<std::array<u8, 0x100>, BufferAttr_HipcMapAlias> out_parameter) {
    LOG_INFO(Service_Audio, "called");

    // [20.0.0+] GetDspStatisticsParameter
    // Return the stored parameter data
    const size_t copy_size = std::min(out_parameter->size(), dsp_statistics_parameter.size());
    std::memcpy(out_parameter->data(), dsp_statistics_parameter.data(), copy_size);

    R_SUCCEED();
}

Result IAudioSnoopManager::GetDspUsage(Out<u64> out_usage) {
    LOG_INFO(Service_Audio, "called");

    // [6.0.0-16.1.0] GetDspUsage
    *out_usage = dsp_usage;

    R_SUCCEED();
}

} // namespace Service::Audio