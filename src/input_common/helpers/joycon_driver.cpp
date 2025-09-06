// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/input.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "common/swap.h"
#include "common/thread.h"
#include "input_common/helpers/joycon_driver.h"
#include "input_common/helpers/joycon_protocol/calibration.h"
#include "input_common/helpers/joycon_protocol/generic_functions.h"
#include "input_common/helpers/joycon_protocol/irs.h"
#include "input_common/helpers/joycon_protocol/nfc.h"
#include "input_common/helpers/joycon_protocol/poller.h"
#include "input_common/helpers/joycon_protocol/ringcon.h"
#include "input_common/helpers/joycon_protocol/rumble.h"

namespace InputCommon::Joycon {
JoyconDriver::JoyconDriver(std::size_t port_) : port{port_} {
    hidapi_handle = std::make_shared<JoyconHandle>();
}

JoyconDriver::~JoyconDriver() {
    Stop();
}

void JoyconDriver::Stop() {
    is_connected = false;
    input_thread = {};
}

Common::Input::DriverResult JoyconDriver::RequestDeviceAccess(SDL_hid_device_info* device_info) {
    std::scoped_lock lock{mutex};

    handle_device_type = ControllerType::None;
    GetDeviceType(device_info, handle_device_type);
    if (handle_device_type == ControllerType::None) {
        return Common::Input::DriverResult::UnsupportedControllerType;
    }

    hidapi_handle->handle =
        SDL_hid_open(device_info->vendor_id, device_info->product_id, device_info->serial_number);
    std::memcpy(&handle_serial_number, device_info->serial_number, 15);
    if (!hidapi_handle->handle) {
        LOG_ERROR(Input, "Citron can't gain access to this device: ID {:04X}:{:04X}.",
                  device_info->vendor_id, device_info->product_id);
        return Common::Input::DriverResult::HandleInUse;
    }
    SDL_hid_set_nonblocking(hidapi_handle->handle, 1);
    return Common::Input::DriverResult::Success;
}

Common::Input::DriverResult JoyconDriver::InitializeDevice() {
    if (!hidapi_handle->handle) {
        return Common::Input::DriverResult::InvalidHandle;
    }
    std::scoped_lock lock{mutex};
    disable_input_thread = true;

    // Reset Counters
    error_counter = 0;
    hidapi_handle->packet_counter = 0;

    // Reset external device status
    starlink_connected = false;
    ring_connected = false;
    amiibo_detected = false;

    // Set HW default configuration
    vibration_enabled = true;
    motion_enabled = true;
    hidbus_enabled = false;
    nfc_enabled = false;
    passive_enabled = false;
    irs_enabled = false;
    input_only_device = false;
    gyro_sensitivity = Joycon::GyroSensitivity::DPS2000;
    gyro_performance = Joycon::GyroPerformance::HZ833;
    accelerometer_sensitivity = Joycon::AccelerometerSensitivity::G8;
    accelerometer_performance = Joycon::AccelerometerPerformance::HZ100;

    // Initialize HW Protocols
    calibration_protocol = std::make_unique<CalibrationProtocol>(hidapi_handle);
    generic_protocol = std::make_unique<GenericProtocol>(hidapi_handle);
    irs_protocol = std::make_unique<IrsProtocol>(hidapi_handle);
    nfc_protocol = std::make_unique<NfcProtocol>(hidapi_handle);
    ring_protocol = std::make_unique<RingConProtocol>(hidapi_handle);
    rumble_protocol = std::make_unique<RumbleProtocol>(hidapi_handle);

    // Get fixed joycon info
    if (generic_protocol->GetVersionNumber(version) != Common::Input::DriverResult::Success) {
        // If this command fails the device doesn't accept configuration commands
        input_only_device = true;
    }

    if (!input_only_device) {
        generic_protocol->SetLowPowerMode(false);
        generic_protocol->GetColor(color);
        if (handle_device_type == ControllerType::Pro) {
            // Some 3rd party controllers aren't pro controllers
            generic_protocol->GetControllerType(device_type);
        } else {
            device_type = handle_device_type;
        }
        generic_protocol->GetSerialNumber(serial_number);
    }

    supported_features = GetSupportedFeatures();

    // Get Calibration data
    calibration_protocol->GetLeftJoyStickCalibration(left_stick_calibration);
    calibration_protocol->GetRightJoyStickCalibration(right_stick_calibration);
    calibration_protocol->GetImuCalibration(motion_calibration);

    // Set led status
    generic_protocol->SetLedBlinkPattern(static_cast<u8>(1 + port));

    // Apply HW configuration
    SetPollingMode();

    // Initialize joycon poller
    joycon_poller = std::make_unique<JoyconPoller>(device_type, left_stick_calibration,
                                                   right_stick_calibration, motion_calibration);

    // Start polling for data
    is_connected = true;
    if (!input_thread_running) {
        input_thread =
            std::jthread([this](std::stop_token stop_token) { InputThread(stop_token); });
    }

    disable_input_thread = false;
    return Common::Input::DriverResult::Success;
}

void JoyconDriver::InputThread(std::stop_token stop_token) {
    LOG_INFO(Input, "Joycon Adapter input thread started");
    Common::SetCurrentThreadName("JoyconInput");
    input_thread_running = true;

    // Max update rate is 5ms, ensure we are always able to read a bit faster
    constexpr int ThreadDelay = 3;
    std::vector<u8> buffer(MaxBufferSize);

    while (!stop_token.stop_requested()) {
        int status = 0;

        if (!IsInputThreadValid()) {
            input_thread.request_stop();
            continue;
        }

        // By disabling the input thread we can ensure custom commands will succeed as no package is
        // skipped
        if (!disable_input_thread) {
            status = SDL_hid_read_timeout(hidapi_handle->handle, buffer.data(), buffer.size(),
                                          ThreadDelay);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(ThreadDelay));
        }

        if (IsPayloadCorrect(status, buffer)) {
            OnNewData(buffer);
        }

        if (!vibration_queue.Empty()) {
            VibrationValue vibration_value;
            vibration_queue.Pop(vibration_value);
            last_vibration_result = rumble_protocol->SendVibration(vibration_value);
        }

        // We can't keep up with vibrations. Start skipping.
        while (vibration_queue.Size() > 6) {
            vibration_queue.Pop();
        }

        std::this_thread::yield();
    }

