// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/core.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "hid_core/hid_types.h"
#include "hid_core/irsensor/irs_types.h"
#include "hid_core/irsensor/processor_base.h"

namespace Core::HID {
class EmulatedController;
} // namespace Core::HID

namespace Service::IRS {

class IRS final : public ServiceFramework<IRS> {
public:
    explicit IRS(Core::System& system_);
    ~IRS() override;

private:
    // This is nn::irsensor::detail::AruidFormat
    struct AruidFormat {
        u64 sensor_aruid;
        u64 sensor_aruid_status;
    };
    static_assert(sizeof(AruidFormat) == 0x10, "AruidFormat is an invalid size");

    // This is nn::irsensor::detail::StatusManager
    struct StatusManager {
        std::array<Core::IrSensor::DeviceFormat, 9> device;
        std::array<AruidFormat, 5> aruid;
    };
    static_assert(sizeof(StatusManager) == 0x8000, "StatusManager is an invalid size");

    Result ActivateIrsensor(ClientAppletResourceUserId aruid);

    Result DeactivateIrsensor(ClientAppletResourceUserId aruid);

    Result GetIrsensorSharedMemoryHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory,
                                         ClientAppletResourceUserId aruid);
    Result StopImageProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                              ClientAppletResourceUserId aruid);

    Result RunMomentProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                              ClientAppletResourceUserId aruid,
                              const Core::IrSensor::PackedMomentProcessorConfig& processor_config);

    Result RunClusteringProcessor(
        Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
        const Core::IrSensor::PackedClusteringProcessorConfig& processor_config);

    Result RunImageTransferProcessor(
        Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
        const Core::IrSensor::PackedImageTransferProcessorConfig& processor_config,
        u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> t_mem);

    Result GetImageTransferProcessorState(
        Out<Core::IrSensor::ImageTransferProcessorState> out_state,
        Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
        OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data);

    Result RunTeraPluginProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                                  Core::IrSensor::PackedTeraPluginProcessorConfig processor_config,
                                  ClientAppletResourceUserId aruid);

    Result GetNpadIrCameraHandle(Out<Core::IrSensor::IrCameraHandle> out_camera_handle,
                                 Core::HID::NpadIdType npad_id);

    Result RunPointingProcessor(
        Core::IrSensor::IrCameraHandle camera_handle,
        const Core::IrSensor::PackedPointingProcessorConfig& processor_config,
        ClientAppletResourceUserId aruid);

    Result SuspendImageProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                                 ClientAppletResourceUserId aruid);

    Result CheckFirmwareVersion(Core::IrSensor::IrCameraHandle camera_handle,
                                Core::IrSensor::PackedMcuVersion mcu_version,
                                ClientAppletResourceUserId aruid);

    Result SetFunctionLevel(Core::IrSensor::IrCameraHandle camera_handle,
                            Core::IrSensor::PackedFunctionLevel function_level,
                            ClientAppletResourceUserId aruid);

    Result RunImageTransferExProcessor(
        Core::IrSensor::IrCameraHandle camera_handle, ClientAppletResourceUserId aruid,
        const Core::IrSensor::PackedImageTransferProcessorExConfig& processor_config,
        u64 transfer_memory_size, InCopyHandle<Kernel::KTransferMemory> t_mem);

    Result RunIrLedProcessor(Core::IrSensor::IrCameraHandle camera_handle,
                             Core::IrSensor::PackedIrLedProcessorConfig processor_config,
                             ClientAppletResourceUserId aruid);

    Result StopImageProcessorAsync(Core::IrSensor::IrCameraHandle camera_handle,
                                   ClientAppletResourceUserId aruid);

    Result ActivateIrsensorWithFunctionLevel(Core::IrSensor::PackedFunctionLevel function_level,
                                             ClientAppletResourceUserId aruid);

    Result IsIrCameraHandleValid(const Core::IrSensor::IrCameraHandle& camera_handle) const;

    Core::IrSensor::DeviceFormat& GetIrCameraSharedMemoryDeviceEntry(
        const Core::IrSensor::IrCameraHandle& camera_handle);

    template <typename T>
    void MakeProcessor(const Core::IrSensor::IrCameraHandle& handle,
                       Core::IrSensor::DeviceFormat& device_state) {
        const auto index = static_cast<std::size_t>(handle.npad_id);
        if (index > sizeof(processors)) {
            LOG_CRITICAL(Service_IRS, "Invalid index {}", index);
            return;
        }
        processors[index] = std::make_unique<T>(device_state);
    }

    template <typename T>
    void MakeProcessorWithCoreContext(const Core::IrSensor::IrCameraHandle& handle,
                                      Core::IrSensor::DeviceFormat& device_state) {
        const auto index = static_cast<std::size_t>(handle.npad_id);
        if (index > sizeof(processors)) {
            LOG_CRITICAL(Service_IRS, "Invalid index {}", index);
            return;
        }

        if constexpr (std::is_constructible_v<T, Core::System&, Core::IrSensor::DeviceFormat&,
                                              std::size_t>) {
            processors[index] = std::make_unique<T>(system, device_state, index);
        } else {
            processors[index] = std::make_unique<T>(system.HIDCore(), device_state, index);
        }
    }

    template <typename T>
    T& GetProcessor(const Core::IrSensor::IrCameraHandle& handle) {
        const auto index = static_cast<std::size_t>(handle.npad_id);
        if (index > sizeof(processors)) {
            LOG_CRITICAL(Service_IRS, "Invalid index {}", index);
            return static_cast<T&>(*processors[0]);
        }
        return static_cast<T&>(*processors[index]);
    }

    template <typename T>
    const T& GetProcessor(const Core::IrSensor::IrCameraHandle& handle) const {
        const auto index = static_cast<std::size_t>(handle.npad_id);
        if (index > sizeof(processors)) {
            LOG_CRITICAL(Service_IRS, "Invalid index {}", index);
            return static_cast<T&>(*processors[0]);
        }
        return static_cast<T&>(*processors[index]);
    }

    Core::HID::EmulatedController* npad_device = nullptr;
    StatusManager* shared_memory = nullptr;
    std::array<std::unique_ptr<ProcessorBase>, 9> processors{};
};

class IRS_SYS final : public ServiceFramework<IRS_SYS> {
public:
    explicit IRS_SYS(Core::System& system);
    ~IRS_SYS() override;
};

} // namespace Service::IRS
