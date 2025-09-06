// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/input.h"
#include "common/logging/log.h"
#include "input_common/helpers/joycon_protocol/generic_functions.h"

namespace InputCommon::Joycon {

GenericProtocol::GenericProtocol(std::shared_ptr<JoyconHandle> handle)
    : JoyconCommonProtocol(std::move(handle)) {}

Common::Input::DriverResult GenericProtocol::EnablePassiveMode() {
    ScopedSetBlocking sb(this);
    return SetReportMode(ReportMode::SIMPLE_HID_MODE);
}

Common::Input::DriverResult GenericProtocol::EnableActiveMode() {
    ScopedSetBlocking sb(this);
    return SetReportMode(ReportMode::STANDARD_FULL_60HZ);
}

Common::Input::DriverResult GenericProtocol::SetLowPowerMode(bool enable) {
    ScopedSetBlocking sb(this);
    const std::array<u8, 1> buffer{static_cast<u8>(enable ? 1 : 0)};
    return SendSubCommand(SubCommand::LOW_POWER_MODE, buffer);
}

Common::Input::DriverResult GenericProtocol::TriggersElapsed() {
    ScopedSetBlocking sb(this);
    return SendSubCommand(SubCommand::TRIGGERS_ELAPSED, {});
}

Common::Input::DriverResult GenericProtocol::GetDeviceInfo(DeviceInfo& device_info) {
    ScopedSetBlocking sb(this);
    SubCommandResponse output{};

    const auto result = SendSubCommand(SubCommand::REQ_DEV_INFO, {}, output);

    device_info = {};
    if (result == Common::Input::DriverResult::Success) {
        device_info = output.device_info;
    }

    return result;
}

Common::Input::DriverResult GenericProtocol::GetControllerType(ControllerType& controller_type) {
    return GetDeviceType(controller_type);
}

Common::Input::DriverResult GenericProtocol::EnableImu(bool enable) {
    ScopedSetBlocking sb(this);
    const std::array<u8, 1> buffer{static_cast<u8>(enable ? 1 : 0)};
    return SendSubCommand(SubCommand::ENABLE_IMU, buffer);
}

Common::Input::DriverResult GenericProtocol::SetImuConfig(GyroSensitivity gsen,
                                                          GyroPerformance gfrec,
                                                          AccelerometerSensitivity asen,
                                                          AccelerometerPerformance afrec) {
    ScopedSetBlocking sb(this);
    const std::array<u8, 4> buffer{static_cast<u8>(gsen), static_cast<u8>(asen),
                                   static_cast<u8>(gfrec), static_cast<u8>(afrec)};
    return SendSubCommand(SubCommand::SET_IMU_SENSITIVITY, buffer);
}

Common::Input::DriverResult GenericProtocol::GetBattery(u32& battery_level) {
    // This function is meant to request the high resolution battery status
    battery_level = 0;
    return Common::Input::DriverResult::NotSupported;
}

Common::Input::DriverResult GenericProtocol::GetColor(Color& color) {
    ScopedSetBlocking sb(this);
    std::array<u8, 12> buffer{};
    const auto result = ReadRawSPI(SpiAddress::COLOR_DATA, buffer);

    color = {};
    if (result == Common::Input::DriverResult::Success) {
        color.body = static_cast<u32>((buffer[0] << 16) | (buffer[1] << 8) | buffer[2]);
        color.buttons = static_cast<u32>((buffer[3] << 16) | (buffer[4] << 8) | buffer[5]);
        color.left_grip = static_cast<u32>((buffer[6] << 16) | (buffer[7] << 8) | buffer[8]);
        color.right_grip = static_cast<u32>((buffer[9] << 16) | (buffer[10] << 8) | buffer[11]);
    }

    return result;
}

Common::Input::DriverResult GenericProtocol::GetSerialNumber(SerialNumber& serial_number) {
    ScopedSetBlocking sb(this);
    std::array<u8, 16> buffer{};
    const auto result = ReadRawSPI(SpiAddress::SERIAL_NUMBER, buffer);

    serial_number = {};
    if (result == Common::Input::DriverResult::Success) {
        memcpy(serial_number.data(), buffer.data() + 1, sizeof(SerialNumber));
    }

    return result;
}

Common::Input::DriverResult GenericProtocol::GetTemperature(u32& temperature) {
    // Not all devices have temperature sensor
    temperature = 25;
    return Common::Input::DriverResult::NotSupported;
}

Common::Input::DriverResult GenericProtocol::GetVersionNumber(FirmwareVersion& version) {
    DeviceInfo device_info{};

    const auto result = GetDeviceInfo(device_info);
    version = device_info.firmware;

    return result;
}

Common::Input::DriverResult GenericProtocol::SetHomeLight() {
    ScopedSetBlocking sb(this);
    static constexpr std::array<u8, 3> buffer{0x0f, 0xf0, 0x00};
    return SendSubCommand(SubCommand::SET_HOME_LIGHT, buffer);
}

Common::Input::DriverResult GenericProtocol::SetLedBusy() {
    return Common::Input::DriverResult::NotSupported;
}

Common::Input::DriverResult GenericProtocol::SetLedPattern(u8 leds) {
    ScopedSetBlocking sb(this);
    const std::array<u8, 1> buffer{leds};
    return SendSubCommand(SubCommand::SET_PLAYER_LIGHTS, buffer);
}

Common::Input::DriverResult GenericProtocol::SetLedBlinkPattern(u8 leds) {
    return SetLedPattern(static_cast<u8>(leds << 4));
}

} // namespace InputCommon::Joycon