    is_connected = false;
    input_thread_running = false;
    LOG_INFO(Input, "Joycon Adapter input thread stopped");
}

void JoyconDriver::OnNewData(std::span<u8> buffer) {
    const auto report_mode = static_cast<ReportMode>(buffer[0]);

    // Packages can be a little bit inconsistent. Average the delta time to provide a smoother
    // motion experience
    switch (report_mode) {
    case ReportMode::STANDARD_FULL_60HZ:
    case ReportMode::NFC_IR_MODE_60HZ:
    case ReportMode::SIMPLE_HID_MODE: {
        const auto now = std::chrono::steady_clock::now();
        const auto new_delta_time = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - last_update).count());
        delta_time = ((delta_time * 8) + (new_delta_time * 2)) / 10;
        last_update = now;
        joycon_poller->UpdateColor(color);
        break;
    }
    default:
        break;
    }

    const MotionStatus motion_status{
        .is_enabled = motion_enabled,
        .delta_time = delta_time,
        .gyro_sensitivity = gyro_sensitivity,
        .accelerometer_sensitivity = accelerometer_sensitivity,
    };

    // TODO: Remove this when calibration is properly loaded and not calculated
    if (ring_connected && report_mode == ReportMode::STANDARD_FULL_60HZ) {
        InputReportActive data{};
        memcpy(&data, buffer.data(), sizeof(InputReportActive));
        calibration_protocol->GetRingCalibration(ring_calibration, data.ring_input);
    }

    const RingStatus ring_status{
        .is_enabled = ring_connected,
        .default_value = ring_calibration.default_value,
        .max_value = ring_calibration.max_value,
        .min_value = ring_calibration.min_value,
    };

    if (irs_protocol->IsEnabled()) {
        irs_protocol->RequestImage(buffer);
        joycon_poller->UpdateCamera(irs_protocol->GetImage(), irs_protocol->GetIrsFormat());
    }

    if (nfc_protocol->IsPolling()) {
        if (amiibo_detected) {
            if (!nfc_protocol->HasAmiibo()) {
                joycon_poller->UpdateAmiibo({});
                amiibo_detected = false;
                return;
            }
        }

        if (!amiibo_detected) {
            Joycon::TagInfo tag_info;
            const auto result = nfc_protocol->GetTagInfo(tag_info);
            if (result == Common::Input::DriverResult::Success) {
                joycon_poller->UpdateAmiibo(tag_info);
                amiibo_detected = true;
            }
        }
    }

    switch (report_mode) {
    case ReportMode::STANDARD_FULL_60HZ:
        joycon_poller->ReadActiveMode(buffer, motion_status, ring_status);
        break;
    case ReportMode::NFC_IR_MODE_60HZ:
        joycon_poller->ReadNfcIRMode(buffer, motion_status);
        break;
    case ReportMode::SIMPLE_HID_MODE:
        joycon_poller->ReadPassiveMode(buffer);
        break;
    case ReportMode::SUBCMD_REPLY:
        LOG_DEBUG(Input, "Unhandled command reply");
        break;
    default:
        LOG_ERROR(Input, "Report mode not Implemented {}", report_mode);
        break;
    }
}

