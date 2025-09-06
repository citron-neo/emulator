// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/fs/fs.h>
#include <common/fs/path_util.h>
#include <common/settings.h>
#include <hid_core/hid_types.h>
#include <jni.h>

#include "android_config.h"
#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/android.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/drivers/virtual_amiibo.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "native.h"

std::unordered_map<std::string, std::unique_ptr<AndroidConfig>> map_profiles;

bool IsHandheldOnly() {
    const auto npad_style_set =
        EmulationSession::GetInstance().System().HIDCore().GetSupportedStyleTag();

    if (npad_style_set.fullkey == 1) {
        return false;
    }

    if (npad_style_set.handheld == 0) {
        return false;
    }

    return !Settings::IsDockedMode();
}

std::filesystem::path GetNameWithoutExtension(std::filesystem::path filename) {
    return filename.replace_extension();
}

bool IsProfileNameValid(std::string_view profile_name) {
    return profile_name.find_first_of("<>:;\"/\\|,.!?*") == std::string::npos;
}

bool ProfileExistsInFilesystem(std::string_view profile_name) {
    return Common::FS::Exists(Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "input" /
                              fmt::format("{}.ini", profile_name));
}

bool ProfileExistsInMap(const std::string& profile_name) {
    return map_profiles.find(profile_name) != map_profiles.end();
}

bool SaveProfile(const std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    Settings::values.players.GetValue()[player_index].profile_name = profile_name;
    map_profiles[profile_name]->SaveAndroidControlPlayerValues(player_index);
    return true;
}

bool LoadProfile(std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name)) {
        map_profiles.erase(profile_name);
        return false;
    }

    LOG_INFO(Config, "Loading input profile `{}`", profile_name);

    Settings::values.players.GetValue()[player_index].profile_name = profile_name;
    map_profiles[profile_name]->ReadAndroidControlPlayerValues(player_index);
    return true;
}

void ApplyControllerConfig(size_t player_index,
                           const std::function<void(Core::HID::EmulatedController*)>& apply) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    if (player_index == 0) {
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        handheld->EnableConfiguration();
        player_one->EnableConfiguration();
        apply(handheld);
        apply(player_one);
        handheld->DisableConfiguration();
        player_one->DisableConfiguration();
        handheld->SaveCurrentConfig();
        player_one->SaveCurrentConfig();
    } else {
        auto* controller = hid_core.GetEmulatedControllerByIndex(player_index);
        controller->EnableConfiguration();
        apply(controller);
        controller->DisableConfiguration();
        controller->SaveCurrentConfig();
    }
}

std::vector<s32> GetSupportedStyles(int player_index) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    const auto npad_style_set = hid_core.GetSupportedStyleTag();
    std::vector<s32> supported_indexes;
    if (npad_style_set.fullkey == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::Fullkey));
    }

    if (npad_style_set.joycon_dual == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::JoyconDual));
    }

    if (npad_style_set.joycon_left == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::JoyconLeft));
    }

    if (npad_style_set.joycon_right == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::JoyconRight));
    }

    if (player_index == 0 && npad_style_set.handheld == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::Handheld));
    }

    if (npad_style_set.gamecube == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::GameCube));
    }

    return supported_indexes;
}

void ConnectController(size_t player_index, bool connected) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    ApplyControllerConfig(player_index, [&](Core::HID::EmulatedController* controller) {
        auto supported_styles = GetSupportedStyles(player_index);
        auto controller_style = controller->GetNpadStyleIndex(true);
        auto style = std::find(supported_styles.begin(), supported_styles.end(),
                               static_cast<int>(controller_style));
        if (style == supported_styles.end() && !supported_styles.empty()) {
            controller->SetNpadStyleIndex(
                static_cast<Core::HID::NpadStyleIndex>(supported_styles[0]));
        }
    });

    if (player_index == 0) {
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        handheld->EnableConfiguration();
        player_one->EnableConfiguration();
        if (player_one->GetNpadStyleIndex(true) == Core::HID::NpadStyleIndex::Handheld) {
            if (connected) {
                handheld->Connect();
            } else {
                handheld->Disconnect();
            }
            player_one->Disconnect();
        } else {
            if (connected) {
                player_one->Connect();
            } else {
                player_one->Disconnect();
            }
            handheld->Disconnect();
        }
        handheld->DisableConfiguration();
        player_one->DisableConfiguration();
        handheld->SaveCurrentConfig();
        player_one->SaveCurrentConfig();
    } else {
        auto* controller = hid_core.GetEmulatedControllerByIndex(player_index);
        controller->EnableConfiguration();
        if (connected) {
            controller->Connect();
        } else {
            controller->Disconnect();
        }
        controller->DisableConfiguration();
        controller->SaveCurrentConfig();
    }
}

