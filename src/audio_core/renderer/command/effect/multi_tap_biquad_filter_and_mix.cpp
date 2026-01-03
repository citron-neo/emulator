// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/effect/biquad_filter.h"
#include "audio_core/renderer/command/effect/multi_tap_biquad_filter_and_mix.h"
#include "audio_core/renderer/voice/voice_state.h"

namespace AudioCore::Renderer {

void MultiTapBiquadFilterAndMixCommand::Dump(
    [[maybe_unused]] const AudioRenderer::CommandListProcessor& processor, std::string& string) {
    string += fmt::format(
        "MultiTapBiquadFilterAndMixCommand\n\tinput {:02X} output {:02X} "
        "has_volume_ramp {} is_first_mix_buffer {}\n",
        input, output, has_volume_ramp, is_first_mix_buffer);
}

void MultiTapBiquadFilterAndMixCommand::Process(
    const AudioRenderer::CommandListProcessor& processor) {
    std::array<VoiceState::BiquadFilterState*, MaxBiquadFilters> states_{};
    std::array<VoiceState::BiquadFilterState*, MaxBiquadFilters> prev_states_{};
    auto* voice_state_{reinterpret_cast<VoiceState*>(voice_state)};

    for (u32 i = 0; i < MaxBiquadFilters; i++) {
        states_[i] = reinterpret_cast<VoiceState::BiquadFilterState*>(states[i]);
        prev_states_[i] = reinterpret_cast<VoiceState::BiquadFilterState*>(previous_states[i]);

        if (needs_init[i]) {
            *states_[i] = {};
        } else if (is_first_mix_buffer) {
            *prev_states_[i] = *states_[i];
        } else {
            *states_[i] = *prev_states_[i];
        }
    }

    auto input_buffer{
        processor.mix_buffers.subspan(input * processor.sample_count, processor.sample_count)};
    auto output_buffer{
        processor.mix_buffers.subspan(output * processor.sample_count, processor.sample_count)};

    std::array<VoiceState::BiquadFilterState, MaxBiquadFilters> states_array{};
    for (u32 i = 0; i < MaxBiquadFilters; i++) {
        states_array[i] = *states_[i];
    }

    if (has_volume_ramp) {
        f32 ramp = (volume1 - volume0) / static_cast<f32>(processor.sample_count);
        f32 last_sample = ApplyDoubleBiquadFilterAndMixRamp(
            output_buffer, input_buffer, biquads, states_array, processor.sample_count, volume0,
            ramp);
        if (voice_state_ && last_sample_index >= 0 &&
            last_sample_index < static_cast<s32>(voice_state_->previous_samples.size())) {
            voice_state_->previous_samples[last_sample_index] = static_cast<s32>(last_sample);
        }
    } else {
        ApplyDoubleBiquadFilterAndMix(output_buffer, input_buffer, biquads, states_array,
                                      processor.sample_count, volume1);
    }

    // Save states back
    for (u32 i = 0; i < MaxBiquadFilters; i++) {
        *states_[i] = states_array[i];
    }
}

bool MultiTapBiquadFilterAndMixCommand::Verify(
    const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
