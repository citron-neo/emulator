// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SDL will break our main function in citron-cmd if we don't define this before adding SDL.h
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "common/logging/log.h"
#include "input_common/main.h"
#include "sdl_config.h"

const std::array<int, Settings::NativeButton::NumButtons> SdlConfig::default_buttons = {
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_T,
    SDL_SCANCODE_G, SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_Q, SDL_SCANCODE_W,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_B,
};

const std::array<int, Settings::NativeMotion::NumMotions> SdlConfig::default_motions = {
    SDL_SCANCODE_7,
    SDL_SCANCODE_8,
};

const std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> SdlConfig::default_analogs{
    {
        {
            SDL_SCANCODE_UP,
            SDL_SCANCODE_DOWN,
            SDL_SCANCODE_LEFT,
            SDL_SCANCODE_RIGHT,
        },
        {
            SDL_SCANCODE_I,
            SDL_SCANCODE_K,
            SDL_SCANCODE_J,
            SDL_SCANCODE_L,
        },
    }};

const std::array<int, 2> SdlConfig::default_stick_mod = {
    SDL_SCANCODE_D,
    0,
};

const std::array<int, 2> SdlConfig::default_ringcon_analogs{{
    0,
    0,
}};

SdlConfig::SdlConfig(const std::optional<std::string> config_path) {
    Initialize(config_path);
    ReadSdlValues();
    SaveSdlValues();
}

SdlConfig::~SdlConfig() {
    if (global) {
        SdlConfig::SaveAllValues();
    }
}

void SdlConfig::ReloadAllValues() {
    Reload();
    ReadSdlValues();
    SaveSdlValues();
}

void SdlConfig::SaveAllValues() {
    SaveValues();
    SaveSdlValues();
}

void SdlConfig::ReadSdlValues() {
    ReadSdlControlValues();
}

void SdlConfig::ReadSdlControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        ReadSdlPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    ReadDebugControlValues();
    ReadHidbusValues();

    EndGroup();
}

void SdlConfig::ReadSdlPlayerValues(const std::size_t player_index) {
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

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& player_buttons = player.buttons[i];

        player_buttons = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeButton::mapping[i]), default_param);
        if (player_buttons.empty()) {
            player_buttons = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& player_analogs = player.analogs[i];

        player_analogs = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]), default_param);
        if (player_analogs.empty()) {
            player_analogs = default_param;
        }
    }

    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_motions[i]);
        auto& player_motions = player.motions[i];

        player_motions = ReadStringSetting(
            std::string(player_prefix).append(Settings::NativeMotion::mapping[i]), default_param);
        if (player_motions.empty()) {
            player_motions = default_param;
        }
    }
}

void SdlConfig::ReadDebugControlValues() {
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        auto& debug_pad_buttons = Settings::values.debug_pad_buttons[i];
        debug_pad_buttons = ReadStringSetting(
            std::string("debug_pad_").append(Settings::NativeButton::mapping[i]), default_param);
        if (debug_pad_buttons.empty()) {
            debug_pad_buttons = default_param;
        }
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        auto& debug_pad_analogs = Settings::values.debug_pad_analogs[i];
        debug_pad_analogs = ReadStringSetting(
            std::string("debug_pad_").append(Settings::NativeAnalog::mapping[i]), default_param);
        if (debug_pad_analogs.empty()) {
            debug_pad_analogs = default_param;
        }
    }
}

void SdlConfig::ReadHidbusValues() {
    const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
        0, 0, default_ringcon_analogs[0], default_ringcon_analogs[1], 0, 0.05f);
    auto& ringcon_analogs = Settings::values.ringcon_analogs;

    ringcon_analogs = ReadStringSetting(std::string("ring_controller"), default_param);
    if (ringcon_analogs.empty()) {
        ringcon_analogs = default_param;
    }
}

void SdlConfig::SaveSdlValues() {
    LOG_DEBUG(Config, "Saving SDL configuration values");
    SaveSdlControlValues();

    WriteToIni();
}

void SdlConfig::SaveSdlControlValues() {
    BeginGroup(Settings::TranslateCategory(Settings::Category::Controls));

    Settings::values.players.SetGlobal(!IsCustomConfig());
    for (std::size_t p = 0; p < Settings::values.players.GetValue().size(); ++p) {
        SaveSdlPlayerValues(p);
    }
    if (IsCustomConfig()) {
        EndGroup();
        return;
    }
    SaveDebugControlValues();
    SaveHidbusValues();

    EndGroup();
}

void SdlConfig::SaveSdlPlayerValues(const std::size_t player_index) {
    std::string player_prefix;
    if (type != ConfigType::InputProfile) {
        player_prefix = std::string("player_").append(ToString(player_index)).append("_");
    }

    const auto& player = Settings::values.players.GetValue()[player_index];
    if (IsCustomConfig() && player.profile_name.empty()) {
        // No custom profile selected
        return;
    }

    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeButton::mapping[i]),
                           player.buttons[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeAnalog::mapping[i]),
                           player.analogs[i], std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeMotion::NumMotions; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_motions[i]);
        WriteStringSetting(std::string(player_prefix).append(Settings::NativeMotion::mapping[i]),
                           player.motions[i], std::make_optional(default_param));
    }
}

void SdlConfig::SaveDebugControlValues() {
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        const std::string default_param = InputCommon::GenerateKeyboardParam(default_buttons[i]);
        WriteStringSetting(std::string("debug_pad_").append(Settings::NativeButton::mapping[i]),
                           Settings::values.debug_pad_buttons[i],
                           std::make_optional(default_param));
    }
    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
            default_analogs[i][0], default_analogs[i][1], default_analogs[i][2],
            default_analogs[i][3], default_stick_mod[i], 0.5f);
        WriteStringSetting(std::string("debug_pad_").append(Settings::NativeAnalog::mapping[i]),
                           Settings::values.debug_pad_analogs[i],
                           std::make_optional(default_param));
    }
}

void SdlConfig::SaveHidbusValues() {
    const std::string default_param = InputCommon::GenerateAnalogParamFromKeys(
        0, 0, default_ringcon_analogs[0], default_ringcon_analogs[1], 0, 0.05f);
    WriteStringSetting(std::string("ring_controller"), Settings::values.ringcon_analogs,
                       std::make_optional(default_param));
}

std::vector<Settings::BasicSetting*>& SdlConfig::FindRelevantList(Settings::Category category) {
    return Settings::values.linkage.by_category[category];
}
