// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>

#include "common/common_types.h"
#include "common/uuid.h"
#include "core/hle/service/set/settings_types.h"

namespace Service::Set {
struct ApplnSettings {
    INSERT_PADDING_BYTES(0x10); // Reserved

    // nn::util::Uuid MiiAuthorId, copied from system settings 0x94B0
    Common::UUID mii_author_id;
    INSERT_PADDING_BYTES(0x30); // Reserved

    // nn::settings::system::ServiceDiscoveryControlSettings
    u32 service_discovery_control_settings;
    INSERT_PADDING_BYTES(0x20); // Reserved

    bool in_repair_process_enable_flag;
    INSERT_PADDING_BYTES(0x3);
};
static_assert(offsetof(ApplnSettings, mii_author_id) == 0x10);
static_assert(offsetof(ApplnSettings, service_discovery_control_settings) == 0x50);
static_assert(offsetof(ApplnSettings, in_repair_process_enable_flag) == 0x74);
static_assert(sizeof(ApplnSettings) == 0x78, "ApplnSettings has the wrong size!");

ApplnSettings DefaultApplnSettings();

} // namespace Service::Set
