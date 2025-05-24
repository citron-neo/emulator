// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IAudioDeviceService final : public ServiceFramework<IAudioDeviceService> {
public:
    explicit IAudioDeviceService(Core::System& system_, const char* name);
    ~IAudioDeviceService() override;

private:
    Result RequestSuspend(u64 process_id);
    Result RequestResume(u64 process_id);
};

} // namespace Service::Audio
