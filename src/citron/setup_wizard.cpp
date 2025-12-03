// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/setup_wizard.h"
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/acc/profile_manager.h"
#include "frontend_common/content_manager.h"
#include "ui_setup_wizard.h"
#include "citron/configuration/configure_input.h"
#include "citron/main.h"
#include "citron/theme.h"
#include "citron/uisettings.h"

#ifdef CITRON_ENABLE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

// Helper function to detect if the application is using a dark theme
static bool IsDarkMode() {
    const std::string& theme_name = UISettings::values.theme;

    if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
        theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
        return true;
    }

    if (theme_name == "default" || theme_name == "colorful") {
        const QPalette palette = qApp->palette();
        const QColor text_color = palette.color(QPalette::WindowText);
        const QColor base_color = palette.color(QPalette::Window);
        return text_color.value() > base_color.value();
    }

    return false;
}

SetupWizard::SetupWizard(Core::System& system_, GMainWindow* main_window_, QWidget* parent)
    : QDialog(parent), ui{std::make_unique<Ui::SetupWizard>()}, system{system_},
      main_window{main_window_}, current_page{0}, is_portable_mode{false},
      profile_name{QStringLiteral("citron")}, firmware_installed{false} {
    ui->setupUi(this);

    setWindowTitle(tr("citron Setup Wizard"));

    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
                   Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    setWindowModality(Qt::NonModal);

    sidebar_list = ui->sidebarList;
    content_stack = ui->contentStack;
    back_button = ui->backButton;
    next_button = ui->nextButton;
    cancel_button = ui->cancelButton;

    SetupPages();

    connect(back_button, &QPushButton::clicked, this, &SetupWizard::OnBackClicked);
    connect(next_button, &QPushButton::clicked, this, &SetupWizard::OnNextClicked);
    connect(cancel_button, &QPushButton::clicked, this, &SetupWizard::OnCancelClicked);
    connect(sidebar_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int index = sidebar_list->row(item);
        if (index >= 0 && index < content_stack->count()) {
            current_page = index;
            content_stack->setCurrentIndex(index);
            UpdateNavigationButtons();
        }
    });

    current_page = 0;
    content_stack->setCurrentIndex(0);
    UpdateNavigationButtons();

    last_palette_text_color = qApp->palette().color(QPalette::WindowText);
    UpdateTheme();
}

SetupWizard::~SetupWizard() = default;

