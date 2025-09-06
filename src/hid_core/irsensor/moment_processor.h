// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/bit_field.h"
#include "common/common_types.h"
#include "hid_core/irsensor/irs_types.h"
#include "hid_core/irsensor/processor_base.h"
#include "hid_core/resources/irs_ring_lifo.h"

namespace Core {
class System;
}

namespace Core::HID {
class EmulatedController;
} // namespace Core::HID

namespace Service::IRS {
class MomentProcessor final : public ProcessorBase {
public:
    explicit MomentProcessor(Core::System& system_, Core::IrSensor::DeviceFormat& device_format,
                             std::size_t npad_index);
    ~MomentProcessor() override;

    // Called when the processor is initialized
    void StartProcessor() override;

    // Called when the processor is suspended
    void SuspendProcessor() override;

    // Called when the processor is stopped
    void StopProcessor() override;

    // Sets config parameters of the camera
    void SetConfig(Core::IrSensor::PackedMomentProcessorConfig config);

private:
    static constexpr std::size_t Columns = 8;
    static constexpr std::size_t Rows = 6;

    // This is nn::irsensor::MomentProcessorConfig
    struct MomentProcessorConfig {
        Core::IrSensor::CameraConfig camera_config;
        Core::IrSensor::IrsRect window_of_interest;
        Core::IrSensor::MomentProcessorPreprocess preprocess;
        u32 preprocess_intensity_threshold;
    };
    static_assert(sizeof(MomentProcessorConfig) == 0x28,
                  "MomentProcessorConfig is an invalid size");

    // This is nn::irsensor::MomentStatistic
    struct MomentStatistic {
        f32 average_intensity;
        Core::IrSensor::IrsCentroid centroid;
    };
    static_assert(sizeof(MomentStatistic) == 0xC, "MomentStatistic is an invalid size");

    // This is nn::irsensor::MomentProcessorState
    struct MomentProcessorState {
        s64 sampling_number;
        u64 timestamp;
        Core::IrSensor::CameraAmbientNoiseLevel ambient_noise_level;
        INSERT_PADDING_BYTES(4);
        std::array<MomentStatistic, Columns * Rows> statistic;
    };
    static_assert(sizeof(MomentProcessorState) == 0x258, "MomentProcessorState is an invalid size");

    struct MomentSharedMemory {
        Service::IRS::Lifo<MomentProcessorState, 6> moment_lifo;
    };
    static_assert(sizeof(MomentSharedMemory) == 0xE20, "MomentSharedMemory is an invalid size");

    void OnControllerUpdate(Core::HID::ControllerTriggerType type);
    u8 GetPixel(const std::vector<u8>& data, std::size_t x, std::size_t y) const;
    MomentStatistic GetStatistic(const std::vector<u8>& data, std::size_t start_x,
                                 std::size_t start_y, std::size_t width, std::size_t height) const;

    MomentSharedMemory* shared_memory = nullptr;
    MomentProcessorState next_state{};

    MomentProcessorConfig current_config{};
    Core::IrSensor::DeviceFormat& device;
    Core::HID::EmulatedController* npad_device;
    int callback_key{};

    Core::System& system;
};

} // namespace Service::IRS
