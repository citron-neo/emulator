// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Audio {

class IAudioSnoopManager final : public ServiceFramework<IAudioSnoopManager> {
public:
    explicit IAudioSnoopManager(Core::System& system_);
    ~IAudioSnoopManager() override;

private:
    // [6.0.0-16.1.0] EnableDspUsageMeasurement / [17.0.0+] GetDspStatistics
    Result EnableDspUsageMeasurement();
    Result GetDspStatistics(Out<u64> out_statistics);

    // [6.0.0-16.1.0] DisableDspUsageMeasurement / [20.0.0+] GetAppletStateSummaries
    Result DisableDspUsageMeasurement();
    Result GetAppletStateSummaries(OutLargeData<std::array<u8, 0x1000>, BufferAttr_HipcMapAlias> out_summaries);

    // [20.0.0+] SetDspStatisticsParameter
    Result SetDspStatisticsParameter(InLargeData<std::array<u8, 0x100>, BufferAttr_HipcMapAlias> parameter);

    // [20.0.0+] GetDspStatisticsParameter
    Result GetDspStatisticsParameter(OutLargeData<std::array<u8, 0x100>, BufferAttr_HipcMapAlias> out_parameter);

    // [6.0.0-16.1.0] GetDspUsage
    Result GetDspUsage(Out<u64> out_usage);

    // State variables
    bool dsp_usage_measurement_enabled{false};
    u64 dsp_statistics{0};
    u64 dsp_usage{0};
    std::array<u8, 0x100> dsp_statistics_parameter{};
};

} // namespace Service::Audio