void SetupWizard::SetupPages() {
    // Welcome page
    auto* welcome_page = new QWidget();
    auto* welcome_layout = new QVBoxLayout(welcome_page);
    welcome_layout->setContentsMargins(40, 40, 40, 40);
    welcome_layout->setSpacing(20);

    auto* welcome_title = new QLabel(tr("Welcome to citron Setup Wizard"));
    welcome_title->setProperty("class", QStringLiteral("wizard-title"));
    welcome_layout->addWidget(welcome_title);

    auto* welcome_text = new QLabel(tr("This wizard will help you configure citron for first-time use.\n"
                                       "You'll be able to set up keys, firmware, game directories, and more."));
    welcome_text->setWordWrap(true);
    welcome_layout->addWidget(welcome_text);
    welcome_layout->addStretch();
    content_stack->addWidget(welcome_page);
    sidebar_list->addItem(tr("Welcome"));

    // Installation type page
    auto* install_page = new QWidget();
    auto* install_layout = new QVBoxLayout(install_page);
    install_layout->setContentsMargins(40, 40, 40, 40);
    install_layout->setSpacing(20);

    auto* install_title = new QLabel(tr("Installation Type"));
    install_title->setProperty("class", QStringLiteral("wizard-title"));
    install_layout->addWidget(install_title);

    auto* install_subtitle = new QLabel(tr("Choose how you want to store citron's data:"));
    install_layout->addWidget(install_subtitle);

    auto* install_group = new QGroupBox();
    auto* install_group_layout = new QVBoxLayout(install_group);
    auto* button_group = new QButtonGroup(this);
    auto* portable_radio = new QRadioButton(tr("Portable (creates 'user' folder in executable directory)"));

    QString standard_path_text;
#ifdef _WIN32
    const auto appdata_path = Common::FS::GetAppDataRoamingDirectory();
    const auto appdata_str = QString::fromStdString(Common::FS::PathToUTF8String(appdata_path));
    standard_path_text = tr("Standard (uses %APPDATA%\\citron)").arg(appdata_str);
#elif defined(__APPLE__)
    standard_path_text = tr("Standard (uses ~/Library/Application Support/citron)");
#else
    const auto data_path = Common::FS::GetDataDirectory("XDG_DATA_HOME");
    const auto data_path_str = QString::fromStdString(Common::FS::PathToUTF8String(data_path));
    standard_path_text = tr("Standard (uses %1/citron)").arg(data_path_str);
#endif
    auto* standard_radio = new QRadioButton(standard_path_text);
    standard_radio->setChecked(true);
    button_group->addButton(portable_radio, 0);
    button_group->addButton(standard_radio, 1);
    install_group_layout->addWidget(portable_radio);
    install_group_layout->addWidget(standard_radio);
    install_layout->addWidget(install_group);
    install_layout->addStretch();
    connect(portable_radio, &QRadioButton::toggled, this, [this](bool checked) { if (checked) is_portable_mode = true; });
    connect(standard_radio, &QRadioButton::toggled, this, [this](bool checked) { if (checked) is_portable_mode = false; });
    content_stack->addWidget(install_page);
    sidebar_list->addItem(tr("Installation Type"));

    // Keys page
    auto* keys_page = new QWidget();
    auto* keys_layout = new QVBoxLayout(keys_page);
    keys_layout->setContentsMargins(40, 40, 40, 40);
    keys_layout->setSpacing(20);
    auto* keys_title = new QLabel(tr("Decryption Keys"));
    keys_title->setProperty("class", QStringLiteral("wizard-title"));
    keys_layout->addWidget(keys_title);
    auto* keys_text = new QLabel(tr("Decryption keys are required to run encrypted games.\nSelect your prod.keys file to install them."));
    keys_text->setWordWrap(true);
    keys_layout->addWidget(keys_text);
    auto* keys_button = new QPushButton(tr("Select Keys File"));
    connect(keys_button, &QPushButton::clicked, this, &SetupWizard::OnSelectKeys);
    keys_layout->addWidget(keys_button);
    auto* keys_status = new QLabel();
    keys_layout->addWidget(keys_status);
    if (CheckKeysInstalled()) {
        keys_status->setText(tr("✓ Keys are installed"));
        keys_status->setProperty("class", QStringLiteral("wizard-status-success"));
    } else {
        keys_status->setText(tr("Keys not installed"));
        keys_status->setProperty("class", QStringLiteral("wizard-status-default"));
    }
    keys_layout->addStretch();
    content_stack->addWidget(keys_page);
    sidebar_list->addItem(tr("Keys"));

    // Firmware page
    auto* firmware_page = new QWidget();
    auto* firmware_layout = new QVBoxLayout(firmware_page);
    firmware_layout->setContentsMargins(40, 40, 40, 40);
    firmware_layout->setSpacing(20);
    auto* firmware_title = new QLabel(tr("Firmware"));
    firmware_title->setProperty("class", QStringLiteral("wizard-title"));
    firmware_layout->addWidget(firmware_title);
    auto* firmware_text = new QLabel(tr("Firmware is required to run system applications and some games.\nYou can install it from a ZIP file or a folder containing NCA files."));
    firmware_text->setWordWrap(true);
    firmware_layout->addWidget(firmware_text);
    auto* firmware_button = new QPushButton(tr("Install Firmware"));
    connect(firmware_button, &QPushButton::clicked, this, &SetupWizard::OnSelectFirmware);
    firmware_layout->addWidget(firmware_button);
    auto* firmware_status = new QLabel();
    firmware_layout->addWidget(firmware_status);
    if (CheckFirmwareInstalled() || firmware_installed) {
        firmware_status->setText(tr("✓ Firmware is installed"));
        firmware_status->setProperty("class", QStringLiteral("wizard-status-success"));
    } else {
        firmware_status->setText(tr("Firmware not installed (optional)"));
        firmware_status->setProperty("class", QStringLiteral("wizard-status-default"));
    }
    firmware_layout->addStretch();
    content_stack->addWidget(firmware_page);
    sidebar_list->addItem(tr("Firmware"));

    // Games directory page
    auto* games_page = new QWidget();
    auto* games_layout = new QVBoxLayout(games_page);
    games_layout->setContentsMargins(40, 40, 40, 40);
    games_layout->setSpacing(20);
    auto* games_title = new QLabel(tr("Games Directory"));
    games_title->setProperty("class", QStringLiteral("wizard-title"));
    games_layout->addWidget(games_title);
    auto* games_text = new QLabel(tr("Select the directory where your game files are located."));
    games_text->setWordWrap(true);
    games_layout->addWidget(games_text);
    auto* games_path_layout = new QHBoxLayout();
    auto* games_path_edit = new QLineEdit();
    games_path_edit->setReadOnly(true);
    games_path_edit->setPlaceholderText(tr("No directory selected"));
    if (!games_directory.isEmpty()) {
        games_path_edit->setText(games_directory);
    }
    games_path_layout->addWidget(games_path_edit);
    auto* games_button = new QPushButton(tr("Browse..."));
    connect(games_button, &QPushButton::clicked, this, [this, games_path_edit]() {
        OnSelectGamesDirectory();
        games_path_edit->setText(games_directory);
    });
    games_path_layout->addWidget(games_button);
    games_layout->addLayout(games_path_layout);
    games_layout->addStretch();
    content_stack->addWidget(games_page);
    sidebar_list->addItem(tr("Games Directory"));

    // Paths page (screenshots)
    auto* paths_page = new QWidget();
    auto* paths_layout = new QVBoxLayout(paths_page);
    paths_layout->setContentsMargins(40, 40, 40, 40);
    paths_layout->setSpacing(20);
    auto* paths_title = new QLabel(tr("Paths"));
    paths_title->setProperty("class", QStringLiteral("wizard-title"));
    paths_layout->addWidget(paths_title);
    auto* paths_text = new QLabel(tr("Configure additional paths for screenshots and other files."));
    paths_text->setWordWrap(true);
    paths_layout->addWidget(paths_text);
    auto* screenshots_label = new QLabel(tr("Screenshots Directory:"));
    paths_layout->addWidget(screenshots_label);
    auto* screenshots_path_layout = new QHBoxLayout();
    auto* screenshots_path_edit = new QLineEdit();
    screenshots_path_edit->setReadOnly(true);
    screenshots_path_edit->setPlaceholderText(tr("Default location"));
    if (!screenshots_path.isEmpty()) {
        screenshots_path_edit->setText(screenshots_path);
    }
    screenshots_path_layout->addWidget(screenshots_path_edit);
    auto* screenshots_button = new QPushButton(tr("Browse..."));
    connect(screenshots_button, &QPushButton::clicked, this, [this, screenshots_path_edit]() {
        OnSelectScreenshotsPath();
        screenshots_path_edit->setText(screenshots_path);
    });
    screenshots_path_layout->addWidget(screenshots_button);
    paths_layout->addLayout(screenshots_path_layout);
    paths_layout->addStretch();
    content_stack->addWidget(paths_page);
    sidebar_list->addItem(tr("Paths"));

    // Profile page
    auto* profile_page = new QWidget();
    auto* profile_layout = new QVBoxLayout(profile_page);
    profile_layout->setContentsMargins(40, 40, 40, 40);
    profile_layout->setSpacing(20);
    auto* profile_title = new QLabel(tr("Profile Name"));
    profile_title->setProperty("class", QStringLiteral("wizard-title"));
    profile_layout->addWidget(profile_title);
    auto* profile_text = new QLabel(tr("Set your profile name (default: 'citron')."));
    profile_text->setWordWrap(true);
    profile_layout->addWidget(profile_text);
    auto* profile_edit = new QLineEdit();
    profile_edit->setPlaceholderText(tr("citron"));
    profile_edit->setText(profile_name);
    connect(profile_edit, &QLineEdit::textChanged, this, [this](const QString& text) { profile_name = text; });
    profile_layout->addWidget(profile_edit);
    profile_layout->addStretch();
    content_stack->addWidget(profile_page);
    sidebar_list->addItem(tr("Profile"));

    // Controller page
    auto* controller_page = new QWidget();
    auto* controller_layout = new QVBoxLayout(controller_page);
    controller_layout->setContentsMargins(40, 40, 40, 40);
    controller_layout->setSpacing(20);
    auto* controller_title = new QLabel(tr("Controller Setup"));
    controller_title->setProperty("class", QStringLiteral("wizard-title"));
    controller_layout->addWidget(controller_title);
    auto* controller_text = new QLabel(tr("You can configure your controller after setup is complete.\nGo to Settings > Configure > Controls to set up your controller."));
    controller_text->setWordWrap(true);
    controller_layout->addWidget(controller_text);
    auto* controller_button = new QPushButton(tr("Open Controller Settings"));
    connect(controller_button, &QPushButton::clicked, this, &SetupWizard::OnControllerSetup);
    controller_layout->addWidget(controller_button);
    controller_layout->addStretch();
    content_stack->addWidget(controller_page);
    sidebar_list->addItem(tr("Controller"));

    // Completion page
    auto* completion_page = new QWidget();
    auto* completion_layout = new QVBoxLayout(completion_page);
    completion_layout->setContentsMargins(40, 40, 40, 40);
    completion_layout->setSpacing(20);
    auto* completion_title = new QLabel(tr("Setup Complete!"));
    completion_title->setProperty("class", QStringLiteral("wizard-title"));
    completion_layout->addWidget(completion_title);
    auto* completion_text = new QLabel(tr("You have completed the setup wizard.\nClick Finish to apply your settings and start using citron."));
    completion_text->setWordWrap(true);
    completion_layout->addWidget(completion_text);
    completion_layout->addStretch();
    content_stack->addWidget(completion_page);
    sidebar_list->addItem(tr("Complete"));
}

