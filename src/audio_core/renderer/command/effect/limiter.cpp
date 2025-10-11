// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <span>

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/effect/limiter.h"
#include "core/memory.h"

namespace AudioCore::Renderer {

void LimiterCommand::Dump([[maybe_unused]] const AudioRenderer::CommandListProcessor& processor,
                          std::string& string) {
    string += fmt::format("LimiterCommand\n\tenabled {} channels {}\n", effect_enabled,
                          parameter.channel_count);
}

void LimiterCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    std::array<std::span<const s32>, MaxChannels> input_buffers{};
    std::array<std::span<s32>, MaxChannels> output_buffers{};

    for (u32 i = 0; i < parameter.channel_count; i++) {
        input_buffers[i] = processor.mix_buffers.subspan(inputs[i] * processor.sample_count,
                                                         processor.sample_count);
        output_buffers[i] = processor.mix_buffers.subspan(outputs[i] * processor.sample_count,
                                                          processor.sample_count);
    }

    auto state_buffer{reinterpret_cast<LimiterInfo::State*>(state)};

    if (effect_enabled) {
        // Convert parameters
        const f32 attack_coeff =
            std::exp(-1.0f / (parameter.attack_time * static_cast<f32>(processor.target_sample_rate) / 1000.0f));
        const f32 release_coeff =
            std::exp(-1.0f / (parameter.release_time * static_cast<f32>(processor.target_sample_rate) / 1000.0f));
        const f32 threshold_linear = std::pow(10.0f, parameter.threshold / 20.0f);
        const f32 makeup_gain_linear = std::pow(10.0f, parameter.makeup_gain / 20.0f);

        for (u32 sample = 0; sample < processor.sample_count; sample++) {
            // Find peak across all channels
            f32 peak = 0.0f;
            for (u32 ch = 0; ch < parameter.channel_count; ch++) {
                const f32 abs_sample = std::abs(static_cast<f32>(input_buffers[ch][sample]));
                peak = std::max(peak, abs_sample);
            }

            // Update envelope
            if (peak > state_buffer->envelope) {
                state_buffer->envelope =
                    attack_coeff * state_buffer->envelope + (1.0f - attack_coeff) * peak;
            } else {
                state_buffer->envelope =
                    release_coeff * state_buffer->envelope + (1.0f - release_coeff) * peak;
            }

            // Calculate gain reduction
            f32 gain = 1.0f;
            if (state_buffer->envelope > threshold_linear) {
                const f32 over = state_buffer->envelope / threshold_linear;
                gain = 1.0f / std::pow(over, (parameter.ratio - 1.0f) / parameter.ratio);
            }

            state_buffer->gain_reduction = gain;

            // Apply limiting with makeup gain
            const f32 total_gain = gain * makeup_gain_linear;
            for (u32 ch = 0; ch < parameter.channel_count; ch++) {
                output_buffers[ch][sample] =
                    static_cast<s32>(static_cast<f32>(input_buffers[ch][sample]) * total_gain);
            }
        }
    } else {
        // Bypass: just copy input to output
        for (u32 ch = 0; ch < parameter.channel_count; ch++) {
            if (inputs[ch] != outputs[ch]) {
                std::memcpy(output_buffers[ch].data(), input_buffers[ch].data(),
                            output_buffers[ch].size_bytes());
            }
        }
    }
}

bool LimiterCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
