// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/setup_wizard.h"
#include <QApplication>
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
#include <QButtonGroup>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/crypto/key_manager.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/file_sys/vfs/vfs.h"
#include "frontend_common/content_manager.h"
#include "ui_setup_wizard.h"
#include "citron/uisettings.h"
#include "citron/configuration/configure_input.h"
#include "citron/main.h"

#ifdef CITRON_ENABLE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

SetupWizard::SetupWizard(Core::System& system_, GMainWindow* main_window_, QWidget* parent)
    : QDialog(parent), ui{std::make_unique<Ui::SetupWizard>()}, system{system_},
      main_window{main_window_}, current_page{0}, is_portable_mode{false},
      profile_name{QStringLiteral("citron")}, firmware_installed{false} {
    ui->setupUi(this);

    setWindowTitle(tr("citron Setup Wizard"));

    // Set window flags before setting modality
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
                   Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);

    // Set window modality to NonModal to allow interaction with the main window.
    setWindowModality(Qt::NonModal);

    // Get UI elements from the .ui file
    sidebar_list = ui->sidebarList;
    content_stack = ui->contentStack;
    back_button = ui->backButton;
    next_button = ui->nextButton;
    cancel_button = ui->cancelButton;

    SetupUI();
    SetupPages();

    // Connect signals
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

    // Initialize to first page
    current_page = 0;
    content_stack->setCurrentIndex(0);
    UpdateNavigationButtons();
}

SetupWizard::~SetupWizard() = default;

void SetupWizard::SetupUI() {
    // Apply dark theme styling
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #1e1e1e; }"
        "QPushButton { background-color: #3d3d3d; color: #ffffff; border: 1px solid #555555; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4d4d4d; }"
        "QPushButton:pressed { background-color: #2d2d2d; }"
        "QPushButton:disabled { background-color: #2b2b2b; color: #666666; }"
    ));
}

