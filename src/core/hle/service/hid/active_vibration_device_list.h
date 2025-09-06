// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include <mutex>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "hid_core/hid_types.h"

namespace Core {
class System;
}

namespace Service::HID {
class ResourceManager;

class IActiveVibrationDeviceList final : public ServiceFramework<IActiveVibrationDeviceList> {
public:
    explicit IActiveVibrationDeviceList(Core::System& system_,
                                        std::shared_ptr<ResourceManager> resource);
    ~IActiveVibrationDeviceList() override;

private:
    static constexpr std::size_t MaxVibrationDevicesHandles{0x100};

    Result ActivateVibrationDevice(Core::HID::VibrationDeviceHandle vibration_device_handle);

    mutable std::mutex mutex;
    std::size_t list_size{};
    std::array<Core::HID::VibrationDeviceHandle, MaxVibrationDevicesHandles>
        vibration_device_list{};
    std::shared_ptr<ResourceManager> resource_manager;
};

} // namespace Service::HID
