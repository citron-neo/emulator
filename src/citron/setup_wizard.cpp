// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <QtGlobal>
#include <QPointF>
#include "citron/setup_wizard.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDir>
#include "citron/main.h"
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QIcon>

class AnimatedLogo : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal rotation READ Rotation WRITE SetRotation)
    Q_PROPERTY(qreal opacity READ Opacity WRITE SetOpacity)
    Q_PROPERTY(qreal scale READ Scale WRITE SetScale)
    Q_PROPERTY(QPointF logoPos READ LogoPos WRITE SetLogoPos)
public:
    explicit AnimatedLogo(QWidget* parent = nullptr) : QWidget(parent) {
        logo_pixmap = QIcon(QStringLiteral(":/citron.svg")).pixmap(256, 256);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    qreal Rotation() const { return rotation_angle; }
    void SetRotation(qreal angle) { rotation_angle = angle; update(); }
    
    qreal Opacity() const { return logo_opacity; }
    void SetOpacity(qreal opacity) { logo_opacity = opacity; update(); }
    
    qreal Scale() const { return logo_scale; }
    void SetScale(qreal scale) { logo_scale = scale; update(); }

    QPointF LogoPos() const { return logo_pos; }
    void SetLogoPos(QPointF pos) { logo_pos = pos; update(); }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (logo_opacity <= 0.0 || logo_pixmap.isNull()) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setOpacity(logo_opacity);

        // Center on the animated logo_pos
        painter.translate(logo_pos);
        painter.scale(logo_scale, logo_scale);
        painter.rotate(rotation_angle);
        painter.translate(-logo_pixmap.width() / 2, -logo_pixmap.height() / 2);

        painter.drawPixmap(0, 0, logo_pixmap);
    }
private:
    QPixmap logo_pixmap;
    qreal rotation_angle{0};
    qreal logo_opacity{1.0};
    qreal logo_scale{1.0};
    QPointF logo_pos;
};

SetupWizard::SetupWizard(Core::System& system_, GMainWindow* main_window_, QWidget* parent)
    : QDialog(parent), system{system_}, main_window{main_window_} {
    
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(tr("citron Setup Wizard"));
    setFixedSize(592, 389);

    SetupUI();
    UpdateTheme();

    if (UISettings::values.setup_resume) {
        current_page_index = 2; // Jump to Keys
        intro_played = true;    // Skip intro animation
        UISettings::values.setup_resume = false;
        
        if (main_window) {
            main_window->OnSaveConfig();
        }
    }

    // Create the logo canvas - fill the whole dialog to prevent clipping
    intro_logo = new AnimatedLogo(this);
    intro_logo->setGeometry(rect());
    intro_logo->hide();
}

SetupWizard::~SetupWizard() = default;