Common::Input::DriverResult JoyconDriver::SetPollingMode() {
    SCOPE_EXIT {
        disable_input_thread = false;
    };
    disable_input_thread = true;

    rumble_protocol->EnableRumble(vibration_enabled && supported_features.vibration);

    if (motion_enabled && supported_features.motion) {
        generic_protocol->EnableImu(true);
        generic_protocol->SetImuConfig(gyro_sensitivity, gyro_performance,
                                       accelerometer_sensitivity, accelerometer_performance);
    } else {
        generic_protocol->EnableImu(false);
    }

    if (input_only_device) {
        return Common::Input::DriverResult::NotSupported;
    }

    if (irs_protocol->IsEnabled()) {
        irs_protocol->DisableIrs();
    }

    if (nfc_protocol->IsEnabled()) {
        amiibo_detected = false;
        nfc_protocol->DisableNfc();
    }

    if (ring_protocol->IsEnabled()) {
        ring_connected = false;
        ring_protocol->DisableRingCon();
    }

    if (irs_enabled && supported_features.irs) {
        auto result = irs_protocol->EnableIrs();
        if (result == Common::Input::DriverResult::Success) {
            return result;
        }
        irs_protocol->DisableIrs();
        LOG_ERROR(Input, "Error enabling IRS");
        return result;
    }

    if (nfc_enabled && supported_features.nfc) {
        auto result = nfc_protocol->EnableNfc();
        if (result == Common::Input::DriverResult::Success) {
            return result;
        }
        nfc_protocol->DisableNfc();
        LOG_ERROR(Input, "Error enabling NFC");
        return result;
    }

    if (hidbus_enabled && supported_features.hidbus) {
        auto result = ring_protocol->EnableRingCon();
        if (result == Common::Input::DriverResult::Success) {
            result = ring_protocol->StartRingconPolling();
        }
        if (result == Common::Input::DriverResult::Success) {
            ring_connected = true;
            return result;
        }
        ring_connected = false;
        ring_protocol->DisableRingCon();
        LOG_ERROR(Input, "Error enabling Ringcon");
        return result;
    }

    if (passive_enabled && supported_features.passive) {
        const auto result = generic_protocol->EnablePassiveMode();
        if (result == Common::Input::DriverResult::Success) {
            return result;
        }
        LOG_ERROR(Input, "Error enabling passive mode");
    }

    // Default Mode
    const auto result = generic_protocol->EnableActiveMode();
    if (result != Common::Input::DriverResult::Success) {
        LOG_ERROR(Input, "Error enabling active mode");
    }
    // Switch calls this function after enabling active mode
    generic_protocol->TriggersElapsed();

    return result;
}

JoyconDriver::SupportedFeatures JoyconDriver::GetSupportedFeatures() {
    SupportedFeatures features{
        .passive = true,
        .motion = true,
        .vibration = true,
    };

    if (input_only_device) {
        return features;
    }

    if (device_type == ControllerType::Right) {
        features.nfc = true;
        features.irs = true;
        features.hidbus = true;
    }

    if (device_type == ControllerType::Pro) {
        features.nfc = true;
    }
    return features;
}

bool JoyconDriver::IsInputThreadValid() const {
    if (!is_connected.load()) {
        return false;
    }
    if (hidapi_handle->handle == nullptr) {
        return false;
    }
    // Controller is not responding. Terminate connection
    if (error_counter > MaxErrorCount) {
        return false;
    }
    return true;
}

bool JoyconDriver::IsPayloadCorrect(int status, std::span<const u8> buffer) {
    if (status <= -1) {
        error_counter++;
        return false;
    }
    // There's no new data
    if (status == 0) {
        return false;
    }
    // No reply ever starts with zero
    if (buffer[0] == 0x00) {
        error_counter++;
        return false;
    }
    error_counter = 0;
    return true;
}

