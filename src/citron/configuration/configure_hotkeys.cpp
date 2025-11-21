// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QMenu>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QTimer>

#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"

#include "frontend_common/config.h"
#include "ui_configure_hotkeys.h"
#include "citron/configuration/configure_hotkeys.h"
#include "citron/hotkeys.h"
#include "citron/uisettings.h"
#include "citron/util/sequence_dialog/sequence_dialog.h"

constexpr int name_column = 0;
constexpr int hotkey_column = 1;
constexpr int controller_column = 2;

ConfigureHotkeys::ConfigureHotkeys(Core::HID::HIDCore& hid_core, QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureHotkeys>()),
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
    ui->hotkey_list->header()->setSectionResizeMode(name_column, QHeaderView::ResizeMode::Stretch);
    ui->hotkey_list->header()->setMinimumSectionSize(150);

    connect(ui->button_restore_defaults, &QPushButton::clicked, this,
            &ConfigureHotkeys::RestoreDefaults);
    connect(ui->button_clear_all, &QPushButton::clicked, this, &ConfigureHotkeys::ClearAll);

    controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);

    connect(timeout_timer.get(), &QTimer::timeout, [this] {
        const bool is_button_pressed = pressed_buttons != Core::HID::NpadButton::None ||
                                       pressed_home_button || pressed_capture_button;
        SetPollingResult(!is_button_pressed);
    });

    connect(poll_timer.get(), &QTimer::timeout, [this] {
        pressed_buttons |= controller->GetNpadButtons().raw;
        pressed_home_button |= this->controller->GetHomeButtons().home != 0;
        pressed_capture_button |= this->controller->GetCaptureButtons().capture != 0;
        if (pressed_buttons != Core::HID::NpadButton::None || pressed_home_button ||
            pressed_capture_button) {
            const QString button_name =
                GetButtonCombinationName(pressed_buttons, pressed_home_button,
                                         pressed_capture_button) +
                QStringLiteral("...");
            model->setData(button_model_index, button_name);
        }
    });
    RetranslateUI();
}

ConfigureHotkeys::~ConfigureHotkeys() = default;

void ConfigureHotkeys::Populate(const HotkeyRegistry& registry) {
    for (const auto& group : registry.hotkey_groups) {
        QString parent_item_data = QString::fromStdString(group.first);
        auto* parent_item =
            new QStandardItem(QCoreApplication::translate("Hotkeys", qPrintable(parent_item_data)));
        parent_item->setEditable(false);
        parent_item->setData(parent_item_data);
        for (const auto& hotkey : group.second) {
            QString hotkey_action_data = QString::fromStdString(hotkey.first);
            auto* action = new QStandardItem(
                QCoreApplication::translate("Hotkeys", qPrintable(hotkey_action_data)));
            auto* keyseq =
                new QStandardItem(hotkey.second.keyseq.toString(QKeySequence::NativeText));
            auto* controller_keyseq =
                new QStandardItem(QString::fromStdString(hotkey.second.controller_keyseq));
            action->setEditable(false);
            action->setData(hotkey_action_data);
            keyseq->setEditable(false);
            controller_keyseq->setEditable(false);
            parent_item->appendRow({action, keyseq, controller_keyseq});
        }
        model->appendRow(parent_item);
    }

    ui->hotkey_list->expandAll();
    ui->hotkey_list->resizeColumnToContents(hotkey_column);
    ui->hotkey_list->resizeColumnToContents(controller_column);
}

void ConfigureHotkeys::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureHotkeys::RetranslateUI() {
    ui->retranslateUi(this);

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

void ConfigureHotkeys::ApplyConfiguration(HotkeyRegistry& registry) {
    for (int key_id = 0; key_id < model->rowCount(); key_id++) {
        const QStandardItem* parent = model->item(key_id, 0);
        for (int key_column_id = 0; key_column_id < parent->rowCount(); key_column_id++) {
            const QStandardItem* action = parent->child(key_column_id, name_column);
            const QStandardItem* keyseq = parent->child(key_column_id, hotkey_column);
            const QStandardItem* controller_keyseq =
                parent->child(key_column_id, controller_column);
            for (auto& [group, sub_actions] : registry.hotkey_groups) {
                if (group != parent->data().toString().toStdString())
                    continue;
                for (auto& [action_name, hotkey] : sub_actions) {
                    if (action_name != action->data().toString().toStdString())
                        continue;
                    hotkey.keyseq = QKeySequence(keyseq->text());
                    hotkey.controller_keyseq = controller_keyseq->text().toStdString();
                }
            }
        }
    }

    registry.SaveHotkeys();
}

void ConfigureHotkeys::RestoreDefaults() {
    size_t hotkey_index = 0;
    const size_t total_default_hotkeys = UISettings::default_hotkeys.size();

    for (int group_row = 0; group_row < model->rowCount(); ++group_row) {
        QStandardItem* parent = model->item(group_row, 0);

        for (int child_row = 0; child_row < parent->rowCount(); ++child_row) {
            // This bounds check prevents a crash, and this was originally a safety check w/ showed if it failed,
            // however with further testing w/ restoring default functionality, it would work yet still display, so was changed to a regular Success!.
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
    const QString& default_key_sequence =
        QString::fromStdString(UISettings::default_hotkeys[index.row()].shortcut.controller_keyseq);
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
    const QKeySequence& default_key_sequence = QKeySequence::fromString(
        QString::fromStdString(UISettings::default_hotkeys[index.row()].shortcut.keyseq),
        QKeySequence::NativeText);
    const auto [key_sequence_used, used_action] = IsUsedKey(default_key_sequence);

    if (key_sequence_used && default_key_sequence != QKeySequence(model->data(index).toString())) {
        QMessageBox::warning(
            this, tr("Conflicting Key Sequence"),
            tr("The default key sequence is already assigned to: %1").arg(used_action));
    } else {
        model->setData(index, default_key_sequence.toString(QKeySequence::NativeText));
    }
}