void SetupWizard::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        const QColor current_color = qApp->palette().color(QPalette::WindowText);
        if (current_color != last_palette_text_color) {
            last_palette_text_color = current_color;
            UpdateTheme();
        }
    }
    QDialog::changeEvent(event);
}

void SetupWizard::UpdateTheme() {
    const bool is_dark = IsDarkMode();
    const QString bg_color = is_dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f5f5f5");
    const QString text_color = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString secondary_bg_color = is_dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#e9e9e9");
    const QString tertiary_bg_color = is_dark ? QStringLiteral("#3d3d3d") : QStringLiteral("#dcdcdc");
    const QString button_bg_color = is_dark ? QStringLiteral("#383838") : QStringLiteral("#e1e1e1");
    const QString hover_bg_color = is_dark ? QStringLiteral("#4d4d4d") : QStringLiteral("#e8f0fe");
    const QString disabled_text_color = is_dark ? QStringLiteral("#666666") : QStringLiteral("#a0a0a0");
    QString style_sheet = property("templateStyleSheet").toString();
    style_sheet.replace(QStringLiteral("%%BACKGROUND_COLOR%%"), bg_color);
    style_sheet.replace(QStringLiteral("%%TEXT_COLOR%%"), text_color);
    style_sheet.replace(QStringLiteral("%%SECONDARY_BG_COLOR%%"), secondary_bg_color);
    style_sheet.replace(QStringLiteral("%%TERTIARY_BG_COLOR%%"), tertiary_bg_color);
    style_sheet.replace(QStringLiteral("%%BUTTON_BG_COLOR%%"), button_bg_color);
    style_sheet.replace(QStringLiteral("%%HOVER_BG_COLOR%%"), hover_bg_color);
    style_sheet.replace(QStringLiteral("%%DISABLED_TEXT_COLOR%%"), disabled_text_color);
    setStyleSheet(style_sheet);
}