Common::Input::DriverResult JoyconDriver::SetVibration(const VibrationValue& vibration) {
    std::scoped_lock lock{mutex};
    if (disable_input_thread) {
        return Common::Input::DriverResult::HandleInUse;
    }
    vibration_queue.Push(vibration);
    return last_vibration_result;
}

Common::Input::DriverResult JoyconDriver::SetLedConfig(u8 led_pattern) {
    std::scoped_lock lock{mutex};
    if (disable_input_thread) {
        return Common::Input::DriverResult::HandleInUse;
    }
    return generic_protocol->SetLedPattern(led_pattern);
}

Common::Input::DriverResult JoyconDriver::SetIrsConfig(IrsMode mode_, IrsResolution format_) {
    std::scoped_lock lock{mutex};
    if (disable_input_thread) {
        return Common::Input::DriverResult::HandleInUse;
    }
    disable_input_thread = true;
    const auto result = irs_protocol->SetIrsConfig(mode_, format_);
    disable_input_thread = false;
    return result;
}

Common::Input::DriverResult JoyconDriver::SetPassiveMode() {
    std::scoped_lock lock{mutex};
    motion_enabled = false;
    hidbus_enabled = false;
    nfc_enabled = false;
    passive_enabled = true;
    irs_enabled = false;
    return SetPollingMode();
}

Common::Input::DriverResult JoyconDriver::SetActiveMode() {
    if (is_ring_disabled_by_irs) {
        is_ring_disabled_by_irs = false;
        SetActiveMode();
        return SetRingConMode();
    }

    std::scoped_lock lock{mutex};
    motion_enabled = true;
    hidbus_enabled = false;
    nfc_enabled = false;
    passive_enabled = false;
    irs_enabled = false;
    return SetPollingMode();
}

Common::Input::DriverResult JoyconDriver::SetIrMode() {
    std::scoped_lock lock{mutex};

    if (!supported_features.irs) {
        return Common::Input::DriverResult::NotSupported;
    }

    if (ring_connected) {
        is_ring_disabled_by_irs = true;
    }

    motion_enabled = false;
    hidbus_enabled = false;
    nfc_enabled = false;
    passive_enabled = false;
    irs_enabled = true;
    return SetPollingMode();
}

Common::Input::DriverResult JoyconDriver::SetNfcMode() {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }

    motion_enabled = true;
    hidbus_enabled = false;
    nfc_enabled = true;
    passive_enabled = false;
    irs_enabled = false;
    return SetPollingMode();
}

Common::Input::DriverResult JoyconDriver::SetRingConMode() {
    std::scoped_lock lock{mutex};

    if (!supported_features.hidbus) {
        return Common::Input::DriverResult::NotSupported;
    }

    motion_enabled = true;
    hidbus_enabled = true;
    nfc_enabled = false;
    passive_enabled = false;
    irs_enabled = false;

    const auto result = SetPollingMode();

    if (!ring_connected) {
        return Common::Input::DriverResult::NoDeviceDetected;
    }

    return result;
}

Common::Input::DriverResult JoyconDriver::StartNfcPolling() {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }
    if (!nfc_protocol->IsEnabled()) {
        return Common::Input::DriverResult::Disabled;
    }

    disable_input_thread = true;
    const auto result = nfc_protocol->StartNFCPollingMode();
    disable_input_thread = false;

    return result;
}

Common::Input::DriverResult JoyconDriver::StopNfcPolling() {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }
    if (!nfc_protocol->IsEnabled()) {
        return Common::Input::DriverResult::Disabled;
    }

    disable_input_thread = true;
    const auto result = nfc_protocol->StopNFCPollingMode();
    disable_input_thread = false;

    if (amiibo_detected) {
        amiibo_detected = false;
        joycon_poller->UpdateAmiibo({});
    }

    return result;
}

Common::Input::DriverResult JoyconDriver::ReadAmiiboData(std::vector<u8>& out_data) {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }
    if (!nfc_protocol->IsEnabled()) {
        return Common::Input::DriverResult::Disabled;
    }
    if (!amiibo_detected) {
        return Common::Input::DriverResult::ErrorWritingData;
    }

    out_data.resize(0x21C);
    disable_input_thread = true;
    const auto result = nfc_protocol->ReadAmiibo(out_data);
    disable_input_thread = false;

    return result;
}

