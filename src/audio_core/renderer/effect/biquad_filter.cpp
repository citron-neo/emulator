// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/renderer/effect/biquad_filter.h"

namespace AudioCore::Renderer {

void BiquadFilterInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                              const InParameterVersion1& in_params, const PoolMapper& pool_mapper) {
    auto in_specific{reinterpret_cast<const ParameterVersion1*>(in_params.specific.data())};
    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};

    std::memcpy(params, in_specific, sizeof(ParameterVersion1));
    mix_id = in_params.mix_id;
    process_order = in_params.process_order;
    enabled = in_params.enabled;

    error_info.error_code = ResultSuccess;
    error_info.address = CpuAddr(0);
}

void BiquadFilterInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                              const InParameterVersion2& in_params, const PoolMapper& pool_mapper) {
    auto in_specific{reinterpret_cast<const ParameterVersion2*>(in_params.specific.data())};
    auto params{reinterpret_cast<ParameterVersion2*>(parameter.data())};

    std::memcpy(params, in_specific, sizeof(ParameterVersion2));
    mix_id = in_params.mix_id;
    process_order = in_params.process_order;
    enabled = in_params.enabled;

    error_info.error_code = ResultSuccess;
    error_info.address = CpuAddr(0);
}

void BiquadFilterInfo::UpdateForCommandGeneration() {
    if (enabled) {
        usage_state = UsageState::Enabled;
    } else {
        usage_state = UsageState::Disabled;
    }

    // Determine which version structure is being used
    // Version 1: state at offset 0x17, structure size ~24 bytes
    // Version 2: state at offset 0x25, structure size ~40 bytes
    auto params_v1{reinterpret_cast<ParameterVersion1*>(parameter.data())};
    auto params_v2{reinterpret_cast<ParameterVersion2*>(parameter.data())};

    // Check which state location contains a valid ParameterState value (0-2)
    // Valid states: Initialized (0), Updating (1), Updated (2)
    const auto state_v1_raw = *reinterpret_cast<const u8*>(&params_v1->state);
    const auto state_v2_raw = *reinterpret_cast<const u8*>(&params_v2->state);

    if (state_v1_raw <= 2) {
        // Version 1 location has valid state, update there
        params_v1->state = ParameterState::Updated;
    } else if (state_v2_raw <= 2) {
        // Version 2 location has valid state, update there
        params_v2->state = ParameterState::Updated;
    } else {
        // Neither looks valid, update both (one will be wrong but command generator handles it)
        params_v1->state = ParameterState::Updated;
        params_v2->state = ParameterState::Updated;
    }
}

void BiquadFilterInfo::InitializeResultState(EffectResultState& result_state) {}

void BiquadFilterInfo::UpdateResultState(EffectResultState& cpu_state,
                                         EffectResultState& dsp_state) {}

} // namespace AudioCore::Renderer