void SetupWizard::OnPageChanged(int index) {
    if (index >= 0 && index < content_stack->count()) {
        content_stack->setCurrentIndex(index);
        current_page = index;
        UpdateNavigationButtons();
        sidebar_list->setCurrentRow(index);
    }
}

void SetupWizard::OnNextClicked() {
    if (ValidateCurrentPage()) {
        if (current_page < content_stack->count() - 1) {
            current_page++;
            OnPageChanged(current_page);
        } else {
            ApplyConfiguration();
            accept();
        }
    }
}

void SetupWizard::OnBackClicked() {
    if (current_page > 0) {
        current_page--;
        OnPageChanged(current_page);
    }
}

void SetupWizard::OnCancelClicked() {
    if (QMessageBox::question(this, tr("Cancel Setup"), tr("Are you sure you want to cancel the setup wizard?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        reject();
    }
}

bool SetupWizard::ValidateCurrentPage() {
    switch (static_cast<Page>(current_page)) {
    case Page_Keys:
        if (!CheckKeysInstalled()) {
            QMessageBox::warning(this, tr("Keys Required"), tr("Please install decryption keys before continuing.\nKeys are required to run encrypted games."));
            return false;
        }
        break;
    case Page_Firmware:
        break;
    case Page_GamesDirectory:
        if (games_directory.isEmpty()) {
            QMessageBox::warning(this, tr("Games Directory Required"), tr("Please select a games directory before continuing."));
            return false;
        }
        break;
    default:
        break;
    }
    return true;
}

void SetupWizard::UpdateNavigationButtons() {
    back_button->setEnabled(current_page > 0);
    if (current_page == content_stack->count() - 1) {
        next_button->setText(tr("Finish"));
    } else {
        next_button->setText(tr("Next"));
    }
    for (int i = 0; i < sidebar_list->count(); ++i) {
        QListWidgetItem* item = sidebar_list->item(i);
        item->setSelected(i == current_page);
    }
}

void SetupWizard::OnInstallationTypeChanged() {}

void SetupWizard::OnSelectKeys() {
    const QString key_source_location = QFileDialog::getOpenFileName(
        this, tr("Select prod.keys File"), {}, QStringLiteral("prod.keys (prod.keys)"), {},
        QFileDialog::ReadOnly);
    if (key_source_location.isEmpty()) {
        return;
    }
    keys_path = key_source_location;
    const std::filesystem::path prod_key_path = key_source_location.toStdString();
    const std::filesystem::path key_source_path = prod_key_path.parent_path();
    if (!Common::FS::IsDir(key_source_path)) {
        return;
    }
    bool prod_keys_found = false;
    std::vector<std::filesystem::path> source_key_files;
    if (Common::FS::Exists(prod_key_path)) {
        prod_keys_found = true;
        source_key_files.emplace_back(prod_key_path);
    }
    if (Common::FS::Exists(key_source_path / "title.keys")) {
        source_key_files.emplace_back(key_source_path / "title.keys");
    }
    if (Common::FS::Exists(key_source_path / "key_retail.bin")) {
        source_key_files.emplace_back(key_source_path / "key_retail.bin");
    }
    if (source_key_files.empty() || !prod_keys_found) {
        QMessageBox::warning(this, tr("Decryption Keys install failed"), tr("prod.keys is a required decryption key file."));
        return;
    }
    const auto citron_keys_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::KeysDir);
    for (auto key_file : source_key_files) {
        std::filesystem::path destination_key_file = citron_keys_dir / key_file.filename();
        if (!std::filesystem::copy_file(key_file, destination_key_file, std::filesystem::copy_options::overwrite_existing)) {
            LOG_ERROR(Frontend, "Failed to copy file {} to {}", key_file.string(), destination_key_file.string());
            QMessageBox::critical(this, tr("Decryption Keys install failed"), tr("One or more keys failed to copy."));
            return;
        }
    }
    Core::Crypto::KeyManager::Instance().ReloadKeys();
    if (system.GetFilesystem()) {
        system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    }
    QMessageBox::information(this, tr("Keys Installed"), tr("Decryption keys have been installed successfully."));
}

void SetupWizard::OnSelectFirmware() {
    if (!CheckKeysInstalled()) {
        QMessageBox::information(this, tr("Keys not installed"), tr("Install decryption keys before attempting to install firmware."));
        return;
    }
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Install Firmware"));
    msgBox.setText(tr("Choose firmware installation method:"));
    msgBox.setInformativeText(tr("Select a folder containing NCA files, or select a ZIP archive."));
    QPushButton* folderButton = msgBox.addButton(tr("Select Folder"), QMessageBox::ActionRole);
    QPushButton* zipButton = msgBox.addButton(tr("Select ZIP File"), QMessageBox::ActionRole);
    QPushButton* cancelButton = msgBox.addButton(QMessageBox::Cancel);
    msgBox.setDefaultButton(zipButton);
    msgBox.exec();
    QPushButton* clicked = qobject_cast<QPushButton*>(msgBox.clickedButton());
    if (clicked == cancelButton) {
        return;
    }
    QString firmware_location;
    bool is_zip = false;
    if (clicked == zipButton) {
        firmware_location = QFileDialog::getOpenFileName(this, tr("Select Firmware ZIP File"), {}, QStringLiteral("ZIP Files (*.zip)"));
        is_zip = true;
    } else if (clicked == folderButton) {
        firmware_location = QFileDialog::getExistingDirectory(this, tr("Select Firmware Folder"));
        is_zip = false;
    }
    if (firmware_location.isEmpty()) {
        return;
    }
    firmware_path = firmware_location;
    InstallFirmware(firmware_location, is_zip);
}

void SetupWizard::OnSelectGamesDirectory() {
    const QString dir_path = QFileDialog::getExistingDirectory(this, tr("Select Games Directory"));
    if (dir_path.isEmpty()) {
        return;
    }
    games_directory = dir_path;
}

void SetupWizard::OnSelectScreenshotsPath() {
    const QString dir_path = QFileDialog::getExistingDirectory(this, tr("Select Screenshots Directory"), screenshots_path);
    if (dir_path.isEmpty()) {
        return;
    }
    screenshots_path = dir_path;
}

void SetupWizard::OnProfileNameChanged() {}

void SetupWizard::OnControllerSetup() {
    QMessageBox::information(this, tr("Controller Setup"), tr("Controller configuration will be available after setup is complete.\nYou can configure your controller from the Settings menu."));
}

void SetupWizard::ApplyConfiguration() {
    if (is_portable_mode) {
#ifdef _WIN32
        const auto exe_dir = Common::FS::GetExeDirectory();
        const auto portable_path = exe_dir / "user";
        if (!Common::FS::Exists(portable_path)) {
            void(Common::FS::CreateDirs(Common::FS::PathToUTF8String(portable_path)));
        }
        Common::FS::SetCitronPath(Common::FS::CitronPath::CitronDir, Common::FS::PathToUTF8String(portable_path));
#else
        const auto current_dir = std::filesystem::current_path();
        const auto portable_path = current_dir / "user";
        if (!Common::FS::Exists(portable_path)) {
            void(Common::FS::CreateDirs(Common::FS::PathToUTF8String(portable_path)));
        }
        Common::FS::SetCitronPath(Common::FS::CitronPath::CitronDir, Common::FS::PathToUTF8String(portable_path));
#endif
    }
    if (!screenshots_path.isEmpty()) {
        Common::FS::SetCitronPath(Common::FS::CitronPath::ScreenshotsDir, screenshots_path.toStdString());
    }
    if (!games_directory.isEmpty()) {
        UISettings::GameDir game_dir{games_directory.toStdString(), false, true};
        if (!UISettings::values.game_dirs.contains(game_dir)) {
            UISettings::values.game_dirs.append(game_dir);
        }
    }
    if (!profile_name.isEmpty() && profile_name != QStringLiteral("citron")) {
        auto& profile_manager = system.GetProfileManager();
        const auto current_user_index = Settings::values.current_user.GetValue();
        const auto current_user = profile_manager.GetUser(current_user_index);
        if (current_user) {
            Service::Account::ProfileBase profile{};
            if (profile_manager.GetProfileBase(*current_user, profile)) {
                const auto username_std = profile_name.toStdString();
                std::fill(profile.username.begin(), profile.username.end(), '\0');
                std::copy(username_std.begin(), username_std.end(), profile.username.begin());
                profile_manager.SetProfileBase(*current_user, profile);
                profile_manager.WriteUserSaveFile();
            }
        }
    }
    UISettings::values.first_start = false;
    if (main_window) {
        main_window->OnSaveConfig();
        main_window->RefreshGameList();
    }
}

bool SetupWizard::CheckKeysInstalled() const {
    return ContentManager::AreKeysPresent();
}

bool SetupWizard::CheckFirmwareInstalled() const {
    try {
        return system.GetFileSystemController().GetSystemNANDContentDirectory() != nullptr;
    } catch (...) {
        return false;
    }
}

void SetupWizard::InstallFirmware(const QString& firmware_path_param, bool is_zip) {
    if (!main_window) {
        return;
    }
    QProgressDialog progress(tr("Installing Firmware..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    auto QtProgressCallback = [&](size_t total_size, size_t processed_size) {
        progress.setValue(static_cast<int>((processed_size * 100) / total_size));
        QApplication::processEvents();
        return progress.wasCanceled();
    };
    std::filesystem::path firmware_source_path;
    std::filesystem::path temp_extract_path;
    if (is_zip) {
        temp_extract_path = std::filesystem::temp_directory_path() / "citron_firmware_temp";
        if (std::filesystem::exists(temp_extract_path)) {
            std::filesystem::remove_all(temp_extract_path);
        }
        progress.setLabelText(tr("Extracting firmware ZIP..."));
        QtProgressCallback(100, 5);
        if (!main_window->ExtractZipToDirectoryPublic(firmware_path_param.toStdString(), temp_extract_path)) {
            progress.close();
            std::filesystem::remove_all(temp_extract_path);
            QMessageBox::critical(this, tr("Firmware install failed"), tr("Failed to extract firmware ZIP file."));
            return;
        }
        firmware_source_path = temp_extract_path;
        QtProgressCallback(100, 15);
    } else {
        firmware_source_path = firmware_path_param.toStdString();
        QtProgressCallback(100, 10);
    }
    std::vector<std::filesystem::path> nca_files;
    const Common::FS::DirEntryCallable callback = [&nca_files](const std::filesystem::directory_entry& entry) {
        if (entry.path().has_extension() && entry.path().extension() == ".nca") {
            nca_files.emplace_back(entry.path());
        }
        return true;
    };
    Common::FS::IterateDirEntries(firmware_source_path, callback, Common::FS::DirEntryFilter::File);
    if (nca_files.empty()) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::warning(this, tr("Firmware install failed"), tr("Unable to locate firmware NCA files."));
        return;
    }
    QtProgressCallback(100, 20);
    auto sysnand_content_vdir = system.GetFileSystemController().GetSystemNANDContentDirectory();
    if (!sysnand_content_vdir) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"), tr("Failed to access system NAND directory."));
        return;
    }
    if (!sysnand_content_vdir->CleanSubdirectoryRecursive("registered")) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"), tr("Failed to clean existing firmware files."));
        return;
    }
    QtProgressCallback(100, 25);
    auto firmware_vdir = sysnand_content_vdir->GetDirectoryRelative("registered");
    if (!firmware_vdir) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"), tr("Failed to create firmware directory."));
        return;
    }
    auto vfs = system.GetFilesystem();
    if (!vfs) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"), tr("Failed to access virtual filesystem."));
        return;
    }
    bool success = true;
    int i = 0;
    for (const auto& nca_path : nca_files) {
        i++;
        auto src_file = vfs->OpenFile(nca_path.generic_string(), FileSys::OpenMode::Read);
        auto dst_file = firmware_vdir->CreateFileRelative(nca_path.filename().string());
        if (!src_file || !dst_file) {
            LOG_ERROR(Frontend, "Failed to open firmware file: {}", nca_path.string());
            success = false;
            continue;
        }
        if (!FileSys::VfsRawCopy(src_file, dst_file)) {
            LOG_ERROR(Frontend, "Failed to copy firmware file: {}", nca_path.string());
            success = false;
        }
        if (QtProgressCallback(100, 25 + static_cast<int>((i * 60) / nca_files.size()))) {
            progress.close();
            if (is_zip) {
                std::filesystem::remove_all(temp_extract_path);
            }
            QMessageBox::warning(this, tr("Firmware install cancelled"), tr("Firmware installation was cancelled."));
            return;
        }
    }
    if (is_zip) {
        std::filesystem::remove_all(temp_extract_path);
    }
    if (!success) {
        progress.close();
        QMessageBox::critical(this, tr("Firmware install failed"), tr("One or more firmware files failed to copy."));
        return;
    }
    system.GetFileSystemController().CreateFactories(*vfs);
    progress.close();
    QMessageBox::information(this, tr("Firmware installed successfully"), tr("The firmware has been installed successfully."));
    firmware_installed = true;
}
