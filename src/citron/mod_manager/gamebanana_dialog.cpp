// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QMessageBox>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QNetworkReply>
#else
#include <QtNetwork/QNetworkReply>
#endif
#include <QPixmap>
#include <QPushButton>
#include <QUrl>

#include "citron/mod_manager/gamebanana_dialog.h"
#include "citron/mod_manager/zip_extractor.h"
#include "citron/configuration/configuration_styling.h"
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "ui_gamebanana_dialog.h"

namespace ModManager {

GameBananaDialog::GameBananaDialog(const QString& title_id_, const QString& game_name_,
                                   QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::GameBananaDialog>()),
      service(new GameBananaService(this)), title_id(title_id_), game_name(game_name_) {
    ui->setupUi(this);

    ui->gameTitleLabel->setText(tr("Mods for: %1").arg(game_name));
    ui->progressBar->setVisible(false);
    ui->buttonLoadMore->setVisible(false);
    ui->modDetailsGroup->setVisible(true);

    // Initialize checkboxes from settings
    ui->alwaysAskManualExtraction->setChecked(
        UISettings::values.always_ask_manual_extraction.GetValue());
    ui->disableBackupModArchives->setChecked(UISettings::values.disable_backup_archives.GetValue());
    ui->label_version->setText(tr("Compatible Ver:"));

    // Hide sorting options as we are forcing a single reliable view of all mods.
    ui->sortComboBox->setVisible(false);

    SetupConnections();

    // Check for cached game ID first
    QString cached_id = GameBananaService::GetCachedGameId(title_id);
    if (!cached_id.isEmpty()) {
        current_game_id = cached_id;
        ui->statusLabel->setText(tr("Loading mods..."));
        service->FetchModsForGame(cached_id);
    } else {
        // Auto-search for the game on GameBanana
        SearchForGame();
    }
    if (UISettings::IsGamescope()) {
        resize(720, 540);
    }

    UpdateTheme();
}

GameBananaDialog::~GameBananaDialog() = default;

void GameBananaDialog::SetupConnections() {
    connect(ui->modList, &QListWidget::currentRowChanged, this, &GameBananaDialog::OnModSelected);
    connect(ui->buttonDownload, &QPushButton::clicked, this, &GameBananaDialog::OnDownloadClicked);
    connect(ui->buttonCancel, &QPushButton::clicked, this, &GameBananaDialog::OnCancelClicked);
    connect(ui->buttonLoadMore, &QPushButton::clicked, this, &GameBananaDialog::OnLoadMoreClicked);
    connect(ui->searchEdit, &QLineEdit::returnPressed, this, &GameBananaDialog::OnSearchTriggered);

    connect(service, &GameBananaService::GamesFound, this, &GameBananaDialog::OnGamesFound);
    connect(service, &GameBananaService::ModsAvailable, this, &GameBananaDialog::PopulateMods);
    connect(service, &GameBananaService::ModDetailsReady, this,
            &GameBananaDialog::UpdateModDetails);
    connect(service, &GameBananaService::DownloadComplete, this, &GameBananaDialog::FinishDownload);
    connect(service, &GameBananaService::DownloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) {
                    ui->progressBar->setValue(static_cast<int>((received * 100) / total));
                }
            });
    connect(service, &GameBananaService::Error, this, [this](const QString& msg) {
        ui->statusLabel->setText(tr("Error: %1").arg(msg));
        ui->progressBar->setVisible(false);
        ui->buttonDownload->setEnabled(true);
    });

    // Save checkbox states when toggled
    connect(ui->alwaysAskManualExtraction, &QCheckBox::toggled, this, [](bool checked) {
        UISettings::values.always_ask_manual_extraction.SetValue(checked);
    });
    connect(ui->disableBackupModArchives, &QCheckBox::toggled, this,
            [](bool checked) { UISettings::values.disable_backup_archives.SetValue(checked); });
}

void GameBananaDialog::SearchForGame() {
    if (game_name.isEmpty()) {
        ui->statusLabel->setText(tr("No game name available to search."));
        return;
    }

    ui->statusLabel->setText(tr("Searching for '%1' on GameBanana...").arg(game_name));
    service->SearchGames(game_name);
}

