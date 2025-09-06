// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/effect/biquad_filter.h"
#include "audio_core/renderer/command/effect/multi_tap_biquad_filter.h"

namespace AudioCore::Renderer {

void MultiTapBiquadFilterCommand::Dump(
    [[maybe_unused]] const AudioRenderer::CommandListProcessor& processor, std::string& string) {
    string += fmt::format(
        "MultiTapBiquadFilterCommand\n\tinput {:02X}\n\toutput {:02X}\n\tneeds_init ({}, {})\n",
        input, output, needs_init[0], needs_init[1]);
}

void MultiTapBiquadFilterCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    if (filter_tap_count > MaxBiquadFilters) {
        LOG_ERROR(Service_Audio, "Too many filter taps! {}", filter_tap_count);
        filter_tap_count = MaxBiquadFilters;
    }

    auto input_buffer{
        processor.mix_buffers.subspan(input * processor.sample_count, processor.sample_count)};
    auto output_buffer{
        processor.mix_buffers.subspan(output * processor.sample_count, processor.sample_count)};

    // TODO: Fix this, currently just applies the filter to the input twice,
    // and doesn't chain the biquads together at all.
    for (u32 i = 0; i < filter_tap_count; i++) {
        auto state{reinterpret_cast<VoiceState::BiquadFilterState*>(states[i])};
        if (needs_init[i]) {
            *state = {};
        }

        ApplyBiquadFilterFloat(output_buffer, input_buffer, biquads[i].b, biquads[i].a, *state,
                               processor.sample_count);
    }
}

bool MultiTapBiquadFilterCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
