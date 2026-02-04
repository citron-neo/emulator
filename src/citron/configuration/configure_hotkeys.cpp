// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-FileCopyrightText: 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardItemModel>
#include <QTimer>

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"

#include "citron/configuration/configure_hotkeys.h"
#include "citron/hotkeys.h"
#include "citron/uisettings.h"
#include "citron/util/sequence_dialog/sequence_dialog.h"
#include "frontend_common/config.h"
#include "ui_configure_hotkeys.h"

constexpr int name_column = 0;
constexpr int hotkey_column = 1;
constexpr int controller_column = 2;

ConfigureHotkeys::ConfigureHotkeys(HotkeyRegistry& registry_, Core::HID::HIDCore& hid_core,
                                   QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureHotkeys>()), registry(registry_),
      controller(new Core::HID::EmulatedController(Core::HID::NpadIdType::Player1)),
      timeout_timer(std::make_unique<QTimer>()), poll_timer(std::make_unique<QTimer>()) {
    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);

    model = new QStandardItemModel(this);
    model->setColumnCount(3);

    connect(ui->hotkey_list, &QTreeView::doubleClicked, this, &ConfigureHotkeys::Configure);
    connect(ui->hotkey_list, &QTreeView::customContextMenuRequested, this,
            &ConfigureHotkeys::PopupContextMenu);
    ui->hotkey_list->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->hotkey_list->setModel(model);

    ui->hotkey_list->header()->setStretchLastSection(false);
    ui->hotkey_list->header()->setSectionResizeMode(name_column, QHeaderView::Interactive);
    ui->hotkey_list->header()->setSectionResizeMode(hotkey_column, QHeaderView::Interactive);
    ui->hotkey_list->header()->setSectionResizeMode(controller_column, QHeaderView::Stretch);
    ui->hotkey_list->header()->setMinimumSectionSize(70);

    connect(ui->button_restore_defaults, &QPushButton::clicked, this,
            &ConfigureHotkeys::RestoreDefaults);
    connect(ui->button_clear_all, &QPushButton::clicked, this, &ConfigureHotkeys::ClearAll);

    // Profile Management Connections
    connect(ui->button_new_profile, &QPushButton::clicked, this,
            &ConfigureHotkeys::OnCreateProfile);
    connect(ui->button_delete_profile, &QPushButton::clicked, this,
            &ConfigureHotkeys::OnDeleteProfile);
    connect(ui->button_rename_profile, &QPushButton::clicked, this,
            &ConfigureHotkeys::OnRenameProfile);
    connect(ui->button_import_profile, &QPushButton::clicked, this,
            &ConfigureHotkeys::OnImportProfile);
    connect(ui->button_export_profile, &QPushButton::clicked, this,
            &ConfigureHotkeys::OnExportProfile);
    connect(ui->combo_box_profile, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureHotkeys::OnProfileChanged);

    connect(timeout_timer.get(), &QTimer::timeout, [this] {
        const bool is_button_pressed = pressed_buttons != Core::HID::NpadButton::None ||
                                       pressed_home_button || pressed_capture_button;
        SetPollingResult(!is_button_pressed);
    });

    connect(poll_timer.get(), &QTimer::timeout, [this] {
        pressed_buttons |= controller->GetNpadButtons().raw;
        pressed_home_button |= controller->GetHomeButtons().home != 0;
        pressed_capture_button |= controller->GetCaptureButtons().capture != 0;
        if (pressed_buttons != Core::HID::NpadButton::None || pressed_home_button ||
            pressed_capture_button) {
            const QString button_name =
                GetButtonCombinationName(pressed_buttons, pressed_home_button,
                                         pressed_capture_button) +
                QStringLiteral("...");
            model->setData(button_model_index, button_name);
        }
    });

    ui->hotkey_list->setContextMenuPolicy(Qt::CustomContextMenu);

    // Populate profile list first
    UpdateProfileList();
}

ConfigureHotkeys::~ConfigureHotkeys() = default;