void GameBananaDialog::OnGamesFound(const QVector<GameBananaGame>& games) {
    if (games.isEmpty()) {
        ui->statusLabel->setText(
            tr("No matching game found on GameBanana for '%1'.").arg(game_name));
        return;
    }

    const auto& game = games.first();
    current_game_id = game.id;

    GameBananaService::CacheGameId(title_id, current_game_id);

    ui->statusLabel->setText(tr("Found '%1'. Loading mods...").arg(game.name));
    current_page = 1;
    current_query.clear();
    service->FetchModsForGame(current_game_id, current_page);
}

void GameBananaDialog::OnSearchTriggered() {
    current_query = ui->searchEdit->text().trimmed();
    current_page = 1;
    loaded_mods.clear();
    ui->modList->clear();
    /* Keep details group visible */

    if (current_query.isEmpty()) {
        ui->statusLabel->setText(tr("Refreshing mod list..."));
        service->FetchModsForGame(current_game_id, current_page);
    } else {
        ui->statusLabel->setText(tr("Searching for '%1'...").arg(current_query));
        service->SearchMods(current_game_id, current_query, current_page);
    }
}

void GameBananaDialog::PopulateMods(const QVector<GameBananaMod>& mods, bool has_more) {
    if (current_page == 1) {
        loaded_mods.clear();
        ui->modList->clear();
    }

    for (const auto& mod : mods) {
        QString display = QStringLiteral("%1 by %2").arg(mod.name, mod.submitter);
        auto* item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, loaded_mods.size());
        ui->modList->addItem(item);
        loaded_mods.append(mod);
    }

    ui->statusLabel->setText(tr("Showing %1 mods").arg(loaded_mods.size()));

    // Automatic Pagination: Fetch more mods until we have everything available
    if (has_more) {
        current_page++;

        // Map current sort index to string for the Subfeed request
        QString sort = QStringLiteral("new");
        switch (ui->sortComboBox->currentIndex()) {
        case 1:
            sort = QStringLiteral("Generic_LatestUpdated");
            break;
        case 2:
            sort = QStringLiteral("Generic_MostDownloaded");
            break;
        case 3:
            sort = QStringLiteral("Generic_MostViewed");
            break;
        case 4:
            sort = QStringLiteral("Generic_MostLiked");
            break;
        case 5:
            sort = QStringLiteral("Generic_HighestRated");
            break;
        }

        if (current_query.isEmpty()) {
            service->FetchModsForGame(current_game_id, current_page, sort);
        } else {
            service->SearchMods(current_game_id, current_query, current_page);
        }
    } else {
        ui->buttonLoadMore->setVisible(false);
    }
}

void GameBananaDialog::OnLoadMoreClicked() {
    current_page++;
    if (current_query.isEmpty()) {
        service->FetchModsForGame(current_game_id, current_page, QStringLiteral("new"));
    } else {
        service->SearchMods(current_game_id, current_query, current_page);
    }
}

void GameBananaDialog::OnModSelected() {
    int row = ui->modList->currentRow();
    if (row < 0 || row >= loaded_mods.size()) {
        ui->modNameLabel->setText(tr("Select a Mod"));
        ui->modCategoryLabel->clear();
        ui->modVersionLabel->clear();
        ui->modDownloadsLabel->clear();
        ui->modDescriptionBrowser->clear();
        ui->screenshotLabel->clear();
        ui->statusArchiveLabel->clear();
        ui->statusLinkLabel->clear();
        return;
    }

    const auto& mod = loaded_mods[row];
    ui->statusLabel->setText(tr("Loading mod details..."));
    service->FetchModDetails(mod);
}