extern "C" {

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_isHandheldOnly(JNIEnv* env,
                                                                           jobject j_obj) {
    return IsHandheldOnly();
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onGamePadButtonEvent(
    JNIEnv* env, jobject j_obj, jstring j_guid, jint j_port, jint j_button_id, jint j_action) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->SetButtonState(
        Common::Android::GetJString(env, j_guid), j_port, j_button_id, j_action != 0);
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onGamePadAxisEvent(
    JNIEnv* env, jobject j_obj, jstring j_guid, jint j_port, jint j_stick_id, jfloat j_value) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->SetAxisPosition(
        Common::Android::GetJString(env, j_guid), j_port, j_stick_id, j_value);
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onGamePadMotionEvent(
    JNIEnv* env, jobject j_obj, jstring j_guid, jint j_port, jlong j_delta_timestamp,
    jfloat j_x_gyro, jfloat j_y_gyro, jfloat j_z_gyro, jfloat j_x_accel, jfloat j_y_accel,
    jfloat j_z_accel) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->SetMotionState(
        Common::Android::GetJString(env, j_guid), j_port, j_delta_timestamp, j_x_gyro, j_y_gyro,
        j_z_gyro, j_x_accel, j_y_accel, j_z_accel);
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onReadNfcTag(JNIEnv* env, jobject j_obj,
                                                                     jbyteArray j_data) {
    jboolean isCopy{false};
    std::span<u8> data(reinterpret_cast<u8*>(env->GetByteArrayElements(j_data, &isCopy)),
                       static_cast<size_t>(env->GetArrayLength(j_data)));

    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualAmiibo()->LoadAmiibo(data);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onRemoveNfcTag(JNIEnv* env, jobject j_obj) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualAmiibo()->CloseAmiibo();
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onTouchPressed(JNIEnv* env, jobject j_obj,
                                                                       jint j_id, jfloat j_x_axis,
                                                                       jfloat j_y_axis) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().Window().OnTouchPressed(j_id, j_x_axis, j_y_axis);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onTouchMoved(JNIEnv* env, jobject j_obj,
                                                                     jint j_id, jfloat j_x_axis,
                                                                     jfloat j_y_axis) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().Window().OnTouchMoved(j_id, j_x_axis, j_y_axis);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onTouchReleased(JNIEnv* env, jobject j_obj,
                                                                        jint j_id) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().Window().OnTouchReleased(j_id);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onOverlayButtonEventImpl(
    JNIEnv* env, jobject j_obj, jint j_port, jint j_button_id, jint j_action) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualGamepad()->SetButtonState(
            j_port, j_button_id, j_action == 1);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onOverlayJoystickEventImpl(
    JNIEnv* env, jobject j_obj, jint j_port, jint j_stick_id, jfloat j_x_axis, jfloat j_y_axis) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualGamepad()->SetStickPosition(
            j_port, j_stick_id, j_x_axis, j_y_axis);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_onDeviceMotionEvent(
    JNIEnv* env, jobject j_obj, jint j_port, jlong j_delta_timestamp, jfloat j_x_gyro,
    jfloat j_y_gyro, jfloat j_z_gyro, jfloat j_x_accel, jfloat j_y_accel, jfloat j_z_accel) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualGamepad()->SetMotionState(
            j_port, j_delta_timestamp, j_x_gyro, j_y_gyro, j_z_gyro, j_x_accel, j_y_accel,
            j_z_accel);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_reloadInputDevices(JNIEnv* env,
                                                                           jobject j_obj) {
    EmulationSession::GetInstance().System().HIDCore().ReloadInputDevices();
}

void Java_org_citron_citron_1emu_features_input_NativeInput_registerController(JNIEnv* env,
                                                                           jobject j_obj,
                                                                           jobject j_device) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->RegisterController(j_device);
}

jobjectArray Java_org_citron_citron_1emu_features_input_NativeInput_getInputDevices(JNIEnv* env,
                                                                                jobject j_obj) {
    auto devices = EmulationSession::GetInstance().GetInputSubsystem().GetInputDevices();
    jobjectArray jdevices = env->NewObjectArray(devices.size(), Common::Android::GetStringClass(),
                                                Common::Android::ToJString(env, ""));
    for (size_t i = 0; i < devices.size(); ++i) {
        env->SetObjectArrayElement(jdevices, i,
                                   Common::Android::ToJString(env, devices[i].Serialize()));
    }
    return jdevices;
}

void Java_org_citron_citron_1emu_features_input_NativeInput_loadInputProfiles(JNIEnv* env,
                                                                          jobject j_obj) {
    map_profiles.clear();
    const auto input_profile_loc =
        Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "input";

    if (Common::FS::IsDir(input_profile_loc)) {
        Common::FS::IterateDirEntries(
            input_profile_loc,
            [&](const std::filesystem::path& full_path) {
                const auto filename = full_path.filename();
                const auto name_without_ext =
                    Common::FS::PathToUTF8String(GetNameWithoutExtension(filename));

                if (filename.extension() == ".ini" && IsProfileNameValid(name_without_ext)) {
                    map_profiles.insert_or_assign(
                        name_without_ext, std::make_unique<AndroidConfig>(
                                              name_without_ext, Config::ConfigType::InputProfile));
                }

                return true;
            },
            Common::FS::DirEntryFilter::File);
    }
}

jobjectArray Java_org_citron_citron_1emu_features_input_NativeInput_getInputProfileNames(
    JNIEnv* env, jobject j_obj) {
    std::vector<std::string> profile_names;
    profile_names.reserve(map_profiles.size());

    auto it = map_profiles.cbegin();
    while (it != map_profiles.cend()) {
        const auto& [profile_name, config] = *it;
        if (!ProfileExistsInFilesystem(profile_name)) {
            it = map_profiles.erase(it);
            continue;
        }

        profile_names.push_back(profile_name);
        ++it;
    }

    std::stable_sort(profile_names.begin(), profile_names.end());

    jobjectArray j_profile_names =
        env->NewObjectArray(profile_names.size(), Common::Android::GetStringClass(),
                            Common::Android::ToJString(env, ""));
    for (size_t i = 0; i < profile_names.size(); ++i) {
        env->SetObjectArrayElement(j_profile_names, i,
                                   Common::Android::ToJString(env, profile_names[i]));
    }

    return j_profile_names;
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_isProfileNameValid(JNIEnv* env,
                                                                               jobject j_obj,
                                                                               jstring j_name) {
    return Common::Android::GetJString(env, j_name).find_first_of("<>:;\"/\\|,.!?*") ==
           std::string::npos;
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_createProfile(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jstring j_name,
                                                                          jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    if (ProfileExistsInMap(profile_name)) {
        return false;
    }

    map_profiles.insert_or_assign(
        profile_name,
        std::make_unique<AndroidConfig>(profile_name, Config::ConfigType::InputProfile));

    return SaveProfile(profile_name, j_player_index);
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_deleteProfile(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jstring j_name,
                                                                          jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name) ||
        Common::FS::RemoveFile(map_profiles[profile_name]->GetConfigFilePath())) {
        map_profiles.erase(profile_name);
    }

    Settings::values.players.GetValue()[j_player_index].profile_name = "";
    return !ProfileExistsInMap(profile_name) && !ProfileExistsInFilesystem(profile_name);
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_loadProfile(JNIEnv* env, jobject j_obj,
                                                                        jstring j_name,
                                                                        jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    return LoadProfile(profile_name, j_player_index);
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_saveProfile(JNIEnv* env, jobject j_obj,
                                                                        jstring j_name,
                                                                        jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    return SaveProfile(profile_name, j_player_index);
}

void Java_org_citron_citron_1emu_features_input_NativeInput_loadPerGameConfiguration(
    JNIEnv* env, jobject j_obj, jint j_player_index, jint j_selected_index,
    jstring j_selected_profile_name) {
    static constexpr size_t HANDHELD_INDEX = 8;

    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    Settings::values.players.SetGlobal(false);

    auto profile_name = Common::Android::GetJString(env, j_selected_profile_name);
    auto* emulated_controller = hid_core.GetEmulatedControllerByIndex(j_player_index);

    if (j_selected_index == 0) {
        Settings::values.players.GetValue()[j_player_index].profile_name = "";
        if (j_player_index == 0) {
            Settings::values.players.GetValue()[HANDHELD_INDEX] = {};
        }
        Settings::values.players.SetGlobal(true);
        emulated_controller->ReloadFromSettings();
        return;
    }
    if (profile_name.empty()) {
        return;
    }
    auto& player = Settings::values.players.GetValue()[j_player_index];
    auto& global_player = Settings::values.players.GetValue(true)[j_player_index];
    player.profile_name = profile_name;
    global_player.profile_name = profile_name;
    // Read from the profile into the custom player settings
    LoadProfile(profile_name, j_player_index);
    // Make sure the controller is connected
    player.connected = true;

    emulated_controller->ReloadFromSettings();

    if (j_player_index > 0) {
        return;
    }
    // Handle Handheld cases
    auto& handheld_player = Settings::values.players.GetValue()[HANDHELD_INDEX];
    auto* handheld_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    if (player.controller_type == Settings::ControllerType::Handheld) {
        handheld_player = player;
    } else {
        handheld_player = {};
    }
    handheld_controller->ReloadFromSettings();
}

void Java_org_citron_citron_1emu_features_input_NativeInput_beginMapping(JNIEnv* env, jobject j_obj,
                                                                     jint jtype) {
    EmulationSession::GetInstance().GetInputSubsystem().BeginMapping(
        static_cast<InputCommon::Polling::InputType>(jtype));
}

jstring Java_org_citron_citron_1emu_features_input_NativeInput_getNextInput(JNIEnv* env,
                                                                        jobject j_obj) {
    return Common::Android::ToJString(
        env, EmulationSession::GetInstance().GetInputSubsystem().GetNextInput().Serialize());
}

void Java_org_citron_citron_1emu_features_input_NativeInput_stopMapping(JNIEnv* env, jobject j_obj) {
    EmulationSession::GetInstance().GetInputSubsystem().StopMapping();
}

void Java_org_citron_citron_1emu_features_input_NativeInput_updateMappingsWithDefaultImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jstring j_device_params,
    jstring j_display_name) {
    auto& input_subsystem = EmulationSession::GetInstance().GetInputSubsystem();

    // Clear all previous mappings
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetButtonParam(button_id, {});
        });
    }
    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetStickParam(analog_id, {});
        });
    }

    // Apply new mappings
    auto device = Common::ParamPackage(Common::Android::GetJString(env, j_device_params));
    auto button_mappings = input_subsystem.GetButtonMappingForDevice(device);
    auto analog_mappings = input_subsystem.GetAnalogMappingForDevice(device);
    auto display_name = Common::Android::GetJString(env, j_display_name);
    for (const auto& button_mapping : button_mappings) {
        const std::size_t index = button_mapping.first;
        auto named_mapping = button_mapping.second;
        named_mapping.Set("display", display_name);
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetButtonParam(index, named_mapping);
        });
    }
    for (const auto& analog_mapping : analog_mappings) {
        const std::size_t index = analog_mapping.first;
        auto named_mapping = analog_mapping.second;
        named_mapping.Set("display", display_name);
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetStickParam(index, named_mapping);
        });
    }
}