void ConfigureHotkeys::Populate() {
    const auto& profiles = profile_manager.GetProfiles();
    const auto& current_profile_name = profiles.current_profile;

    // Use default if current profile missing (safety)
    std::vector<Hotkey::BackendShortcut> current_shortcuts;
    if (profiles.profiles.count(current_profile_name)) {
        current_shortcuts = profiles.profiles.at(current_profile_name);
    } else if (profiles.profiles.count("Default")) {
        current_shortcuts = profiles.profiles.at("Default");
    }

    // Map overrides for easy lookup: Key = Group + Name
    std::map<std::pair<std::string, std::string>, Hotkey::BackendShortcut> overrides;
    for (const auto& s : current_shortcuts) {
        overrides[{s.group, s.name}] = s;
    }

    model->clear();
    model->setColumnCount(3);
    model->setHorizontalHeaderLabels({tr("Action"), tr("Hotkey"), tr("Controller Hotkey")});

    for (const auto& [group_name, group_map] : registry.hotkey_groups) {
        auto* parent_item = new QStandardItem(
            QCoreApplication::translate("Hotkeys", qPrintable(QString::fromStdString(group_name))));
        parent_item->setEditable(false);
        parent_item->setData(QString::fromStdString(group_name));
        model->appendRow(parent_item);

        for (const auto& [action_name, hotkey] : group_map) {
            // Determine values (Registry Default vs Profile Override)
            QString keyseq_str = hotkey.keyseq.toString(QKeySequence::NativeText);
            QString controller_keyseq_str = QString::fromStdString(hotkey.controller_keyseq);

            if (overrides.count({group_name, action_name})) {
                const auto& overridden = overrides.at({group_name, action_name});
                keyseq_str = QKeySequence(QString::fromStdString(overridden.shortcut.keyseq))
                                 .toString(QKeySequence::NativeText);
                controller_keyseq_str =
                    QString::fromStdString(overridden.shortcut.controller_keyseq);
            }

            auto* action_item = new QStandardItem(QCoreApplication::translate(
                "Hotkeys", qPrintable(QString::fromStdString(action_name))));
            action_item->setEditable(false);
            action_item->setData(QString::fromStdString(action_name), Qt::UserRole);

            auto* keyseq_item = new QStandardItem(keyseq_str);
            keyseq_item->setData(keyseq_str, Qt::UserRole);
            keyseq_item->setEditable(false);

            auto* controller_item = new QStandardItem(controller_keyseq_str);
            controller_item->setEditable(false);

            // Store metadata (context and repeat) for saving later
            int context = hotkey.context;
            bool repeat = hotkey.repeat;
            if (overrides.count({group_name, action_name})) {
                const auto& overridden = overrides.at({group_name, action_name});
                context = overridden.shortcut.context;
                repeat = overridden.shortcut.repeat;
            }
            action_item->setData(context, Qt::UserRole + 1);
            action_item->setData(repeat, Qt::UserRole + 2);

            parent_item->appendRow({action_item, keyseq_item, controller_item});
        }

        if (group_name == "General" || group_name == "Main Window") {
            ui->hotkey_list->expand(parent_item->index());
        }
    }
    ui->hotkey_list->expandAll();

    // Re-apply column sizing after model reset
    ui->hotkey_list->header()->setStretchLastSection(false);
    ui->hotkey_list->header()->setSectionResizeMode(name_column, QHeaderView::Interactive);
    ui->hotkey_list->header()->setSectionResizeMode(hotkey_column, QHeaderView::Interactive);
    ui->hotkey_list->header()->setSectionResizeMode(controller_column, QHeaderView::Stretch);
    ui->hotkey_list->header()->setMinimumSectionSize(70);

    ui->hotkey_list->setColumnWidth(name_column, 432);
    ui->hotkey_list->setColumnWidth(hotkey_column, 240);

    // Enforce fixed width for Restore Defaults button to prevent smudging
    ui->button_restore_defaults->setFixedWidth(143);
}

void ConfigureHotkeys::UpdateProfileList() {
    const QSignalBlocker blocker(ui->combo_box_profile);
    ui->combo_box_profile->clear();

    const auto& profiles = profile_manager.GetProfiles();
    for (const auto& [name, val] : profiles.profiles) {
        ui->combo_box_profile->addItem(QString::fromStdString(name));
    }

    ui->combo_box_profile->setCurrentText(QString::fromStdString(profiles.current_profile));
    Populate();
}

