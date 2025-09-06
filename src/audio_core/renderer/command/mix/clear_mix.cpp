// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include "audio_core/adsp/apps/audio_renderer/command_list_processor.h"
#include "audio_core/renderer/command/mix/clear_mix.h"

namespace AudioCore::Renderer {

void ClearMixBufferCommand::Dump(
    [[maybe_unused]] const AudioRenderer::CommandListProcessor& processor, std::string& string) {
    string += fmt::format("ClearMixBufferCommand\n");
}

void ClearMixBufferCommand::Process(const AudioRenderer::CommandListProcessor& processor) {
    memset(processor.mix_buffers.data(), 0, processor.mix_buffers.size_bytes());
}

bool ClearMixBufferCommand::Verify(const AudioRenderer::CommandListProcessor& processor) {
    return true;
}

} // namespace AudioCore::Renderer
