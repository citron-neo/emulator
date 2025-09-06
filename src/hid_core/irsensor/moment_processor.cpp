// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "core/core_timing.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/irsensor/moment_processor.h"

namespace Service::IRS {
static constexpr auto format = Core::IrSensor::ImageTransferProcessorFormat::Size40x30;
static constexpr std::size_t ImageWidth = 40;
static constexpr std::size_t ImageHeight = 30;

MomentProcessor::MomentProcessor(Core::System& system_, Core::IrSensor::DeviceFormat& device_format,
                                 std::size_t npad_index)
    : device(device_format), system{system_} {
    npad_device = system.HIDCore().GetEmulatedControllerByIndex(npad_index);

    device.mode = Core::IrSensor::IrSensorMode::MomentProcessor;
    device.camera_status = Core::IrSensor::IrCameraStatus::Unconnected;
    device.camera_internal_status = Core::IrSensor::IrCameraInternalStatus::Stopped;

    shared_memory = std::construct_at(
        reinterpret_cast<MomentSharedMemory*>(&device_format.state.processor_raw_data));

    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change = [this](Core::HID::ControllerTriggerType type) { OnControllerUpdate(type); },
        .is_npad_service = true,
    };
    callback_key = npad_device->SetCallback(engine_callback);
}

MomentProcessor::~MomentProcessor() {
    npad_device->DeleteCallback(callback_key);
};

void MomentProcessor::StartProcessor() {
    device.camera_status = Core::IrSensor::IrCameraStatus::Available;
    device.camera_internal_status = Core::IrSensor::IrCameraInternalStatus::Ready;
}

void MomentProcessor::SuspendProcessor() {}

void MomentProcessor::StopProcessor() {}

void MomentProcessor::OnControllerUpdate(Core::HID::ControllerTriggerType type) {
    if (type != Core::HID::ControllerTriggerType::IrSensor) {
        return;
    }

    next_state = {};
    const auto& camera_data = npad_device->GetCamera();

    const auto window_width = static_cast<std::size_t>(current_config.window_of_interest.width);
    const auto window_height = static_cast<std::size_t>(current_config.window_of_interest.height);
    const auto window_start_x = static_cast<std::size_t>(current_config.window_of_interest.x);
    const auto window_start_y = static_cast<std::size_t>(current_config.window_of_interest.y);

    const std::size_t block_width = window_width / Columns;
    const std::size_t block_height = window_height / Rows;

    for (std::size_t row = 0; row < Rows; row++) {
        for (std::size_t column = 0; column < Columns; column++) {
            const size_t x_pos = (column * block_width) + window_start_x;
            const size_t y_pos = (row * block_height) + window_start_y;
            auto& statistic = next_state.statistic[column + (row * Columns)];
            statistic = GetStatistic(camera_data.data, x_pos, y_pos, block_width, block_height);
        }
    }

    next_state.sampling_number = camera_data.sample;
    next_state.timestamp = system.CoreTiming().GetGlobalTimeNs().count();
    next_state.ambient_noise_level = Core::IrSensor::CameraAmbientNoiseLevel::Low;
    shared_memory->moment_lifo.WriteNextEntry(next_state);

    if (!IsProcessorActive()) {
        StartProcessor();
    }
}

u8 MomentProcessor::GetPixel(const std::vector<u8>& data, std::size_t x, std::size_t y) const {
    if ((y * ImageWidth) + x >= data.size()) {
        return 0;
    }
    return data[(y * ImageWidth) + x];
}

MomentProcessor::MomentStatistic MomentProcessor::GetStatistic(const std::vector<u8>& data,
                                                               std::size_t start_x,
                                                               std::size_t start_y,
                                                               std::size_t width,
                                                               std::size_t height) const {
    // The actual implementation is always 320x240
    static constexpr std::size_t RealWidth = 320;
    static constexpr std::size_t RealHeight = 240;
    static constexpr std::size_t Threshold = 30;
    MomentStatistic statistic{};
    std::size_t active_points{};

    // Sum all data points on the block that meet with the threshold
    for (std::size_t y = 0; y < width; y++) {
        for (std::size_t x = 0; x < height; x++) {
            const size_t x_pos = x + start_x;
            const size_t y_pos = y + start_y;
            const auto pixel =
                GetPixel(data, x_pos * ImageWidth / RealWidth, y_pos * ImageHeight / RealHeight);

            if (pixel < Threshold) {
                continue;
            }

            statistic.average_intensity += pixel;

            statistic.centroid.x += static_cast<float>(x_pos);
            statistic.centroid.y += static_cast<float>(y_pos);

            active_points++;
        }
    }

    // Return an empty field if no points were available
    if (active_points == 0) {
        return {};
    }

    // Finally calculate the actual centroid and average intensity
    statistic.centroid.x /= static_cast<float>(active_points);
    statistic.centroid.y /= static_cast<float>(active_points);
    statistic.average_intensity /= static_cast<f32>(width * height);

    return statistic;
}

void MomentProcessor::SetConfig(Core::IrSensor::PackedMomentProcessorConfig config) {
    current_config.camera_config.exposure_time = config.camera_config.exposure_time;
    current_config.camera_config.gain = config.camera_config.gain;
    current_config.camera_config.is_negative_used = config.camera_config.is_negative_used;
    current_config.camera_config.light_target =
        static_cast<Core::IrSensor::CameraLightTarget>(config.camera_config.light_target);
    current_config.window_of_interest = config.window_of_interest;
    current_config.preprocess =
        static_cast<Core::IrSensor::MomentProcessorPreprocess>(config.preprocess);
    current_config.preprocess_intensity_threshold = config.preprocess_intensity_threshold;

    npad_device->SetCameraFormat(format);
}

} // namespace Service::IRS
