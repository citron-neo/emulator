#pragma once

#include <memory>
#include <QStandardItemModel>
#include <QWidget>
#include "citron/hotkey_profile_manager.h"

namespace Common {
class ParamPackage;
}

namespace Core::HID {
class HIDCore;
class EmulatedController;
enum class NpadButton : u64;
} // namespace Core::HID

namespace Ui {
class ConfigureHotkeys;
}

class HotkeyRegistry;
class QStandardItemModel;

class ConfigureHotkeys : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureHotkeys(HotkeyRegistry& registry, Core::HID::HIDCore& hid_core_,
                              QWidget* parent = nullptr);
    ~ConfigureHotkeys() override;

    void ApplyConfiguration();

    /**
     * Populates the hotkey list widget using data from the provided profiles.
     * Called every time the Configure dialog is opened.
     * @param profiles The UserHotkeyProfiles used to populate the list.
     */
    void Populate(const std::string& profile_name = "");

private slots:
    void OnCreateProfile();
    void OnDeleteProfile();
    void OnRenameProfile();
    void OnImportProfile();
    void OnExportProfile();
    void OnProfileChanged(int index);

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();
    void UpdateProfileList();

    void Configure(QModelIndex index);
    void ConfigureController(QModelIndex index);
    std::pair<bool, QString> IsUsedKey(QKeySequence key_sequence) const;
    std::pair<bool, QString> IsUsedControllerKey(const QString& key_sequence) const;

    void RestoreDefaults();
    void ClearAll();
    void PopupContextMenu(const QPoint& menu_location);
    void RestoreControllerHotkey(QModelIndex index);
    void RestoreHotkey(QModelIndex index);

    std::vector<Hotkey::BackendShortcut> GatherShortcutsFromUI() const;
    void ApplyShortcutsToModel(const std::vector<Hotkey::BackendShortcut>& shortcuts);

    void SetPollingResult(bool cancel);
    QString GetButtonCombinationName(Core::HID::NpadButton button, bool home, bool capture) const;

    std::unique_ptr<Ui::ConfigureHotkeys> ui;
    Hotkey::ProfileManager profile_manager;
    HotkeyRegistry& registry;

    QStandardItemModel* model;

    bool pressed_home_button;
    bool pressed_capture_button;
    QModelIndex button_model_index;
    Core::HID::NpadButton pressed_buttons;

    Core::HID::EmulatedController* controller;
    std::unique_ptr<QTimer> timeout_timer;
    std::unique_ptr<QTimer> poll_timer;
    std::optional<std::function<void(bool)>> input_setter;
};