void GameBananaDialog::UpdateModDetails(const GameBananaMod& mod) {
    selected_mod = mod;
    ui->modDetailsGroup->setVisible(true);

    ui->modNameLabel->setText(mod.name);
    ui->modCategoryLabel->setText(mod.category);
    ui->modVersionLabel->setText(mod.version.isEmpty() ? tr("-") : mod.version);
    ui->modDownloadsLabel->setText(QString::number(mod.download_count));

    ui->statusArchiveLabel->setText(
        tr("Archive Type: %1").arg(mod.archive_type.isEmpty() ? tr("-") : mod.archive_type.toUpper()));

    QString mod_url = mod.website_url;
    if (mod_url.isEmpty()) {
        QString item_type_plural = mod.item_type.toLower();
        if (item_type_plural == QStringLiteral("mod")) {
            item_type_plural = QStringLiteral("mods");
        } else {
            item_type_plural += QStringLiteral("s");
        }
        mod_url = QStringLiteral("https://gamebanana.com/%1/%2").arg(item_type_plural, mod.id);
    }

    const QString accent = Theme::GetAccentColor();
    ui->statusLinkLabel->setText(
        tr("<a href=\"%1\" style=\"color: %2; text-decoration: none;\">Website Link For "
           "Mod</a>")
            .arg(mod_url, accent));

    if (!mod.description.isEmpty()) {
        ui->modDescriptionBrowser->setHtml(mod.description);
    } else {
        ui->modDescriptionBrowser->setPlainText(tr("No description available."));
    }

    ui->screenshotLabel->clear();
    ui->screenshotLabel->setText(tr("Loading preview..."));
    if (!mod.screenshots.isEmpty()) {
        QString url = mod.screenshots[0];
        if (screenshot_cache.contains(url)) {
            ui->screenshotLabel->setPixmap(screenshot_cache[url]);
        } else {
            QNetworkRequest request{QUrl{url}};
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              QStringLiteral("CitronEmulator/1.0"));
            QNetworkReply* reply = service->GetNetworkManager()->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    QPixmap pixmap;
                    if (pixmap.loadFromData(reply->readAll())) {
                        pixmap = pixmap.scaled(ui->screenshotLabel->size(), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
                        screenshot_cache[url] = pixmap;
                        if (selected_mod.screenshots.contains(url)) {
                            ui->screenshotLabel->setPixmap(pixmap);
                        }
                    }
                }
            });
        }
    } else {
        ui->screenshotLabel->setText(tr("No preview available"));
    }

    ui->buttonDownload->setEnabled(!mod.download_url.isEmpty());
    ui->statusLabel->setText(tr("Ready to download"));
}

void GameBananaDialog::OnDownloadClicked() {
    if (selected_mod.download_url.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("No download URL available for this mod."));
        return;
    }

    StartDownload(selected_mod);
}

void GameBananaDialog::StartDownload(const GameBananaMod& mod) {
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    ui->buttonDownload->setEnabled(false);
    ui->statusLabel->setText(tr("Downloading %1...").arg(mod.name));

    std::filesystem::path mod_folder;
    namespace FS = Common::FS;

    if (ui->locationComboBox->currentIndex() == 0) {
        mod_folder = FS::GetCitronPath(FS::CitronPath::LoadDir) / title_id.toStdString() /
                     mod.name.toStdString();
    } else {
        mod_folder = FS::GetCitronPath(FS::CitronPath::SDMCDir) / "atmosphere" / "contents" /
                     title_id.toStdString();
    }

    std::filesystem::create_directories(mod_folder);
    QString dest_file = QString::fromStdString((mod_folder / mod.file_name.toStdString()).string());
    service->DownloadMod(mod.download_url, dest_file);
}

void GameBananaDialog::FinishDownload(const QString& file_path) {
    ui->progressBar->setVisible(false);
    ui->buttonDownload->setEnabled(true);

    // 1. Backup logic
    if (!ui->disableBackupModArchives->isChecked()) {
        QString backup_dir =
            QCoreApplication::applicationDirPath() + QStringLiteral("/citron-mods");
        QDir().mkpath(backup_dir);
        QString backup_path = backup_dir + QStringLiteral("/") + QFileInfo(file_path).fileName();
        if (QFile::copy(file_path, backup_path)) {
            LOG_INFO(WebService, "Backed up mod archive to: {}", backup_path.toStdString());
        }
    }

    // 2. Check for archive and extraction
    bool is_archive = file_path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) ||
                      file_path.endsWith(QStringLiteral(".7z"), Qt::CaseInsensitive) ||
                      file_path.endsWith(QStringLiteral(".rar"), Qt::CaseInsensitive);

    if (is_archive) {
        bool always_ask = ui->alwaysAskManualExtraction->isChecked();
        bool can_extract = ZipExtractor::CanExtract();

        if (always_ask || !can_extract) {
            QString reason = always_ask ? tr("User requested manual extraction.")
                                        : tr("No system extraction tools (7z/unzip) found.");

            ui->statusLabel->setText(tr("Manual extraction required"));
            auto reply = QMessageBox::information(
                this, tr("Manual Extraction Required"),
                tr("%1\n\nThe mod archive has been downloaded to the mod directory. "
                   "Please manually extract its contents to complete the installation.\n\n"
                   "Open mod folder now?")
                    .arg(reason),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                OpenModFolder(QFileInfo(file_path).absolutePath());
            }
            return;
        }

        ui->statusLabel->setText(tr("Extracting mod..."));
        QFileInfo file_info(file_path);
        QString mod_folder = file_info.absolutePath();

        bool success = ZipExtractor::ExtractAndOrganize(file_path, mod_folder);

        if (success) {
            QFile::remove(file_path); // Only remove if successful
            ui->statusLabel->setText(tr("Mod installed successfully!"));
            QMessageBox::information(
                this, tr("Success"),
                tr("The mod '%1' has been installed successfully.").arg(selected_mod.name));
        } else {
            ui->statusLabel->setText(tr("Extraction failed - Archive preserved"));
            auto reply = QMessageBox::warning(this, tr("Extraction Error"),
                                              tr("Failed to extract the mod automatically. The "
                                                 "archive has been preserved in the mod directory "
                                                 "for manual extraction.\n\nOpen mod folder now?"),
                                              QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                OpenModFolder(mod_folder);
            }
        }
    } else {
        ui->statusLabel->setText(tr("Mod downloaded successfully!"));
        QMessageBox::information(this, tr("Success"),
                                 tr("The mod '%1' has been downloaded.").arg(selected_mod.name));
    }
}