void SetupWizard::SetupUI() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(40, 30, 40, 30);
    main_layout->setSpacing(15);

    page_stack = new QStackedWidget(this);
    page_stack->setStyleSheet(QStringLiteral("background: transparent;"));
    main_layout->addWidget(page_stack);

    // Page 1: Welcome
    welcome_page = new SetupPage(this);
    auto* welcome_layout = new QVBoxLayout(welcome_page);
    welcome_layout->setAlignment(Qt::AlignCenter);
    welcome_layout->setSpacing(10);
    
    auto* welcome_title = new QLabel(tr("WELCOME TO CITRON NEO"), welcome_page);
    welcome_title->setObjectName(QStringLiteral("welcomeTitle"));
    welcome_title->setAlignment(Qt::AlignCenter);
    
    auto* welcome_desc = new QLabel(tr("Citron Neo wants to offer you one of the simplest ways of setting up your emulator as quick as you can to have an easier experience getting into everything we have to offer. Please click \"Next\" so we can get you up and running!"), welcome_page);
    welcome_desc->setObjectName(QStringLiteral("descriptionLabel"));
    welcome_desc->setAlignment(Qt::AlignCenter);
    welcome_desc->setWordWrap(true);
    
    welcome_layout->addStretch();
    welcome_layout->addWidget(welcome_title);
    welcome_layout->addWidget(welcome_desc);
    welcome_layout->addStretch();
    page_stack->addWidget(welcome_page);

    // Page 2: Installation Path
    path_page = new SetupPage(this);
    auto* path_layout = new QVBoxLayout(path_page);
    path_layout->setSpacing(15);
    path_layout->setAlignment(Qt::AlignCenter);

    auto* path_title = new QLabel(tr("CHOOSE INSTALLATION TYPE"), path_page);
    path_title->setObjectName(QStringLiteral("sectionTitle"));
    path_title->setAlignment(Qt::AlignCenter);
    
    auto* path_desc = new QLabel(tr("Choose how citron should store your data."), path_page);
    path_desc->setObjectName(QStringLiteral("descriptionLabel"));
    path_desc->setAlignment(Qt::AlignCenter);
    path_desc->setWordWrap(true);

    auto* standard_radio = new QRadioButton(tr("Standard Mode (Recommended)"), path_page);
    standard_radio->setToolTip(tr("Stores data in the system's AppData/Home directory."));
    
    auto* portable_radio = new QRadioButton(tr("Portable Mode"), path_page);
    portable_radio->setToolTip(tr("Creates a 'user' folder in the emulator directory. Everything is self-contained."));

    bool is_portable = IsCurrentlyPortable();
    standard_radio->setChecked(!is_portable);
    portable_radio->setChecked(is_portable);

    path_layout->addStretch();
    path_layout->addWidget(path_title);
    path_layout->addWidget(path_desc);
    path_layout->addSpacing(20);
    path_layout->addWidget(standard_radio);
    path_layout->addWidget(portable_radio);
    path_layout->addStretch();
    page_stack->addWidget(path_page);

    // Placeholders
    keys_placeholder = new SetupPage(this);
    auto* keys_layout = new QVBoxLayout(keys_placeholder);
    keys_layout->setAlignment(Qt::AlignCenter);
    auto* k_title = new QLabel(tr("DECRYPTION KEYS"), keys_placeholder);
    k_title->setObjectName(QStringLiteral("sectionTitle"));
    k_title->setAlignment(Qt::AlignCenter);
    keys_layout->addWidget(k_title);
    keys_layout->addWidget(new QLabel(tr("The configuration for decryption keys will be added here."), keys_placeholder));
    page_stack->addWidget(keys_placeholder);

    firmware_placeholder = new SetupPage(this);
    auto* firmware_layout = new QVBoxLayout(firmware_placeholder);
    firmware_layout->setAlignment(Qt::AlignCenter);
    auto* f_title = new QLabel(tr("FIRMWARE INSTALLATION"), firmware_placeholder);
    f_title->setObjectName(QStringLiteral("sectionTitle"));
    f_title->setAlignment(Qt::AlignCenter);
    firmware_layout->addWidget(f_title);
    firmware_layout->addWidget(new QLabel(tr("Firmware setup logic will be implemented in the next step."), firmware_placeholder));
    page_stack->addWidget(firmware_placeholder);

    // Navigation buttons
    auto* nav_layout = new QHBoxLayout();
    back_button = new QPushButton(tr("BACK"), this);
    next_button = new QPushButton(tr("NEXT"), this);
    auto* cancel_button = new QPushButton(tr("CANCEL"), this);

    nav_layout->addWidget(back_button);
    nav_layout->addWidget(cancel_button);
    nav_layout->addWidget(next_button);
    nav_layout->addStretch();
    main_layout->addLayout(nav_layout);

    // Connections
    connect(next_button, &QPushButton::clicked, this, &SetupWizard::OnNext);
    connect(back_button, &QPushButton::clicked, this, &SetupWizard::OnBack);
    connect(cancel_button, &QPushButton::clicked, this, &SetupWizard::OnCancel);

    UpdateNavigation();
    
    // Set initial invisibility for intro
    page_stack->setGraphicsEffect(new QGraphicsOpacityEffect(this));
    static_cast<QGraphicsOpacityEffect*>(page_stack->graphicsEffect())->setOpacity(0);
    
    back_button->setGraphicsEffect(new QGraphicsOpacityEffect(this));
    static_cast<QGraphicsOpacityEffect*>(back_button->graphicsEffect())->setOpacity(0);
    next_button->setGraphicsEffect(new QGraphicsOpacityEffect(this));
    static_cast<QGraphicsOpacityEffect*>(next_button->graphicsEffect())->setOpacity(0);
    
    SetPage(current_page_index);
}

