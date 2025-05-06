// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>
#include <common/settings_input.h>
#include <common/thread.h>
#include <jni.h>
#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "input_common/drivers/android.h"

namespace InputCommon {

Android::Android(std::string input_engine_) : InputEngine(std::move(input_engine_)) {
    vibration_thread = std::jthread([this](std::stop_token token) {
        Common::SetCurrentThreadName("Android_Vibration");
        auto env = Common::Android::GetEnvForThread();
        using namespace std::chrono_literals;
        while (!token.stop_requested()) {
            SendVibrations(env, token);
        }
    });
}

Android::~Android() = default;

void Android::RegisterController(jobject j_input_device) {
    auto env = Common::Android::GetEnvForThread();
    const std::string guid = Common::Android::GetJString(
        env, static_cast<jstring>(
                 env->CallObjectMethod(j_input_device, Common::Android::GetCitronDeviceGetGUID())));
    const s32 port = env->CallIntMethod(j_input_device, Common::Android::GetCitronDeviceGetPort());
    const auto identifier = GetIdentifier(guid, static_cast<size_t>(port));
    PreSetController(identifier);

    if (input_devices.find(identifier) != input_devices.end()) {
        env->DeleteGlobalRef(input_devices[identifier]);
    }
    auto new_device = env->NewGlobalRef(j_input_device);
    input_devices[identifier] = new_device;
}

void Android::SetButtonState(std::string guid, size_t port, int button_id, bool value) {
    const auto identifier = GetIdentifier(guid, port);
    SetButton(identifier, button_id, value);
}

void Android::SetAxisPosition(std::string guid, size_t port, int axis_id, float value) {
    const auto identifier = GetIdentifier(guid, port);
    SetAxis(identifier, axis_id, value);
}

void Android::SetMotionState(std::string guid, size_t port, u64 delta_timestamp, float gyro_x,
                             float gyro_y, float gyro_z, float accel_x, float accel_y,
                             float accel_z) {
    const auto identifier = GetIdentifier(guid, port);
    const BasicMotion motion_data{
        .gyro_x = gyro_x,
        .gyro_y = gyro_y,
        .gyro_z = gyro_z,
        .accel_x = accel_x,
        .accel_y = accel_y,
        .accel_z = accel_z,
        .delta_timestamp = delta_timestamp,
    };
    SetMotion(identifier, 0, motion_data);
}

Common::Input::DriverResult Android::SetVibration(
    [[maybe_unused]] const PadIdentifier& identifier,
    [[maybe_unused]] const Common::Input::VibrationStatus& vibration) {
    vibration_queue.Push(VibrationRequest{
        .identifier = identifier,
        .vibration = vibration,
    });
    return Common::Input::DriverResult::Success;
}

bool Android::IsVibrationEnabled([[maybe_unused]] const PadIdentifier& identifier) {
    auto device = input_devices.find(identifier);
    if (device != input_devices.end()) {
        return Common::Android::RunJNIOnFiber<bool>([&](JNIEnv* env) {
            return static_cast<bool>(env->CallBooleanMethod(
                device->second, Common::Android::GetCitronDeviceGetSupportsVibration()));
        });
    }
    return false;
}

std::vector<Common::ParamPackage> Android::GetInputDevices() const {
    std::vector<Common::ParamPackage> devices;
    auto env = Common::Android::GetEnvForThread();
    for (const auto& [key, value] : input_devices) {
        auto name_object = static_cast<jstring>(
            env->CallObjectMethod(value, Common::Android::GetCitronDeviceGetName()));
        const std::string name =
            fmt::format("{} {}", Common::Android::GetJString(env, name_object), key.port);
        devices.emplace_back(Common::ParamPackage{
            {"engine", GetEngineName()},
            {"display", std::move(name)},
            {"guid", key.guid.RawString()},
            {"port", std::to_string(key.port)},
        });
    }
    return devices;
}

std::set<s32> Android::GetDeviceAxes(JNIEnv* env, jobject& j_device) const {
    auto j_axes = static_cast<jobjectArray>(
        env->CallObjectMethod(j_device, Common::Android::GetCitronDeviceGetAxes()));
    std::set<s32> axes;
    for (int i = 0; i < env->GetArrayLength(j_axes); ++i) {
        jobject axis = env->GetObjectArrayElement(j_axes, i);
        axes.insert(env->GetIntField(axis, Common::Android::GetIntegerValueField()));
    }
    return axes;
}

Common::ParamPackage Android::BuildParamPackageForAnalog(PadIdentifier identifier, int axis_x,
                                                         int axis_y) const {
    Common::ParamPackage params;
    params.Set("engine", GetEngineName());
    params.Set("port", static_cast<int>(identifier.port));
    params.Set("guid", identifier.guid.RawString());
    params.Set("axis_x", axis_x);
    params.Set("axis_y", axis_y);
    params.Set("offset_x", 0);
    params.Set("offset_y", 0);
    params.Set("invert_x", "+");

    // Invert Y-Axis by default
    params.Set("invert_y", "-");
    return params;
}

Common::ParamPackage Android::BuildAnalogParamPackageForButton(PadIdentifier identifier, s32 axis,
                                                               bool invert) const {
    Common::ParamPackage params{};
    params.Set("engine", GetEngineName());
    params.Set("port", static_cast<int>(identifier.port));
    params.Set("guid", identifier.guid.RawString());
    params.Set("axis", axis);
    params.Set("threshold", "0.5");
    params.Set("invert", invert ? "-" : "+");
    return params;
}

Common::ParamPackage Android::BuildButtonParamPackageForButton(PadIdentifier identifier,
                                                               s32 button) const {
    Common::ParamPackage params{};
    params.Set("engine", GetEngineName());
    params.Set("port", static_cast<int>(identifier.port));
    params.Set("guid", identifier.guid.RawString());
    params.Set("button", button);
    return params;
}

bool Android::MatchVID(Common::UUID device, const std::vector<std::string>& vids) const {
    for (size_t i = 0; i < vids.size(); ++i) {
        auto fucker = device.RawString();
        if (fucker.find(vids[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

AnalogMapping Android::GetAnalogMappingForDevice(const Common::ParamPackage& params) {
    if (!params.Has("guid") || !params.Has("port")) {
        return {};
    }

    auto identifier =
        GetIdentifier(params.Get("guid", ""), static_cast<size_t>(params.Get("port", 0)));
    auto& j_device = input_devices[identifier];
    if (j_device == nullptr) {
        return {};
    }

    auto env = Common::Android::GetEnvForThread();
    std::set<s32> axes = GetDeviceAxes(env, j_device);
    if (axes.size() == 0) {
        return {};
    }

    AnalogMapping mapping = {};
    if (axes.find(AXIS_X) != axes.end() && axes.find(AXIS_Y) != axes.end()) {
        mapping.insert_or_assign(Settings::NativeAnalog::LStick,
                                 BuildParamPackageForAnalog(identifier, AXIS_X, AXIS_Y));
    }

    if (axes.find(AXIS_RX) != axes.end() && axes.find(AXIS_RY) != axes.end()) {
        mapping.insert_or_assign(Settings::NativeAnalog::RStick,
                                 BuildParamPackageForAnalog(identifier, AXIS_RX, AXIS_RY));
    } else if (axes.find(AXIS_Z) != axes.end() && axes.find(AXIS_RZ) != axes.end()) {
        mapping.insert_or_assign(Settings::NativeAnalog::RStick,
                                 BuildParamPackageForAnalog(identifier, AXIS_Z, AXIS_RZ));
    }
    return mapping;
}

ButtonMapping Android::GetButtonMappingForDevice(const Common::ParamPackage& params) {
    if (!params.Has("guid") || !params.Has("port")) {
        return {};
    }

    auto identifier =
        GetIdentifier(params.Get("guid", ""), static_cast<size_t>(params.Get("port", 0)));
    auto& j_device = input_devices[identifier];
    if (j_device == nullptr) {
        return {};
    }

    auto env = Common::Android::GetEnvForThread();
    jintArray j_keys = env->NewIntArray(static_cast<int>(keycode_ids.size()));
    env->SetIntArrayRegion(j_keys, 0, static_cast<int>(keycode_ids.size()), keycode_ids.data());
    auto j_has_keys_object = static_cast<jbooleanArray>(
        env->CallObjectMethod(j_device, Common::Android::GetCitronDeviceHasKeys(), j_keys));
    jboolean isCopy = false;
    jboolean* j_has_keys = env->GetBooleanArrayElements(j_has_keys_object, &isCopy);

    std::set<s32> available_keys;
    for (size_t i = 0; i < keycode_ids.size(); ++i) {
        if (j_has_keys[i]) {
            available_keys.insert(keycode_ids[i]);
        }
    }

    // Some devices use axes instead of buttons for certain controls so we need all the axes here
    std::set<s32> axes = GetDeviceAxes(env, j_device);

    ButtonMapping mapping = {};
    if (axes.find(AXIS_HAT_X) != axes.end() && axes.find(AXIS_HAT_Y) != axes.end()) {
        mapping.insert_or_assign(Settings::NativeButton::DUp,
                                 BuildAnalogParamPackageForButton(identifier, AXIS_HAT_Y, true));
        mapping.insert_or_assign(Settings::NativeButton::DDown,
                                 BuildAnalogParamPackageForButton(identifier, AXIS_HAT_Y, false));
        mapping.insert_or_assign(Settings::NativeButton::DLeft,
                                 BuildAnalogParamPackageForButton(identifier, AXIS_HAT_X, true));
        mapping.insert_or_assign(Settings::NativeButton::DRight,
                                 BuildAnalogParamPackageForButton(identifier, AXIS_HAT_X, false));
    } else if (available_keys.find(KEYCODE_DPAD_UP) != available_keys.end() &&
               available_keys.find(KEYCODE_DPAD_DOWN) != available_keys.end() &&
               available_keys.find(KEYCODE_DPAD_LEFT) != available_keys.end() &&
               available_keys.find(KEYCODE_DPAD_RIGHT) != available_keys.end()) {
        mapping.insert_or_assign(Settings::NativeButton::DUp,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_DPAD_UP));
        mapping.insert_or_assign(Settings::NativeButton::DDown,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_DPAD_DOWN));
        mapping.insert_or_assign(Settings::NativeButton::DLeft,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_DPAD_LEFT));
        mapping.insert_or_assign(Settings::NativeButton::DRight,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_DPAD_RIGHT));
    }

    if (axes.find(AXIS_LTRIGGER) != axes.end()) {
        mapping.insert_or_assign(Settings::NativeButton::ZL, BuildAnalogParamPackageForButton(
                                                                 identifier, AXIS_LTRIGGER, false));
    } else if (available_keys.find(KEYCODE_BUTTON_L2) != available_keys.end()) {
        mapping.insert_or_assign(Settings::NativeButton::ZL,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_L2));
    }

    if (axes.find(AXIS_RTRIGGER) != axes.end()) {
        mapping.insert_or_assign(Settings::NativeButton::ZR, BuildAnalogParamPackageForButton(
                                                                 identifier, AXIS_RTRIGGER, false));
    } else if (available_keys.find(KEYCODE_BUTTON_R2) != available_keys.end()) {
        mapping.insert_or_assign(Settings::NativeButton::ZR,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_R2));
    }