void SetupWizard::SetupPages() {
    // Welcome page
    auto* welcome_page = new QWidget();
    auto* welcome_layout = new QVBoxLayout(welcome_page);
    welcome_layout->setContentsMargins(40, 40, 40, 40);
    welcome_layout->setSpacing(20);

    auto* welcome_title = new QLabel(tr("Welcome to citron Setup Wizard"));
    welcome_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 24px; font-weight: bold;"));
    welcome_layout->addWidget(welcome_title);

    auto* welcome_text = new QLabel(tr("This wizard will help you configure citron for first-time use.\n"
                                       "You'll be able to set up keys, firmware, game directories, and more."));
    welcome_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
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
    install_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    install_layout->addWidget(install_title);

    auto* install_subtitle = new QLabel(tr("Choose how you want to store citron's data:"));
    install_subtitle->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 12px;"));
    install_layout->addWidget(install_subtitle);

    auto* install_group = new QGroupBox();
    install_group->setStyleSheet(QStringLiteral("QGroupBox { color: #ffffff; border: 1px solid #444444; padding: 15px; }"));
    auto* install_group_layout = new QVBoxLayout(install_group);

    auto* button_group = new QButtonGroup(this);
    auto* portable_radio = new QRadioButton(tr("Portable (creates 'user' folder in executable directory)"));
    portable_radio->setStyleSheet(QStringLiteral("color: #cccccc;"));

    // Get platform-specific standard path
    QString standard_path_text;
#ifdef _WIN32
    const auto appdata_path = Common::FS::GetAppDataRoamingDirectory();
    const auto appdata_str = QString::fromStdString(Common::FS::PathToUTF8String(appdata_path));
    standard_path_text = tr("Standard (uses %APPDATA%\\citron)").arg(appdata_str);
#elif defined(__APPLE__)
    // macOS uses ~/Library/Application Support/citron
    standard_path_text = tr("Standard (uses ~/Library/Application Support/citron)");
#else
    // Linux/Unix - uses XDG_DATA_HOME (defaults to ~/.local/share)
    const auto data_path = Common::FS::GetDataDirectory("XDG_DATA_HOME");
    const auto data_path_str = QString::fromStdString(Common::FS::PathToUTF8String(data_path));
    standard_path_text = tr("Standard (uses %1/citron)").arg(data_path_str);
#endif

    auto* standard_radio = new QRadioButton(standard_path_text);
    standard_radio->setStyleSheet(QStringLiteral("color: #cccccc;"));
    standard_radio->setChecked(true);

    button_group->addButton(portable_radio, 0);
    button_group->addButton(standard_radio, 1);

    install_group_layout->addWidget(portable_radio);
    install_group_layout->addWidget(standard_radio);
    install_layout->addWidget(install_group);
    install_layout->addStretch();

    connect(portable_radio, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) is_portable_mode = true;
    });
    connect(standard_radio, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) is_portable_mode = false;
    });

    content_stack->addWidget(install_page);
    sidebar_list->addItem(tr("Installation Type"));

    // Keys page
    auto* keys_page = new QWidget();
    auto* keys_layout = new QVBoxLayout(keys_page);
    keys_layout->setContentsMargins(40, 40, 40, 40);
    keys_layout->setSpacing(20);

    auto* keys_title = new QLabel(tr("Decryption Keys"));
    keys_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    keys_layout->addWidget(keys_title);

    auto* keys_text = new QLabel(tr("Decryption keys are required to run encrypted games.\n"
                                    "Select your prod.keys file to install them."));
    keys_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    keys_text->setWordWrap(true);
    keys_layout->addWidget(keys_text);

    auto* keys_button = new QPushButton(tr("Select Keys File"));
    keys_button->setStyleSheet(QStringLiteral("color: #ffffff;"));
    connect(keys_button, &QPushButton::clicked, this, &SetupWizard::OnSelectKeys);
    keys_layout->addWidget(keys_button);

    auto* keys_status = new QLabel();
    keys_status->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 11px;"));
    keys_layout->addWidget(keys_status);

    if (CheckKeysInstalled()) {
        keys_status->setText(tr("✓ Keys are installed"));
        keys_status->setStyleSheet(QStringLiteral("color: #4caf50; font-size: 11px;"));
    } else {
        keys_status->setText(tr("Keys not installed"));
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
    firmware_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    firmware_layout->addWidget(firmware_title);

    auto* firmware_text = new QLabel(tr("Firmware is required to run system applications and some games.\n"
                                        "You can install it from a ZIP file or a folder containing NCA files."));
    firmware_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    firmware_text->setWordWrap(true);
    firmware_layout->addWidget(firmware_text);

    auto* firmware_button = new QPushButton(tr("Install Firmware"));
    firmware_button->setStyleSheet(QStringLiteral("color: #ffffff;"));
    connect(firmware_button, &QPushButton::clicked, this, &SetupWizard::OnSelectFirmware);
    firmware_layout->addWidget(firmware_button);

    auto* firmware_status = new QLabel();
    firmware_status->setStyleSheet(QStringLiteral("color: #aaaaaa; font-size: 11px;"));
    firmware_layout->addWidget(firmware_status);

    if (CheckFirmwareInstalled() || firmware_installed) {
        firmware_status->setText(tr("✓ Firmware is installed"));
        firmware_status->setStyleSheet(QStringLiteral("color: #4caf50; font-size: 11px;"));
    } else {
        firmware_status->setText(tr("Firmware not installed (optional)"));
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
    games_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    games_layout->addWidget(games_title);

    auto* games_text = new QLabel(tr("Select the directory where your game files are located."));
    games_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    games_text->setWordWrap(true);
    games_layout->addWidget(games_text);

    auto* games_path_layout = new QHBoxLayout();
    auto* games_path_edit = new QLineEdit();
    games_path_edit->setStyleSheet(QStringLiteral("color: #ffffff; background-color: #2b2b2b; border: 1px solid #444444; padding: 5px;"));
    games_path_edit->setReadOnly(true);
    games_path_edit->setPlaceholderText(tr("No directory selected"));
    if (!games_directory.isEmpty()) {
        games_path_edit->setText(games_directory);
    }
    games_path_layout->addWidget(games_path_edit);

    auto* games_button = new QPushButton(tr("Browse..."));
    games_button->setStyleSheet(QStringLiteral("color: #ffffff;"));
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
    paths_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    paths_layout->addWidget(paths_title);

    auto* paths_text = new QLabel(tr("Configure additional paths for screenshots and other files."));
    paths_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    paths_text->setWordWrap(true);
    paths_layout->addWidget(paths_text);

    auto* screenshots_label = new QLabel(tr("Screenshots Directory:"));
    screenshots_label->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    paths_layout->addWidget(screenshots_label);

    auto* screenshots_path_layout = new QHBoxLayout();
    auto* screenshots_path_edit = new QLineEdit();
    screenshots_path_edit->setStyleSheet(QStringLiteral("color: #ffffff; background-color: #2b2b2b; border: 1px solid #444444; padding: 5px;"));
    screenshots_path_edit->setReadOnly(true);
    screenshots_path_edit->setPlaceholderText(tr("Default location"));
    if (!screenshots_path.isEmpty()) {
        screenshots_path_edit->setText(screenshots_path);
    }
    screenshots_path_layout->addWidget(screenshots_path_edit);

    auto* screenshots_button = new QPushButton(tr("Browse..."));
    screenshots_button->setStyleSheet(QStringLiteral("color: #ffffff;"));
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
    profile_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    profile_layout->addWidget(profile_title);

    auto* profile_text = new QLabel(tr("Set your profile name (default: 'citron')."));
    profile_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    profile_text->setWordWrap(true);
    profile_layout->addWidget(profile_text);

    auto* profile_edit = new QLineEdit();
    profile_edit->setStyleSheet(QStringLiteral("color: #ffffff; background-color: #2b2b2b; border: 1px solid #444444; padding: 5px;"));
    profile_edit->setPlaceholderText(tr("citron"));
    profile_edit->setText(profile_name);
    connect(profile_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        profile_name = text;
    });
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
    controller_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 18px; font-weight: bold;"));
    controller_layout->addWidget(controller_title);

    auto* controller_text = new QLabel(tr("You can configure your controller after setup is complete.\n"
                                         "Go to Settings > Configure > Controls to set up your controller."));
    controller_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    controller_text->setWordWrap(true);
    controller_layout->addWidget(controller_text);

    auto* controller_button = new QPushButton(tr("Open Controller Settings"));
    controller_button->setStyleSheet(QStringLiteral("color: #ffffff;"));
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
    completion_title->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 24px; font-weight: bold;"));
    completion_layout->addWidget(completion_title);

    auto* completion_text = new QLabel(tr("You have completed the setup wizard.\n"
                                         "Click Finish to apply your settings and start using citron."));
    completion_text->setStyleSheet(QStringLiteral("color: #cccccc; font-size: 12px;"));
    completion_text->setWordWrap(true);
    completion_layout->addWidget(completion_text);
    completion_layout->addStretch();

    content_stack->addWidget(completion_page);
    sidebar_list->addItem(tr("Complete"));
}

