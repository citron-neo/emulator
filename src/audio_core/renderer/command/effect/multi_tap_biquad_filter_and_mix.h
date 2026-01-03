// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

#include "audio_core/renderer/command/icommand.h"
#include "audio_core/renderer/voice/voice_info.h"
#include "audio_core/renderer/voice/voice_state.h"
#include "common/common_types.h"

namespace AudioCore::ADSP::AudioRenderer {
class CommandListProcessor;
}

namespace AudioCore::Renderer {

/**
 * AudioRenderer command for applying two biquad filters and mixing the result into the output buffer
 * (REV12+).
 */
struct MultiTapBiquadFilterAndMixCommand : ICommand {
    /**
     * Print this command's information to a string.
     *
     * @param processor - The CommandListProcessor processing this command.
     * @param string    - The string to print into.
     */
    void Dump(const AudioRenderer::CommandListProcessor& processor, std::string& string) override;

    /**
     * Process this command.
     *
     * @param processor - The CommandListProcessor processing this command.
     */
    void Process(const AudioRenderer::CommandListProcessor& processor) override;

    /**
     * Verify this command's data is valid.
     *
     * @param processor - The CommandListProcessor processing this command.
     * @return True if the command is valid, otherwise false.
     */
    bool Verify(const AudioRenderer::CommandListProcessor& processor) override;

    /// Input mix buffer index
    s16 input;
    /// Output mix buffer index
    s16 output;
    /// Input parameters for biquads (fixed-point)
    std::array<VoiceInfo::BiquadFilterParameter, MaxBiquadFilters> biquads;
    /// Biquad states, updated each call
    std::array<CpuAddr, MaxBiquadFilters> states;
    /// Previous biquad states (for state restoration)
    std::array<CpuAddr, MaxBiquadFilters> previous_states;
    /// Voice state address (for last sample storage)
    CpuAddr voice_state;
    /// Index in voice state last_samples array
    s32 last_sample_index;
    /// Initial volume (for ramp)
    f32 volume0;
    /// Final volume
    f32 volume1;
    /// If each biquad needs initialisation
    std::array<bool, MaxBiquadFilters> needs_init;
    /// If true, use volume ramp
    bool has_volume_ramp;
    /// If true, this is the first mix buffer
    bool is_first_mix_buffer;
};

} // namespace AudioCore::Renderer
