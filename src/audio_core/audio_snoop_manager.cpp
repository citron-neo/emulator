// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_snoop_manager.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/result.h"

namespace AudioCore {

AudioSnoopManager::AudioSnoopManager(Core::System& system_) : system{system_} {}
AudioSnoopManager::~AudioSnoopManager() = default;

Result AudioSnoopManager::GetDspStatistics(DspStatistics& statistics_out) {
    std::scoped_lock lock{mutex};

    if (!statistics_enabled) {
        LOG_DEBUG(Service_Audio, "DSP statistics not enabled");
        statistics_out = {};
        return ResultSuccess;
    }

    statistics_out = statistics;
    return ResultSuccess;
}

Result AudioSnoopManager::SetDspStatisticsParameter(bool enabled) {
    std::scoped_lock lock{mutex};

    LOG_DEBUG(Service_Audio, "Set DSP statistics enabled: {}", enabled);
    statistics_enabled = enabled;

    if (!enabled) {
        // Clear statistics when disabled
        statistics = {};
    }

    return ResultSuccess;
}

Result AudioSnoopManager::GetDspStatisticsParameter(bool& enabled) {
    std::scoped_lock lock{mutex};
    enabled = statistics_enabled;
    return ResultSuccess;
}

Result AudioSnoopManager::GetAppletStateSummaries(std::span<AppletStateSummary> summaries,
                                                   u32& count) {
    std::scoped_lock lock{mutex};

    // For now, return empty summaries
    // A full implementation would track active audio sessions per applet
    count = 0;
    return ResultSuccess;
}

void AudioSnoopManager::UpdateStatistics(u64 cycles_elapsed, u32 active_voices,
                                          u32 dropped_commands) {
    if (!statistics_enabled) {
        return;
    }

    std::scoped_lock lock{mutex};

    statistics.total_cycles += cycles_elapsed;
    statistics.active_cycles += cycles_elapsed;
    statistics.command_drop_count += dropped_commands;

    // Calculate usage percentages (simplified)
    if (statistics.total_cycles > 0) {
        statistics.cpu_usage_percent =
            static_cast<f32>(statistics.active_cycles) / static_cast<f32>(statistics.total_cycles) *
            100.0f;
        statistics.dsp_usage_percent = statistics.cpu_usage_percent * 0.5f; // Estimate
    }
}

} // namespace AudioCore
