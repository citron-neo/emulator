// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <common/logging/log.h>
#include <input_common/main.h>
#include "android_config.h"
#include "android_settings.h"
#include "common/settings_setting.h"

AndroidConfig::AndroidConfig(const std::string& config_name, ConfigType config_type)
    : Config(config_type) {
    Initialize(config_name);
    if (config_type != ConfigType::InputProfile) {
        ReadAndroidValues();
        SaveAndroidValues();
    }
}

void AndroidConfig::ReloadAllValues() {
    Reload();
    ReadAndroidValues();
    SaveAndroidValues();
}

void AndroidConfig::SaveAllValues() {
    SaveValues();
    SaveAndroidValues();
}

void AndroidConfig::ReadAndroidValues() {
    if (global) {
        ReadAndroidUIValues();
        ReadUIValues();
        ReadOverlayValues();
    }
    ReadDriverValues();
    ReadAndroidControlValues();
}

void AndroidConfig::ReadAndroidUIValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Android));

    ReadCategory(Settings::Category::Android);

    EndGroup();
}

void AndroidConfig::ReadUIValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Ui));

    ReadPathValues();

    EndGroup();
}

void AndroidConfig::ReadPathValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Paths));

    AndroidSettings::values.game_dirs.clear();
    const int gamedirs_size = BeginArray(std::string("gamedirs"));
    for (int i = 0; i < gamedirs_size; ++i) {
        SetArrayIndex(i);
        AndroidSettings::GameDir game_dir;
        game_dir.path = ReadStringSetting(std::string("path"));
        game_dir.deep_scan =
            ReadBooleanSetting(std::string("deep_scan"), std::make_optional(false));
        AndroidSettings::values.game_dirs.push_back(game_dir);
    }
    EndArray();

    EndGroup();
}

void AndroidConfig::ReadDriverValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::GpuDriver));

    ReadCategory(Settings::Category::GpuDriver);

    EndGroup();
}

void AndroidConfig::ReadOverlayValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Overlay));

    ReadCategory(Settings::Category::Overlay);

    AndroidSettings::values.overlay_control_data.clear();
    const int control_data_size = BeginArray("control_data");
    for (int i = 0; i < control_data_size; ++i) {
        SetArrayIndex(i);
        AndroidSettings::OverlayControlData control_data;
        control_data.id = ReadStringSetting(std::string("id"));
        control_data.enabled = ReadBooleanSetting(std::string("enabled"));
        control_data.landscape_position.first =
            ReadDoubleSetting(std::string("landscape\\x_position"));
        control_data.landscape_position.second =
            ReadDoubleSetting(std::string("landscape\\y_position"));
        control_data.portrait_position.first =
            ReadDoubleSetting(std::string("portrait\\x_position"));
        control_data.portrait_position.second =
            ReadDoubleSetting(std::string("portrait\\y_position"));
        control_data.foldable_position.first =
            ReadDoubleSetting(std::string("foldable\\x_position"));
        control_data.foldable_position.second =
            ReadDoubleSetting(std::string("foldable\\y_position"));
        AndroidSettings::values.overlay_control_data.push_back(control_data);
    }
    EndArray();

    EndGroup();
}

void AndroidConfig::ReadAndroidPlayerValues(std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix.append("player_").append(ToString(player_index)).append("_");
    }

    auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig()) {
        const auto profile_name =
            ReadStringSetting(std::string(player_prefix).append("profile_name"));
        if (profile_name.empty()) {
            // Use the global input config
            player = Settings::values.players.GetValue(true)[player_index];
            player.profile_name = "";
            return;
        }
    }

    // Android doesn't have default options for controllers. We have the input overlay for that.
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param;
        auto& player_buttons = player.buttons[i];

        player_buttons = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeButton::mapping[i]), default_param);
        if (player_buttons.empty()) {
            player_buttons = default_param;
        }
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param;
        auto& player_analogs = player.analogs[i];

        player_analogs = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]), default_param);
        if (player_analogs.empty()) {
            player_analogs = default_param;
        }
    }
    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param;
        auto& player_motions = player.motions[i];

        player_motions = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeMotion::mapping[i]), default_param);
        if (player_motions.empty()) {
            player_motions = default_param;
        }
    }
    player.use_system_vibrator = ReadBooleanSetting(
        std::string(player_prefix).append("use_system_vibrator"), player_index == 0);
}

