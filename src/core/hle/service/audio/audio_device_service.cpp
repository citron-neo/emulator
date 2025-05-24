// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/audio/audio_device_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {

IAudioDeviceService::IAudioDeviceService(Core::System& system_, const char* name)
    : ServiceFramework{system_, name} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioDeviceService::RequestSuspend>, "RequestSuspend"},
        {1, D<&IAudioDeviceService::RequestResume>, "RequestResume"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioDeviceService::~IAudioDeviceService() = default;

Result IAudioDeviceService::RequestSuspend(u64 process_id) {
    LOG_WARNING(Service_Audio, "(STUBBED) called, process_id={:#x}", process_id);
    R_SUCCEED();
}

Result IAudioDeviceService::RequestResume(u64 process_id) {
    LOG_WARNING(Service_Audio, "(STUBBED) called, process_id={:#x}", process_id);
    R_SUCCEED();
}

} // namespace Service::Audio