Common::Input::DriverResult JoyconDriver::WriteNfcData(std::span<const u8> data) {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }
    if (!nfc_protocol->IsEnabled()) {
        return Common::Input::DriverResult::Disabled;
    }
    if (!amiibo_detected) {
        return Common::Input::DriverResult::ErrorWritingData;
    }

    disable_input_thread = true;
    const auto result = nfc_protocol->WriteAmiibo(data);
    disable_input_thread = false;

    return result;
}

Common::Input::DriverResult JoyconDriver::ReadMifareData(std::span<const MifareReadChunk> data,
                                                         std::span<MifareReadData> out_data) {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }
    if (!nfc_protocol->IsEnabled()) {
        return Common::Input::DriverResult::Disabled;
    }
    if (!amiibo_detected) {
        return Common::Input::DriverResult::ErrorWritingData;
    }

    disable_input_thread = true;
    const auto result = nfc_protocol->ReadMifare(data, out_data);
    disable_input_thread = false;

    return result;
}

Common::Input::DriverResult JoyconDriver::WriteMifareData(std::span<const MifareWriteChunk> data) {
    std::scoped_lock lock{mutex};

    if (!supported_features.nfc) {
        return Common::Input::DriverResult::NotSupported;
    }
    if (!nfc_protocol->IsEnabled()) {
        return Common::Input::DriverResult::Disabled;
    }
    if (!amiibo_detected) {
        return Common::Input::DriverResult::ErrorWritingData;
    }

    disable_input_thread = true;
    const auto result = nfc_protocol->WriteMifare(data);
    disable_input_thread = false;

    return result;
}

bool JoyconDriver::IsConnected() const {
    std::scoped_lock lock{mutex};
    return is_connected.load();
}

bool JoyconDriver::IsVibrationEnabled() const {
    std::scoped_lock lock{mutex};
    return vibration_enabled;
}

FirmwareVersion JoyconDriver::GetDeviceVersion() const {
    std::scoped_lock lock{mutex};
    return version;
}

Color JoyconDriver::GetDeviceColor() const {
    std::scoped_lock lock{mutex};
    return color;
}

std::size_t JoyconDriver::GetDevicePort() const {
    std::scoped_lock lock{mutex};
    return port;
}

ControllerType JoyconDriver::GetDeviceType() const {
    std::scoped_lock lock{mutex};
    return device_type;
}

ControllerType JoyconDriver::GetHandleDeviceType() const {
    std::scoped_lock lock{mutex};
    return handle_device_type;
}

SerialNumber JoyconDriver::GetSerialNumber() const {
    std::scoped_lock lock{mutex};
    return serial_number;
}

SerialNumber JoyconDriver::GetHandleSerialNumber() const {
    std::scoped_lock lock{mutex};
    return handle_serial_number;
}

void JoyconDriver::SetCallbacks(const JoyconCallbacks& callbacks) {
    joycon_poller->SetCallbacks(callbacks);
}

Common::Input::DriverResult JoyconDriver::GetDeviceType(SDL_hid_device_info* device_info,
                                                        ControllerType& controller_type) {
    static constexpr std::array<std::pair<u32, ControllerType>, 6> supported_devices{
        std::pair<u32, ControllerType>{0x2006, ControllerType::Left},
        {0x2007, ControllerType::Right},
        {0x2009, ControllerType::Pro},
    };
    constexpr u16 nintendo_vendor_id = 0x057e;

    controller_type = ControllerType::None;
    if (device_info->vendor_id != nintendo_vendor_id) {
        return Common::Input::DriverResult::UnsupportedControllerType;
    }

    for (const auto& [product_id, type] : supported_devices) {
        if (device_info->product_id == static_cast<u16>(product_id)) {
            controller_type = type;
            return Common::Input::DriverResult::Success;
        }
    }
    return Common::Input::DriverResult::UnsupportedControllerType;
}

Common::Input::DriverResult JoyconDriver::GetSerialNumber(SDL_hid_device_info* device_info,
                                                          SerialNumber& serial_number) {
    if (device_info->serial_number == nullptr) {
        return Common::Input::DriverResult::Unknown;
    }
    std::memcpy(&serial_number, device_info->serial_number, 15);
    return Common::Input::DriverResult::Success;
}

} // namespace InputCommon::Joycon
