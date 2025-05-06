// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include <jni.h>

#include "android_config.h"
#include "android_settings.h"
#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "frontend_common/config.h"
#include "native.h"

std::unique_ptr<AndroidConfig> global_config;
std::unique_ptr<AndroidConfig> per_game_config;

template <typename T>
Settings::Setting<T>* getSetting(JNIEnv* env, jstring jkey) {
    auto key = Common::Android::GetJString(env, jkey);
    auto basic_setting = Settings::values.linkage.by_key[key];
    if (basic_setting != 0) {
        return static_cast<Settings::Setting<T>*>(basic_setting);
    }
    auto basic_android_setting = AndroidSettings::values.linkage.by_key[key];
    if (basic_android_setting != 0) {
        return static_cast<Settings::Setting<T>*>(basic_android_setting);
    }
    LOG_ERROR(Frontend, "[Android Native] Could not find setting - {}", key);
    return nullptr;
}

extern "C" {

void Java_org_citron_citron_1emu_utils_NativeConfig_initializeGlobalConfig(JNIEnv* env, jobject obj) {
    global_config = std::make_unique<AndroidConfig>();
}

void Java_org_citron_citron_1emu_utils_NativeConfig_unloadGlobalConfig(JNIEnv* env, jobject obj) {
    global_config.reset();
}

void Java_org_citron_citron_1emu_utils_NativeConfig_reloadGlobalConfig(JNIEnv* env, jobject obj) {
    global_config->AndroidConfig::ReloadAllValues();
}

void Java_org_citron_citron_1emu_utils_NativeConfig_saveGlobalConfig(JNIEnv* env, jobject obj) {
    global_config->AndroidConfig::SaveAllValues();
}

void Java_org_citron_citron_1emu_utils_NativeConfig_initializePerGameConfig(JNIEnv* env, jobject obj,
                                                                        jstring jprogramId,
                                                                        jstring jfileName) {
    auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    auto file_name = Common::Android::GetJString(env, jfileName);
    const auto config_file_name = program_id == 0 ? file_name : fmt::format("{:016X}", program_id);
    per_game_config =
        std::make_unique<AndroidConfig>(config_file_name, Config::ConfigType::PerGameConfig);
}

jboolean Java_org_citron_citron_1emu_utils_NativeConfig_isPerGameConfigLoaded(JNIEnv* env,
                                                                          jobject obj) {
    return per_game_config != nullptr;
}

void Java_org_citron_citron_1emu_utils_NativeConfig_savePerGameConfig(JNIEnv* env, jobject obj) {
    per_game_config->AndroidConfig::SaveAllValues();
}

void Java_org_citron_citron_1emu_utils_NativeConfig_unloadPerGameConfig(JNIEnv* env, jobject obj) {
    per_game_config.reset();
}

jboolean Java_org_citron_citron_1emu_utils_NativeConfig_getBoolean(JNIEnv* env, jobject obj,
                                                               jstring jkey, jboolean needGlobal) {
    auto setting = getSetting<bool>(env, jkey);
    if (setting == nullptr) {
        return false;
    }
    return setting->GetValue(static_cast<bool>(needGlobal));
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setBoolean(JNIEnv* env, jobject obj, jstring jkey,
                                                           jboolean value) {
    auto setting = getSetting<bool>(env, jkey);
    if (setting == nullptr) {
        return;
    }
    setting->SetValue(static_cast<bool>(value));
}

jbyte Java_org_citron_citron_1emu_utils_NativeConfig_getByte(JNIEnv* env, jobject obj, jstring jkey,
                                                         jboolean needGlobal) {
    auto setting = getSetting<u8>(env, jkey);
    if (setting == nullptr) {
        return -1;
    }
    return setting->GetValue(static_cast<bool>(needGlobal));
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setByte(JNIEnv* env, jobject obj, jstring jkey,
                                                        jbyte value) {
    auto setting = getSetting<u8>(env, jkey);
    if (setting == nullptr) {
        return;
    }
    setting->SetValue(value);
}

jshort Java_org_citron_citron_1emu_utils_NativeConfig_getShort(JNIEnv* env, jobject obj, jstring jkey,
                                                           jboolean needGlobal) {
    auto setting = getSetting<u16>(env, jkey);
    if (setting == nullptr) {
        return -1;
    }
    return setting->GetValue(static_cast<bool>(needGlobal));
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setShort(JNIEnv* env, jobject obj, jstring jkey,
                                                         jshort value) {
    auto setting = getSetting<u16>(env, jkey);
    if (setting == nullptr) {
        return;
    }
    setting->SetValue(value);
}

jint Java_org_citron_citron_1emu_utils_NativeConfig_getInt(JNIEnv* env, jobject obj, jstring jkey,
                                                       jboolean needGlobal) {
    auto setting = getSetting<int>(env, jkey);
    if (setting == nullptr) {
        return -1;
    }
    return setting->GetValue(needGlobal);
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setInt(JNIEnv* env, jobject obj, jstring jkey,
                                                       jint value) {
    auto setting = getSetting<int>(env, jkey);
    if (setting == nullptr) {
        return;
    }
    setting->SetValue(value);
}

jfloat Java_org_citron_citron_1emu_utils_NativeConfig_getFloat(JNIEnv* env, jobject obj, jstring jkey,
                                                           jboolean needGlobal) {
    auto setting = getSetting<float>(env, jkey);
    if (setting == nullptr) {
        return -1;
    }
    return setting->GetValue(static_cast<bool>(needGlobal));
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setFloat(JNIEnv* env, jobject obj, jstring jkey,
                                                         jfloat value) {
    auto setting = getSetting<float>(env, jkey);
    if (setting == nullptr) {
        return;
    }
    setting->SetValue(value);
}

jlong Java_org_citron_citron_1emu_utils_NativeConfig_getLong(JNIEnv* env, jobject obj, jstring jkey,
                                                         jboolean needGlobal) {
    auto setting = getSetting<s64>(env, jkey);
    if (setting == nullptr) {
        return -1;
    }
    return setting->GetValue(static_cast<bool>(needGlobal));
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setLong(JNIEnv* env, jobject obj, jstring jkey,
                                                        jlong value) {
    auto setting = getSetting<long>(env, jkey);
    if (setting == nullptr) {
        return;
    }
    setting->SetValue(value);
}

jstring Java_org_citron_citron_1emu_utils_NativeConfig_getString(JNIEnv* env, jobject obj, jstring jkey,
                                                             jboolean needGlobal) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting == nullptr) {
        return Common::Android::ToJString(env, "");
    }
    return Common::Android::ToJString(env, setting->GetValue(static_cast<bool>(needGlobal)));
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setString(JNIEnv* env, jobject obj, jstring jkey,
                                                          jstring value) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting == nullptr) {
        return;
    }

    setting->SetValue(Common::Android::GetJString(env, value));
}

jboolean Java_org_citron_citron_1emu_utils_NativeConfig_getIsRuntimeModifiable(JNIEnv* env, jobject obj,
                                                                           jstring jkey) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting != nullptr) {
        return setting->RuntimeModifiable();
    }
    return true;
}

jstring Java_org_citron_citron_1emu_utils_NativeConfig_getPairedSettingKey(JNIEnv* env, jobject obj,
                                                                       jstring jkey) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting == nullptr) {
        return Common::Android::ToJString(env, "");
    }
    if (setting->PairedSetting() == nullptr) {
        return Common::Android::ToJString(env, "");
    }

    return Common::Android::ToJString(env, setting->PairedSetting()->GetLabel());
}

