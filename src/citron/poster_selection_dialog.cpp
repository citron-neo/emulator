#include <QGridLayout>
#include <QLabel>
#include <QMovie>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include "citron/poster_selection_dialog.h"
#include "citron/theme.h"
#include "citron/uisettings.h"

PosterSelectionDialog::PosterSelectionDialog(QWidget* parent, u64 program_id,
                                             const QString& game_name, Citron::SteamGridDB* sgdb)
    : QDialog(parent), m_program_id(program_id), m_game_name(game_name), m_sgdb(sgdb) {

    setWindowTitle(tr("Select Poster - %1").arg(game_name));
    setMinimumSize(800, 600);
    resize(1000, 750);

    const bool is_dark = UISettings::IsDarkTheme();
    const QString bg_color = is_dark ? QStringLiteral("#1c1c22") : QStringLiteral("#f0f0f5");
    const QString text_color = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString card_color = is_dark ? QStringLiteral("#2a2a32") : QStringLiteral("#ffffff");

    setStyleSheet(
        QStringLiteral("QDialog { background-color: %1; color: %2; }").arg(bg_color, text_color));

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(15);

    auto* header =
        new QLabel(tr("Choose a poster for <b>%1</b> from SteamGridDB:").arg(game_name), this);
    header->setStyleSheet(QStringLiteral("font-size: 12pt; color: %1;").arg(text_color));
    main_layout->addWidget(header);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setStyleSheet(QStringLiteral("QScrollArea { background-color: transparent; }"));

    m_container = new QWidget();
    m_container->setStyleSheet(QStringLiteral("background-color: transparent;"));
    m_layout = new QGridLayout(m_container);
    m_layout->setSpacing(20);
    m_layout->setContentsMargins(10, 10, 10, 10);

    m_scroll_area->setWidget(m_container);
    main_layout->addWidget(m_scroll_area);

    m_loading_label = new QLabel(tr("Fetching posters..."), this);
    m_loading_label->setAlignment(Qt::AlignCenter);
    m_loading_label->setStyleSheet(
        QStringLiteral("font-size: 14pt; font-weight: bold; color: %1;").arg(text_color));
    main_layout->addWidget(m_loading_label);

    auto* btn_box = new QHBoxLayout();
    auto* btn_cancel = new QPushButton(tr("Cancel"), this);
    btn_cancel->setFixedWidth(120);
    btn_cancel->setStyleSheet(
        QStringLiteral("QPushButton { background-color: #44444a; color: white; border: none; "
                       "padding: 8px; border-radius: 4px; font-weight: bold; }"
                       "QPushButton:hover { background-color: #5a5a62; }"));

    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);
    btn_box->addStretch();
    btn_box->addWidget(btn_cancel);
    main_layout->addLayout(btn_box);

    FetchOptions();
}

PosterSelectionDialog::~PosterSelectionDialog() = default;

void PosterSelectionDialog::FetchOptions() {
    m_sgdb->FetchPosterOptions(
        m_program_id, m_game_name.toStdString(),
        [this](bool success, std::vector<Citron::SteamGridDBPoster> options) {
            QMetaObject::invokeMethod(
                this, [this, success, options]() { OnOptionsFetched(success, options); },
                Qt::QueuedConnection);
        });
}

void PosterSelectionDialog::OnOptionsFetched(bool success,
                                             std::vector<Citron::SteamGridDBPoster> options) {
    m_loading_label->hide();

    if (!success || options.empty()) {
        auto* err =
            new QLabel(tr("No posters found for this game. Check your API key or game title. Go to "
                          "Configure -> Web to add your SteamGridDB API key if not done yet."),
                       m_container);
        err->setAlignment(Qt::AlignCenter);
        m_layout->addWidget(err, 0, 0);
        return;
    }

    for (int i = 0; i < (int)options.size(); ++i) {
        AddPosterOption(options[i], i);
    }
}