void SetupWizard::SetPage(int index) {
    if (index == current_page_index && intro_played) return;

    current_page_index = index;
    page_stack->setCurrentIndex(index);
    UpdateNavigation();
    
    if (intro_played) {
        auto* page = page_stack->currentWidget();
        if (auto* setup_page = qobject_cast<SetupPage*>(page)) {
            setup_page->FadeIn();
        }
    }
}

void SetupWizard::OnNext() {
    if (current_page_index == 1) { // Path Selection Page
        bool wants_portable = false;
        const auto buttons = path_page->findChildren<QRadioButton*>();
        for (auto* btn : buttons) {
            if (btn->text().contains(QStringLiteral("Portable")) && btn->isChecked()) {
                wants_portable = true;
                break;
            }
        }

        if (wants_portable != IsCurrentlyPortable()) {
            MigrateDataAndRestart(wants_portable);
            return;
        }
    }

    if (current_page_index < page_stack->count() - 1) {
        SetPage(current_page_index + 1);
    } else {
        ApplyAndFinish();
    }
}

void SetupWizard::OnBack() {
    if (current_page_index > 0) {
        SetPage(current_page_index - 1);
    }
}

void SetupWizard::OnCancel() {
    if (QMessageBox::question(this, tr("Cancel Setup"), tr("Are you sure you want to exit the setup wizard? Changes will not be saved."), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        reject();
    }
}

void SetupWizard::MigrateDataAndRestart(bool to_portable) {
    std::filesystem::path portable_path = Common::FS::GetCurrentDir() / "user";
    std::filesystem::path backup_path = Common::FS::GetCurrentDir() / "user_backup";
    std::filesystem::path standard_path = GetStandardPath();
    std::filesystem::path target_path = to_portable ? portable_path : standard_path;

    bool has_backup = Common::FS::IsDir(backup_path);
    bool target_exists = Common::FS::IsDir(target_path);
    
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Switch Installation Mode"));
    QString mode_name = to_portable ? tr("Portable") : tr("Standard");
    msgBox.setText(tr("How would you like to switch to %1 Mode?").arg(mode_name));
    
    // Context-aware buttons
    QString copy_text = target_exists ? tr("Overwrite %1 with Current Data").arg(mode_name) : tr("Port Data to %1 Mode").arg(mode_name);
    QString switch_text = target_exists ? tr("Keep Existing %1 Data").arg(mode_name) : tr("Start Fresh in %1 Mode").arg(mode_name);

    QPushButton* copyButton = msgBox.addButton(copy_text, QMessageBox::AcceptRole);
    QPushButton* switchButton = msgBox.addButton(switch_text, QMessageBox::ActionRole);
    QPushButton* restoreButton = nullptr;
    
    if (to_portable && has_backup) {
        restoreButton = msgBox.addButton(tr("Restore from 'user_backup'"), QMessageBox::ActionRole);
        restoreButton->setToolTip(tr("Moves your existing 'user_backup' folder back to 'user'."));
    }
    
    msgBox.addButton(QMessageBox::Cancel);

    msgBox.exec();

    if (msgBox.clickedButton() == nullptr || msgBox.standardButton(msgBox.clickedButton()) == QMessageBox::Cancel) {
        return;
    }

    bool do_copy = (msgBox.clickedButton() == copyButton);
    bool do_restore = (restoreButton && msgBox.clickedButton() == restoreButton);
    bool do_switch = (msgBox.clickedButton() == switchButton);

    // Show progress hint if copying
    QMessageBox* progress = nullptr;
    if (do_copy) {
        progress = new QMessageBox(this);
        progress->setWindowTitle(tr("Porting Data"));
        progress->setText(tr("Porting your data, please wait... \nThis may take a moment depending on your data size."));
        progress->setStandardButtons(QMessageBox::NoButton);
        progress->show();
        qApp->processEvents();
    }

    try {
        if (to_portable) {
            if (do_restore) {
                if (!Common::FS::RenameDir(backup_path, portable_path)) {
                    throw std::runtime_error("Failed to restore backup directory.");
                }
            } else if (do_copy) {
                if (Common::FS::Exists(standard_path)) {
                    std::filesystem::create_directories(portable_path);
                    std::filesystem::copy(standard_path, portable_path, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
                }
            } else if (do_switch) {
                if (!Common::FS::CreateDir(portable_path)) {
                    throw std::runtime_error("Failed to create portable user directory.");
                }
            }
        } else {
            // Moving to Standard
            if (do_copy) {
                if (Common::FS::Exists(portable_path)) {
                    std::filesystem::create_directories(standard_path);
                    std::filesystem::copy(portable_path, standard_path, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
                }
            }
            
            // Always move 'user' to backup to break the portable lock
            std::filesystem::path target_backup = backup_path;
            int counter = 1;
            while (Common::FS::Exists(target_backup)) {
                target_backup = Common::FS::GetCurrentDir() / QStringLiteral("user_backup_%1").arg(counter++).toStdString();
            }
            if (!Common::FS::RenameDir(portable_path, target_backup)) {
                throw std::runtime_error("Failed to move current portable directory to backup.");
            }
            // Hard Cleanup: Ensure the portable 'user' folder is truly gone so it doesn't trick the emulator on reboot
            if (Common::FS::Exists(portable_path)) {
                Common::FS::RemoveDirRecursively(portable_path);
            }
        }

        UISettings::values.setup_resume = true;
        if (main_window) {
            main_window->OnSaveConfig();
        }
        
        if (progress) progress->close();
        hide();
        qApp->quit();
    } catch (const std::exception& e) {
        if (progress) progress->close();
        QMessageBox::critical(this, tr("Operation Error"), tr("An error occurred during the mode switch: %1").arg(e.what()));
    }
}

bool SetupWizard::IsCurrentlyPortable() const {
    return Common::FS::IsDir(Common::FS::GetCurrentDir() / "user");
}

std::filesystem::path SetupWizard::GetStandardPath() const {
#ifdef _WIN32
    return Common::FS::GetExeDirectory().parent_path() / "AppData" / "Roaming" / "citron"; // Simplification for finding target
#else
    // On Linux/Mac, reconstruction is more reliable
    const char* home = getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".local" / "share" / "citron";
    }
    return {};
#endif
}

void SetupWizard::UpdateNavigation() {
    back_button->setVisible(current_page_index > 0);
    next_button->setText(current_page_index == page_stack->count() - 1 ? tr("Finish") : tr("Next"));
}

void SetupWizard::UpdateTheme() {
    if (is_updating_theme) return;
    is_updating_theme = true;

    bool is_dark = Theme::IsDarkMode();
    QString bg = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#f5f5fa");
    QString txt = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    QString sub_txt = is_dark ? QStringLiteral("#aaaab4") : QStringLiteral("#666670");
    QString accent = Theme::GetAccentColor();

    QString style = QStringLiteral(
        "SetupWizard { background-color: %1; }"
        "SetupPage { background-color: transparent; }"
        "QLabel { color: %2; background: transparent; }"
        "#welcomeTitle { color: %2; font-size: 32px; font-weight: 600; text-transform: uppercase; letter-spacing: 2px; margin-bottom: 5px; }"
        "#sectionTitle { color: %2; font-size: 20px; font-weight: 600; text-transform: uppercase; letter-spacing: 1.5px; margin-bottom: 2px; }"
        "#descriptionLabel { color: %3; font-size: 13px; margin-bottom: 20px; }"
        "QPushButton { background-color: transparent; border: 2px solid %4; color: %4; border-radius: 12px; padding: 6px 20px; font-weight: bold; font-size: 11px; text-transform: uppercase; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 15); }"
        "QPushButton:pressed { background-color: rgba(255, 255, 255, 25); }"
        "QRadioButton { color: %2; font-size: 14px; spacing: 10px; }"
        "QRadioButton::indicator { width: 14px; height: 14px; }"
    ).arg(bg, txt, sub_txt, accent); // Fixed: Arg count must match placeholder count (4)
    
    setStyleSheet(style);
    is_updating_theme = false;
}

void SetupWizard::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (!intro_played) {
        intro_played = true;
        // Small delay to let the window settle
        QTimer::singleShot(100, this, &SetupWizard::StartIntroAnimation);
    }
}

void SetupWizard::StartIntroAnimation() {
    QPointF end_pos(width() / 2.0, height() / 2.0);
    QPointF start_pos(end_pos.x(), height() + 150);
    
    // Set position and state BEFORE show to prevent popping
    intro_logo->SetLogoPos(start_pos);
    intro_logo->SetOpacity(1.0);
    intro_logo->SetScale(1.0);
    intro_logo->SetRotation(0);
    
    intro_logo->show();
    intro_logo->raise();
    
    // Master parallel group
    auto* master_group = new QParallelAnimationGroup(this);
    
    // Sequential group for Pos, Pause, and Scale/Opacity
    auto* seq_group = new QSequentialAnimationGroup(this);
    
    // Phase 1: Fly in to center
    auto* move = new QPropertyAnimation(intro_logo, "logoPos");
    move->setDuration(1200);
    move->setStartValue(start_pos);
    move->setEndValue(end_pos);
    move->setEasingCurve(QEasingCurve::OutCubic);
    seq_group->addAnimation(move);
    
    seq_group->addPause(400);
    
    // Phase 2: Supernova Bloom (Unlimited scaling since widget is full-screen)
    auto* bloom = new QParallelAnimationGroup(this);
    auto* scale = new QPropertyAnimation(intro_logo, "scale");
    scale->setDuration(900);
    scale->setStartValue(1.0);
    scale->setEndValue(5.0);
    scale->setEasingCurve(QEasingCurve::InCubic);
    
    auto* fade = new QPropertyAnimation(intro_logo, "opacity");
    fade->setDuration(900);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InExpo);
    
    bloom->addAnimation(scale);
    bloom->addAnimation(fade);
    seq_group->addAnimation(bloom);
    
    // Independent Perpetual Spin
    auto* spin = new QPropertyAnimation(intro_logo, "rotation");
    spin->setDuration(2500); 
    spin->setStartValue(0);
    spin->setEndValue(1440);
    spin->setEasingCurve(QEasingCurve::Linear);
    
    master_group->addAnimation(seq_group);
    master_group->addAnimation(spin);
    
    connect(master_group, &QParallelAnimationGroup::finished, this, [this]() {
        intro_logo->hide();
        
        // Trigger the reveal for the first page (Welcome)
        if (auto* page = qobject_cast<SetupPage*>(page_stack->currentWidget())) {
            page->FadeIn();
        }
        
        auto* ui_fade = new QParallelAnimationGroup(this);
        auto* p_fade = new QPropertyAnimation(page_stack->graphicsEffect(), "opacity");
        p_fade->setDuration(1500);
        p_fade->setStartValue(0.0);
        p_fade->setEndValue(1.0);
        
        auto* n_fade = new QPropertyAnimation(next_button->graphicsEffect(), "opacity");
        n_fade->setDuration(1500);
        n_fade->setStartValue(0.0);
        n_fade->setEndValue(1.0);
        
        ui_fade->addAnimation(p_fade);
        ui_fade->addAnimation(n_fade);

        connect(ui_fade, &QParallelAnimationGroup::finished, this, [this] {
            page_stack->setGraphicsEffect(nullptr);
            next_button->setGraphicsEffect(nullptr);
            if (back_button->graphicsEffect()) {
                back_button->setGraphicsEffect(nullptr);
            }
        });
        
        ui_fade->start(QAbstractAnimation::DeleteWhenStopped);
    });
    
    master_group->start(QAbstractAnimation::DeleteWhenStopped);
}

void SetupWizard::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) {
        UpdateTheme();
    }
    QDialog::changeEvent(event);
}

void SetupWizard::resizeEvent(QResizeEvent* event) {
    if (intro_logo) {
        intro_logo->setGeometry(rect());
    }
    QDialog::resizeEvent(event);
}

void SetupWizard::ApplyAndFinish() {
    UISettings::values.first_start = false;
    if (main_window) {
        main_window->OnSaveConfig();
    }
    accept();
}

#include "setup_wizard.moc"
