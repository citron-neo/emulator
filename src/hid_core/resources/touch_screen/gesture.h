// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>

#include "common/common_types.h"
#include "core/hle/result.h"

namespace Service::HID {
class TouchResource;

/// Handles gesture request from HID interfaces
class Gesture {
public:
    Gesture(std::shared_ptr<TouchResource> resource);
    ~Gesture();

    Result Activate();
    Result Activate(u64 aruid, u32 basic_gesture_id);

    Result Deactivate();

    Result IsActive(bool& out_is_active) const;

private:
    mutable std::mutex mutex;
    std::shared_ptr<TouchResource> touch_resource;
};

} // namespace Service::HID
