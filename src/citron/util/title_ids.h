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
};

} // namespace UICommon
