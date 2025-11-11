// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace UICommon {

// Game-specific title IDs for workarounds and special handling
class TitleID {
private:
    TitleID() = default;

public:
    static constexpr u64 FinalFantasyTactics = 0x010038B015560000ULL;
    // Base title ID for Little Nightmares 3 (covers both 0x010066101A55A800 and 0x010066101A55A000)
    // The base title ID is obtained by masking with 0xFFFFFFFFFFFFE000
    static constexpr u64 LittleNightmares3Base = 0x010066101A55A000ULL;
};

} // namespace UICommon
