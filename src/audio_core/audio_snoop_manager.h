// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <mutex>
#include <span>

#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace AudioCore {

/**
 * Manages DSP statistics and performance monitoring.
 */
class AudioSnoopManager {
public:
    struct DspStatistics {
        /* 0x00 */ u64 total_cycles;
        /* 0x08 */ u64 active_cycles;
        /* 0x10 */ u32 voice_drop_count;
        /* 0x14 */ u32 command_drop_count;
        /* 0x18 */ u32 buffer_underrun_count;
        /* 0x1C */ u32 buffer_overrun_count;
        /* 0x20 */ f32 cpu_usage_percent;
        /* 0x24 */ f32 dsp_usage_percent;
    };
    static_assert(sizeof(DspStatistics) == 0x28, "DspStatistics has the wrong size!");

    struct AppletStateSummary {
        /* 0x00 */ u64 applet_resource_user_id;
        /* 0x08 */ u32 audio_in_active_count;
        /* 0x0C */ u32 audio_out_active_count;
        /* 0x10 */ u32 audio_renderer_active_count;
        /* 0x14 */ u32 final_output_recorder_active_count;
        /* 0x18 */ u32 total_active_count;
        /* 0x1C */ u32 reserved;
    };
    static_assert(sizeof(AppletStateSummary) == 0x20, "AppletStateSummary has the wrong size!");

    explicit AudioSnoopManager(Core::System& system);
    ~AudioSnoopManager();

    /**
     * Get DSP statistics.
     *
     * @param statistics - Output statistics structure.
     * @return Result code.
     */
    Result GetDspStatistics(DspStatistics& statistics);

    /**
     * Set DSP statistics parameter.
     *
     * @param enabled - Enable or disable statistics gathering.
     * @return Result code.
     */
    Result SetDspStatisticsParameter(bool enabled);

    /**
     * Get DSP statistics parameter.
     *
     * @param enabled - Output enabled state.
     * @return Result code.
     */
    Result GetDspStatisticsParameter(bool& enabled);

    /**
     * Get applet state summaries.
     *
     * @param summaries - Output array of summaries.
     * @param count - Output count of summaries.
     * @return Result code.
     */
    Result GetAppletStateSummaries(std::span<AppletStateSummary> summaries, u32& count);

    /**
     * Update statistics (called periodically by the audio system).
     *
     * @param cycles_elapsed - Number of CPU cycles elapsed.
     * @param active_voices - Number of active voices.
     * @param dropped_commands - Number of dropped commands.
     */
    void UpdateStatistics(u64 cycles_elapsed, u32 active_voices, u32 dropped_commands);

private:
    Core::System& system;
    std::mutex mutex;

    DspStatistics statistics{};
    bool statistics_enabled{false};
};

} // namespace AudioCore