void PosterSelectionDialog::AddPosterOption(const Citron::SteamGridDBPoster& poster, int index) {
    auto* card = new QWidget(m_container);
    const bool is_dark = UISettings::IsDarkTheme();
    const QString card_color = is_dark ? QStringLiteral("#2d2d35") : QStringLiteral("#ffffff");
    const QString text_color = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString sub_text = is_dark ? QStringLiteral("#a0a0ab") : QStringLiteral("#66666e");

    card->setStyleSheet(
        QStringLiteral("QWidget { background-color: %1; border-radius: 8px; }").arg(card_color));
    card->setFixedWidth(200);

    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(10, 10, 10, 10);
    card_layout->setSpacing(8);

    auto* img_label = new QLabel(card);
    img_label->setFixedSize(180, 270);
    img_label->setAlignment(Qt::AlignCenter);
    img_label->setCursor(Qt::PointingHandCursor);
    img_label->setStyleSheet(QStringLiteral(
        "background-color: #15151a; border-radius: 4px; border: 1px solid #3d3d45;"));
    card_layout->addWidget(img_label);

    auto* author_label = new QLabel(tr("By %1").arg(QString::fromStdString(poster.author)), card);
    author_label->setStyleSheet(QStringLiteral("color: %1; font-size: 9pt;").arg(sub_text));
    author_label->setWordWrap(true);
    author_label->setAlignment(Qt::AlignCenter);
    card_layout->addWidget(author_label);

    auto* btn_select = new QPushButton(tr("Set as Poster"), card);
    const QString accent_hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    const QColor accent = QColor(accent_hex).isValid() ? QColor(accent_hex) : QColor(0, 150, 255);

    // Use a more distinct button style with a dark background if accent is too light
    const bool is_accent_light = accent.lightness() > 200;
    const QString btn_bg = is_accent_light ? QStringLiteral("#32323a") : accent.name();
    const QString btn_hover =
        is_accent_light ? QStringLiteral("#42424a") : accent.lighter(110).name();

    btn_select->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; color: white; border: none; padding: "
                       "8px; border-radius: 6px; font-weight: bold; }"
                       "QPushButton:hover { background-color: %2; }"
                       "QPushButton:focus { background-color: %2; border: 2px solid %3; }")
            .arg(btn_bg, btn_hover, accent.name()));
    btn_select->setCursor(Qt::PointingHandCursor);
    m_buttons.append(btn_select);

    std::string url = poster.url;
    auto select_func = [this, url]() { OnPosterSelected(url); };
    connect(btn_select, &QPushButton::clicked, select_func);

    card_layout->addWidget(btn_select);

    int row = index / 4;
    int col = index % 4;
    m_layout->addWidget(card, row, col);

    DownloadThumbnail(QString::fromStdString(poster.thumb_url), img_label);
}

void PosterSelectionDialog::DownloadThumbnail(const QString& url, QLabel* target_label) {
    QNetworkRequest request((QUrl(url)));
    QNetworkReply* reply = m_network_manager.get(request);

    connect(reply, &QNetworkReply::finished, [reply, target_label]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pix;
            if (pix.loadFromData(reply->readAll())) {
                target_label->setPixmap(pix.scaled(target_label->size(),
                                                   Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation));
            }
        }
    });
}

void PosterSelectionDialog::OnPosterSelected(const std::string& url) {
    m_sgdb->DownloadSpecificPoster(m_program_id, url, [this](bool success, std::string path) {
        QMetaObject::invokeMethod(
            this,
            [this, success]() {
                if (success) {
                    accept();
                }
            },
            Qt::QueuedConnection);
    });
}
void PosterSelectionDialog::onNavigated(int dx, int dy) {
    if (m_buttons.isEmpty()) return;
    
    // Grid is 4 columns wide
    int next_index = m_current_index;
    if (next_index == -1) {
        next_index = 0;
    } else {
        if (dx != 0) next_index += dx;
        if (dy != 0) next_index += dy * 4;
    }

    next_index = std::clamp(next_index, 0, static_cast<int>(m_buttons.size()) - 1);
    if (next_index != m_current_index) {
        m_current_index = next_index;
        m_buttons[m_current_index]->setFocus();
        m_scroll_area->ensureWidgetVisible(m_buttons[m_current_index]);
    }
}

void PosterSelectionDialog::onActivated() {
    if (m_current_index >= 0 && m_current_index < m_buttons.size()) {
        m_buttons[m_current_index]->animateClick();
    }
}

void PosterSelectionDialog::onCancelled() {
    reject();
}
