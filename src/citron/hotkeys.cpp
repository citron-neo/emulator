// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <sstream>
#include <QShortcut>
#include <QTreeWidgetItem>
#include <QtGlobal>

#include "hid_core/frontend/emulated_controller.h"
#include "citron/hotkeys.h"
#include "citron/uisettings.h"

HotkeyRegistry::HotkeyRegistry() = default;
HotkeyRegistry::~HotkeyRegistry() = default;

void HotkeyRegistry::SaveHotkeys() {
    std::map<std::string, std::map<std::string, Hotkey>> default_groups;
    for (const auto& def : UISettings::default_hotkeys) {
        default_groups[def.group][def.name] =
            Hotkey{QKeySequence::fromString(QString::fromStdString(def.shortcut.keyseq)),
                   def.shortcut.controller_keyseq,
                   nullptr,
                   nullptr,
                   static_cast<Qt::ShortcutContext>(def.shortcut.context),
                   def.shortcut.repeat};
    }

    UISettings::values.shortcuts.clear();
    for (const auto& [group_name, actions] : hotkey_groups) {
        for (const auto& [action_name, current_hotkey] : actions) {
            Hotkey default_hotkey;
            auto group_it = default_groups.find(group_name);
            if (group_it != default_groups.end()) {
                auto action_it = group_it->second.find(action_name);
                if (action_it != group_it->second.end()) {
                    default_hotkey = action_it->second;
                }
            }

            bool is_modified =
                (current_hotkey.keyseq != default_hotkey.keyseq) ||
                (current_hotkey.controller_keyseq != default_hotkey.controller_keyseq) ||
                (current_hotkey.context != default_hotkey.context);

            if (is_modified) {
                UISettings::values.shortcuts.push_back(
                    {action_name, group_name,
                     UISettings::ContextualShortcut(
                         {current_hotkey.keyseq.toString().toStdString(),
                          current_hotkey.controller_keyseq, current_hotkey.context,
                          current_hotkey.repeat})});
            }
        }
    }
}

void HotkeyRegistry::LoadHotkeys() {
    // First, populate the registry with ALL default hotkeys, including blank ones.
    hotkey_groups.clear();
    for (const auto& def : UISettings::default_hotkeys) {
        Hotkey& hk = hotkey_groups[def.group][def.name];
        hk.keyseq = QKeySequence::fromString(QString::fromStdString(def.shortcut.keyseq));
        hk.controller_keyseq = def.shortcut.controller_keyseq;
        hk.context = static_cast<Qt::ShortcutContext>(def.shortcut.context);
        hk.repeat = def.shortcut.repeat;
    }

    // Now, layer the user's saved (non-default) settings on top.
    for (const auto& shortcut : UISettings::values.shortcuts) {
        Hotkey& hk = hotkey_groups[shortcut.group][shortcut.name];
        if (!shortcut.shortcut.keyseq.empty()) {
            hk.keyseq = QKeySequence::fromString(QString::fromStdString(shortcut.shortcut.keyseq),
                                                 QKeySequence::NativeText);
        } else {
            // This is the fix: explicitly clear the key sequence if it was saved as empty.
            hk.keyseq = QKeySequence();
        }
        hk.controller_keyseq = shortcut.shortcut.controller_keyseq;
        hk.context = static_cast<Qt::ShortcutContext>(shortcut.shortcut.context);
        hk.repeat = shortcut.shortcut.repeat;

        if (hk.shortcut) {
            hk.shortcut->setKey(hk.keyseq);
        }
        if (hk.controller_shortcut) {
            hk.controller_shortcut->SetKey(hk.controller_keyseq);
        }
    }
}

QShortcut* HotkeyRegistry::GetHotkey(const std::string& group, const std::string& action,
                                     QWidget* widget) const {
    Hotkey& hk = hotkey_groups[group][action];

    if (!hk.shortcut) {
        hk.shortcut = new QShortcut(hk.keyseq, widget, nullptr, nullptr, hk.context);
    }

    hk.shortcut->setAutoRepeat(hk.repeat);
    return hk.shortcut;
}

ControllerShortcut* HotkeyRegistry::GetControllerHotkey(const std::string& group,
                                                        const std::string& action,
                                                        Core::HID::EmulatedController* controller) const {
    Hotkey& hk = hotkey_groups[group][action];

    if (!hk.controller_shortcut) {
        hk.controller_shortcut = new ControllerShortcut(controller);
        hk.controller_shortcut->SetKey(hk.controller_keyseq);
    }

    return hk.controller_shortcut;
}