jstring Java_org_citron_citron_1emu_features_input_NativeInput_getButtonParamImpl(JNIEnv* env,
                                                                              jobject j_obj,
                                                                              jint j_player_index,
                                                                              jint j_button) {
    return Common::Android::ToJString(env, EmulationSession::GetInstance()
                                               .System()
                                               .HIDCore()
                                               .GetEmulatedControllerByIndex(j_player_index)
                                               ->GetButtonParam(j_button)
                                               .Serialize());
}

void Java_org_citron_citron_1emu_features_input_NativeInput_setButtonParamImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jint j_button_id, jstring j_param) {
    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        controller->SetButtonParam(j_button_id,
                                   Common::ParamPackage(Common::Android::GetJString(env, j_param)));
    });
}

jstring Java_org_citron_citron_1emu_features_input_NativeInput_getStickParamImpl(JNIEnv* env,
                                                                             jobject j_obj,
                                                                             jint j_player_index,
                                                                             jint j_stick) {
    return Common::Android::ToJString(env, EmulationSession::GetInstance()
                                               .System()
                                               .HIDCore()
                                               .GetEmulatedControllerByIndex(j_player_index)
                                               ->GetStickParam(j_stick)
                                               .Serialize());
}

void Java_org_citron_citron_1emu_features_input_NativeInput_setStickParamImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jint j_stick_id, jstring j_param) {
    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        controller->SetStickParam(j_stick_id,
                                  Common::ParamPackage(Common::Android::GetJString(env, j_param)));
    });
}

