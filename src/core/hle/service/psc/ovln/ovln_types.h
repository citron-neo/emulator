// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/bit_field.h"
#include "common/common_types.h"

namespace Service::PSC {

using OverlayNotification = std::array<u64, 0x10>;
static_assert(sizeof(OverlayNotification) == 0x80, "OverlayNotification has incorrect size");

union MessageFlags {
    u64 raw;
    BitField<0, 8, u64> message_type;
    BitField<8, 8, u64> queue_type;
};
static_assert(sizeof(MessageFlags) == 0x8, "MessageFlags has incorrect size");

} // namespace Service::PSC
