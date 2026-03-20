// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "citron/uisettings.h"

#ifndef CANNOT_EXPLICITLY_INSTANTIATE
namespace Settings {
    template class Setting<bool>;
    template class Setting<std::string>;
    template class Setting<u16, true>;
    template class Setting<u32>;
    template class Setting<u8, true>;
    template class Setting<u8>;
    template class Setting<unsigned long long>;
} // namespace Settings
#endif

namespace FS = Common::FS;

namespace UISettings {
    const std::array<Shortcut, 31> default_hotkeys{{
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Audio Mute/Unmute")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+M"),  std::string("Home+Dpad_Right"), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Audio Volume Down")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("-"),       std::string("Home+Dpad_Down"), Qt::ApplicationShortcut, true}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Audio Volume Up")).toStdString(),          QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("="),       std::string("Home+Dpad_Up"), Qt::ApplicationShortcut, true}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Capture Screenshot")).toStdString(),       QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+P"),  std::string("Screenshot"), Qt::WidgetWithChildrenShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Change Adapting Filter")).toStdString(),   QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F8"),      std::string("Home+L"), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Change Docked Mode")).toStdString(),       QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F10"),     std::string("Home+X"), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Change GPU Accuracy")).toStdString(),      QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F9"),      std::string("Home+R"), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Continue/Pause Emulation")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F4"),      std::string("Home+Plus"), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Exit Fullscreen")).toStdString(),          QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Esc"),     std::string(""), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Exit citron")).toStdString(),                QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+Q"),  std::string("Home+Minus"), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Fullscreen")).toStdString(),               QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F11"),     std::string("Home+B"), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Load File")).toStdString(),                QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+O"),  std::string(""), Qt::WidgetWithChildrenShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Load/Remove Amiibo")).toStdString(),       QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F2"),      std::string("Home+A"), Qt::WidgetWithChildrenShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Browse Public Game Lobby")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+B"),  std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Create Room")).toStdString(),  QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+N"),  std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Direct Connect to Room")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+C"),  std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Leave Room")).toStdString(),   QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+L"),  std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Multiplayer Show Current Room")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+R"),  std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Restart Emulation")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F6"),      std::string("R+Plus+Minus"), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Stop Emulation")).toStdString(),           QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("F5"),      std::string("L+Plus+Minus"), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "TAS Record")).toStdString(),               QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F7"), std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "TAS Reset")).toStdString(),                QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F6"), std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "TAS Start/Stop")).toStdString(),           QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F5"), std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Controller Overlay")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Filter Bar")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F"),  std::string(""), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Grid View")).toStdString(),         QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+G"),  std::string(""), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Framerate Limit")).toStdString(),   QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+U"),  std::string("Home+Y"), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Mouse Panning")).toStdString(),     QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+F9"), std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Memory & Cheat Engine")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+Shift+M"), std::string(""), Qt::WindowShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Renderdoc Capture")).toStdString(), QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string(""),        std::string(""), Qt::ApplicationShortcut, false}},
        {QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Toggle Status Bar")).toStdString(),        QStringLiteral(QT_TRANSLATE_NOOP("Hotkeys", "Main Window")).toStdString(), {std::string("Ctrl+S"),  std::string(""), Qt::WindowShortcut, false}},
    }};

    const Themes themes{{
        {"Default", "default"},
        {"Default Colorful", "colorful"},
        {"Dark", "qdarkstyle"},
        {"Dark Colorful", "colorful_dark"},
        {"Midnight Blue", "qdarkstyle_midnight_blue"},
        {"Midnight Blue Colorful", "colorful_midnight_blue"},
    }};

    bool IsDarkTheme() {
        const auto& theme_name = UISettings::values.theme;

        // Priority 1: Check for explicitly chosen dark themes.
        if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
            theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
            return true;
        }

        // Priority 2: Check for adaptive themes ("default" and "colorful").
        if (theme_name == "default" || theme_name == "colorful") {
            const QPalette palette = qApp->palette();
            const QColor text_color = palette.color(QPalette::WindowText);
            const QColor base_color = palette.color(QPalette::Window);
            return text_color.value() > base_color.value();
        }

        // Fallback for any other unknown themes is to assume they are light.
        return false;
    }

    Values values = {};

    // definition for the function.
    u32 CalculateWidth(u32 height, Settings::AspectRatio ratio) {
        switch (ratio) {
        case Settings::AspectRatio::R4_3:
            return height * 4 / 3;
        case Settings::AspectRatio::R21_9:
            return height * 21 / 9;
        case Settings::AspectRatio::R16_10:
            return height * 16 / 10;
        case Settings::AspectRatio::R32_9:
            return height * 32 / 9;
        case Settings::AspectRatio::R16_9:
        case Settings::AspectRatio::Stretch:
            // TODO: Move this function wherever appropriate to implement Stretched aspect
            break;
        }
        return height * 16 / 9;
    }

    void SaveWindowState() {
        const auto window_state_config_loc =
            FS::PathToUTF8String(FS::GetCitronPath(FS::CitronPath::ConfigDir) / "window_state.ini");

        void(FS::CreateParentDir(window_state_config_loc));
        QSettings config(QString::fromStdString(window_state_config_loc), QSettings::IniFormat);

        config.setValue(QStringLiteral("geometry"), values.geometry);
        config.setValue(QStringLiteral("state"), values.state);
        config.setValue(QStringLiteral("geometryRenderWindow"), values.renderwindow_geometry);
        config.setValue(QStringLiteral("configureDialogGeometry"), values.configure_dialog_geometry);
        config.setValue(QStringLiteral("perGameConfigureGeometry"), values.per_game_configure_geometry);
        config.setValue(QStringLiteral("gameListHeaderState"), values.gamelist_header_state);

        config.sync();
    }

    void RestoreWindowState(std::unique_ptr<QtConfig>& qtConfig) {
        const auto window_state_config_loc =
            FS::PathToUTF8String(FS::GetCitronPath(FS::CitronPath::ConfigDir) / "window_state.ini");

        // Migrate window state from old location
        if (!FS::Exists(window_state_config_loc) && qtConfig->Exists("UI", "UILayout\\geometry")) {
            const auto config_loc =
                FS::PathToUTF8String(FS::GetCitronPath(FS::CitronPath::ConfigDir) / "qt-config.ini");
            QSettings config(QString::fromStdString(config_loc), QSettings::IniFormat);

            config.beginGroup(QStringLiteral("UI"));
            config.beginGroup(QStringLiteral("UILayout"));
            values.geometry = config.value(QStringLiteral("geometry")).toByteArray();
            values.state = config.value(QStringLiteral("state")).toByteArray();
            values.renderwindow_geometry =
                config.value(QStringLiteral("geometryRenderWindow")).toByteArray();
            values.gamelist_header_state =
                config.value(QStringLiteral("gameListHeaderState")).toByteArray();
            config.endGroup();
            config.endGroup();
            return;
        }

        void(FS::CreateParentDir(window_state_config_loc));
        const QSettings config(QString::fromStdString(window_state_config_loc), QSettings::IniFormat);

        values.geometry = config.value(QStringLiteral("geometry")).toByteArray();
        values.state = config.value(QStringLiteral("state")).toByteArray();
        values.renderwindow_geometry =
            config.value(QStringLiteral("geometryRenderWindow")).toByteArray();
        values.configure_dialog_geometry =
            config.value(QStringLiteral("configureDialogGeometry")).toByteArray();
        values.per_game_configure_geometry =
            config.value(QStringLiteral("perGameConfigureGeometry")).toByteArray();
        values.gamelist_header_state =
            config.value(QStringLiteral("gameListHeaderState")).toByteArray();
    }

    bool IsGamescope() {
#ifdef __linux__
    static const bool is_gamescope = !qgetenv("GAMESCOPE_WIDTH").isEmpty() ||
                                     qgetenv("XDG_CURRENT_DESKTOP") == "gamescope" ||
                                     !qgetenv("STEAM_DECK").isEmpty();
    return is_gamescope;
#else
    return false;
#endif
}

} // namespace UISettings
