// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/hidbus.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/service.h"
#include "core/memory.h"
#include "hid_core/hid_types.h"
#include "hid_core/hidbus/ringcon.h"
#include "hid_core/hidbus/starlink.h"
#include "hid_core/hidbus/stubbed.h"

namespace Service::HID {
// (15ms, 66Hz)
constexpr auto hidbus_update_ns = std::chrono::nanoseconds{15 * 1000 * 1000};

Hidbus::Hidbus(Core::System& system_)
    : ServiceFramework{system_, "hidbus"}, service_context{system_, service_name} {

    // clang-format off
    static const FunctionInfo functions[] = {
            {1, C<&Hidbus::GetBusHandle>, "GetBusHandle"},
            {2, C<&Hidbus::IsExternalDeviceConnected>, "IsExternalDeviceConnected"},
            {3, C<&Hidbus::Initialize>, "Initialize"},
            {4, C<&Hidbus::Finalize>, "Finalize"},
            {5, C<&Hidbus::EnableExternalDevice>, "EnableExternalDevice"},
            {6, C<&Hidbus::GetExternalDeviceId>, "GetExternalDeviceId"},
            {7, C<&Hidbus::SendCommandAsync>, "SendCommandAsync"},
            {8, C<&Hidbus::GetSendCommandAsynceResult>, "GetSendCommandAsynceResult"},
            {9, C<&Hidbus::SetEventForSendCommandAsycResult>, "SetEventForSendCommandAsycResult"},
            {10, C<&Hidbus::GetSharedMemoryHandle>, "GetSharedMemoryHandle"},
            {11, C<&Hidbus::EnableJoyPollingReceiveMode>, "EnableJoyPollingReceiveMode"},
            {12, C<&Hidbus::DisableJoyPollingReceiveMode>, "DisableJoyPollingReceiveMode"},
            {13, nullptr, "GetPollingData"},
            {14, C<&Hidbus::SetStatusManagerType>, "SetStatusManagerType"},
    };
    // clang-format on

    RegisterHandlers(functions);

    // Register update callbacks
    hidbus_update_event = Core::Timing::CreateEvent(
        "Hidbus::UpdateCallback",
        [this](s64 time,
               std::chrono::nanoseconds ns_late) -> std::optional<std::chrono::nanoseconds> {
            const auto guard = LockService();
            UpdateHidbus(ns_late);
            return std::nullopt;
        });

    system_.CoreTiming().ScheduleLoopingEvent(hidbus_update_ns, hidbus_update_ns,
                                              hidbus_update_event);
}

Hidbus::~Hidbus() {
    system.CoreTiming().UnscheduleEvent(hidbus_update_event);
}

void Hidbus::UpdateHidbus(std::chrono::nanoseconds ns_late) {
    if (is_hidbus_enabled) {
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (!devices[i].is_device_initialized) {
                continue;
            }
            auto& device = devices[i].device;
            device->OnUpdate();
            auto& cur_entry = hidbus_status.entries[devices[i].handle.internal_index];
            cur_entry.is_polling_mode = device->IsPollingMode();
            cur_entry.polling_mode = device->GetPollingMode();
            cur_entry.is_enabled = device->IsEnabled();

            u8* shared_memory = system.Kernel().GetHidBusSharedMem().GetPointer();
            std::memcpy(shared_memory + (i * sizeof(HidbusStatusManagerEntry)), &hidbus_status,
                        sizeof(HidbusStatusManagerEntry));
        }
    }
}

std::optional<std::size_t> Hidbus::GetDeviceIndexFromHandle(BusHandle handle) const {
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device_handle = devices[i].handle;
        if (handle.abstracted_pad_id == device_handle.abstracted_pad_id &&
            handle.internal_index == device_handle.internal_index &&
            handle.player_number == device_handle.player_number &&
            handle.bus_type_id == device_handle.bus_type_id &&
            handle.is_valid == device_handle.is_valid) {
            return i;
        }
    }
    return std::nullopt;
}