void ConfigureHotkeys::OnCreateProfile() {
    bool ok;
    QString text = QInputDialog::getText(this, tr("Create Profile"), tr("Profile Name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty()) {
        if (profile_manager.CreateProfile(text.toStdString())) {
            // New profile is empty. Fill with current defaults or copy current?
            // "Defaults" logic usually implies defaults.
            UpdateProfileList();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Failed to create profile."));
        }
    }
}

void ConfigureHotkeys::OnDeleteProfile() {
    if (QMessageBox::question(this, tr("Delete Profile"),
                              tr("Are you sure you want to delete this profile?"),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (profile_manager.DeleteProfile(ui->combo_box_profile->currentText().toStdString())) {
            UpdateProfileList();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Failed to delete profile."));
        }
    }
}

void ConfigureHotkeys::OnRenameProfile() {
    bool ok;
    QString current_name = ui->combo_box_profile->currentText();
    QString text = QInputDialog::getText(this, tr("Rename Profile"), tr("New Name:"),
                                         QLineEdit::Normal, current_name, &ok);
    if (ok && !text.isEmpty()) {
        if (profile_manager.RenameProfile(current_name.toStdString(), text.toStdString())) {
            UpdateProfileList();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Failed to rename profile."));
        }
    }
}

void ConfigureHotkeys::OnImportProfile() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Import Profile"), QString(),
                                                    tr("JSON Files (*.json)"));
    if (!fileName.isEmpty()) {
        if (profile_manager.ImportProfile(fileName.toStdString())) {
            UpdateProfileList();
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Failed to import profile."));
        }
    }
}

void ConfigureHotkeys::OnExportProfile() {
    QString current = ui->combo_box_profile->currentText();
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export Profile"), current + QStringLiteral(".json"), tr("JSON Files (*.json)"));
    if (!fileName.isEmpty()) {
        if (!profile_manager.ExportProfile(current.toStdString(), fileName.toStdString())) {
            QMessageBox::warning(this, tr("Error"), tr("Failed to export profile."));
        }
    }
}

void ConfigureHotkeys::OnProfileChanged(int index) {
    if (index == -1)
        return;
    const std::string name = ui->combo_box_profile->currentText().toStdString();
    profile_manager.SetCurrentProfile(name);
    Populate();
}

void ConfigureHotkeys::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureHotkeys::RetranslateUI() {
    ui->retranslateUi(this);
    ui->label_profile->setText(tr("Hotkey Profile:"));

    model->setHorizontalHeaderLabels({tr("Action"), tr("Hotkey"), tr("Controller Hotkey")});
    for (int key_id = 0; key_id < model->rowCount(); key_id++) {
        QStandardItem* parent = model->item(key_id, 0);
        parent->setText(
            QCoreApplication::translate("Hotkeys", qPrintable(parent->data().toString())));
        for (int key_column_id = 0; key_column_id < parent->rowCount(); key_column_id++) {
            QStandardItem* action = parent->child(key_column_id, name_column);
            action->setText(
                QCoreApplication::translate("Hotkeys", qPrintable(action->data().toString())));
        }
    }
}

void ConfigureHotkeys::Configure(QModelIndex index) {
    if (!index.parent().isValid()) {
        return;
    }

    // Controller configuration is selected
    if (index.column() == controller_column) {
        ConfigureController(index);
        return;
    }

    // Swap to the hotkey column
    index = index.sibling(index.row(), hotkey_column);

    const auto previous_key = model->data(index);

    SequenceDialog hotkey_dialog{this};

    const int return_code = hotkey_dialog.exec();
    const auto key_sequence = hotkey_dialog.GetSequence();
    if (return_code == QDialog::Rejected || key_sequence.isEmpty()) {
        return;
    }
    const auto [key_sequence_used, used_action] = IsUsedKey(key_sequence);

    if (key_sequence_used && key_sequence != QKeySequence(previous_key.toString())) {
        QMessageBox::warning(
            this, tr("Conflicting Key Sequence"),
            tr("The entered key sequence is already assigned to: %1").arg(used_action));
    } else {
        model->setData(index, key_sequence.toString(QKeySequence::NativeText));
    }
}
void ConfigureHotkeys::ConfigureController(QModelIndex index) {
    if (timeout_timer->isActive()) {
        return;
    }

    const auto previous_key = model->data(index);

    input_setter = [this, index, previous_key](const bool cancel) {
        if (cancel) {
            model->setData(index, previous_key);
            return;
        }

        const QString button_string =
            GetButtonCombinationName(pressed_buttons, pressed_home_button, pressed_capture_button);

        const auto [key_sequence_used, used_action] = IsUsedControllerKey(button_string);

        if (key_sequence_used) {
            QMessageBox::warning(
                this, tr("Conflicting Key Sequence"),
                tr("The entered key sequence is already assigned to: %1").arg(used_action));
            model->setData(index, previous_key);
        } else {
            model->setData(index, button_string);
        }
    };

    button_model_index = index;
    pressed_buttons = Core::HID::NpadButton::None;
    pressed_home_button = false;
    pressed_capture_button = false;

    model->setData(index, tr("[waiting]"));
    timeout_timer->start(2500); // Cancel after 2.5 seconds
    poll_timer->start(100);     // Check for new inputs every 100ms
    // We need to disable configuration to be able to read npad buttons
    controller->DisableConfiguration();
}

