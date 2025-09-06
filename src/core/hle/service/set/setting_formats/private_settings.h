// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>

#include "common/common_types.h"
#include "common/uuid.h"
#include "core/hle/service/set/settings_types.h"

namespace Service::Set {

struct PrivateSettings {
    INSERT_PADDING_BYTES(0x10); // Reserved

    InitialLaunchSettings initial_launch_settings;
    INSERT_PADDING_BYTES(0x20); // Reserved

    Common::UUID external_clock_source_id;
    s64 shutdown_rtc_value;
    s64 external_steady_clock_internal_offset;
    INSERT_PADDING_BYTES(0x60); // Reserved

    // nn::settings::system::PlatformRegion
    s32 platform_region;
    INSERT_PADDING_BYTES(0x4); // Reserved
};
static_assert(offsetof(PrivateSettings, initial_launch_settings) == 0x10);
static_assert(offsetof(PrivateSettings, external_clock_source_id) == 0x50);
static_assert(offsetof(PrivateSettings, shutdown_rtc_value) == 0x60);
static_assert(offsetof(PrivateSettings, external_steady_clock_internal_offset) == 0x68);
static_assert(offsetof(PrivateSettings, platform_region) == 0xD0);
static_assert(sizeof(PrivateSettings) == 0xD8, "PrivateSettings has the wrong size!");

PrivateSettings DefaultPrivateSettings();

} // namespace Service::Set