void GameBananaDialog::OpenModFolder(const QString& path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void GameBananaDialog::OnCancelClicked() {
    service->CancelDownload();
    reject();
}

void GameBananaDialog::UpdateTheme() {
    const bool is_dark = UISettings::IsDarkTheme();
    const QString accent = Theme::GetAccentColor();
    const QString bg = is_dark ? QStringLiteral("#15151a") : QStringLiteral("#f5f5fa");
    const QString txt = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString sub_txt = is_dark ? QStringLiteral("#888890") : QStringLiteral("#666670");
    const QString panel = is_dark ? QStringLiteral("#1c1c22") : QStringLiteral("#ffffff");
    const QString border = is_dark ? QStringLiteral("#2d2d35") : QStringLiteral("#dcdce2");

    QString style = ConfigurationStyling::GetMasterStyleSheet();
    style += QStringLiteral(
        "QDialog#GameBananaDialog { background-color: %1; color: %2; }"
        "QLabel { color: %2; }"
        "QLineEdit, QComboBox { background-color: %4; border: 1px solid %5; border-radius: 6px; "
        "padding: 5px; color: %2; }"
        "QListWidget { background-color: %6; border: 1px solid %5; border-radius: 12px; padding: "
        "8px; }"
        "QListWidget::item { background-color: %4; border-radius: 8px; margin-bottom: 6px; "
        "padding: 12px; color: %2; }"
        "QListWidget::item:selected { background-color: %5; border: 2px solid %3; }"
        "QPushButton { background-color: %4; border: 1px solid %5; border-radius: 10px; padding: "
        "8px 16px; color: %2; font-weight: bold; }"
        "QPushButton:hover { background-color: %5; }"
        "QPushButton#buttonDownload { background-color: %3; color: #000000; }"
        "QPushButton#buttonDownload:disabled { background-color: %5; color: %7; }"
        "QGroupBox { border: 2px solid %5; border-radius: 12px; margin-top: %8px; padding-top: "
        "%9px; color: %3; font-weight: bold; }"
        "QTextBrowser { background-color: %6; border: 1px solid %5; border-radius: 8px; color: "
        "%2; padding: 10px; }"
        "QProgressBar { border: 1px solid %5; border-radius: 6px; text-align: center; "
        "background-color: %6; height: 10px; }"
        "QProgressBar::chunk { background-color: %3; border-radius: 5px; }"
        "QCheckBox { color: %2; font-weight: 600; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid %5; border-radius: "
        "5px; background: %4; }"
        "QCheckBox::indicator:checked { background: %3; border-color: %3; }")
        .arg(bg, txt, accent, panel, border, bg, sub_txt, 
             UISettings::IsGamescope() ? "12" : "24", 
             UISettings::IsGamescope() ? "10" : "18");

    setStyleSheet(style);

    // Update the mod version label color specifically
    ui->modVersionLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(accent));
}

} // namespace ModManager
