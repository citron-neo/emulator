// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/mix/copy_mix.h"

namespace AudioCore::Renderer {

void CopyMixBufferCommand::Dump(
    [[maybe_unused]] const AudioRenderer::CommandListProcessor& processor, std::string& string) {
    string += fmt::format("CopyMixBufferCommand\n\tinput {:02X} output {:02X}\n", input_index,
                          output_index);
}

void CopyMixBufferCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    auto output{processor.mix_buffers.subspan(output_index * processor.sample_count,
                                              processor.sample_count)};
    auto input{processor.mix_buffers.subspan(input_index * processor.sample_count,
                                             processor.sample_count)};
    std::memcpy(output.data(), input.data(), processor.sample_count * sizeof(s32));
}

bool CopyMixBufferCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
