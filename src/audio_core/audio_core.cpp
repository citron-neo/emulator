// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_core.h"
#include "audio_core/sink/sink_details.h"
#include "common/settings.h"
#include "core/core.h"

namespace AudioCore {

AudioCore::AudioCore(Core::System& system) : audio_manager{std::make_unique<AudioManager>()} {
    CreateSinks();
    // Must be created after the sinks
    adsp = std::make_unique<ADSP::ADSP>(system, *output_sink);
}

AudioCore ::~AudioCore() {
    Shutdown();
}

void AudioCore::CreateSinks() {
    const auto& sink_id{Settings::values.sink_id};
    const auto& audio_output_device_id{Settings::values.audio_output_device_id};
    const auto& audio_input_device_id{Settings::values.audio_input_device_id};

    output_sink = Sink::CreateSinkFromID(sink_id.GetValue(), audio_output_device_id.GetValue());
    input_sink = Sink::CreateSinkFromID(sink_id.GetValue(), audio_input_device_id.GetValue());
}

void AudioCore::Shutdown() {
    audio_manager->Shutdown();
}

AudioManager& AudioCore::GetAudioManager() {
    return *audio_manager;
}

Sink::Sink& AudioCore::GetOutputSink() {
    return *output_sink;
}

Sink::Sink& AudioCore::GetInputSink() {
    return *input_sink;
}

ADSP::ADSP& AudioCore::ADSP() {
    return *adsp;
}

} // namespace AudioCore
