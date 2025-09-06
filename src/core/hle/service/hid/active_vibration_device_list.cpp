// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/active_vibration_device_list.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/vibration/vibration_device.h"

namespace Service::HID {

IActiveVibrationDeviceList::IActiveVibrationDeviceList(Core::System& system_,
                                                       std::shared_ptr<ResourceManager> resource)
    : ServiceFramework{system_, "IActiveVibrationDeviceList"}, resource_manager(resource) {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&IActiveVibrationDeviceList::ActivateVibrationDevice>, "ActivateVibrationDevice"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IActiveVibrationDeviceList::~IActiveVibrationDeviceList() = default;

Result IActiveVibrationDeviceList::ActivateVibrationDevice(
    Core::HID::VibrationDeviceHandle vibration_device_handle) {
    LOG_DEBUG(Service_HID, "called, npad_type={}, npad_id={}, device_index={}",
              vibration_device_handle.npad_type, vibration_device_handle.npad_id,
              vibration_device_handle.device_index);

    std::scoped_lock lock{mutex};

    R_TRY(IsVibrationHandleValid(vibration_device_handle));

    for (std::size_t i = 0; i < list_size; i++) {
        if (vibration_device_handle.device_index == vibration_device_list[i].device_index &&
            vibration_device_handle.npad_id == vibration_device_list[i].npad_id &&
            vibration_device_handle.npad_type == vibration_device_list[i].npad_type) {
            R_SUCCEED();
        }
    }

    R_UNLESS(list_size < MaxVibrationDevicesHandles, ResultVibrationDeviceIndexOutOfRange);
    R_TRY(resource_manager->GetVibrationDevice(vibration_device_handle)->Activate());

    vibration_device_list[list_size++] = vibration_device_handle;
    R_SUCCEED();
}

} // namespace Service::HID
