// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QMetaType>

#include "frontend_common/config.h"

class QtConfig final : public Config {
public:
    explicit QtConfig(const std::string& config_name = "qt-config",
                      ConfigType config_type = ConfigType::GlobalConfig);
    ~QtConfig() override;

    void ReloadAllValues() override;
    void SaveAllValues() override;

    void ReadQtControlPlayerValues(std::size_t player_index);
    void SaveQtControlPlayerValues(std::size_t player_index);

protected:
    void ReadQtValues();
    void ReadQtPlayerValues(std::size_t player_index);
    void ReadQtControlValues();
    void ReadHidbusValues() override;
    void ReadDebugControlValues() override;
    void ReadPathValues() override;
    void ReadShortcutValues() override;
    void ReadUIValues() override;
    void ReadUIGamelistValues() override;
    void ReadUILayoutValues() override;
    void ReadMultiplayerValues() override;

    void SaveQtValues();
    void SaveQtPlayerValues(std::size_t player_index);
    void SaveQtControlValues();
    void SaveHidbusValues() override;
    void SaveDebugControlValues() override;
    void SavePathValues() override;
    void SaveShortcutValues() override;
    void SaveUIValues() override;
    void SaveUIGamelistValues() override;
    void SaveUILayoutValues() override;
    void SaveMultiplayerValues() override;

    std::vector<Settings::BasicSetting*>& FindRelevantList(Settings::Category category) override;

public:
    static const std::array<int, Settings::NativeButton::NumButtons> default_buttons;
    static const std::array<int, Settings::NativeMotion::NumMotions> default_motions;
    static const std::array<std::array<int, 4>, Settings::NativeAnalog::NumAnalogs> default_analogs;
    static const std::array<int, 2> default_stick_mod;
    static const std::array<int, 2> default_ringcon_analogs;
};