Result Hidbus::GetBusHandle(Out<bool> out_is_valid, Out<BusHandle> out_bus_handle,
                            Core::HID::NpadIdType npad_id, BusType bus_type,
                            AppletResourceUserId aruid) {
    LOG_INFO(Service_HID, "called, npad_id={}, bus_type={}, applet_resource_user_id={}", npad_id,
             bus_type, aruid.pid);

    bool is_handle_found = 0;
    std::size_t handle_index = 0;

    for (std::size_t i = 0; i < devices.size(); i++) {
        const auto& handle = devices[i].handle;
        if (!handle.is_valid) {
            continue;
        }
        if (handle.player_number.As<Core::HID::NpadIdType>() == npad_id &&
            handle.bus_type_id == static_cast<u8>(bus_type)) {
            is_handle_found = true;
            handle_index = i;
            break;
        }
    }

    // Handle not found. Create a new one
    if (!is_handle_found) {
        for (std::size_t i = 0; i < devices.size(); i++) {
            if (devices[i].handle.is_valid) {
                continue;
            }
            devices[i].handle.raw = 0;
            devices[i].handle.abstracted_pad_id.Assign(i);
            devices[i].handle.internal_index.Assign(i);
            devices[i].handle.player_number.Assign(static_cast<u8>(npad_id));
            devices[i].handle.bus_type_id.Assign(static_cast<u8>(bus_type));
            devices[i].handle.is_valid.Assign(true);
            handle_index = i;
            break;
        }
    }

    *out_is_valid = true;
    *out_bus_handle = devices[handle_index].handle;
    R_SUCCEED();
}

Result Hidbus::IsExternalDeviceConnected(Out<bool> out_is_connected, BusHandle bus_handle) {
    LOG_INFO(Service_HID,
             "Called, abstracted_pad_id={}, bus_type={}, internal_index={}, "
             "player_number={}, is_valid={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    *out_is_connected = devices[device_index.value()].device->IsDeviceActivated();
    R_SUCCEED();
}

Result Hidbus::Initialize(BusHandle bus_handle, AppletResourceUserId aruid) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={} bus_type={} internal_index={} "
             "player_number={} is_valid={}, applet_resource_user_id={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid, aruid.pid);

    is_hidbus_enabled = true;

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    const auto entry_index = devices[device_index.value()].handle.internal_index;
    auto& cur_entry = hidbus_status.entries[entry_index];

    if (bus_handle.internal_index == 0 && Settings::values.enable_ring_controller) {
        MakeDevice<RingController>(bus_handle);
        devices[device_index.value()].is_device_initialized = true;
        devices[device_index.value()].device->ActivateDevice();
        cur_entry.is_in_focus = true;
        cur_entry.is_connected = true;
        cur_entry.is_connected_result = ResultSuccess;
        cur_entry.is_enabled = false;
        cur_entry.is_polling_mode = false;
    } else {
        MakeDevice<HidbusStubbed>(bus_handle);
        devices[device_index.value()].is_device_initialized = true;
        cur_entry.is_in_focus = true;
        cur_entry.is_connected = false;
        cur_entry.is_connected_result = ResultSuccess;
        cur_entry.is_enabled = false;
        cur_entry.is_polling_mode = false;
    }

    std::memcpy(system.Kernel().GetHidBusSharedMem().GetPointer(), &hidbus_status,
                sizeof(hidbus_status));
    R_SUCCEED();
}

Result Hidbus::Finalize(BusHandle bus_handle, AppletResourceUserId aruid) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={}, bus_type={}, internal_index={}, "
             "player_number={}, is_valid={}, applet_resource_user_id={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid, aruid.pid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    const auto entry_index = devices[device_index.value()].handle.internal_index;
    auto& cur_entry = hidbus_status.entries[entry_index];
    auto& device = devices[device_index.value()].device;
    devices[device_index.value()].is_device_initialized = false;
    device->DeactivateDevice();

    cur_entry.is_in_focus = true;
    cur_entry.is_connected = false;
    cur_entry.is_connected_result = ResultSuccess;
    cur_entry.is_enabled = false;
    cur_entry.is_polling_mode = false;
    std::memcpy(system.Kernel().GetHidBusSharedMem().GetPointer(), &hidbus_status,
                sizeof(hidbus_status));
    R_SUCCEED();
}