void ConfigureHotkeys::SetPollingResult(const bool cancel) {
    timeout_timer->stop();
    poll_timer->stop();
    (*input_setter)(cancel);
    // Re-Enable configuration
    controller->EnableConfiguration();

    input_setter = std::nullopt;
}

QString ConfigureHotkeys::GetButtonCombinationName(Core::HID::NpadButton button,
                                                   const bool home = false,
                                                   const bool capture = false) const {
    Core::HID::NpadButtonState state{button};
    QString button_combination;
    if (home) {
        button_combination.append(QStringLiteral("Home+"));
    }
    if (capture) {
        button_combination.append(QStringLiteral("Screenshot+"));
    }
    if (state.a) {
        button_combination.append(QStringLiteral("A+"));
    }
    if (state.b) {
        button_combination.append(QStringLiteral("B+"));
    }
    if (state.x) {
        button_combination.append(QStringLiteral("X+"));
    }
    if (state.y) {
        button_combination.append(QStringLiteral("Y+"));
    }
    if (state.l || state.right_sl || state.left_sl) {
        button_combination.append(QStringLiteral("L+"));
    }
    if (state.r || state.right_sr || state.left_sr) {
        button_combination.append(QStringLiteral("R+"));
    }
    if (state.zl) {
        button_combination.append(QStringLiteral("ZL+"));
    }
    if (state.zr) {
        button_combination.append(QStringLiteral("ZR+"));
    }
    if (state.left) {
        button_combination.append(QStringLiteral("Dpad_Left+"));
    }
    if (state.right) {
        button_combination.append(QStringLiteral("Dpad_Right+"));
    }
    if (state.up) {
        button_combination.append(QStringLiteral("Dpad_Up+"));
    }
    if (state.down) {
        button_combination.append(QStringLiteral("Dpad_Down+"));
    }
    if (state.stick_l) {
        button_combination.append(QStringLiteral("Left_Stick+"));
    }
    if (state.stick_r) {
        button_combination.append(QStringLiteral("Right_Stick+"));
    }
    if (state.minus) {
        button_combination.append(QStringLiteral("Minus+"));
    }
    if (state.plus) {
        button_combination.append(QStringLiteral("Plus+"));
    }
    if (button_combination.isEmpty()) {
        return tr("Invalid");
    } else {
        button_combination.chop(1);
        return button_combination;
    }
}

std::pair<bool, QString> ConfigureHotkeys::IsUsedKey(QKeySequence key_sequence) const {
    for (int r = 0; r < model->rowCount(); ++r) {
        const QStandardItem* const parent = model->item(r, 0);

        for (int r2 = 0; r2 < parent->rowCount(); ++r2) {
            const QStandardItem* const key_seq_item = parent->child(r2, hotkey_column);
            const auto key_seq_str = key_seq_item->text();
            const auto key_seq = QKeySequence::fromString(key_seq_str, QKeySequence::NativeText);

            if (key_sequence == key_seq) {
                return std::make_pair(true, parent->child(r2, 0)->text());
            }
        }
    }

    return std::make_pair(false, QString());
}