void AndroidConfig::ReadAndroidControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        ReadAndroidPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    // ReadDebugControlValues();
    // ReadHidbusValues();

    EndGroup();
}

void AndroidConfig::SaveAndroidValues() {
    if (global) {
        SaveAndroidUIValues();
        SaveUIValues();
        SaveOverlayValues();
    }
    SaveDriverValues();
    SaveAndroidControlValues();

    WriteToIni();
}

void AndroidConfig::SaveAndroidUIValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Android));

    WriteCategory(Settings::Category::Android);

    EndGroup();
}

void AndroidConfig::SaveUIValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Ui));

    SavePathValues();

    EndGroup();
}

void AndroidConfig::SavePathValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Paths));

    BeginArray(std::string("gamedirs"));
    for (size_t i = 0; i < AndroidSettings::values.game_dirs.size(); ++i) {
        SetArrayIndex(i);
        const auto& game_dir = AndroidSettings::values.game_dirs[i];
        WriteStringSetting(std::string("path"), game_dir.path);
        WriteBooleanSetting(std::string("deep_scan"), game_dir.deep_scan,
                            std::make_optional(false));
    }
    EndArray();

    EndGroup();
}

void AndroidConfig::SaveDriverValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::GpuDriver));

    WriteCategory(Settings::Category::GpuDriver);

    EndGroup();
}

void AndroidConfig::SaveOverlayValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Overlay));

    WriteCategory(Settings::Category::Overlay);

    BeginArray("control_data");
    for (size_t i = 0; i < AndroidSettings::values.overlay_control_data.size(); ++i) {
        SetArrayIndex(i);
        const auto& control_data = AndroidSettings::values.overlay_control_data[i];
        WriteStringSetting(std::string("id"), control_data.id);
        WriteBooleanSetting(std::string("enabled"), control_data.enabled);
        WriteDoubleSetting(std::string("landscape\\x_position"),
                           control_data.landscape_position.first);
        WriteDoubleSetting(std::string("landscape\\y_position"),
                           control_data.landscape_position.second);
        WriteDoubleSetting(std::string("portrait\\x_position"),
                           control_data.portrait_position.first);
        WriteDoubleSetting(std::string("portrait\\y_position"),
                           control_data.portrait_position.second);
        WriteDoubleSetting(std::string("foldable\\x_position"),
                           control_data.foldable_position.first);
        WriteDoubleSetting(std::string("foldable\\y_position"),
                           control_data.foldable_position.second);
    }
    EndArray();

    EndGroup();
}

void AndroidConfig::SaveAndroidPlayerValues(std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix = std::string("player_").append(ToString(player_index)).append("_");
    }

    const auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig() && player.profile_name.empty()) {
        // No custom profile selected
        return;
    }

    const std::string default_param;
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeButton::mapping[i]),
                           player.buttons[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]),
                           player.analogs[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeMotion::mapping[i]),
                           player.motions[i], std::make_optional(default_param));
    }
    WriteBooleanSetting(std::string(player_prefix).append("use_system_vibrator"),
                        player.use_system_vibrator, std::make_optional(player_index == 0));
}

void AndroidConfig::SaveAndroidControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        SaveAndroidPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    // SaveDebugControlValues();
    // SaveHidbusValues();

    EndGroup();
}

std::vector<Settings::BasicSetting*>& AndroidConfig::FindRelevantList(Settings::Category category) {
    auto& map = Settings::values.linkage.by_category;
    if (map.contains(category)) {
        return Settings::values.linkage.by_category[category];
    }
    return AndroidSettings::values.linkage.by_category[category];
}

void AndroidConfig::ReadAndroidControlPlayerValues(std::size_t player_index) {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    ReadPlayerValues(player_index);
    ReadAndroidPlayerValues(player_index);

    EndGroup();
}

void AndroidConfig::SaveAndroidControlPlayerValues(std::size_t player_index) {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    LOG_DEBUG(Config, "Saving players control configuration values");
    SavePlayerValues(player_index);
    SaveAndroidPlayerValues(player_index);

    EndGroup();

    WriteToIni();
}
