// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <set>
#include <common/threadsafe_queue.h>
#include <jni.h>
#include "input_common/input_engine.h"

namespace InputCommon {

/**
 * A virtual controller that is always assigned to the game input
 */
class Android final : public InputEngine {
public:
    explicit Android(std::string input_engine_);

    ~Android() override;

    /**
     * Registers controller number to accept new inputs.
     * @param j_input_device CitronInputDevice object from the Android frontend to register.
     */
    void RegisterController(jobject j_input_device);

    /**
     * Sets the status of a button on a specific controller.
     * @param guid 32 character hexadecimal string consisting of the controller's PID+VID.
     * @param port Port determined by controller connection order.
     * @param button_id The Android Keycode corresponding to this event.
     * @param value Whether the button is pressed or not.
     */
    void SetButtonState(std::string guid, size_t port, int button_id, bool value);

    /**
     * Sets the status of an axis on a specific controller.
     * @param guid 32 character hexadecimal string consisting of the controller's PID+VID.
     * @param port Port determined by controller connection order.
     * @param axis_id The Android axis ID corresponding to this event.
     * @param value Value along the given axis.
     */
    void SetAxisPosition(std::string guid, size_t port, int axis_id, float value);

    /**
     * Sets the status of the motion sensor on a specific controller
     * @param guid 32 character hexadecimal string consisting of the controller's PID+VID.
     * @param port Port determined by controller connection order.
     * @param delta_timestamp Time passed since the last read.
     * @param gyro_x,gyro_y,gyro_z Gyro sensor readings.
     * @param accel_x,accel_y,accel_z Accelerometer sensor readings.
     */
    void SetMotionState(std::string guid, size_t port, u64 delta_timestamp, float gyro_x,
                        float gyro_y, float gyro_z, float accel_x, float accel_y, float accel_z);

    Common::Input::DriverResult SetVibration(
        const PadIdentifier& identifier, const Common::Input::VibrationStatus& vibration) override;

    bool IsVibrationEnabled(const PadIdentifier& identifier) override;

    std::vector<Common::ParamPackage> GetInputDevices() const override;

    /**
     * Gets the axes reported by the CitronInputDevice.
     * @param env JNI environment pointer.
     * @param j_device CitronInputDevice from the Android frontend.
     * @return Set of the axes reported by the underlying Android InputDevice
     */
    std::set<s32> GetDeviceAxes(JNIEnv* env, jobject& j_device) const;

    Common::ParamPackage BuildParamPackageForAnalog(PadIdentifier identifier, int axis_x,
                                                    int axis_y) const;

    Common::ParamPackage BuildAnalogParamPackageForButton(PadIdentifier identifier, s32 axis,
                                                          bool invert) const;

    Common::ParamPackage BuildButtonParamPackageForButton(PadIdentifier identifier,
                                                          s32 button) const;

    bool MatchVID(Common::UUID device, const std::vector<std::string>& vids) const;

    AnalogMapping GetAnalogMappingForDevice(const Common::ParamPackage& params) override;

    ButtonMapping GetButtonMappingForDevice(const Common::ParamPackage& params) override;

    Common::Input::ButtonNames GetUIName(const Common::ParamPackage& params) const override;

private:
    std::unordered_map<PadIdentifier, jobject> input_devices;

    /// Returns the correct identifier corresponding to the player index
    PadIdentifier GetIdentifier(const std::string& guid, size_t port) const;

    /// Takes all vibrations from the queue and sends the command to the controller
    void SendVibrations(JNIEnv* env, std::stop_token token);

    static constexpr s32 AXIS_X = 0;
    static constexpr s32 AXIS_Y = 1;
    static constexpr s32 AXIS_Z = 11;
    static constexpr s32 AXIS_RX = 12;
    static constexpr s32 AXIS_RY = 13;
    static constexpr s32 AXIS_RZ = 14;
    static constexpr s32 AXIS_HAT_X = 15;
    static constexpr s32 AXIS_HAT_Y = 16;
    static constexpr s32 AXIS_LTRIGGER = 17;
    static constexpr s32 AXIS_RTRIGGER = 18;

    static constexpr s32 KEYCODE_DPAD_UP = 19;
    static constexpr s32 KEYCODE_DPAD_DOWN = 20;
    static constexpr s32 KEYCODE_DPAD_LEFT = 21;
    static constexpr s32 KEYCODE_DPAD_RIGHT = 22;
    static constexpr s32 KEYCODE_BUTTON_A = 96;
    static constexpr s32 KEYCODE_BUTTON_B = 97;
    static constexpr s32 KEYCODE_BUTTON_X = 99;
    static constexpr s32 KEYCODE_BUTTON_Y = 100;
    static constexpr s32 KEYCODE_BUTTON_L1 = 102;
    static constexpr s32 KEYCODE_BUTTON_R1 = 103;
    static constexpr s32 KEYCODE_BUTTON_L2 = 104;
    static constexpr s32 KEYCODE_BUTTON_R2 = 105;
    static constexpr s32 KEYCODE_BUTTON_THUMBL = 106;
    static constexpr s32 KEYCODE_BUTTON_THUMBR = 107;
    static constexpr s32 KEYCODE_BUTTON_START = 108;
    static constexpr s32 KEYCODE_BUTTON_SELECT = 109;
    const std::vector<s32> keycode_ids{
        KEYCODE_DPAD_UP,       KEYCODE_DPAD_DOWN,     KEYCODE_DPAD_LEFT,    KEYCODE_DPAD_RIGHT,
        KEYCODE_BUTTON_A,      KEYCODE_BUTTON_B,      KEYCODE_BUTTON_X,     KEYCODE_BUTTON_Y,
        KEYCODE_BUTTON_L1,     KEYCODE_BUTTON_R1,     KEYCODE_BUTTON_L2,    KEYCODE_BUTTON_R2,
        KEYCODE_BUTTON_THUMBL, KEYCODE_BUTTON_THUMBR, KEYCODE_BUTTON_START, KEYCODE_BUTTON_SELECT,
    };

    const std::string sony_vid{"054c"};
    const std::string nintendo_vid{"057e"};
    const std::string razer_vid{"1532"};
    const std::string redmagic_vid{"3537"};
    const std::string backbone_labs_vid{"358a"};
    const std::string xbox_vid{"045e"};
    const std::vector<std::string> flipped_ab_vids{sony_vid,     nintendo_vid,      razer_vid,
                                                   redmagic_vid, backbone_labs_vid, xbox_vid};
    const std::vector<std::string> flipped_xy_vids{sony_vid, razer_vid, redmagic_vid,
                                                   backbone_labs_vid, xbox_vid};

    /// Queue of vibration request to controllers
    Common::SPSCQueue<VibrationRequest> vibration_queue;
    std::jthread vibration_thread;
};

} // namespace InputCommon