std::pair<bool, QString> ConfigureHotkeys::IsUsedControllerKey(const QString& key_sequence) const {
    for (int r = 0; r < model->rowCount(); ++r) {
        const QStandardItem* const parent = model->item(r, 0);

        for (int r2 = 0; r2 < parent->rowCount(); ++r2) {
            const QStandardItem* const key_seq_item = parent->child(r2, controller_column);
            const auto key_seq_str = key_seq_item->text();

            if (key_sequence == key_seq_str) {
                return std::make_pair(true, parent->child(r2, 0)->text());
            }
        }
    }

    return std::make_pair(false, QString());
}

void ConfigureHotkeys::ApplyConfiguration() {
    // 1. Update the runtime HotkeyRegistry and UISettings
    const auto& root = model->invisibleRootItem();
    std::vector<UISettings::Shortcut> new_ui_shortcuts;

    for (int group_row = 0; group_row < root->rowCount(); group_row++) {
        const auto* group_item = root->child(group_row);
        const std::string group_name = group_item->data().toString().toStdString();

        for (int row = 0; row < group_item->rowCount(); row++) {
            const auto* action_item = group_item->child(row, name_column);
            const auto* keyseq_item = group_item->child(row, hotkey_column);
            const auto* controller_item = group_item->child(row, controller_column);

            const std::string action_name =
                action_item->data(Qt::UserRole).toString().toStdString();
            const QString keyseq_str = keyseq_item->text();
            const std::string controller_keyseq = controller_item->text().toStdString();
            const int context = action_item->data(Qt::UserRole + 1).toInt();
            const bool repeat = action_item->data(Qt::UserRole + 2).toBool();

            // Update Registry
            auto& hk = registry.hotkey_groups[group_name][action_name];
            hk.keyseq = QKeySequence::fromString(keyseq_str, QKeySequence::NativeText);
            hk.controller_keyseq = controller_keyseq;
            hk.context = static_cast<Qt::ShortcutContext>(context);
            hk.repeat = repeat;

            if (hk.shortcut) {
                hk.shortcut->setKey(hk.keyseq);
            }
            if (hk.controller_shortcut) {
                hk.controller_shortcut->SetKey(hk.controller_keyseq);
            }

            // Sync with UISettings::values.shortcuts (only if modified from default)
            // Actually, registry.SaveHotkeys() handles the "is_modified" check,
            // but we'll collect them here for completeness if needed or just call SaveHotkeys.
        }
    }

    // This will correctly populate UISettings::values.shortcuts based on current registry state
    registry.SaveHotkeys();

    // 2. Update the ProfileManager (Storage)
    const std::string current_profile_name = profile_manager.GetProfiles().current_profile;
    std::vector<Hotkey::BackendShortcut> new_shortcuts;

    for (int group_row = 0; group_row < root->rowCount(); group_row++) {
        const auto* group_item = root->child(group_row);
        const std::string group_name = group_item->data().toString().toStdString();

        for (int row = 0; row < group_item->rowCount(); row++) {
            const auto* action_item = group_item->child(row, name_column);
            const auto* keyseq_item = group_item->child(row, hotkey_column);
            const auto* controller_item = group_item->child(row, controller_column);

            Hotkey::BackendShortcut s;
            s.group = group_name;
            s.name = action_item->data(Qt::UserRole).toString().toStdString();
            s.shortcut.keyseq =
                QKeySequence::fromString(keyseq_item->text(), QKeySequence::NativeText)
                    .toString()
                    .toStdString();
            s.shortcut.controller_keyseq = controller_item->text().toStdString();
            s.shortcut.context = action_item->data(Qt::UserRole + 1).toInt();
            s.shortcut.repeat = action_item->data(Qt::UserRole + 2).toBool();

            new_shortcuts.push_back(s);
        }
    }

    profile_manager.SetProfileShortcuts(current_profile_name, new_shortcuts);
    profile_manager.Save();
}

