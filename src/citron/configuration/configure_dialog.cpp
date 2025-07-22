// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QApplication>
#include <QButtonGroup>
#include <QScrollArea>
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "ui_configure.h"
#include "vk_device_info.h"
#include "citron/configuration/configure_applets.h"
#include "citron/configuration/configure_audio.h"
#include "citron/configuration/configure_cpu.h"
#include "citron/configuration/configure_debug_tab.h"
#include "citron/configuration/configure_dialog.h"
#include "citron/configuration/configure_filesystem.h"
#include "citron/configuration/configure_general.h"
#include "citron/configuration/configure_graphics.h"
#include "citron/configuration/configure_graphics_advanced.h"
#include "citron/configuration/configure_hotkeys.h"
#include "citron/configuration/configure_input.h"
#include "citron/configuration/configure_input_player.h"
#include "citron/configuration/configure_network.h"
#include "citron/configuration/configure_profile_manager.h"
#include "citron/configuration/configure_system.h"
#include "citron/configuration/configure_ui.h"
#include "citron/configuration/configure_web.h"
#include "citron/hotkeys.h"
#include "citron/uisettings.h"

// Helper function to create a scroll area for a widget
QScrollArea* CreateScrollArea(QWidget* widget) {
    auto* scroll_area = new QScrollArea();
    scroll_area->setWidget(widget);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // High DPI support: Scroll area will inherit scaling from parent

    // Set style with high DPI aware styling
    scroll_area->setStyleSheet(QLatin1String(
        "QScrollArea { "
        "border: none; "
        "background-color: #2b2b2b; "
        "}"
        "QScrollArea > QWidget > QWidget { "
        "background-color: #2b2b2b; "
        "}"
        "QScrollBar:vertical { "
        "background-color: #3d3d3d; "
        "width: 14px; "
        "border-radius: 7px; "
        "margin: 2px; "
        "}"
        "QScrollBar::handle:vertical { "
        "background-color: #5d5d5d; "
        "border-radius: 6px; "
        "min-height: 30px; "
        "margin: 1px; "
        "}"
        "QScrollBar::handle:vertical:hover { "
        "background-color: #4a9eff; "
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "border: none; "
        "background: none; "
        "height: 0px; "
        "}"
        "QScrollBar:horizontal { "
        "background-color: #3d3d3d; "
        "height: 14px; "
        "border-radius: 7px; "
        "margin: 2px; "
        "}"
        "QScrollBar::handle:horizontal { "
        "background-color: #5d5d5d; "
        "border-radius: 6px; "
        "min-width: 30px; "
        "margin: 1px; "
        "}"
        "QScrollBar::handle:horizontal:hover { "
        "background-color: #4a9eff; "
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { "
        "border: none; "
        "background: none; "
        "width: 0px; "
        "}"
    ));

    return scroll_area;
}

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()},
      registry(registry_), system{system_}, builder{std::make_unique<ConfigurationShared::Builder>(
                                                this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    // Set window flags to include maximize button and make it resizable
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    // High DPI support: Set proper attributes for scaling
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_DontShowOnScreen, false);

    ui->setupUi(this);

    // Set size policy and enable resizing
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Get screen geometry and set to fullscreen with high DPI awareness
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();

        // Calculate logical size based on device pixel ratio for high DPI support
        qreal devicePixelRatio = screen->devicePixelRatio();
        int logicalWidth = static_cast<int>(screenGeometry.width() / devicePixelRatio);
        int logicalHeight = static_cast<int>(screenGeometry.height() / devicePixelRatio);

        // Set geometry using logical units
        setGeometry(0, 0, logicalWidth, logicalHeight);
        showMaximized(); // Start maximized/fullscreen
    }

    // Create button group for exclusive tab selection
    tab_button_group = std::make_unique<QButtonGroup>(this);
    tab_button_group->setExclusive(true);

    // Add tab buttons to the button group and connect to stacked widget
    tab_button_group->addButton(ui->generalTabButton, 0);
    tab_button_group->addButton(ui->uiTabButton, 1);
    tab_button_group->addButton(ui->systemTabButton, 2);
    tab_button_group->addButton(ui->cpuTabButton, 3);
    tab_button_group->addButton(ui->graphicsTabButton, 4);
    tab_button_group->addButton(ui->graphicsAdvancedTabButton, 5);
    tab_button_group->addButton(ui->audioTabButton, 6);
    tab_button_group->addButton(ui->inputTabButton, 7);
    tab_button_group->addButton(ui->hotkeysTabButton, 8);
    tab_button_group->addButton(ui->networkTabButton, 9);
    tab_button_group->addButton(ui->webTabButton, 10);
    tab_button_group->addButton(ui->filesystemTabButton, 11);
    tab_button_group->addButton(ui->profilesTabButton, 12);
    tab_button_group->addButton(ui->appletsTabButton, 13);
    tab_button_group->addButton(ui->loggingTabButton, 14);

    // Add pages to stacked widget wrapped in scroll areas in the same order as button group
    ui->stackedWidget->addWidget(CreateScrollArea(general_tab.get()));   // 0
    ui->stackedWidget->addWidget(CreateScrollArea(ui_tab.get()));        // 1
    ui->stackedWidget->addWidget(CreateScrollArea(system_tab.get()));    // 2
    ui->stackedWidget->addWidget(CreateScrollArea(cpu_tab.get()));       // 3
    ui->stackedWidget->addWidget(CreateScrollArea(graphics_tab.get()));  // 4
    ui->stackedWidget->addWidget(CreateScrollArea(graphics_advanced_tab.get())); // 5
    ui->stackedWidget->addWidget(CreateScrollArea(audio_tab.get()));     // 6
    ui->stackedWidget->addWidget(CreateScrollArea(input_tab.get()));     // 7
    ui->stackedWidget->addWidget(CreateScrollArea(hotkeys_tab.get()));   // 8
    ui->stackedWidget->addWidget(CreateScrollArea(network_tab.get()));   // 9
    ui->stackedWidget->addWidget(CreateScrollArea(web_tab.get()));       // 10
    ui->stackedWidget->addWidget(CreateScrollArea(filesystem_tab.get()));// 11
    ui->stackedWidget->addWidget(CreateScrollArea(profile_tab.get()));   // 12
    ui->stackedWidget->addWidget(CreateScrollArea(applets_tab.get()));   // 13
    ui->stackedWidget->addWidget(CreateScrollArea(debug_tab_tab.get())); // 14

    // Connect button group to stacked widget
    connect(tab_button_group.get(), QOverload<int>::of(&QButtonGroup::idClicked),
            [this](int id) {
                ui->stackedWidget->setCurrentIndex(id);
                if (id == 14) { // Logging tab
                    debug_tab_tab->SetCurrentIndex(0);
                }
            });

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);

    input_tab->Initialize(input_subsystem);

    general_tab->SetResetCallback([&] { this->close(); });

    SetConfiguration();

    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->button(QDialogButtonBox::Apply);
        if (apply_button) {
            connect(apply_button, &QAbstractButton::clicked, this,
                    &ConfigureDialog::HandleApplyButtonClicked);
        }
    }

    // Set initial tab to General (index 0)
    ui->stackedWidget->setCurrentIndex(0);
    ui->generalTabButton->setChecked(true);

    // Focus on the OK button by default
    ui->buttonBox->setFocus();
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    const int old_index = ui->stackedWidget->currentIndex();

    ui->retranslateUi(this);

    SetConfiguration();
    ui->stackedWidget->setCurrentIndex(old_index);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    // Reloading the game list is needed to force retranslation.
    UISettings::values.is_game_list_reload_pending = true;
    // first apply the configuration, and then restore the display
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}