QKeySequence HotkeyRegistry::GetKeySequence(const std::string& group, const std::string& action) const {
    return hotkey_groups[group][action].keyseq;
}

Qt::ShortcutContext HotkeyRegistry::GetShortcutContext(const std::string& group,
                                                       const std::string& action) const {
    return hotkey_groups[group][action].context;
}

ControllerShortcut::ControllerShortcut(Core::HID::EmulatedController* controller) {
    emulated_controller = controller;
    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change = [this](Core::HID::ControllerTriggerType type) { ControllerUpdateEvent(type); },
        .is_npad_service = false,
    };
    callback_key = emulated_controller->SetCallback(engine_callback);
    is_enabled = true;
}

ControllerShortcut::~ControllerShortcut() {
    emulated_controller->DeleteCallback(callback_key);
}

void ControllerShortcut::SetKey(const ControllerButtonSequence& buttons) {
    button_sequence = buttons;
}

void ControllerShortcut::SetKey(const std::string& buttons_shortcut) {
    ControllerButtonSequence sequence{};
    name = buttons_shortcut;
    std::istringstream command_line(buttons_shortcut);
    std::string line;
    while (std::getline(command_line, line, '+')) {
        if (line.empty()) {
            continue;
        }
        if (line == "A") {
            sequence.npad.a.Assign(1);
        }
        if (line == "B") {
            sequence.npad.b.Assign(1);
        }
        if (line == "X") {
            sequence.npad.x.Assign(1);
        }
        if (line == "Y") {
            sequence.npad.y.Assign(1);
        }
        if (line == "L") {
            sequence.npad.l.Assign(1);
        }
        if (line == "R") {
            sequence.npad.r.Assign(1);
        }
        if (line == "ZL") {
            sequence.npad.zl.Assign(1);
        }
        if (line == "ZR") {
            sequence.npad.zr.Assign(1);
        }
        if (line == "Dpad_Left") {
            sequence.npad.left.Assign(1);
        }
        if (line == "Dpad_Right") {
            sequence.npad.right.Assign(1);
        }
        if (line == "Dpad_Up") {
            sequence.npad.up.Assign(1);
        }
        if (line == "Dpad_Down") {
            sequence.npad.down.Assign(1);
        }
        if (line == "Left_Stick") {
            sequence.npad.stick_l.Assign(1);
        }
        if (line == "Right_Stick") {
            sequence.npad.stick_r.Assign(1);
        }
        if (line == "Minus") {
            sequence.npad.minus.Assign(1);
        }
        if (line == "Plus") {
            sequence.npad.plus.Assign(1);
        }
        if (line == "Home") {
            sequence.home.home.Assign(1);
        }
        if (line == "Screenshot") {
            sequence.capture.capture.Assign(1);
        }
    }

    button_sequence = sequence;
}

ControllerButtonSequence ControllerShortcut::ButtonSequence() const {
    return button_sequence;
}

void ControllerShortcut::SetEnabled(bool enable) {
    is_enabled = enable;
}

bool ControllerShortcut::IsEnabled() const {
    return is_enabled;
}

void ControllerShortcut::ControllerUpdateEvent(Core::HID::ControllerTriggerType type) {
    if (!is_enabled) {
        return;
    }
    if (type != Core::HID::ControllerTriggerType::Button) {
        return;
    }
    if (button_sequence.npad.raw == Core::HID::NpadButton::None &&
        button_sequence.capture.raw == 0 && button_sequence.home.raw == 0) {
        return;
    }

    const auto player_npad_buttons =
        emulated_controller->GetNpadButtons().raw & button_sequence.npad.raw;
    const u64 player_capture_buttons =
        emulated_controller->GetCaptureButtons().raw & button_sequence.capture.raw;
    const u64 player_home_buttons =
        emulated_controller->GetHomeButtons().raw & button_sequence.home.raw;

    if (player_npad_buttons == button_sequence.npad.raw &&
        player_capture_buttons == button_sequence.capture.raw &&
        player_home_buttons == button_sequence.home.raw && !active) {
        // Force user to press the home or capture button again
        active = true;
        emit Activated();
        return;
    }
    active = false;
}
