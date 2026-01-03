// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/effect/biquad_filter.h"
#include "audio_core/renderer/command/effect/biquad_filter_and_mix.h"
#include "audio_core/renderer/voice/voice_state.h"

namespace AudioCore::Renderer {

void BiquadFilterAndMixCommand::Dump(
    [[maybe_unused]] const AudioRenderer::CommandListProcessor& processor, std::string& string) {
    string += fmt::format(
        "BiquadFilterAndMixCommand\n\tinput {:02X} output {:02X} needs_init {} "
        "has_volume_ramp {} is_first_mix_buffer {}\n",
        input, output, needs_init, has_volume_ramp, is_first_mix_buffer);
}

void BiquadFilterAndMixCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    auto* state_{reinterpret_cast<VoiceState::BiquadFilterState*>(state)};
    auto* prev_state_{reinterpret_cast<VoiceState::BiquadFilterState*>(previous_state)};
    auto* voice_state_{reinterpret_cast<VoiceState*>(voice_state)};

    if (needs_init) {
        *state_ = {};
    } else if (is_first_mix_buffer) {
        *prev_state_ = *state_;
    } else {
        *state_ = *prev_state_;
    }

    auto input_buffer{
        processor.mix_buffers.subspan(input * processor.sample_count, processor.sample_count)};
    auto output_buffer{
        processor.mix_buffers.subspan(output * processor.sample_count, processor.sample_count)};

    if (has_volume_ramp) {
        f32 ramp = (volume1 - volume0) / static_cast<f32>(processor.sample_count);
        f32 last_sample = ApplyBiquadFilterAndMixRamp(output_buffer, input_buffer, biquad.b,
                                                      biquad.a, *state_, processor.sample_count,
                                                      volume0, ramp);
        if (voice_state_ && last_sample_index >= 0 &&
            last_sample_index < static_cast<s32>(voice_state_->previous_samples.size())) {
            voice_state_->previous_samples[last_sample_index] = static_cast<s32>(last_sample);
        }
    } else {
        ApplyBiquadFilterAndMix(output_buffer, input_buffer, biquad.b, biquad.a, *state_,
                               processor.sample_count, volume1);
    }
}

bool BiquadFilterAndMixCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
