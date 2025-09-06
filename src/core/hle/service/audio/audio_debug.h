// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::Audio {

class IAudioDebugManager final : public ServiceFramework<IAudioDebugManager> {
public:
    explicit IAudioDebugManager(Core::System& system_);
    ~IAudioDebugManager() override;

private:
    // TODO: Implement debug functions
};

} // namespace Service::Audio