void ConfigureHotkeys::RestoreDefaults() {
    size_t hotkey_index = 0;
    const size_t total_default_hotkeys = UISettings::default_hotkeys.size();

    for (int group_row = 0; group_row < model->rowCount(); ++group_row) {
        QStandardItem* parent = model->item(group_row, 0);

        for (int child_row = 0; child_row < parent->rowCount(); ++child_row) {
            // This bounds check prevents a crash.
            if (hotkey_index >= total_default_hotkeys) {
                QMessageBox::information(this, tr("Success!"),
                                         tr("Citron's Default hotkey entries have been restored!"));
                return;
            }

            const auto& default_shortcut = UISettings::default_hotkeys[hotkey_index].shortcut;

            parent->child(child_row, hotkey_column)
                ->setText(QString::fromStdString(default_shortcut.keyseq));
            parent->child(child_row, controller_column)
                ->setText(QString::fromStdString(default_shortcut.controller_keyseq));

            hotkey_index++;
        }
    }

    QMessageBox::information(this, tr("Success"), tr("Hotkeys have been restored to defaults."));
}

void ConfigureHotkeys::ClearAll() {
    for (int r = 0; r < model->rowCount(); ++r) {
        const QStandardItem* parent = model->item(r, 0);

        for (int r2 = 0; r2 < parent->rowCount(); ++r2) {
            model->item(r, 0)->child(r2, hotkey_column)->setText(QString{});
            model->item(r, 0)->child(r2, controller_column)->setText(QString{});
        }
    }
}

void ConfigureHotkeys::PopupContextMenu(const QPoint& menu_location) {
    QModelIndex index = ui->hotkey_list->indexAt(menu_location);
    if (!index.parent().isValid()) {
        return;
    }

    // Swap to the hotkey column if the controller hotkey column is not selected
    if (index.column() != controller_column) {
        index = index.sibling(index.row(), hotkey_column);
    }

    QMenu context_menu;

    QAction* restore_default = context_menu.addAction(tr("Restore Default"));
    QAction* clear = context_menu.addAction(tr("Clear"));

    connect(restore_default, &QAction::triggered, [this, index] {
        if (index.column() == controller_column) {
            RestoreControllerHotkey(index);
            return;
        }
        RestoreHotkey(index);
    });
    connect(clear, &QAction::triggered, [this, index] { model->setData(index, QString{}); });

    context_menu.exec(ui->hotkey_list->viewport()->mapToGlobal(menu_location));
}

void ConfigureHotkeys::RestoreControllerHotkey(QModelIndex index) {
    const auto* group_item = model->itemFromIndex(index.parent());
    const auto* action_item = group_item->child(index.row(), name_column);
    const std::string group_name = group_item->data().toString().toStdString();
    const std::string action_name = action_item->data().toString().toStdString();

    QString default_key_sequence;
    for (const auto& def : UISettings::default_hotkeys) {
        if (def.group == group_name && def.name == action_name) {
            default_key_sequence = QString::fromStdString(def.shortcut.controller_keyseq);
            break;
        }
    }

    const auto [key_sequence_used, used_action] = IsUsedControllerKey(default_key_sequence);

    if (key_sequence_used && default_key_sequence != model->data(index).toString()) {
        QMessageBox::warning(
            this, tr("Conflicting Button Sequence"),
            tr("The default button sequence is already assigned to: %1").arg(used_action));
    } else {
        model->setData(index, default_key_sequence);
    }
}

void ConfigureHotkeys::RestoreHotkey(QModelIndex index) {
    const auto* group_item = model->itemFromIndex(index.parent());
    const auto* action_item = group_item->child(index.row(), name_column);
    const std::string group_name = group_item->data().toString().toStdString();
    const std::string action_name = action_item->data().toString().toStdString();

    QString default_key_str;
    for (const auto& def : UISettings::default_hotkeys) {
        if (def.group == group_name && def.name == action_name) {
            default_key_str = QString::fromStdString(def.shortcut.keyseq);
            break;
        }
    }

    const QKeySequence& default_key_sequence =
        QKeySequence::fromString(default_key_str, QKeySequence::NativeText);
    const auto [key_sequence_used, used_action] = IsUsedKey(default_key_sequence);

    if (key_sequence_used && default_key_sequence != QKeySequence(model->data(index).toString())) {
        QMessageBox::warning(
            this, tr("Conflicting Key Sequence"),
            tr("The default key sequence is already assigned to: %1").arg(used_action));
    } else {
        model->setData(index, default_key_sequence.toString(QKeySequence::NativeText));
    }
}
