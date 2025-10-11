// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "audio_core/common/common.h"
#include "audio_core/renderer/effect/effect_info_base.h"
#include "common/common_types.h"

namespace AudioCore::Renderer {

/**
 * A full-featured limiter effect with attack, release, and threshold controls.
 * More sophisticated than LightLimiter.
 */
class LimiterInfo : public EffectInfoBase {
public:
    struct ParameterVersion1 {
        /* 0x00 */ std::array<s8, MaxChannels> inputs;
        /* 0x06 */ std::array<s8, MaxChannels> outputs;
        /* 0x0C */ u16 channel_count;
        /* 0x0E */ u16 padding;
        /* 0x10 */ s32 sample_rate;
        /* 0x14 */ f32 attack_time;    // Attack time in milliseconds
        /* 0x18 */ f32 release_time;   // Release time in milliseconds
        /* 0x1C */ f32 threshold;      // Threshold in dB
        /* 0x20 */ f32 makeup_gain;    // Makeup gain in dB
        /* 0x24 */ f32 ratio;          // Compression ratio
        /* 0x28 */ ParameterState state;
        /* 0x29 */ bool is_enabled;
        /* 0x2A */ char unk2A[0x2];
    };
    static_assert(sizeof(ParameterVersion1) <= sizeof(EffectInfoBase::InParameterVersion1),
                  "LimiterInfo::ParameterVersion1 has the wrong size!");

    using ParameterVersion2 = ParameterVersion1;

    struct State {
        /* 0x00 */ f32 envelope;
        /* 0x04 */ f32 gain_reduction;
        /* 0x08 */ f32 peak_hold;
        /* 0x0C */ u32 peak_hold_count;
        /* 0x10 */ std::array<f32, MaxChannels> channel_peaks;
    };
    static_assert(sizeof(State) <= sizeof(EffectInfoBase::State),
                  "LimiterInfo::State is too large!");

    /**
     * Update the info with new parameters.
     *
     * @param error_info - Output error information.
     * @param in_params - Input parameters.
     * @param pool_mapper - Memory pool mapper for buffers.
     */
    void Update(BehaviorInfo::ErrorInfo& error_info,
                const EffectInfoBase::InParameterVersion1& in_params,
                const PoolMapper& pool_mapper);

    /**
     * Update the info with new parameters (version 2).
     *
     * @param error_info - Output error information.
     * @param in_params - Input parameters.
     * @param pool_mapper - Memory pool mapper for buffers.
     */
    void Update(BehaviorInfo::ErrorInfo& error_info,
                const EffectInfoBase::InParameterVersion2& in_params,
                const PoolMapper& pool_mapper);

    /**
     * Update the usage state for command generation.
     */
    void UpdateForCommandGeneration();

    /**
     * Initialize the result state.
     *
     * @param result_state - Result state to initialize.
     */
    void InitializeResultState(EffectResultState& result_state);

    /**
     * Update the result state.
     *
     * @param cpu_state - CPU-side result state.
     * @param dsp_state - DSP-side result state.
     */
    void UpdateResultState(EffectResultState& cpu_state, EffectResultState& dsp_state);

    /**
     * Get a workbuffer address.
     *
     * @param index - Index of the workbuffer.
     * @return Address of the workbuffer.
     */
    CpuAddr GetWorkbuffer(s32 index);
};

} // namespace AudioCore::Renderer
