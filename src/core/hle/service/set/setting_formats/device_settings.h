// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>

#include "common/common_types.h"
#include "common/vector_math.h"
#include "core/hle/service/set/settings_types.h"

namespace Service::Set {
struct DeviceSettings {
    INSERT_PADDING_BYTES(0x10); // Reserved

    // nn::settings::BatteryLot
    std::array<u8, 0x18> ptm_battery_lot;
    // nn::settings::system::PtmFuelGaugeParameter
    std::array<u8, 0x18> ptm_fuel_gauge_parameter;
    u8 ptm_battery_version;
    // nn::settings::system::PtmCycleCountReliability
    u32 ptm_cycle_count_reliability;
    INSERT_PADDING_BYTES(0x48); // Reserved

    // nn::settings::system::AnalogStickUserCalibration L
    std::array<u8, 0x10> analog_user_stick_calibration_l;
    // nn::settings::system::AnalogStickUserCalibration R
    std::array<u8, 0x10> analog_user_stick_calibration_r;
    INSERT_PADDING_BYTES(0x20); // Reserved

    // nn::settings::system::ConsoleSixAxisSensorAccelerationBias
    Common::Vec3<f32> console_six_axis_sensor_acceleration_bias;
    // nn::settings::system::ConsoleSixAxisSensorAngularVelocityBias
    Common::Vec3<f32> console_six_axis_sensor_angular_velocity_bias;
    // nn::settings::system::ConsoleSixAxisSensorAccelerationGain
    std::array<u8, 0x24> console_six_axis_sensor_acceleration_gain;
    // nn::settings::system::ConsoleSixAxisSensorAngularVelocityGain
    std::array<u8, 0x24> console_six_axis_sensor_angular_velocity_gain;
    // nn::settings::system::ConsoleSixAxisSensorAngularVelocityTimeBias
    Common::Vec3<f32> console_six_axis_sensor_angular_velocity_time_bias;
    // nn::settings::system::ConsoleSixAxisSensorAngularAcceleration
    std::array<u8, 0x24> console_six_axis_sensor_angular_acceleration;
};
static_assert(offsetof(DeviceSettings, ptm_battery_lot) == 0x10);
static_assert(offsetof(DeviceSettings, ptm_cycle_count_reliability) == 0x44);
static_assert(offsetof(DeviceSettings, analog_user_stick_calibration_l) == 0x90);
static_assert(offsetof(DeviceSettings, console_six_axis_sensor_acceleration_bias) == 0xD0);
static_assert(offsetof(DeviceSettings, console_six_axis_sensor_angular_acceleration) == 0x13C);
static_assert(sizeof(DeviceSettings) == 0x160, "DeviceSettings has the wrong size!");

DeviceSettings DefaultDeviceSettings();

} // namespace Service::Set
