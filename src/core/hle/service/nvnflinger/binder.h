// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2014 The Android Open Source Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Parts of this implementation were based on:
// https://cs.android.com/android/platform/superproject/+/android-5.1.1_r38:frameworks/native/include/binder/IBinder.h

#pragma once

#include <span>

#include "common/common_types.h"

namespace Kernel {
class KReadableEvent;
} // namespace Kernel

namespace Service {
class HLERequestContext;
}

namespace Service::android {

class IBinder {
public:
    virtual ~IBinder() = default;
    virtual void Transact(u32 code, std::span<const u8> parcel_data, std::span<u8> parcel_reply,
                          u32 flags) = 0;
    virtual Kernel::KReadableEvent* GetNativeHandle(u32 type_id) = 0;
};

} // namespace Service::android