jboolean Java_org_citron_citron_1emu_utils_NativeConfig_getIsSwitchable(JNIEnv* env, jobject obj,
                                                                    jstring jkey) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting != nullptr) {
        return setting->Switchable();
    }
    return false;
}

jboolean Java_org_citron_citron_1emu_utils_NativeConfig_usingGlobal(JNIEnv* env, jobject obj,
                                                                jstring jkey) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting != nullptr) {
        return setting->UsingGlobal();
    }
    return true;
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setGlobal(JNIEnv* env, jobject obj, jstring jkey,
                                                          jboolean global) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting != nullptr) {
        setting->SetGlobal(static_cast<bool>(global));
    }
}

jboolean Java_org_citron_citron_1emu_utils_NativeConfig_getIsSaveable(JNIEnv* env, jobject obj,
                                                                  jstring jkey) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting != nullptr) {
        return setting->Save();
    }
    return false;
}

jstring Java_org_citron_citron_1emu_utils_NativeConfig_getDefaultToString(JNIEnv* env, jobject obj,
                                                                      jstring jkey) {
    auto setting = getSetting<std::string>(env, jkey);
    if (setting != nullptr) {
        return Common::Android::ToJString(env, setting->DefaultToString());
    }
    return Common::Android::ToJString(env, "");
}