    if (available_keys.find(KEYCODE_BUTTON_A) != available_keys.end()) {
        if (MatchVID(identifier.guid, flipped_ab_vids)) {
            mapping.insert_or_assign(Settings::NativeButton::B, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_A));
        } else {
            mapping.insert_or_assign(Settings::NativeButton::A, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_A));
        }
    }
    if (available_keys.find(KEYCODE_BUTTON_B) != available_keys.end()) {
        if (MatchVID(identifier.guid, flipped_ab_vids)) {
            mapping.insert_or_assign(Settings::NativeButton::A, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_B));
        } else {
            mapping.insert_or_assign(Settings::NativeButton::B, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_B));
        }
    }
    if (available_keys.find(KEYCODE_BUTTON_X) != available_keys.end()) {
        if (MatchVID(identifier.guid, flipped_xy_vids)) {
            mapping.insert_or_assign(Settings::NativeButton::Y, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_X));
        } else {
            mapping.insert_or_assign(Settings::NativeButton::X, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_X));
        }
    }
    if (available_keys.find(KEYCODE_BUTTON_Y) != available_keys.end()) {
        if (MatchVID(identifier.guid, flipped_xy_vids)) {
            mapping.insert_or_assign(Settings::NativeButton::X, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_Y));
        } else {
            mapping.insert_or_assign(Settings::NativeButton::Y, BuildButtonParamPackageForButton(
                                                                    identifier, KEYCODE_BUTTON_Y));
        }
    }

    if (available_keys.find(KEYCODE_BUTTON_L1) != available_keys.end()) {
        mapping.insert_or_assign(Settings::NativeButton::L,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_L1));
    }
    if (available_keys.find(KEYCODE_BUTTON_R1) != available_keys.end()) {
        mapping.insert_or_assign(Settings::NativeButton::R,
                                 BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_R1));
    }

    if (available_keys.find(KEYCODE_BUTTON_THUMBL) != available_keys.end()) {
        mapping.insert_or_assign(
            Settings::NativeButton::LStick,
            BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_THUMBL));
    }
    if (available_keys.find(KEYCODE_BUTTON_THUMBR) != available_keys.end()) {
        mapping.insert_or_assign(
            Settings::NativeButton::RStick,
            BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_THUMBR));
    }

    if (available_keys.find(KEYCODE_BUTTON_START) != available_keys.end()) {
        mapping.insert_or_assign(
            Settings::NativeButton::Plus,
            BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_START));
    }
    if (available_keys.find(KEYCODE_BUTTON_SELECT) != available_keys.end()) {
        mapping.insert_or_assign(
            Settings::NativeButton::Minus,
            BuildButtonParamPackageForButton(identifier, KEYCODE_BUTTON_SELECT));
    }

    return mapping;
}

Common::Input::ButtonNames Android::GetUIName(
    [[maybe_unused]] const Common::ParamPackage& params) const {
    return Common::Input::ButtonNames::Value;
}

PadIdentifier Android::GetIdentifier(const std::string& guid, size_t port) const {
    return {
        .guid = Common::UUID{guid},
        .port = port,
        .pad = 0,
    };
}

void Android::SendVibrations(JNIEnv* env, std::stop_token token) {
    VibrationRequest request = vibration_queue.PopWait(token);
    auto device = input_devices.find(request.identifier);
    if (device != input_devices.end()) {
        float average_intensity = static_cast<float>(
            (request.vibration.high_amplitude + request.vibration.low_amplitude) / 2.0);
        env->CallVoidMethod(device->second, Common::Android::GetCitronDeviceVibrate(),
                            average_intensity);
    }
}

} // namespace InputCommon