jint Java_org_citron_citron_1emu_features_input_NativeInput_getButtonNameImpl(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jstring j_param) {
    return static_cast<jint>(EmulationSession::GetInstance().GetInputSubsystem().GetButtonName(
        Common::ParamPackage(Common::Android::GetJString(env, j_param))));
}

jintArray Java_org_citron_citron_1emu_features_input_NativeInput_getSupportedStyleTagsImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index) {
    auto supported_styles = GetSupportedStyles(j_player_index);
    jintArray j_supported_indexes = env->NewIntArray(supported_styles.size());
    env->SetIntArrayRegion(j_supported_indexes, 0, supported_styles.size(),
                           supported_styles.data());
    return j_supported_indexes;
}

jint Java_org_citron_citron_1emu_features_input_NativeInput_getStyleIndexImpl(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jint j_player_index) {
    return static_cast<s32>(EmulationSession::GetInstance()
                                .System()
                                .HIDCore()
                                .GetEmulatedControllerByIndex(j_player_index)
                                ->GetNpadStyleIndex(true));
}

void Java_org_citron_citron_1emu_features_input_NativeInput_setStyleIndexImpl(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jint j_player_index,
                                                                          jint j_style_index) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    auto type = static_cast<Core::HID::NpadStyleIndex>(j_style_index);
    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        controller->SetNpadStyleIndex(type);
    });
    if (j_player_index == 0) {
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        ConnectController(j_player_index,
                          player_one->IsConnected(true) || handheld->IsConnected(true));
    }
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_isControllerImpl(JNIEnv* env,
                                                                             jobject j_obj,
                                                                             jstring jparams) {
    return static_cast<jint>(EmulationSession::GetInstance().GetInputSubsystem().IsController(
        Common::ParamPackage(Common::Android::GetJString(env, jparams))));
}

