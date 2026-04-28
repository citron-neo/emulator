// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <memory>
#include <QApplication>
#include <QButtonGroup>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QMessageBox>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include "citron/configuration/configuration_shared.h"
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
#include "citron/configuration/configuration_styling.h"
#include "citron/configuration/style_animation_event_filter.h"
#include "citron/game_list.h"
#include "citron/hotkeys.h"
#include "citron/main.h"
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "citron/util/rainbow_style.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "ui_configure.h"
#include "vk_device_info.h"

static QScrollArea* CreateScrollArea(QWidget* widget) {
    auto* scroll_area = new QScrollArea();
    scroll_area->setWidget(widget);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    return scroll_area;
}

static bool DialogIsDarkMode() {
    const std::string& theme_name = UISettings::values.theme;
    if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
        theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
        return true;
    }
    if (theme_name == "default" || theme_name == "colorful") {
        return qApp->palette().color(QPalette::WindowText).value() >
               qApp->palette().color(QPalette::Window).value();
    }
    return false;
}

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)

    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
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
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(registry, system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {

    if (auto* main_window = qobject_cast<GMainWindow*>(parent)) {
        connect(filesystem_tab.get(), &ConfigureFilesystem::RequestGameListRefresh, main_window,
                &GMainWindow::RefreshGameList);
    }

    const bool is_gamescope = UISettings::IsGamescope();
    if (is_gamescope) {
        // GameScope: Use Window flags instead of Dialog to ensure mouse focus
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setWindowModality(Qt::NonModal);
    } else {
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                       Qt::WindowCloseButtonHint);
        setWindowModality(Qt::WindowModal);
    }

    ui->setupUi(this);
 
    animation_filter = new StyleAnimationEventFilter(this);
    
    // Explicitly list buttons to ensure correct order in the layout
    const std::vector<QPushButton*> ordered_buttons = {
        ui->generalTabButton, ui->uiTabButton,      ui->systemTabButton,
        ui->cpuTabButton,     ui->graphicsTabButton, ui->graphicsAdvancedTabButton,
        ui->audioTabButton,   ui->inputTabButton,    ui->hotkeysTabButton,
        ui->networkTabButton, ui->webTabButton,      ui->filesystemTabButton,
        ui->profilesTabButton, ui->appletsTabButton,  ui->loggingTabButton,
    };
    tab_buttons = ordered_buttons;

    for (QPushButton* button : tab_buttons) {
        button->setProperty("class", QStringLiteral("tabButton"));
        button->setParent(ui->topButtonWidget);
    }

    auto* nav_layout = new QVBoxLayout();
    nav_layout->setContentsMargins(10, 10, 10, 10);
    nav_layout->setSpacing(0);

    nav_layout->addStretch(10);
    for (size_t i = 0; i < tab_buttons.size(); ++i) {
        if (i > 0) {
            nav_layout->addStretch(1);
        }
        nav_layout->addWidget(tab_buttons[i]);
        tab_buttons[i]->installEventFilter(animation_filter);
    }
    nav_layout->addStretch(10);

    delete ui->topButtonWidget->layout();
    ui->topButtonWidget->setLayout(nav_layout);

    last_palette_text_color = qApp->palette().color(QPalette::WindowText);

    if (is_gamescope) {
        resize(1100, 700);
    } else if (!UISettings::values.configure_dialog_geometry.isEmpty()) {
        restoreGeometry(UISettings::values.configure_dialog_geometry);
    }

    UpdateTheme();

    tab_button_group = std::make_unique<QButtonGroup>(this);
    tab_button_group->setExclusive(true);
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

    ui->stackedWidget->addWidget(CreateScrollArea(general_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(ui_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(system_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(cpu_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(graphics_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(graphics_advanced_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(audio_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(input_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(hotkeys_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(network_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(web_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(filesystem_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(profile_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(applets_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(debug_tab_tab.get()));

    connect(tab_button_group.get(), qOverload<int>(&QButtonGroup::idClicked), this,
            &ConfigureDialog::SwitchTab);
    connect(ui_tab.get(), &ConfigureUi::themeChanged, this, &ConfigureDialog::UpdateTheme);
    connect(ui_tab.get(), &ConfigureUi::UIPositioningChanged, this,
            &ConfigureDialog::SetUIPositioning);
    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate();
    input_tab->Initialize(input_subsystem);
    general_tab->SetResetCallback([&] { this->close(); });
    SetConfiguration();
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    if (system.IsPoweredOn()) {
        if (auto* apply_button = ui->buttonBox->button(QDialogButtonBox::Apply)) {
            connect(apply_button, &QAbstractButton::clicked, this,
                    &ConfigureDialog::HandleApplyButtonClicked);
        }
    }
    ui->stackedWidget->setCurrentIndex(0);
    ui->generalTabButton->setChecked(true);

    if (UISettings::values.neo_ui_theme.GetValue() == "lightning") {
        animation_filter->triggerInitialState(ui->generalTabButton);
    }

    SetUIPositioning(QString::fromStdString(UISettings::values.ui_positioning.GetValue()));
}

ConfigureDialog::~ConfigureDialog() {
    UISettings::values.configure_dialog_geometry = saveGeometry();
}

void ConfigureDialog::UpdateTheme() {
    const bool is_rainbow = UISettings::values.enable_rainbow_mode.GetValue();
    const QString accent = Theme::GetAccentColor();
    const bool is_dark = DialogIsDarkMode();

    // Onyx Palette
    const QString bg = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#f5f5fa");
    const QString txt = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString sec = is_dark ? QStringLiteral("#2a2a32") : QStringLiteral("#ffffff");
    const QString ter = is_dark ? QStringLiteral("#32323a") : QStringLiteral("#dc dce2");
    const QString b_bg = is_dark ? QStringLiteral("#1e1e24") : QStringLiteral("#f0f0f5");
    const QString h_bg = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#e8e8ed");
    const QString f_bg = is_dark ? QStringLiteral("#3d3d47") : QStringLiteral("#ccccd4");
    const QString d_txt = is_dark ? QStringLiteral("#aaaab4") : QStringLiteral("#666670");

    // Use dark shadow on light backgrounds, light shadow on dark backgrounds
    const QString shadow_color =
        is_dark ? QStringLiteral("rgba(0, 0, 0, 0.5)") : QStringLiteral("rgba(255, 255, 255, 0.8)");

    QString style_sheet = ConfigurationStyling::GetMasterStyleSheet();
    QString full_style = style_sheet;
    full_style +=
        QStringLiteral("QDialog#ConfigureDialog { background-color: %1; color: %2; }").arg(bg, txt);
    setStyleSheet(full_style);
    ui->stackedWidget->setStyleSheet(style_sheet);

    QString sidebar_css =
        QStringLiteral(
            "QPushButton.tabButton { "
            "background-color: %1; "
            "color: %2; "
            "border: 2px solid transparent; "
            "text-align: left; "
            "padding: 8px 16px 8px 12px; "
            "font-size: 13px; "
            "outline: none; "
            "}"
            "QPushButton.tabButton:checked { "
            "color: %4; "
            "border: 2px solid %3; "
            "background-color: rgba(255, 255, 255, 10); "
            "}"
            "QPushButton.tabButton:hover { "
            "border: 2px solid %3; "
            "}")
            .arg(b_bg, d_txt, accent, txt);

    if (ui->topButtonWidget)
        ui->topButtonWidget->setStyleSheet(sidebar_css);
    if (ui->horizontalNavWidget)
        ui->horizontalNavWidget->setStyleSheet(sidebar_css);

    if (is_rainbow) {
        if (!rainbow_timer) {
            rainbow_timer = new QTimer(this);
            connect(rainbow_timer, &QTimer::timeout, this, [this, b_bg, d_txt, txt, shadow_color] {
                if (ui->buttonBox->underMouse() || !this->isVisible() ||
                    !this->isActiveWindow()) {
                    return;
                }

                const int current_index = ui->stackedWidget->currentIndex();
                const int input_tab_index = 7;

                const QColor current_color = RainbowStyle::GetCurrentHighlightColor();
                const QString hue_hex = current_color.name();
                const QString hue_light = current_color.lighter(125).name();
                const QString hue_dark = current_color.darker(150).name();

                QString rainbow_sidebar_css =
                    QStringLiteral("QPushButton.tabButton { "
                                   "background-color: %1; "
                                   "color: %2; "
                                   "border: 2px solid transparent; "
                                   "}"
                                   "QPushButton.tabButton:checked { "
                                   "color: %4; " // Use main text color for visibility
                                   "border: 2px solid %3; "
                                   "}"
                                   "QPushButton.tabButton:hover { "
                                   "border: 2px solid %3; "
                                   "}"
                                   "QPushButton.tabButton:pressed { "
                                   "background-color: %3; "
                                   "color: #ffffff; "
                                   "}")
                        .arg(b_bg, d_txt, hue_hex, txt);

                if (ui->topButtonWidget)
                    ui->topButtonWidget->setStyleSheet(rainbow_sidebar_css);
                if (ui->horizontalNavWidget)
                    ui->horizontalNavWidget->setStyleSheet(rainbow_sidebar_css);

                // Tab Content Area
                if (current_index == input_tab_index)
                    return;

                // Tab Content Area
                if (current_index == input_tab_index)
                    return;

                QWidget* currentContainer = ui->stackedWidget->currentWidget();
                if (currentContainer) {
                    // PERFORMANCE: Do not re-parse the full stylesheet 30 times a second.
                    // Instead, update a single dynamic property or use a simpler update.
                    // For now, we'll only update if the hue has changed significantly
                    // or just use a more efficient way to apply colors.
                    
                    static QString last_applied_hue;
                    if (last_applied_hue == hue_hex) return;
                    last_applied_hue = hue_hex;

                    QString tab_css =
                        QStringLiteral(
                            "QCheckBox::indicator:checked, QRadioButton::indicator:checked { "
                            "background-color: %1; border: 1px solid %1; }"
                            "QSlider::sub-page:horizontal { background: %1; border-radius: 4px; }"
                            "QSlider::handle:horizontal { background-color: %1; border: 1px solid "
                            "%1; width: 18px; height: 18px; margin: -5px 0; border-radius: 9px; }"
                            "QPushButton, QToolButton { background-color: transparent; color: %4; "
                            "border: 2px solid %1; border-radius: 4px; padding: 5px; }"
                            "QPushButton:hover, QToolButton:hover { border-color: %2; color: %2; }"
                            "QPushButton:pressed, QToolButton:pressed { background-color: %3; "
                            "color: #ffffff; border-color: %3; }")
                            .arg(hue_hex, hue_light, hue_dark, txt);
                    currentContainer->setStyleSheet(tab_css);
                    if (ui->buttonBox)
                        ui->buttonBox->setStyleSheet(tab_css);
                }
            });
        }
        rainbow_timer->start(100); // 10 FPS is plenty for smooth color transitions
    }

    if (UISettings::values.enable_rainbow_mode.GetValue() == false && rainbow_timer) {
        rainbow_timer->stop();

        if (ui->buttonBox)
            ui->buttonBox->setStyleSheet({});
        for (int i = 0; i < ui->stackedWidget->count(); ++i) {
            if (auto* w = ui->stackedWidget->widget(i)) {
                w->setStyleSheet({});
            }
        }
    }
}

void ConfigureDialog::SetUIPositioning(const QString& positioning) {
    auto* v_layout = qobject_cast<QVBoxLayout*>(ui->topButtonWidget->layout());
    auto* h_layout = qobject_cast<QHBoxLayout*>(ui->horizontalNavWidget->layout());

    if (!v_layout || !h_layout) {
        LOG_ERROR(Frontend, "Could not find navigation layouts to rearrange");
        return;
    }

    if (positioning == QStringLiteral("Horizontal")) {
        ui->nav_container->hide();
        ui->horizontalNavScrollArea->show();
        if (v_layout->count() > 0) {
            if (auto* item = v_layout->itemAt(v_layout->count() - 1); item && item->spacerItem()) {
                v_layout->takeAt(v_layout->count() - 1);
                delete item;
            }
        }
        for (QPushButton* button : tab_buttons) {
            v_layout->removeWidget(button);
            h_layout->addWidget(button);
        }
        h_layout->addStretch(1);

        if (!tab_buttons.empty()) {
            const int button_height = tab_buttons[0]->sizeHint().height();
            const int margins =
                h_layout->contentsMargins().top() + h_layout->contentsMargins().bottom();
            // The scroll area frame adds a few pixels, this accounts for it.
            const int fixed_height = button_height + margins + 4;
            ui->horizontalNavScrollArea->setMaximumHeight(fixed_height);
            ui->horizontalNavScrollArea->setMinimumHeight(fixed_height);
            ui->horizontalNavScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
        QSizePolicy policy = ui->topButtonWidget->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Preferred);
        ui->topButtonWidget->setSizePolicy(policy);

    } else { // Vertical
        ui->horizontalNavScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->horizontalNavScrollArea->setMaximumHeight(QWIDGETSIZE_MAX);
        ui->horizontalNavScrollArea->setMinimumHeight(0);

        ui->horizontalNavScrollArea->hide();
        ui->nav_container->show();
        
        // Clear all items from layouts to avoid duplicates or orphaned stretches
        auto clear_layout = [](QLayout* layout) {
            if (!layout) return;
            QLayoutItem* item;
            while ((item = layout->takeAt(0)) != nullptr) {
                if (item->widget()) {
                    item->widget()->setParent(nullptr);
                }
                delete item;
            }
        };

        clear_layout(h_layout);
        clear_layout(v_layout);

        v_layout->setSpacing(0);
        v_layout->addStretch(10);

        for (size_t i = 0; i < tab_buttons.size(); ++i) {
            if (i > 0) {
                v_layout->addStretch(1);
            }
            v_layout->addWidget(tab_buttons[i]);
        }
        v_layout->addStretch(10);

        QSizePolicy policy = ui->topButtonWidget->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Expanding);
        ui->topButtonWidget->setSizePolicy(policy);
    }
}

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration();
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
    if (event->type() == QEvent::PaletteChange) {
        if (qApp->palette().color(QPalette::WindowText) != last_palette_text_color) {
            last_palette_text_color = qApp->palette().color(QPalette::WindowText);
            UpdateTheme();
        }
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
    UISettings::values.is_game_list_reload_pending = true;
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::SwitchTab(int id) {
    const bool lightning_enabled = UISettings::values.neo_ui_theme.GetValue() == "lightning";

    if (animation_filter && tab_button_group && lightning_enabled) {
        QPushButton* from_button = qobject_cast<QPushButton*>(tab_button_group->button(ui->stackedWidget->currentIndex()));
        QPushButton* to_button = qobject_cast<QPushButton*>(tab_button_group->button(id));

        if (to_button) {
            // Restore the sidebar "volt" animation
            animation_filter->triggerElectrification(from_button, to_button);

            // Trigger the massive Thunderstrike on the right half of the dialog window
            animation_filter->triggerPageLightning(this, QPoint(width() * 0.65, 0));
        }
    }
    ui->stackedWidget->setCurrentIndex(id);
}
