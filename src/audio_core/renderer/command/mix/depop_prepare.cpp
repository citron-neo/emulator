// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/mix/depop_prepare.h"
#include "audio_core/renderer/voice/voice_state.h"
#include "common/fixed_point.h"

namespace AudioCore::Renderer {

void DepopPrepareCommand::Dump(
    [[maybe_unused]] const AudioRenderer::CommandListProcessor& processor, std::string& string) {
    string += fmt::format("DepopPrepareCommand\n\tinputs: ");
    for (u32 i = 0; i < buffer_count; i++) {
        string += fmt::format("{:02X}, ", inputs[i]);
    }
    string += "\n";
}

void DepopPrepareCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    auto samples{reinterpret_cast<s32*>(previous_samples)};
    auto buffer{reinterpret_cast<s32*>(depop_buffer)};

    for (u32 i = 0; i < buffer_count; i++) {
        if (samples[i]) {
            buffer[inputs[i]] += samples[i];
            samples[i] = 0;
        }
    }
}

bool DepopPrepareCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
