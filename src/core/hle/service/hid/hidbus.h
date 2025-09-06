// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"
#include "hid_core/hid_types.h"
#include "hid_core/hidbus/hidbus_base.h"

namespace Core::Timing {
struct EventType;
} // namespace Core::Timing

namespace Core {
class System;
} // namespace Core

namespace Service::HID {

class Hidbus final : public ServiceFramework<Hidbus> {
public:
    explicit Hidbus(Core::System& system_);
    ~Hidbus() override;

private:
    static const std::size_t max_number_of_handles = 0x13;

    enum class HidBusDeviceId : std::size_t {
        RingController = 0x20,
        FamicomRight = 0x21,
        Starlink = 0x28,
    };

    // This is nn::hidbus::detail::StatusManagerType
    enum class StatusManagerType : u32 {
        None,
        Type16,
        Type32,
    };

    // This is nn::hidbus::BusType
    enum class BusType : u64 {
        LeftJoyRail,
        RightJoyRail,
        InternalBus, // Lark microphone

        MaxBusType,
    };

    // This is nn::hidbus::BusHandle
    struct BusHandle {
        union {
            u64 raw{};

            BitField<0, 32, u64> abstracted_pad_id;
            BitField<32, 8, u64> internal_index;
            BitField<40, 8, u64> player_number;
            BitField<48, 8, u64> bus_type_id;
            BitField<56, 1, u64> is_valid;
        };
    };
    static_assert(sizeof(BusHandle) == 0x8, "BusHandle is an invalid size");

    // This is nn::hidbus::JoyPollingReceivedData
    struct JoyPollingReceivedData {
        std::array<u8, 0x30> data;
        u64 out_size;
        u64 sampling_number;
    };
    static_assert(sizeof(JoyPollingReceivedData) == 0x40,
                  "JoyPollingReceivedData is an invalid size");

    struct HidbusStatusManagerEntry {
        u8 is_connected{};
        INSERT_PADDING_BYTES(0x3);
        Result is_connected_result{0};
        u8 is_enabled{};
        u8 is_in_focus{};
        u8 is_polling_mode{};
        u8 reserved{};
        JoyPollingMode polling_mode{};
        INSERT_PADDING_BYTES(0x70); // Unknown
    };
    static_assert(sizeof(HidbusStatusManagerEntry) == 0x80,
                  "HidbusStatusManagerEntry is an invalid size");

    struct HidbusStatusManager {
        std::array<HidbusStatusManagerEntry, max_number_of_handles> entries{};
        INSERT_PADDING_BYTES(0x680); // Unused
    };
    static_assert(sizeof(HidbusStatusManager) <= 0x1000, "HidbusStatusManager is an invalid size");

    struct HidbusDevice {
        bool is_device_initialized{};
        BusHandle handle{};
        std::unique_ptr<HidbusBase> device{nullptr};
    };

    Result GetBusHandle(Out<bool> out_is_valid, Out<BusHandle> out_bus_handle,
                        Core::HID::NpadIdType npad_id, BusType bus_type,
                        AppletResourceUserId aruid);

    Result IsExternalDeviceConnected(Out<bool> out_is_connected, BusHandle bus_handle);

    Result Initialize(BusHandle bus_handle, AppletResourceUserId aruid);

    Result Finalize(BusHandle bus_handle, AppletResourceUserId aruid);

    Result EnableExternalDevice(bool is_enabled, BusHandle bus_handle, u64 inval,
                                AppletResourceUserId aruid);

    Result GetExternalDeviceId(Out<u32> out_device_id, BusHandle bus_handle);

    Result SendCommandAsync(BusHandle bus_handle, InBuffer<BufferAttr_HipcAutoSelect> buffer_data);

    Result GetSendCommandAsynceResult(Out<u64> out_data_size, BusHandle bus_handle,
                                      OutBuffer<BufferAttr_HipcAutoSelect> out_buffer_data);

    Result SetEventForSendCommandAsycResult(OutCopyHandle<Kernel::KReadableEvent> out_event,
                                            BusHandle bus_handle);

    Result GetSharedMemoryHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory);

    Result EnableJoyPollingReceiveMode(u32 t_mem_size, JoyPollingMode polling_mode,
                                       BusHandle bus_handle,
                                       InCopyHandle<Kernel::KTransferMemory> t_mem);

    Result DisableJoyPollingReceiveMode(BusHandle bus_handle);

    Result SetStatusManagerType(StatusManagerType manager_type);

    void UpdateHidbus(std::chrono::nanoseconds ns_late);
    std::optional<std::size_t> GetDeviceIndexFromHandle(BusHandle handle) const;

    template <typename T>
    void MakeDevice(BusHandle handle) {
        const auto device_index = GetDeviceIndexFromHandle(handle);
        if (device_index) {
            devices[device_index.value()].device = std::make_unique<T>(system, service_context);
        }
    }

    bool is_hidbus_enabled{false};
    HidbusStatusManager hidbus_status{};
    std::array<HidbusDevice, max_number_of_handles> devices{};
    std::shared_ptr<Core::Timing::EventType> hidbus_update_event;
    KernelHelpers::ServiceContext service_context;
};

} // namespace Service::HID
