// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"

namespace Service::PCTL {

enum class Capability : u32 {
    None = 0,
    Application = 1 << 0,
    SnsPost = 1 << 1,
    Recovery = 1 << 6,
    Status = 1 << 8,
    StereoVision = 1 << 9,
    System = 1 << 15,
};
DECLARE_ENUM_FLAG_OPERATORS(Capability);

struct ApplicationInfo {
    u64 application_id{};
    std::array<u8, 32> age_rating{};
    u32 parental_control_flag{};
    Capability capability{};
};
static_assert(sizeof(ApplicationInfo) == 0x30, "ApplicationInfo has incorrect size.");

// This is nn::pctl::RestrictionSettings
struct RestrictionSettings {
    u8 rating_age;
    bool sns_post_restriction;
    bool free_communication_restriction;
};
static_assert(sizeof(RestrictionSettings) == 0x3, "RestrictionSettings has incorrect size.");

// This is nn::pctl::PlayTimerSettings
struct PlayTimerSettings {
    std::array<u32, 13> settings;
};
static_assert(sizeof(PlayTimerSettings) == 0x34, "PlayTimerSettings has incorrect size.");

} // namespace Service::PCTL