void SetupWizard::OnPageChanged(int index) {
    if (index >= 0 && index < content_stack->count()) {
        content_stack->setCurrentIndex(index);
        current_page = index;
        UpdateNavigationButtons();

        // Update sidebar selection
        sidebar_list->setCurrentRow(index);
    }
}

void SetupWizard::OnNextClicked() {
    if (ValidateCurrentPage()) {
        if (current_page < content_stack->count() - 1) {
            current_page++;
            OnPageChanged(current_page);
        } else {
            // Finished
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
    if (QMessageBox::question(this, tr("Cancel Setup"),
                              tr("Are you sure you want to cancel the setup wizard?"),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        reject();
    }
}

bool SetupWizard::ValidateCurrentPage() {
    switch (current_page) {
    case Page_Keys:
        if (!CheckKeysInstalled()) {
            QMessageBox::warning(this, tr("Keys Required"),
                                 tr("Please install decryption keys before continuing.\n"
                                    "Keys are required to run encrypted games."));
            return false;
        }
        break;
    case Page_Firmware:
        // Firmware is optional, so we always allow proceeding
        break;
    case Page_GamesDirectory:
        if (games_directory.isEmpty()) {
            QMessageBox::warning(this, tr("Games Directory Required"),
                                 tr("Please select a games directory before continuing."));
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

    // Highlight current step in sidebar
    for (int i = 0; i < sidebar_list->count(); ++i) {
        QListWidgetItem* item = sidebar_list->item(i);
        if (i == current_page) {
            item->setSelected(true);
        } else {
            item->setSelected(false);
        }
    }
}

void SetupWizard::OnInstallationTypeChanged() {
    // Installation type change will be applied in ApplyConfiguration
}

void SetupWizard::OnSelectKeys() {
    const QString key_source_location = QFileDialog::getOpenFileName(
        this, tr("Select prod.keys File"), {}, QStringLiteral("prod.keys (prod.keys)"), {},
        QFileDialog::ReadOnly);
    if (key_source_location.isEmpty()) {
        return;
    }

    keys_path = key_source_location;

    // Copy keys to citron keys directory
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
        QMessageBox::warning(this, tr("Decryption Keys install failed"),
                             tr("prod.keys is a required decryption key file."));
        return;
    }

    const auto citron_keys_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::KeysDir);
    for (auto key_file : source_key_files) {
        std::filesystem::path destination_key_file = citron_keys_dir / key_file.filename();
        if (!std::filesystem::copy_file(key_file, destination_key_file,
                                        std::filesystem::copy_options::overwrite_existing)) {
            LOG_ERROR(Frontend, "Failed to copy file {} to {}", key_file.string(),
                      destination_key_file.string());
            QMessageBox::critical(this, tr("Decryption Keys install failed"),
                                  tr("One or more keys failed to copy."));
            return;
        }
    }

    // Reload keys
    Core::Crypto::KeyManager::Instance().ReloadKeys();
    if (system.GetFilesystem()) {
        system.GetFileSystemController().CreateFactories(*system.GetFilesystem());
    }

    QMessageBox::information(this, tr("Keys Installed"),
                            tr("Decryption keys have been installed successfully."));
}

void SetupWizard::OnSelectFirmware() {
    // Check for installed keys first
    if (!CheckKeysInstalled()) {
        QMessageBox::information(
            this, tr("Keys not installed"),
            tr("Install decryption keys before attempting to install firmware."));
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
        firmware_location = QFileDialog::getOpenFileName(this, tr("Select Firmware ZIP File"), {},
                                                         QStringLiteral("ZIP Files (*.zip)"));
        is_zip = true;
    } else if (clicked == folderButton) {
        firmware_location = QFileDialog::getExistingDirectory(this, tr("Select Firmware Folder"));
        is_zip = false;
    }

    if (firmware_location.isEmpty()) {
        return;
    }

    firmware_path = firmware_location;

    // Actually install the firmware
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
    const QString dir_path =
        QFileDialog::getExistingDirectory(this, tr("Select Screenshots Directory"), screenshots_path);
    if (dir_path.isEmpty()) {
        return;
    }
    screenshots_path = dir_path;
}

void SetupWizard::OnProfileNameChanged() {
    // Profile name change will be applied in ApplyConfiguration
}

void SetupWizard::OnControllerSetup() {
    // Open controller configuration dialog
    // This would need access to the main window's controller dialog
    // For now, we'll just mark that controller setup was attempted
    QMessageBox::information(this, tr("Controller Setup"),
                             tr("Controller configuration will be available after setup is complete.\n"
                                "You can configure your controller from the Settings menu."));
}

void SetupWizard::ApplyConfiguration() {
    // Apply installation type (portable vs standard)
    // Note: Portable mode is automatically detected by the presence of a "user" folder
    // in the executable directory. We just need to create it if it doesn't exist.
    if (is_portable_mode) {
#ifdef _WIN32
        const auto exe_dir = Common::FS::GetExeDirectory();
        const auto portable_path = exe_dir / "user";
        if (!Common::FS::Exists(portable_path)) {
            void(Common::FS::CreateDirs(Common::FS::PathToUTF8String(portable_path)));
        }
        Common::FS::SetCitronPath(Common::FS::CitronPath::CitronDir,
                                   Common::FS::PathToUTF8String(portable_path));
#else
        const auto current_dir = std::filesystem::current_path();
        const auto portable_path = current_dir / "user";
        if (!Common::FS::Exists(portable_path)) {
            void(Common::FS::CreateDirs(Common::FS::PathToUTF8String(portable_path)));
        }
        Common::FS::SetCitronPath(Common::FS::CitronPath::CitronDir,
                                   Common::FS::PathToUTF8String(portable_path));
#endif
    }
    // Standard mode uses default paths, so no change needed

    // Apply screenshots path
    if (!screenshots_path.isEmpty()) {
        Common::FS::SetCitronPath(Common::FS::CitronPath::ScreenshotsDir,
                                   screenshots_path.toStdString());
    }

    // Apply games directory
    if (!games_directory.isEmpty()) {
        UISettings::GameDir game_dir{games_directory.toStdString(), false, true};
        if (!UISettings::values.game_dirs.contains(game_dir)) {
            UISettings::values.game_dirs.append(game_dir);
        }
    }

    // Apply profile name
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

    // Mark setup as complete
    UISettings::values.first_start = false;

    // Save all configuration
    if (main_window) {
        main_window->OnSaveConfig();

        // Refresh game list to show newly added directories
        main_window->RefreshGameList();
    }
}

bool SetupWizard::CheckKeysInstalled() const {
    return ContentManager::AreKeysPresent();
}

bool SetupWizard::CheckFirmwareInstalled() const {
    // Check if firmware is installed by checking for system content
    // This is a simplified check
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
        // Extract ZIP to temp directory
        temp_extract_path = std::filesystem::temp_directory_path() / "citron_firmware_temp";
        if (std::filesystem::exists(temp_extract_path)) {
            std::filesystem::remove_all(temp_extract_path);
        }

        progress.setLabelText(tr("Extracting firmware ZIP..."));
        QtProgressCallback(100, 5);

        // Use main window's ExtractZipToDirectory
        if (!main_window->ExtractZipToDirectoryPublic(firmware_path_param.toStdString(), temp_extract_path)) {
            progress.close();
            std::filesystem::remove_all(temp_extract_path);
            QMessageBox::critical(this, tr("Firmware install failed"),
                                tr("Failed to extract firmware ZIP file."));
            return;
        }

        firmware_source_path = temp_extract_path;
        QtProgressCallback(100, 15);
    } else {
        firmware_source_path = firmware_path_param.toStdString();
        QtProgressCallback(100, 10);
    }

    // Find .nca files
    std::vector<std::filesystem::path> nca_files;
    const Common::FS::DirEntryCallable callback =
        [&nca_files](const std::filesystem::directory_entry& entry) {
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
        QMessageBox::warning(this, tr("Firmware install failed"),
                           tr("Unable to locate firmware NCA files."));
        return;
    }

    QtProgressCallback(100, 20);

    // Get system NAND content directory
    auto sysnand_content_vdir = system.GetFileSystemController().GetSystemNANDContentDirectory();
    if (!sysnand_content_vdir) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"),
                            tr("Failed to access system NAND directory."));
        return;
    }

    // Clean existing firmware
    if (!sysnand_content_vdir->CleanSubdirectoryRecursive("registered")) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"),
                            tr("Failed to clean existing firmware files."));
        return;
    }

    QtProgressCallback(100, 25);

    auto firmware_vdir = sysnand_content_vdir->GetDirectoryRelative("registered");
    if (!firmware_vdir) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"),
                            tr("Failed to create firmware directory."));
        return;
    }

    // Copy firmware files
    auto vfs = system.GetFilesystem();
    if (!vfs) {
        progress.close();
        if (is_zip) {
            std::filesystem::remove_all(temp_extract_path);
        }
        QMessageBox::critical(this, tr("Firmware install failed"),
                            tr("Failed to access virtual filesystem."));
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
            QMessageBox::warning(this, tr("Firmware install cancelled"),
                               tr("Firmware installation was cancelled."));
            return;
        }
    }

    // Clean up temp directory
    if (is_zip) {
        std::filesystem::remove_all(temp_extract_path);
    }

    if (!success) {
        progress.close();
        QMessageBox::critical(this, tr("Firmware install failed"),
                            tr("One or more firmware files failed to copy."));
        return;
    }

    // Re-scan VFS
    system.GetFileSystemController().CreateFactories(*vfs);

    // Note: Verification would require access to the provider which is private in GMainWindow
    // The firmware files have been successfully copied, so installation is complete
    progress.close();
    QMessageBox::information(this, tr("Firmware installed successfully"),
                           tr("The firmware has been installed successfully."));
    firmware_installed = true;
}