jboolean Java_org_citron_citron_1emu_features_input_NativeInput_getIsConnected(JNIEnv* env,
                                                                           jobject j_obj,
                                                                           jint j_player_index) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    auto* controller = hid_core.GetEmulatedControllerByIndex(static_cast<size_t>(j_player_index));
    if (j_player_index == 0 &&
        controller->GetNpadStyleIndex(true) == Core::HID::NpadStyleIndex::Handheld) {
        return hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld)->IsConnected(true);
    }
    return controller->IsConnected(true);
}

void Java_org_citron_citron_1emu_features_input_NativeInput_connectControllersImpl(
    JNIEnv* env, jobject j_obj, jbooleanArray j_connected) {
    jboolean isCopy = false;
    auto j_connected_array_size = env->GetArrayLength(j_connected);
    jboolean* j_connected_array = env->GetBooleanArrayElements(j_connected, &isCopy);
    for (int i = 0; i < j_connected_array_size; ++i) {
        ConnectController(i, j_connected_array[i]);
    }
}

void Java_org_citron_citron_1emu_features_input_NativeInput_resetControllerMappings(
    JNIEnv* env, jobject j_obj, jint j_player_index) {
    // Clear all previous mappings
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetButtonParam(button_id, {});
        });
    }
    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetStickParam(analog_id, {});
        });
    }
}

} // extern "C"
