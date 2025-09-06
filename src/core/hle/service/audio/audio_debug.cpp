// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/audio_debug.h"

namespace Service::Audio {

IAudioDebugManager::IAudioDebugManager(Core::System& system_)
    : ServiceFramework{system_, "auddebug"} {
    // TODO: Implement debug functions
}

IAudioDebugManager::~IAudioDebugManager() = default;

} // namespace Service::Audio
