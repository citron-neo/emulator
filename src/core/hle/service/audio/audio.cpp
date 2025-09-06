// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/audio/audio.h"
#include "core/hle/service/audio/audio_controller.h"
#include "core/hle/service/audio/audio_debug.h"
#include "core/hle/service/audio/audio_device_service.h"
#include "core/hle/service/audio/audio_in_manager.h"
#include "core/hle/service/audio/audio_out_manager.h"
#include "core/hle/service/audio/audio_renderer_manager.h"
#include "core/hle/service/audio/audio_snoop_manager.h"
#include "core/hle/service/audio/audio_system_manager.h"
#include "core/hle/service/audio/codecctl.h"
#include "core/hle/service/audio/final_output_recorder_manager.h"
#include "core/hle/service/audio/final_output_recorder_manager_for_applet.h"
#include "core/hle/service/audio/hardware_opus_decoder_manager.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    // Main audio services
    server_manager->RegisterNamedService("audctl", std::make_shared<IAudioController>(system));
    server_manager->RegisterNamedService("audin:u", std::make_shared<IAudioInManager>(system));
    server_manager->RegisterNamedService("audout:u", std::make_shared<IAudioOutManager>(system));
    server_manager->RegisterNamedService("audren:u", std::make_shared<IAudioRendererManager>(system));
    server_manager->RegisterNamedService("hwopus", std::make_shared<IHardwareOpusDecoderManager>(system));

    // Final output recorder services
    server_manager->RegisterNamedService("audrec:a", std::make_shared<IFinalOutputRecorderManagerForApplet>(system));
    server_manager->RegisterNamedService("audrec:u", std::make_shared<IFinalOutputRecorderManager>(system));

    // Applet versions of audio services
    server_manager->RegisterNamedService("audout:a", std::make_shared<IAudioOutManager>(system));
    server_manager->RegisterNamedService("audin:a", std::make_shared<IAudioInManager>(system));
    server_manager->RegisterNamedService("audren:a", std::make_shared<IAudioRendererManager>(system));

    // Device suspend/resume services
    server_manager->RegisterNamedService("audout:d", std::make_shared<IAudioDeviceService>(system, "audout:d"));
    server_manager->RegisterNamedService("audin:d", std::make_shared<IAudioDeviceService>(system, "audin:d"));
    server_manager->RegisterNamedService("audrec:d", std::make_shared<IAudioDeviceService>(system, "audrec:d"));
    server_manager->RegisterNamedService("audren:d", std::make_shared<IAudioDeviceService>(system, "audren:d"));

    // Codec controller service
    server_manager->RegisterNamedService("codecctl", std::make_shared<ICodecController>(system));

    // Debug service
    server_manager->RegisterNamedService("auddebug", std::make_shared<IAudioDebugManager>(system));

    // Audio snoop service (auddev)
    server_manager->RegisterNamedService("auddev", std::make_shared<IAudioSnoopManager>(system));

    // System-level audio services [11.0.0+]
    server_manager->RegisterNamedService("aud:a", std::make_shared<IAudioSystemManagerForApplet>(system)); // System manager for applet
    server_manager->RegisterNamedService("aud:d", std::make_shared<IAudioSystemManagerForDebugger>(system)); // System manager for debugger

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Audio
