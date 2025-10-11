// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/renderer/effect/limiter.h"
#include "core/hle/result.h"

namespace AudioCore::Renderer {

void LimiterInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                          const EffectInfoBase::InParameterVersion1& in_params,
                          const PoolMapper& pool_mapper) {
    auto in_specific{reinterpret_cast<const ParameterVersion1*>(in_params.specific.data())};
    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};

    std::memcpy(params, in_specific, sizeof(ParameterVersion1));
    mix_id = in_params.mix_id;
    process_order = in_params.process_order;
    enabled = in_params.enabled;

    error_info.error_code = ResultSuccess;
    error_info.address = CpuAddr(0);
}

void LimiterInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                          const EffectInfoBase::InParameterVersion2& in_params,
                          const PoolMapper& pool_mapper) {
    auto in_specific{reinterpret_cast<const ParameterVersion2*>(in_params.specific.data())};
    auto params{reinterpret_cast<ParameterVersion2*>(parameter.data())};

    std::memcpy(params, in_specific, sizeof(ParameterVersion2));
    mix_id = in_params.mix_id;
    process_order = in_params.process_order;
    enabled = in_params.enabled;

    error_info.error_code = ResultSuccess;
    error_info.address = CpuAddr(0);
}

void LimiterInfo::UpdateForCommandGeneration() {
    if (enabled) {
        usage_state = UsageState::Enabled;
    } else {
        usage_state = UsageState::Disabled;
    }

    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};
    params->state = ParameterState::Updated;
}

void LimiterInfo::InitializeResultState(EffectResultState& result_state) {
    auto limiter_state{reinterpret_cast<State*>(result_state.state.data())};
    limiter_state->envelope = 1.0f;
    limiter_state->gain_reduction = 1.0f;
    limiter_state->peak_hold = 0.0f;
    limiter_state->peak_hold_count = 0;
    limiter_state->channel_peaks.fill(0.0f);
}

void LimiterInfo::UpdateResultState(EffectResultState& cpu_state, EffectResultState& dsp_state) {
    cpu_state = dsp_state;
}

CpuAddr LimiterInfo::GetWorkbuffer(s32 index) {
    return GetSingleBuffer(index);
}

} // namespace AudioCore::Renderer