jobjectArray Java_org_citron_citron_1emu_utils_NativeConfig_getGameDirs(JNIEnv* env, jobject obj) {
    jclass gameDirClass = Common::Android::GetGameDirClass();
    jmethodID gameDirConstructor = Common::Android::GetGameDirConstructor();
    jobjectArray jgameDirArray =
        env->NewObjectArray(AndroidSettings::values.game_dirs.size(), gameDirClass, nullptr);
    for (size_t i = 0; i < AndroidSettings::values.game_dirs.size(); ++i) {
        jobject jgameDir = env->NewObject(
            gameDirClass, gameDirConstructor,
            Common::Android::ToJString(env, AndroidSettings::values.game_dirs[i].path),
            static_cast<jboolean>(AndroidSettings::values.game_dirs[i].deep_scan));
        env->SetObjectArrayElement(jgameDirArray, i, jgameDir);
    }
    return jgameDirArray;
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setGameDirs(JNIEnv* env, jobject obj,
                                                            jobjectArray gameDirs) {
    AndroidSettings::values.game_dirs.clear();
    int size = env->GetArrayLength(gameDirs);

    if (size == 0) {
        return;
    }

    jobject dir = env->GetObjectArrayElement(gameDirs, 0);
    jclass gameDirClass = Common::Android::GetGameDirClass();
    jfieldID uriStringField = env->GetFieldID(gameDirClass, "uriString", "Ljava/lang/String;");
    jfieldID deepScanBooleanField = env->GetFieldID(gameDirClass, "deepScan", "Z");
    for (int i = 0; i < size; ++i) {
        dir = env->GetObjectArrayElement(gameDirs, i);
        jstring juriString = static_cast<jstring>(env->GetObjectField(dir, uriStringField));
        jboolean jdeepScanBoolean = env->GetBooleanField(dir, deepScanBooleanField);
        std::string uriString = Common::Android::GetJString(env, juriString);
        AndroidSettings::values.game_dirs.push_back(
            AndroidSettings::GameDir{uriString, static_cast<bool>(jdeepScanBoolean)});
    }
}

void Java_org_citron_citron_1emu_utils_NativeConfig_addGameDir(JNIEnv* env, jobject obj,
                                                           jobject gameDir) {
    jclass gameDirClass = Common::Android::GetGameDirClass();
    jfieldID uriStringField = env->GetFieldID(gameDirClass, "uriString", "Ljava/lang/String;");
    jfieldID deepScanBooleanField = env->GetFieldID(gameDirClass, "deepScan", "Z");

    jstring juriString = static_cast<jstring>(env->GetObjectField(gameDir, uriStringField));
    jboolean jdeepScanBoolean = env->GetBooleanField(gameDir, deepScanBooleanField);
    std::string uriString = Common::Android::GetJString(env, juriString);
    AndroidSettings::values.game_dirs.push_back(
        AndroidSettings::GameDir{uriString, static_cast<bool>(jdeepScanBoolean)});
}

jobjectArray Java_org_citron_citron_1emu_utils_NativeConfig_getDisabledAddons(JNIEnv* env, jobject obj,
                                                                          jstring jprogramId) {
    auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    auto& disabledAddons = Settings::values.disabled_addons[program_id];
    jobjectArray jdisabledAddonsArray =
        env->NewObjectArray(disabledAddons.size(), Common::Android::GetStringClass(),
                            Common::Android::ToJString(env, ""));
    for (size_t i = 0; i < disabledAddons.size(); ++i) {
        env->SetObjectArrayElement(jdisabledAddonsArray, i,
                                   Common::Android::ToJString(env, disabledAddons[i]));
    }
    return jdisabledAddonsArray;
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setDisabledAddons(JNIEnv* env, jobject obj,
                                                                  jstring jprogramId,
                                                                  jobjectArray jdisabledAddons) {
    auto program_id = EmulationSession::GetProgramId(env, jprogramId);
    Settings::values.disabled_addons[program_id].clear();
    std::vector<std::string> disabled_addons;
    const int size = env->GetArrayLength(jdisabledAddons);
    for (int i = 0; i < size; ++i) {
        auto jaddon = static_cast<jstring>(env->GetObjectArrayElement(jdisabledAddons, i));
        disabled_addons.push_back(Common::Android::GetJString(env, jaddon));
    }
    Settings::values.disabled_addons[program_id] = disabled_addons;
}

jobjectArray Java_org_citron_citron_1emu_utils_NativeConfig_getOverlayControlData(JNIEnv* env,
                                                                              jobject obj) {
    jobjectArray joverlayControlDataArray =
        env->NewObjectArray(AndroidSettings::values.overlay_control_data.size(),
                            Common::Android::GetOverlayControlDataClass(), nullptr);
    for (size_t i = 0; i < AndroidSettings::values.overlay_control_data.size(); ++i) {
        const auto& control_data = AndroidSettings::values.overlay_control_data[i];
        jobject jlandscapePosition =
            env->NewObject(Common::Android::GetPairClass(), Common::Android::GetPairConstructor(),
                           Common::Android::ToJDouble(env, control_data.landscape_position.first),
                           Common::Android::ToJDouble(env, control_data.landscape_position.second));
        jobject jportraitPosition =
            env->NewObject(Common::Android::GetPairClass(), Common::Android::GetPairConstructor(),
                           Common::Android::ToJDouble(env, control_data.portrait_position.first),
                           Common::Android::ToJDouble(env, control_data.portrait_position.second));
        jobject jfoldablePosition =
            env->NewObject(Common::Android::GetPairClass(), Common::Android::GetPairConstructor(),
                           Common::Android::ToJDouble(env, control_data.foldable_position.first),
                           Common::Android::ToJDouble(env, control_data.foldable_position.second));

        jobject jcontrolData =
            env->NewObject(Common::Android::GetOverlayControlDataClass(),
                           Common::Android::GetOverlayControlDataConstructor(),
                           Common::Android::ToJString(env, control_data.id), control_data.enabled,
                           jlandscapePosition, jportraitPosition, jfoldablePosition);
        env->SetObjectArrayElement(joverlayControlDataArray, i, jcontrolData);
    }
    return joverlayControlDataArray;
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setOverlayControlData(
    JNIEnv* env, jobject obj, jobjectArray joverlayControlDataArray) {
    AndroidSettings::values.overlay_control_data.clear();
    int size = env->GetArrayLength(joverlayControlDataArray);

    if (size == 0) {
        return;
    }

    for (int i = 0; i < size; ++i) {
        jobject joverlayControlData = env->GetObjectArrayElement(joverlayControlDataArray, i);
        jstring jidString = static_cast<jstring>(env->GetObjectField(
            joverlayControlData, Common::Android::GetOverlayControlDataIdField()));
        bool enabled = static_cast<bool>(env->GetBooleanField(
            joverlayControlData, Common::Android::GetOverlayControlDataEnabledField()));

        jobject jlandscapePosition = env->GetObjectField(
            joverlayControlData, Common::Android::GetOverlayControlDataLandscapePositionField());
        std::pair<double, double> landscape_position = std::make_pair(
            Common::Android::GetJDouble(
                env, env->GetObjectField(jlandscapePosition, Common::Android::GetPairFirstField())),
            Common::Android::GetJDouble(
                env,
                env->GetObjectField(jlandscapePosition, Common::Android::GetPairSecondField())));

        jobject jportraitPosition = env->GetObjectField(
            joverlayControlData, Common::Android::GetOverlayControlDataPortraitPositionField());
        std::pair<double, double> portrait_position = std::make_pair(
            Common::Android::GetJDouble(
                env, env->GetObjectField(jportraitPosition, Common::Android::GetPairFirstField())),
            Common::Android::GetJDouble(
                env,
                env->GetObjectField(jportraitPosition, Common::Android::GetPairSecondField())));

        jobject jfoldablePosition = env->GetObjectField(
            joverlayControlData, Common::Android::GetOverlayControlDataFoldablePositionField());
        std::pair<double, double> foldable_position = std::make_pair(
            Common::Android::GetJDouble(
                env, env->GetObjectField(jfoldablePosition, Common::Android::GetPairFirstField())),
            Common::Android::GetJDouble(
                env,
                env->GetObjectField(jfoldablePosition, Common::Android::GetPairSecondField())));

        AndroidSettings::values.overlay_control_data.push_back(AndroidSettings::OverlayControlData{
            Common::Android::GetJString(env, jidString), enabled, landscape_position,
            portrait_position, foldable_position});
    }
}

jobjectArray Java_org_citron_citron_1emu_utils_NativeConfig_getInputSettings(JNIEnv* env, jobject obj,
                                                                         jboolean j_global) {
    Settings::values.players.SetGlobal(static_cast<bool>(j_global));
    auto& players = Settings::values.players.GetValue();
    jobjectArray j_input_settings =
        env->NewObjectArray(players.size(), Common::Android::GetPlayerInputClass(), nullptr);
    for (size_t i = 0; i < players.size(); ++i) {
        auto j_connected = static_cast<jboolean>(players[i].connected);

        jobjectArray j_buttons = env->NewObjectArray(
            players[i].buttons.size(), Common::Android::GetStringClass(), env->NewStringUTF(""));
        for (size_t j = 0; j < players[i].buttons.size(); ++j) {
            env->SetObjectArrayElement(j_buttons, j,
                                       Common::Android::ToJString(env, players[i].buttons[j]));
        }
        jobjectArray j_analogs = env->NewObjectArray(
            players[i].analogs.size(), Common::Android::GetStringClass(), env->NewStringUTF(""));
        for (size_t j = 0; j < players[i].analogs.size(); ++j) {
            env->SetObjectArrayElement(j_analogs, j,
                                       Common::Android::ToJString(env, players[i].analogs[j]));
        }
        jobjectArray j_motions = env->NewObjectArray(
            players[i].motions.size(), Common::Android::GetStringClass(), env->NewStringUTF(""));
        for (size_t j = 0; j < players[i].motions.size(); ++j) {
            env->SetObjectArrayElement(j_motions, j,
                                       Common::Android::ToJString(env, players[i].motions[j]));
        }

        auto j_vibration_enabled = static_cast<jboolean>(players[i].vibration_enabled);
        auto j_vibration_strength = static_cast<jint>(players[i].vibration_strength);

        auto j_body_color_left = static_cast<jlong>(players[i].body_color_left);
        auto j_body_color_right = static_cast<jlong>(players[i].body_color_right);
        auto j_button_color_left = static_cast<jlong>(players[i].button_color_left);
        auto j_button_color_right = static_cast<jlong>(players[i].button_color_right);

        auto j_profile_name = Common::Android::ToJString(env, players[i].profile_name);

        auto j_use_system_vibrator = players[i].use_system_vibrator;

        jobject playerInput = env->NewObject(
            Common::Android::GetPlayerInputClass(), Common::Android::GetPlayerInputConstructor(),
            j_connected, j_buttons, j_analogs, j_motions, j_vibration_enabled, j_vibration_strength,
            j_body_color_left, j_body_color_right, j_button_color_left, j_button_color_right,
            j_profile_name, j_use_system_vibrator);
        env->SetObjectArrayElement(j_input_settings, i, playerInput);
    }
    return j_input_settings;
}

void Java_org_citron_citron_1emu_utils_NativeConfig_setInputSettings(JNIEnv* env, jobject obj,
                                                                 jobjectArray j_value,
                                                                 jboolean j_global) {
    auto& players = Settings::values.players.GetValue(static_cast<bool>(j_global));
    int playersSize = env->GetArrayLength(j_value);
    for (int i = 0; i < playersSize; ++i) {
        jobject jplayer = env->GetObjectArrayElement(j_value, i);

        players[i].connected = static_cast<bool>(
            env->GetBooleanField(jplayer, Common::Android::GetPlayerInputConnectedField()));

        auto j_buttons_array = static_cast<jobjectArray>(
            env->GetObjectField(jplayer, Common::Android::GetPlayerInputButtonsField()));
        int buttons_size = env->GetArrayLength(j_buttons_array);
        for (int j = 0; j < buttons_size; ++j) {
            auto button = static_cast<jstring>(env->GetObjectArrayElement(j_buttons_array, j));
            players[i].buttons[j] = Common::Android::GetJString(env, button);
        }
        auto j_analogs_array = static_cast<jobjectArray>(
            env->GetObjectField(jplayer, Common::Android::GetPlayerInputAnalogsField()));
        int analogs_size = env->GetArrayLength(j_analogs_array);
        for (int j = 0; j < analogs_size; ++j) {
            auto analog = static_cast<jstring>(env->GetObjectArrayElement(j_analogs_array, j));
            players[i].analogs[j] = Common::Android::GetJString(env, analog);
        }
        auto j_motions_array = static_cast<jobjectArray>(
            env->GetObjectField(jplayer, Common::Android::GetPlayerInputMotionsField()));
        int motions_size = env->GetArrayLength(j_motions_array);
        for (int j = 0; j < motions_size; ++j) {
            auto motion = static_cast<jstring>(env->GetObjectArrayElement(j_motions_array, j));
            players[i].motions[j] = Common::Android::GetJString(env, motion);
        }

        players[i].vibration_enabled = static_cast<bool>(
            env->GetBooleanField(jplayer, Common::Android::GetPlayerInputVibrationEnabledField()));
        players[i].vibration_strength = static_cast<int>(
            env->GetIntField(jplayer, Common::Android::GetPlayerInputVibrationStrengthField()));

        players[i].body_color_left = static_cast<u32>(
            env->GetLongField(jplayer, Common::Android::GetPlayerInputBodyColorLeftField()));
        players[i].body_color_right = static_cast<u32>(
            env->GetLongField(jplayer, Common::Android::GetPlayerInputBodyColorRightField()));
        players[i].button_color_left = static_cast<u32>(
            env->GetLongField(jplayer, Common::Android::GetPlayerInputButtonColorLeftField()));
        players[i].button_color_right = static_cast<u32>(
            env->GetLongField(jplayer, Common::Android::GetPlayerInputButtonColorRightField()));

        auto profileName = static_cast<jstring>(
            env->GetObjectField(jplayer, Common::Android::GetPlayerInputProfileNameField()));
        players[i].profile_name = Common::Android::GetJString(env, profileName);

        players[i].use_system_vibrator =
            env->GetBooleanField(jplayer, Common::Android::GetPlayerInputUseSystemVibratorField());
    }
}

void Java_org_citron_citron_1emu_utils_NativeConfig_saveControlPlayerValues(JNIEnv* env, jobject obj) {
    Settings::values.players.SetGlobal(false);

    // Clear all controls from the config in case the user reverted back to globals
    per_game_config->ClearControlPlayerValues();
    for (size_t index = 0; index < Settings::values.players.GetValue().size(); ++index) {
        per_game_config->SaveAndroidControlPlayerValues(index);
    }
}

} // extern "C"
