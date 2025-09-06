// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hid_core/hid_types.h"

namespace Service::HID {

// This is nn::hid::system::SleepButtonState
struct SleepButtonState {
    s64 sampling_number{};
    Core::HID::SleepButtonState buttons;
};
static_assert(sizeof(SleepButtonState) == 0x10, "SleepButtonState is an invalid size");

// This is nn::hid::system::HomeButtonState
struct HomeButtonState {
    s64 sampling_number{};
    Core::HID::HomeButtonState buttons;
};
static_assert(sizeof(HomeButtonState) == 0x10, "HomeButtonState is an invalid size");

// This is nn::hid::system::SleepButtonState
struct CaptureButtonState {
    s64 sampling_number{};
    Core::HID::CaptureButtonState buttons;
};
static_assert(sizeof(CaptureButtonState) == 0x10, "CaptureButtonState is an invalid size");

} // namespace Service::HID