Result Hidbus::EnableExternalDevice(bool is_enabled, BusHandle bus_handle, u64 inval,
                                    AppletResourceUserId aruid) {
    LOG_DEBUG(Service_HID,
              "called, enable={}, abstracted_pad_id={}, bus_type={}, internal_index={}, "
              "player_number={}, is_valid={}, inval={}, applet_resource_user_id{}",
              is_enabled, bus_handle.abstracted_pad_id, bus_handle.bus_type_id,
              bus_handle.internal_index, bus_handle.player_number, bus_handle.is_valid, inval,
              aruid.pid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);
    devices[device_index.value()].device->Enable(is_enabled);
    R_SUCCEED();
}

Result Hidbus::GetExternalDeviceId(Out<u32> out_device_id, BusHandle bus_handle) {
    LOG_DEBUG(Service_HID,
              "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
              "is_valid={}",
              bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
              bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    *out_device_id = devices[device_index.value()].device->GetDeviceId();
    R_SUCCEED();
}

Result Hidbus::SendCommandAsync(BusHandle bus_handle,
                                InBuffer<BufferAttr_HipcAutoSelect> buffer_data) {
    LOG_DEBUG(Service_HID,
              "called, data_size={}, abstracted_pad_id={}, bus_type={}, internal_index={}, "
              "player_number={}, is_valid={}",
              buffer_data.size(), bus_handle.abstracted_pad_id, bus_handle.bus_type_id,
              bus_handle.internal_index, bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    devices[device_index.value()].device->SetCommand(buffer_data);
    R_SUCCEED();
};

Result Hidbus::GetSendCommandAsynceResult(Out<u64> out_data_size, BusHandle bus_handle,
                                          OutBuffer<BufferAttr_HipcAutoSelect> out_buffer_data) {
    LOG_DEBUG(Service_HID,
              "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
              "is_valid={}",
              bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
              bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    *out_data_size = devices[device_index.value()].device->GetReply(out_buffer_data);
    R_SUCCEED();
};

Result Hidbus::SetEventForSendCommandAsycResult(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                                BusHandle bus_handle) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
             "is_valid={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    *out_event = &devices[device_index.value()].device->GetSendCommandAsycEvent();
    R_SUCCEED();
};

Result Hidbus::GetSharedMemoryHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory) {
    LOG_DEBUG(Service_HID, "called");

    *out_shared_memory = &system.Kernel().GetHidBusSharedMem();
    R_SUCCEED();
}

Result Hidbus::EnableJoyPollingReceiveMode(u32 t_mem_size, JoyPollingMode polling_mode,
                                           BusHandle bus_handle,
                                           InCopyHandle<Kernel::KTransferMemory> t_mem) {
    ASSERT_MSG(t_mem_size == 0x1000, "t_mem_size is not 0x1000 bytes");
    ASSERT_MSG(t_mem->GetSize() == t_mem_size, "t_mem has incorrect size");

    LOG_INFO(Service_HID,
             "called, polling_mode={}, abstracted_pad_id={}, bus_type={}, "
             "internal_index={}, player_number={}, is_valid={}",
             polling_mode, bus_handle.abstracted_pad_id, bus_handle.bus_type_id,
             bus_handle.internal_index, bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    auto& device = devices[device_index.value()].device;
    device->SetPollingMode(polling_mode);
    device->SetTransferMemoryAddress(t_mem->GetSourceAddress());
    R_SUCCEED();
}

Result Hidbus::DisableJoyPollingReceiveMode(BusHandle bus_handle) {
    LOG_INFO(Service_HID,
             "called, abstracted_pad_id={}, bus_type={}, internal_index={}, player_number={}, "
             "is_valid={}",
             bus_handle.abstracted_pad_id, bus_handle.bus_type_id, bus_handle.internal_index,
             bus_handle.player_number, bus_handle.is_valid);

    const auto device_index = GetDeviceIndexFromHandle(bus_handle);

    R_UNLESS(device_index.has_value(), ResultUnknown);

    auto& device = devices[device_index.value()].device;
    device->DisablePollingMode();
    R_SUCCEED();
}

Result Hidbus::SetStatusManagerType(StatusManagerType manager_type) {
    LOG_WARNING(Service_HID, "(STUBBED) called, manager_type={}", manager_type);
    R_SUCCEED();
};

} // namespace Service::HID
