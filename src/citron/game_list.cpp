// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <random>

#include <vector>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QEasingCurve>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTimer>
#include <QtGlobal>

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QSequentialAnimationGroup>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <fmt/format.h>
#include "citron/compatibility_list.h"
#include "citron/custom_metadata.h"
#include "citron/custom_metadata_dialog.h"
#include "citron/game_details_panel.h"
#include "citron/game_grid_delegate.h"
#include "citron/game_list.h"
#include "citron/game_list_delegate.h"
#include "citron/game_list_loading_overlay.h"
#include "citron/game_list_p.h"
#include "citron/game_list_worker.h"
#include "citron/main.h"
#include "citron/mod_manager/gamebanana_dialog.h"
#include "citron/multiplayer/state.h"
#include "citron/theme.h"
#include "citron/ui/game_carousel_view.h"
#include "citron/ui/game_grid_view.h"
#include "citron/ui/game_tree_view.h"
#include "citron/ui/nav_settings_overlay.h"
#include "citron/uisettings.h"
#include "citron/util/blackjack_widget.h"
#include "citron/util/card_flip.h"
#include "citron/util/confetti.h"
#include "citron/util/controller_navigation.h"
#include "citron/util/plinko_widget.h"
#include "common/common_types.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/savedata_factory.h"
#include "core/hle/service/acc/profile_manager.h"

// A helper struct to cleanly pass game data
struct SurpriseGame {
    QString name;
    QString path;
    quint64 title_id;
    QPixmap icon;
};

// This is the custom widget that shows the actual spinning game icons
class GameReelWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal scrollOffset READ getScrollOffset WRITE setScrollOffset)

public:
    explicit GameReelWidget(QWidget* parent = nullptr) : QWidget(parent), m_scroll_offset(0.0) {
        setMinimumHeight(160);
    }

    void setGameReel(const QVector<SurpriseGame>& games) {
        m_games = games;
        update();
    }

    qreal getScrollOffset() const {
        return m_scroll_offset;
    }
    void setScrollOffset(qreal offset) {
        m_scroll_offset = offset;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (m_games.isEmpty())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        painter.fillRect(rect(), palette().color(QPalette::Window));

        const int icon_size = 192;
        const int icon_spacing = 30;
        const int total_slot_width = icon_size + icon_spacing;
        const int widget_center_x = width() / 2;
        const int widget_center_y = height() / 2;

        // Subtle horizontal track line behind icons
        const bool dark = Theme::IsDarkMode();
        painter.setPen(
            QPen(dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 20), 1, Qt::DashLine));
        painter.drawLine(0, widget_center_y, width(), widget_center_y);

        // Sleek small centering markers at top and bottom
        QColor accent(Theme::GetAccentColor());
        if (!accent.isValid())
            accent = QColor(0, 150, 255);
        painter.setPen(QPen(accent, 3));
        painter.drawLine(widget_center_x, 5, widget_center_x, 25);
        painter.drawLine(widget_center_x, height() - 25, widget_center_x, height() - 5);

        for (int i = 0; i < m_games.size(); ++i) {
            const qreal icon_x_position = (static_cast<qreal>(widget_center_x) - icon_size / 2.0) +
                                          (i * static_cast<qreal>(total_slot_width)) -
                                          m_scroll_offset;
            const int draw_x = static_cast<int>(icon_x_position);
            const int draw_y = widget_center_y - (icon_size / 2);

            if (draw_x + icon_size < 0 || draw_x > width()) {
                continue;
            }

            painter.save();

            QPainterPath path;
            path.addRoundedRect(draw_x, draw_y, icon_size, icon_size, 12, 12);
            painter.setClipPath(path);

            // Draw original high-res icon with smooth scaling
            painter.drawPixmap(draw_x, draw_y, icon_size, icon_size, m_games[i].icon);

            painter.restore();
        }
    }

private:
    QVector<SurpriseGame> m_games;
    qreal m_scroll_offset;
};

class LogoAnimationWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal rotation READ getRotation WRITE setRotation)
    Q_PROPERTY(qreal scale READ getScale WRITE setScale)

public:
    explicit LogoAnimationWidget(QWidget* parent = nullptr)
        : QWidget(parent), m_rotation(0.0), m_scale(1.0) {
        m_logo_pixmap.load(QStringLiteral(":/citron.svg"));
        setAttribute(Qt::WA_TranslucentBackground);
    }

    qreal getRotation() const {
        return m_rotation;
    }
    void setRotation(qreal rotation) {
        m_rotation = rotation;
        update();
    }

    qreal getScale() const {
        return m_scale;
    }
    void setScale(qreal scale) {
        m_scale = scale;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (m_logo_pixmap.isNull())
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const int centerX = width() / 2;
        const int centerY = height() / 2;

        painter.translate(centerX, centerY);
        painter.rotate(m_rotation);
        painter.scale(m_scale, m_scale);

        // Draw the logo centered at (0, 0)
        const int logoSize = UISettings::IsGamescope() ? 250 : 400;
        painter.drawPixmap(-logoSize / 2, -logoSize / 2, logoSize, logoSize, m_logo_pixmap);
    }

private:
    QPixmap m_logo_pixmap;
    qreal m_rotation;
    qreal m_scale;
};

// This is the main pop-up window that holds the spinning icons, title, and buttons
class SurpriseMeDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Reel, Cards, Plinko, Blackjack };

    explicit SurpriseMeDialog(QVector<SurpriseGame> games, QWidget* parent = nullptr)
        : QDialog(parent), m_available_games(games),
          m_last_choice({QString(), QString(), 0, QPixmap()}) {
        setWindowTitle(tr("Surprise Me!"));
        setModal(true);

        const bool is_gamescope = UISettings::IsGamescope();
        if (is_gamescope) {
            setFixedSize(1280, 800);
        } else {
            setFixedSize(850, 640);
        }

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(is_gamescope ? 12 : 15);
        layout->setContentsMargins(is_gamescope ? 20 : 15, is_gamescope ? 20 : 15,
                                   is_gamescope ? 20 : 15, is_gamescope ? 20 : 15);

        // Navigation Bar
        const bool dark = Theme::IsDarkMode();
        auto* nav_layout = new QHBoxLayout();
        auto* reel_btn = new QPushButton(tr("Reel"), this);
        auto* cards_btn = new QPushButton(tr("Cards"), this);
        auto* plinko_btn = new QPushButton(tr("Plinko"), this);
        auto* blackjack_btn = new QPushButton(tr("Blackjack"), this);

        QString accent = Theme::GetAccentColor();
        if (accent.isEmpty())
            accent = QStringLiteral("#0096ff");

        QString nav_style =
            dark ? QStringLiteral(
                       "QPushButton { background: #333; color: #bbb; border: none; padding: 5px "
                       "15px; border-radius: 4px; }"
                       "QPushButton:hover { background: #444; color: white; }"
                       "QPushButton:checked { background: #555; color: %1; font-weight: bold; }")
                       .arg(accent)
                 : QStringLiteral(
                       "QPushButton { background: #eee; color: #666; border: 1px solid #ddd; "
                       "padding: 5px 15px; border-radius: 4px; }"
                       "QPushButton:hover { background: #f5f5f5; color: #333; border-color: #ccc; }"
                       "QPushButton:checked { background: #fff; color: %1; border-color: %1; "
                       "font-weight: bold; }")
                       .arg(accent);

        for (auto* btn : {reel_btn, cards_btn, plinko_btn, blackjack_btn}) {
            btn->setCheckable(true);
            btn->setStyleSheet(nav_style);
            nav_layout->addWidget(btn);
        }
        reel_btn->setChecked(true);

        m_reel_widget = new GameReelWidget(this);
        m_card_widget = new CardFlipWidget(this);
        m_plinko_widget = new PlinkoWidget(this);
        m_blackjack_widget = new BlackjackWidget(this);
        m_confetti_widget = new ConfettiWidget(this);

        m_stack = new QStackedWidget(this);
        m_stack->addWidget(m_reel_widget);
        m_stack->addWidget(m_card_widget);
        m_stack->addWidget(m_plinko_widget);
        m_stack->addWidget(m_blackjack_widget);

        m_game_title_label = new QLabel(tr("Ready?"), this);
        m_launch_button = new QPushButton(tr("Launch Game"), this);
        m_reroll_button = new QPushButton(tr("Try Again?"), this);
        m_exit_button = new QPushButton(tr("Exit"), this);

        m_launch_button->setFixedHeight(35);
        m_reroll_button->setFixedHeight(35);
        m_exit_button->setFixedHeight(35);

        QString btn_style =
            dark
                ? QStringLiteral(
                      "QPushButton { background: #333; color: white; border: 1px solid #444; "
                      "border-radius: 6px; padding: 0 20px; }"
                      "QPushButton:hover { background: #444; border-color: #555; }"
                      "QPushButton:pressed { background: #222; }"
                      "QPushButton:disabled { background: #222; color: #555; border-color: #333; }")
                : QStringLiteral("QPushButton { background: white; color: #333; border: 1px solid "
                                 "#ccc; border-radius: 6px; padding: 0 20px; }"
                                 "QPushButton:hover { background: #f8f8f8; border-color: #bbb; }"
                                 "QPushButton:pressed { background: #eeeeee; }"
                                 "QPushButton:disabled { background: #f0f0f0; color: #aaa; "
                                 "border-color: #ddd; }");

        for (auto* btn : {m_launch_button, m_reroll_button, m_exit_button}) {
            btn->setStyleSheet(btn_style);
        }

        QFont title_font = m_game_title_label->font();
        title_font.setPointSize(16);
        title_font.setBold(true);
        m_game_title_label->setFont(title_font);
        m_game_title_label->setAlignment(Qt::AlignCenter);

        auto* button_layout = new QHBoxLayout();
        button_layout->addStretch();
        button_layout->addWidget(m_reroll_button);
        button_layout->addWidget(m_launch_button);
        button_layout->addWidget(m_exit_button);

        layout->addLayout(nav_layout);
        layout->addWidget(m_stack);
        layout->addWidget(m_game_title_label);
        layout->addLayout(button_layout);

        m_launch_button->setEnabled(false);
        m_reroll_button->setEnabled(false);

        m_animation = new QPropertyAnimation(m_reel_widget, "scrollOffset", this);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(reel_btn, &QPushButton::clicked, this, [this] { setMode(Mode::Reel); });
        connect(cards_btn, &QPushButton::clicked, this, [this] { setMode(Mode::Cards); });
        connect(plinko_btn, &QPushButton::clicked, this, [this] { setMode(Mode::Plinko); });
        connect(blackjack_btn, &QPushButton::clicked, this, [this] { setMode(Mode::Blackjack); });

        connect(m_launch_button, &QPushButton::clicked, this, &SurpriseMeDialog::onLaunch);
        connect(m_reroll_button, &QPushButton::clicked, this, &SurpriseMeDialog::startRoll);
        connect(m_exit_button, &QPushButton::clicked, this, &SurpriseMeDialog::reject);
        connect(m_card_widget, &CardFlipWidget::gameSelected, this,
                &SurpriseMeDialog::onGameSelected);
        connect(m_plinko_widget, &PlinkoWidget::gameSelected, this,
                &SurpriseMeDialog::onGameSelected);
        connect(m_blackjack_widget, &BlackjackWidget::gameSelected, this,
                &SurpriseMeDialog::onGameSelected);

        QTimer::singleShot(100, this, &SurpriseMeDialog::startRoll);
    }

    void resizeEvent(QResizeEvent* event) override {
        QDialog::resizeEvent(event);
        m_confetti_widget->setGeometry(rect());
    }

    const SurpriseGame& getFinalChoice() const {
        return m_last_choice;
    }

private slots:
    void setMode(Mode mode) {
        m_current_mode = mode;
        m_stack->setCurrentIndex(static_cast<int>(mode));
        updateTitleFont();

        // Update check state of nav buttons
        for (int i = 0; i < 4; ++i) {
            auto* btn =
                qobject_cast<QPushButton*>(layout()->itemAt(0)->layout()->itemAt(i)->widget());
            if (btn)
                btn->setChecked(i == static_cast<int>(mode));
        }

        startRoll();
    }

    void onGameSelected(int index) {
        // Ignore signals from widgets that are not currently visible
        if (sender() != m_stack->currentWidget()) {
            return;
        }

        if (m_current_mode == Mode::Reel) {
            return;
        }

        if (index == -1) {
            // Loss or Push
            m_last_choice.name = tr("Try again!");
            m_launch_button->setEnabled(false);
            if (!m_available_games.isEmpty()) {
                m_reroll_button->setEnabled(true);
            } else if (m_current_mode != Mode::Reel) {
                // Allow reroll in minigames even if pool is empty (for fun)
                m_reroll_button->setEnabled(true);
            }
            m_game_title_label->setText(m_last_choice.name);
            m_reroll_button->update();
            return;
        }

        if (m_current_mode == Mode::Cards || m_current_mode == Mode::Plinko ||
            m_current_mode == Mode::Blackjack) {
            if (index >= 0 && index < m_card_pool.size()) {
                m_last_choice = m_card_pool[index];
            }
        } else {
            m_last_choice = m_available_games[index % m_available_games.size()];
        }

        m_confetti_widget->burst();
        onRollFinished();
    }

    void updateTitleFont() {
        QFont font = m_game_title_label->font();
        font.setBold(true);
        if (m_current_mode == Mode::Reel) {
            font.setPointSize(28);
        } else if (m_current_mode == Mode::Cards) {
            font.setPointSize(24);
        } else {
            font.setPointSize(18);
        }
        m_game_title_label->setFont(font);
    }

    void startRoll() {
        m_animation->stop();
        disconnect(m_animation, &QPropertyAnimation::finished, nullptr, nullptr);

        if (m_available_games.isEmpty() && m_current_mode == Mode::Reel) {
            m_game_title_label->setText(tr("No more games to choose!"));
            m_reroll_button->setEnabled(false);
            return;
        }

        m_launch_button->setEnabled(false);
        m_reroll_button->setEnabled(false);
        m_last_choice = {QString(), QString(), 0, QPixmap()};

        // Prep data for widgets
        std::vector<QImage> icons;
        QVector<SurpriseGame> temp_pool = m_available_games;
        m_card_pool.clear();

        std::random_device rd;
        std::mt19937 gen(rd());

        auto pickGames = [&](int count) {
            for (int i = 0; i < count && !temp_pool.isEmpty(); ++i) {
                std::uniform_int_distribution<> d(0, temp_pool.size() - 1);
                int idx = d(gen);
                icons.push_back(temp_pool[idx].icon.toImage());
                m_card_pool.push_back(temp_pool[idx]);
                temp_pool.removeAt(idx);
            }
        };

        if (m_current_mode == Mode::Cards) {
            m_game_title_label->setText(tr("Pick a Card!"));
            pickGames(5);
            m_card_widget->setGames(icons);
            m_card_widget->reset();
            return;
        } else if (m_current_mode == Mode::Plinko) {
            m_game_title_label->setText(tr("Drop the Ball!"));
            pickGames(5); // 5 Bins
            m_plinko_widget->setGames(icons);
            m_plinko_widget->reset();
            return;
        } else if (m_current_mode == Mode::Blackjack) {
            m_game_title_label->setText(tr("Blackjack!"));
            pickGames(m_available_games.size());
            m_blackjack_widget->setGames(icons);
            m_blackjack_widget->reset();
            return;
        }

        m_game_title_label->setText(tr("Spinning..."));

        std::uniform_int_distribution<> full_distrib(
            0, static_cast<int>(m_available_games.size() - 1));
        const int winning_index = full_distrib(gen);

        const SurpriseGame winner = m_available_games.at(winning_index);
        m_available_games.removeAt(winning_index);

        QVector<SurpriseGame> reel;
        if (!m_available_games.isEmpty()) {
            std::uniform_int_distribution<> filler_distrib(0, m_available_games.size() - 1);
            for (int i = 0; i < 20; ++i)
                reel.push_back(m_available_games.at(filler_distrib(gen)));
            reel.push_back(winner);
            for (int i = 0; i < 20; ++i)
                reel.push_back(m_available_games.at(filler_distrib(gen)));
        } else {
            reel.push_back(winner);
        }

        m_reel_widget->setGameReel(reel);

        const int icon_size = 192;
        const int icon_spacing = 30;
        const int total_slot_width = icon_size + icon_spacing;
        const qreal start_offset = 0;

        const int winning_reel_index = m_available_games.isEmpty() ? 0 : 20;
        const qreal end_offset = (winning_reel_index * total_slot_width);

        m_animation->stop();
        m_reel_widget->setScrollOffset(start_offset);
        m_animation->setDuration(4000);
        m_animation->setStartValue(start_offset);
        m_animation->setEndValue(end_offset);

        disconnect(m_animation, &QPropertyAnimation::finished, nullptr, nullptr);
        connect(m_animation, &QPropertyAnimation::finished, this, [this, winner]() {
            m_last_choice = winner;
            m_confetti_widget->burst();
            onRollFinished();
        });

        m_animation->start();
    }

    void onRollFinished() {
        m_game_title_label->setText(m_last_choice.name);
        m_launch_button->setEnabled(true);
        m_reroll_button->setEnabled(true); // Always allow try again on finish
        update();
    }

    void onLaunch() {
        accept();
    }

private:
    QVector<SurpriseGame> m_available_games;
    QVector<SurpriseGame> m_card_pool;
    SurpriseGame m_last_choice;

    QStackedWidget* m_stack;
    GameReelWidget* m_reel_widget;
    CardFlipWidget* m_card_widget;
    PlinkoWidget* m_plinko_widget;
    BlackjackWidget* m_blackjack_widget;
    ConfettiWidget* m_confetti_widget;

    QLabel* m_game_title_label;
    QPushButton* m_launch_button;
    QPushButton* m_reroll_button;
    QPushButton* m_exit_button;
    QPropertyAnimation* m_animation;
    Mode m_current_mode = Mode::Reel;
};

// Static helper for Save Detection
static QString GetDetectedEmulatorName(const QString& path, u64 program_id,
                                       const QString& citron_nand_base) {
    QString abs_path = QDir(path).absolutePath();
    QString citron_abs_base = QDir(citron_nand_base).absolutePath();
    QString tid_str = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));

    // SELF-EXCLUSION
    if (abs_path.startsWith(citron_abs_base, Qt::CaseInsensitive)) {
        return QString{};
    }

    // Ryujinx
    if (abs_path.contains(QStringLiteral("bis/user/save"), Qt::CaseInsensitive)) {
        if (abs_path.contains(QStringLiteral("ryubing"), Qt::CaseInsensitive))
            return QStringLiteral("Ryubing");
        if (abs_path.contains(QStringLiteral("ryujinx"), Qt::CaseInsensitive))
            return QStringLiteral("Ryujinx");

        // Fallback if it's a generic Ryujinx-structure folder
        return abs_path.contains(tid_str, Qt::CaseInsensitive)
                   ? QStringLiteral("Ryujinx/Ryubing")
                   : QStringLiteral("Ryujinx/Ryubing (Manual Slot)");
    }

    // Fork
    if (abs_path.contains(QStringLiteral("nand/user/save"), Qt::CaseInsensitive) ||
        abs_path.contains(QStringLiteral("nand/system/Containers"), Qt::CaseInsensitive)) {

        if (abs_path.contains(QStringLiteral("eden"), Qt::CaseInsensitive))
            return QStringLiteral("Eden");
        if (abs_path.contains(QStringLiteral("suyu"), Qt::CaseInsensitive))
            return QStringLiteral("Suyu");
        if (abs_path.contains(QStringLiteral("sudachi"), Qt::CaseInsensitive))
            return QStringLiteral("Sudachi");
        if (abs_path.contains(QStringLiteral("yuzu"), Qt::CaseInsensitive))
            return QStringLiteral("Yuzu");

        return QStringLiteral("another emulator");
    }

    return QString{};
}

GameListSearchField::KeyReleaseEater::KeyReleaseEater(GameList* gamelist_, QObject* parent)
    : QObject(parent), gamelist{gamelist_} {}

// EventFilter in order to process systemkeys while editing the searchfield
bool GameListSearchField::KeyReleaseEater::eventFilter(QObject* obj, QEvent* event) {
    // If it isn't a KeyRelease event then continue with standard event processing
    if (event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(obj, event);

    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
    QString edit_filter_text = gamelist->search_field->edit_filter->text().toLower();

    // If the searchfield's text hasn't changed special function keys get checked
    // If no function key changes the searchfield's text the filter doesn't need to get reloaded
    if (edit_filter_text == edit_filter_text_old) {
        switch (keyEvent->key()) {
        case Qt::Key_Escape: {
            if (edit_filter_text_old.isEmpty()) {
                return QObject::eventFilter(obj, event);
            } else {
                gamelist->search_field->edit_filter->clear();
                edit_filter_text.clear();
            }
            break;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            if (gamelist->search_field->visible == 1) {
                const QString file_path = gamelist->GetLastFilterResultItem();
                gamelist->search_field->edit_filter->clear();
                edit_filter_text.clear();
                emit gamelist->GameChosen(file_path);
            } else {
                return QObject::eventFilter(obj, event);
            }
            break;
        }
        default:
            return QObject::eventFilter(obj, event);
        }
    }
    edit_filter_text_old = edit_filter_text;
    return QObject::eventFilter(obj, event);
}

void GameListSearchField::setFilterResult(int visible_, int total_) {
    visible = visible_;
    total = total_;
    label_filter_result->setText(tr("%1 of %n result(s)", "", total).arg(visible));
}

QString GameListSearchField::filterText() const {
    return edit_filter->text();
}

QString GameList::GetLastFilterResultItem() const {
    QString file_path;
    for (int i = 1; i < item_model->rowCount() - 1; ++i) {
        const QStandardItem* folder = item_model->item(i, 0);
        const QModelIndex folder_index = folder->index();
        const int children_count = folder->rowCount();
        for (int j = 0; j < children_count; ++j) {
            if (tree_view->isRowHidden(j, folder_index)) {
                continue;
            }
            const QStandardItem* child = folder->child(j, 0);
            file_path = child->data(GameListItemPath::FullPathRole).toString();
        }
    }
    return file_path;
}

void GameListSearchField::clear() {
    edit_filter->clear();
}

void GameListSearchField::setFocus() {
    if (edit_filter->isVisible()) {
        edit_filter->setFocus();
    }
}

GameListSearchField::GameListSearchField(GameList* parent) : QWidget{parent} {
    auto* const key_release_eater = new KeyReleaseEater(parent, this);
    layout_filter = new QHBoxLayout;
    layout_filter->setContentsMargins(0, 0, 0, 0);
    label_filter = new QLabel;
    edit_filter = new QLineEdit;
    edit_filter->clear();
    edit_filter->installEventFilter(key_release_eater);
    edit_filter->setClearButtonEnabled(true);
    edit_filter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(edit_filter, &QLineEdit::textChanged, parent, &GameList::OnTextChanged);
    label_filter_result = new QLabel;
    button_filter_close = new QToolButton(this);
    button_filter_close->setText(QStringLiteral("X"));
    button_filter_close->setCursor(Qt::ArrowCursor);
    button_filter_close->setStyleSheet(QStringLiteral(
        "QToolButton{ border: 1px solid palette(mid); border-radius: 4px; padding: 4px 8px; color: "
        "palette(text); font-weight: bold; background: palette(button); }"
        "QToolButton:hover{ border: 1px solid palette(highlight); color: "
        "palette(highlighted-text); background: palette(highlight)}"));
    connect(button_filter_close, &QToolButton::clicked, parent, &GameList::OnFilterCloseClicked);
    layout_filter->setSpacing(4);
    // Hide label_filter to save critical horizontal space on 720p
    label_filter->hide();
    layout_filter->addWidget(edit_filter);
    layout_filter->addWidget(label_filter_result);
    layout_filter->addWidget(button_filter_close);
    setLayout(layout_filter);
    RetranslateUI();
}

static bool ContainsAllWords(const QString& haystack, const QString& userinput) {
    const QStringList userinput_split = userinput.split(QLatin1Char{' '}, Qt::SkipEmptyParts);
    return std::all_of(userinput_split.begin(), userinput_split.end(),
                       [&haystack](const QString& s) { return haystack.contains(s); });
}

void GameList::OnItemExpanded(const QModelIndex& item) {
    const auto type = item.data(GameListItem::TypeRole).value<GameListItemType>();
    const bool is_dir = type == GameListItemType::CustomDir || type == GameListItemType::SdmcDir ||
                        type == GameListItemType::UserNandDir ||
                        type == GameListItemType::SysNandDir;
    const bool is_fave = type == GameListItemType::Favorites;
    if (!is_dir && !is_fave) {
        return;
    }
    const bool is_expanded = tree_view->isExpanded(item);
    if (is_fave) {
        UISettings::values.favorites_expanded = is_expanded;
        return;
    }
    const int item_dir_index = item.data(GameListDir::GameDirRole).toInt();
    UISettings::values.game_dirs[item_dir_index].expanded = is_expanded;
}

void GameList::OnTextChanged(const QString& new_text) {
    QString edit_filter_text = new_text.toLower();
    FilterGridView(edit_filter_text);
    FilterTreeView(edit_filter_text);
}

void GameList::FilterGridView(const QString& filter_text) {
    auto cleanup = [&](QAbstractItemModel* m) {
        if (m && m != item_model)
            m->deleteLater();
    };
    cleanup(grid_view->favModel());
    cleanup(grid_view->mainModel());
    cleanup(carousel_view->model());

    QStandardItemModel* hierarchical_model = item_model;
    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    int visible_count = 0, total_count = 0;

    QStandardItemModel* fav_model = new QStandardItemModel(this);
    QStandardItemModel* main_model = new QStandardItemModel(this);
    QStandardItemModel* carousel_model = new QStandardItemModel(this);
    QSet<QString> fav_paths;
    QStandardItem* h_fav_folder = nullptr;

    for (int i = 0; i < hierarchical_model->rowCount(); ++i) {
        QStandardItem* item = hierarchical_model->item(i);
        if (item && item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                        GameListItemType::Favorites) {
            h_fav_folder = item;
            break;
        }
    }

    auto cloneTo = [&](QStandardItem* src, QStandardItemModel* target, bool force_fav = false) {
        QStandardItem* item = src->clone();
        QString title = src->data(GameListItemPath::TitleRole).toString();
        if (title.isEmpty()) {
            std::string fn;
            Common::SplitPath(src->data(GameListItemPath::FullPathRole).toString().toStdString(),
                              nullptr, &fn, nullptr);
            title = QString::fromStdString(fn);
        }
        item->setText(title);
        item->setData(title, GameListItemPath::SortRole);
        if (force_fav) {
            item->setData(static_cast<int>(GameListItemType::Favorites), GameListItem::TypeRole);
        } else {
            item->setData(static_cast<int>(GameListItemType::Game), GameListItem::TypeRole);
        }
        target->appendRow(item);
    };

    auto isFavorite = [&](QStandardItem* item) {
        u64 pid = item->data(GameListItemPath::ProgramIdRole).toULongLong();
        return UISettings::values.favorited_ids.contains(pid);
    };

    if (h_fav_folder) {
        for (int j = 0; j < h_fav_folder->rowCount(); ++j) {
            QStandardItem* game = h_fav_folder->child(j, 0);
            if (!game || game->data(GameListItem::TypeRole).value<GameListItemType>() !=
                             GameListItemType::Game)
                continue;
            if (!isFavorite(game))
                continue;
            total_count++;
            QString path = game->data(GameListItemPath::FullPathRole).toString();
            if (UISettings::values.hidden_paths.contains(path))
                continue;
            if (!filter_text.isEmpty()) {
                QString title = game->data(GameListItemPath::TitleRole).toString().toLower();
                if (!ContainsAllWords(title, filter_text.toLower()))
                    continue;
            }
            cloneTo(game, fav_model, true);
            cloneTo(game, carousel_model, true);
            fav_paths.insert(path);
            visible_count++;
        }
    }

    for (int i = 0; i < hierarchical_model->rowCount(); ++i) {
        QStandardItem* folder = hierarchical_model->item(i);
        if (!folder ||
            folder->data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::AddDir ||
            folder->data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::Favorites)
            continue;
        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game = folder->child(j, 0);
            if (!game || game->data(GameListItem::TypeRole).value<GameListItemType>() !=
                             GameListItemType::Game)
                continue;
            total_count++;
            QString path = game->data(GameListItemPath::FullPathRole).toString();
            if (fav_paths.contains(path) || UISettings::values.hidden_paths.contains(path))
                continue;
            if (!filter_text.isEmpty()) {
                QString title = game->data(GameListItemPath::TitleRole).toString().toLower();
                if (!ContainsAllWords(title, filter_text.toLower()))
                    continue;
            }
            cloneTo(game, main_model);
            cloneTo(game, carousel_model);
            visible_count++;
        }
    }

    auto scaleIcons = [&](QStandardItemModel* model) {
        for (int i = 0; i < model->rowCount(); i++) {
            QStandardItem* item = model->item(i);
            if (!item)
                continue;
            QVariant icon_data = item->data(Qt::DecorationRole);
            if (icon_data.isValid() && icon_data.canConvert<QPixmap>()) {
                QPixmap pixmap = icon_data.value<QPixmap>();
                if (!pixmap.isNull()) {
                    item->setData(pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation),
                                  Qt::DecorationRole);
                }
            }
        }
    };
    scaleIcons(fav_model);
    scaleIcons(main_model);
    scaleIcons(carousel_model);

    fav_model->setSortRole(GameListItemPath::SortRole);
    fav_model->sort(0, current_sort_order);
    main_model->setSortRole(GameListItemPath::SortRole);
    main_model->sort(0, current_sort_order);
    carousel_model->setSortRole(
        GameListItemPath::SortRole); // Carousel is usually manual or logic-based, but we sort it
                                     // for consistency
    grid_view->setModels(fav_model, main_model);
    carousel_view->setModel(carousel_model);
    search_field->setFilterResult(visible_count, total_count);
}

void GameList::FilterTreeView(const QString& filter_text) {
    int visible_count = 0;
    int total_count = 0;

    tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                            filter_text.isEmpty() ? (UISettings::values.favorited_ids.size() == 0)
                                                  : true);

    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder)
            continue;

        const QModelIndex folder_index = folder->index();
        for (int j = 0; j < folder->rowCount(); ++j) {
            const QStandardItem* child = folder->child(j, 0);
            if (!child)
                continue;

            total_count++;
            const QString full_path = child->data(GameListItemPath::FullPathRole).toString();
            bool is_hidden_by_user = UISettings::values.hidden_paths.contains(full_path);
            bool matches_filter = true;

            if (!filter_text.isEmpty()) {
                const auto program_id = child->data(GameListItemPath::ProgramIdRole).toULongLong();
                const QString file_title =
                    child->data(GameListItemPath::TitleRole).toString().toLower();
                const QString file_program_id =
                    QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));
                const QString file_name =
                    full_path.mid(full_path.lastIndexOf(QLatin1Char{'/'}) + 1).toLower() +
                    QLatin1Char{' '} + file_title;
                matches_filter =
                    ContainsAllWords(file_name, filter_text) ||
                    (file_program_id.size() == 16 && file_program_id.contains(filter_text));
            }

            if (!is_hidden_by_user && matches_filter) {
                tree_view->setRowHidden(j, folder_index, false);
                visible_count++;
            } else {
                tree_view->setRowHidden(j, folder_index, true);
            }
        }
    }
    search_field->setFilterResult(visible_count, total_count);
}

void GameList::OnUpdateThemedIcons() {
    for (int i = 0; i < item_model->invisibleRootItem()->rowCount(); i++) {
        QStandardItem* child = item_model->invisibleRootItem()->child(i);
        if (!child) {
            continue;
        }
        const int icon_size = UISettings::values.folder_icon_size.GetValue();
        switch (child->data(GameListItem::TypeRole).value<GameListItemType>()) {
        case GameListItemType::SdmcDir:
            child->setData(
                QIcon::fromTheme(QStringLiteral("sd_card"))
                    .pixmap(icon_size)
                    .scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                Qt::DecorationRole);
            break;
        case GameListItemType::UserNandDir:
            child->setData(
                QIcon::fromTheme(QStringLiteral("chip"))
                    .pixmap(icon_size)
                    .scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                Qt::DecorationRole);
            break;
        case GameListItemType::SysNandDir:
            child->setData(
                QIcon::fromTheme(QStringLiteral("chip"))
                    .pixmap(icon_size)
                    .scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                Qt::DecorationRole);
            break;
        case GameListItemType::CustomDir: {
            const UISettings::GameDir& game_dir =
                UISettings::values.game_dirs[child->data(GameListDir::GameDirRole).toInt()];
            const QString icon_name = QFileInfo::exists(QString::fromStdString(game_dir.path))
                                          ? QStringLiteral("folder")
                                          : QStringLiteral("bad_folder");
            child->setData(
                QIcon::fromTheme(icon_name).pixmap(icon_size).scaled(
                    icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                Qt::DecorationRole);
            break;
        }
        case GameListItemType::AddDir:
            child->setData(
                QIcon::fromTheme(QStringLiteral("list-add"))
                    .pixmap(icon_size)
                    .scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                Qt::DecorationRole);
            break;
        case GameListItemType::Favorites:
            child->setData(
                QIcon::fromTheme(QStringLiteral("star"))
                    .pixmap(icon_size)
                    .scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                Qt::DecorationRole);
            break;
        case GameListItemType::Game:
            break;
        }
    }

    // Refresh all theme-aware styles and icons
    UpdateProgressBarColor();
    UpdateAccentColorStyles();
    RefreshTooltips();
}

void GameList::OnFilterCloseClicked() {
    main_window->filterBarSetChecked(false);
}

QString GameList::GenerateAddonsTooltip(const QString& patch_versions) {
    if (patch_versions.isEmpty())
        return {};

    const bool is_dark = Theme::IsDarkMode();
    const QString bg_color = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#f5f5fa");
    const QString divider_color = is_dark ? QStringLiteral("#303035") : QStringLiteral("#dcdce2");
    const QString text_color = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString accent_color = Theme::GetAccentColor();
    const QString item_text_color = is_dark ? QStringLiteral("#e0e0e4") : QStringLiteral("#444444");

    QString tooltip =
        QStringLiteral(
            "<html><body style='background-color: %1; color: %2; padding: 15px; border-radius: "
            "10px; font-family: \"Outfit\", \"Inter\", sans-serif;'>"
            "<div style='margin-bottom: 8px; color: %3; font-size: 13px; font-weight: bold; "
            "border-bottom: 1px solid %4; padding-bottom: 4px;'>ACTIVE ADD-ONS & MODS</div>"
            "<div style='line-height: 1.5;'>")
            .arg(bg_color, text_color, accent_color, divider_color);

    QStringList categories = patch_versions.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const auto& line : categories) {
        tooltip.append(
            QStringLiteral("<div style='margin: 3px 0; color: %1;'><b>&bull;</b> %2</div>")
                .arg(item_text_color, line.toHtmlEscaped()));
    }

    tooltip.append(QStringLiteral("</div></body></html>"));
    return tooltip;
}

void GameList::RefreshTooltips() {
    if (!item_model)
        return;

    std::function<void(QStandardItem*)> update_item = [&](QStandardItem* parent) {
        for (int i = 0; i < parent->rowCount(); ++i) {
            QStandardItem* name_item = parent->child(i, COLUMN_NAME);
            if (!name_item)
                continue;

            // Recurse if it's a directory
            if (name_item->hasChildren()) {
                update_item(name_item);
            }

            // Update addon column if it's a game row
            QStandardItem* addon_item = parent->child(i, COLUMN_ADD_ONS);
            if (addon_item && !addon_item->text().isEmpty()) {
                addon_item->setData(GenerateAddonsTooltip(addon_item->text()), Qt::ToolTipRole);
            }
        }
    };

    update_item(item_model->invisibleRootItem());
}

GameList::GameList(std::shared_ptr<FileSys::VfsFilesystem> vfs_,
                   FileSys::ManualContentProvider* provider_,
                   PlayTime::PlayTimeManager& play_time_manager_, Core::System& system_,
                   GMainWindow* parent)
    : QWidget{parent}, vfs{std::move(vfs_)}, provider{provider_},
      play_time_manager{play_time_manager_}, system{system_} {
    watcher = new QFileSystemWatcher(this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &GameList::RefreshGameDirectory);

    this->main_window = parent;
    layout = new QVBoxLayout;
    controller_navigation = new ControllerNavigation(system.HIDCore(), this);
    search_field = new GameListSearchField(this);
    search_field->setMinimumWidth(150);
    search_field->setMaximumWidth(600);
    item_model = new QStandardItemModel(this);

    // New Decoupled View Architecture
    details_panel = new GameDetailsPanel(this);
    details_panel->hide();

    main_stack = new QStackedWidget(this);
    tree_view = new GameTreeView(this);
    grid_view = new GameGridView(this);
    carousel_view = new GameCarouselView(this);

    main_stack->addWidget(tree_view);     // Index 0: List
    main_stack->addWidget(grid_view);     // Index 1: Grid
    main_stack->addWidget(carousel_view); // Index 2: Carousel

    auto* root_layout = new QHBoxLayout();
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);
    root_layout->addWidget(main_stack, 1);
    root_layout->addWidget(details_panel, 0);
    // Initial Model Setup
    tree_view->setModel(item_model);
    grid_view->setModel(new QStandardItemModel(this));
    carousel_view->setModel(new QStandardItemModel(this));

    loading_overlay = new GameListLoadingOverlay(this);
    loading_overlay->hide();

    // Unified Signal Connections
    // Use currentChanged from selection models for robust tracking (keyboard + mouse)
    connect(
        tree_view->selectionModel(), &QItemSelectionModel::currentChanged,
        [this](const QModelIndex& current, const QModelIndex&) { OnSelectionChanged(current); });
    connect(grid_view, &GameGridView::itemSelectionChanged, this, &GameList::OnSelectionChanged);
    connect(carousel_view, &GameCarouselView::itemSelectionChanged, this,
            &GameList::OnSelectionChanged);

    connect(tree_view, &GameTreeView::itemActivated, this, &GameList::ValidateEntry);
    connect(grid_view, &GameGridView::itemActivated, this, &GameList::ValidateEntry);
    connect(carousel_view, &GameCarouselView::itemActivated, this, &GameList::ValidateEntry);

    tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_view->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_view->favView()->setContextMenuPolicy(Qt::CustomContextMenu);
    carousel_view->view()->setContextMenuPolicy(Qt::CustomContextMenu);

    // Load the persistent index instantly to provide a console-grade experience
    LoadGameListIndex();

    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);
    connect(grid_view->view(), &QListView::customContextMenuRequested, this,
            &GameList::PopupContextMenu);
    connect(grid_view->favView(), &QListView::customContextMenuRequested, this,
            &GameList::PopupContextMenu);
    connect(carousel_view->view(), &QWidget::customContextMenuRequested, this,
            &GameList::PopupContextMenu);

    item_model->insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

    item_model->setSortRole(GameListItemPath::SortRole);

    // Apply modernized delegate
    tree_view->setItemDelegate(new GameListDelegate(tree_view, this));
    tree_view->setIndentation(0);
    tree_view->setRootIsDecorated(true);

    // Setup Kinetic Scrolling (Drag-to-Scroll) for a premium console experience
    auto setupScroller = [](QWidget* target) {
        if (!target) return;
        
        // Force Pixel-based scrolling; without this, QScroller behaves hypersensitively
        if (auto* view = qobject_cast<QAbstractItemView*>(target->parent())) {
            view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }
        
        // IMPORTANT: For scroll areas, we must grab on the viewport to avoid coordinate feedback loops
        QScroller::grabGesture(target, QScroller::LeftMouseButtonGesture);
        
        QScroller* scroller = QScroller::scroller(target);
        QScrollerProperties props = scroller->scrollerProperties();
        
        // Use standard Touch profile as baseline to handle DPI scaling correctly across devices
        props.setScrollMetric(QScrollerProperties::DragStartDistance, 0.015);
        props.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor, 0.5);
        props.setScrollMetric(QScrollerProperties::MinimumVelocity, 0.05);
        props.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.6);
        props.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.3);
        props.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.1);
        
        // Input Filtering: Delay press event to help distinguish tap from drag
        props.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.15); // 150ms
        
        props.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.4);
        props.setScrollMetric(QScrollerProperties::OvershootDragDistanceFactor, 0.1);
        props.setScrollMetric(QScrollerProperties::OvershootScrollDistanceFactor, 0.1);
        props.setScrollMetric(QScrollerProperties::OvershootScrollTime, 0.4);
        props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, QScrollerProperties::OvershootAlwaysOn);
        
        scroller->setScrollerProperties(props);
    };

    setupScroller(tree_view->viewport());
    setupScroller(grid_view->viewport());

    // Load persisted view mode on launch
    SetViewMode(static_cast<ViewMode>(UISettings::values.game_list_view_mode.GetValue()));

    connect(main_window, &GMainWindow::UpdateThemedIcons, this, &GameList::OnUpdateThemedIcons);
    connect(tree_view, &QTreeView::expanded, this, &GameList::OnItemExpanded);
    connect(tree_view, &QTreeView::collapsed, this, &GameList::OnItemExpanded);
    // Sync sort button with Name column header sort order
    connect(tree_view->header(), &QHeaderView::sortIndicatorChanged,
            [this](int logicalIndex, Qt::SortOrder order) {
                if (logicalIndex == COLUMN_NAME) {
                    current_sort_order = order;
                    UpdateSortButtonIcon();
                }
            });
    connect(
        controller_navigation, &ControllerNavigation::TriggerKeyboardEvent, [this](Qt::Key key) {
            if (system.IsPoweredOn() || !this->isActiveWindow()) {
                return;
            }
            QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
            if (tree_view->isVisible() && tree_view->model()) {
                QCoreApplication::postEvent(tree_view, event);
            }
            if (grid_view->isVisible() && grid_view->view()->model()) {
                QCoreApplication::postEvent(grid_view->view(),
                                            new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
            }
            if (carousel_view->isVisible()) {
                QCoreApplication::postEvent(carousel_view->view(),
                                            new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
            }
        });

    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");
    qRegisterMetaType<std::map<u64, std::pair<int, int>>>("std::map<u64, std::pair<int, int>>");

    // Create toolbar
    toolbar = new QWidget(this);
    toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(2, 0, 2, 0);
    toolbar_layout->setSpacing(1);

    // List view button - icon-only with rounded corners
    btn_list_view = new QToolButton(toolbar);
    QIcon list_icon(QStringLiteral(":/dist/list.svg"));
    if (list_icon.isNull()) {
        list_icon = QIcon::fromTheme(QStringLiteral("view-list-details"));
        if (list_icon.isNull())
            list_icon = QIcon::fromTheme(QStringLiteral("view-list"));
        if (list_icon.isNull())
            list_icon = style()->standardIcon(QStyle::SP_FileDialogListView);
    }
    btn_list_view->setIcon(list_icon);
    btn_list_view->setToolTip(tr("List View"));
    btn_list_view->setCheckable(true);
    btn_list_view->setChecked(!UISettings::values.game_list_grid_view.GetValue());
    btn_list_view->setAutoRaise(true);
    btn_list_view->setIconSize(QSize(16, 16));
    btn_list_view->setFixedSize(26, 26);
    btn_list_view->setStyleSheet(QStringLiteral("QToolButton {"
                                                "  border: 1px solid #3e3e42;"
                                                "  border-radius: 4px;"
                                                "  background: #2b2b2f;"
                                                "  color: #ffffff;"
                                                "}"
                                                "QToolButton:hover {"
                                                "  background: #3e3e42;"
                                                "}"
                                                "QToolButton:checked {"
                                                "  background: #0078d4;"
                                                "  border-color: #0078d4;"
                                                "}"));
    connect(btn_list_view, &QToolButton::clicked,
            [this]() { SetViewMode(GameList::ViewMode::List); });

    // Grid view button - icon-only with rounded corners
    btn_grid_view = new QToolButton(toolbar);
    QIcon grid_icon(QStringLiteral(":/dist/grid.svg"));
    if (grid_icon.isNull()) {
        grid_icon = QIcon::fromTheme(QStringLiteral("view-grid"));
        if (grid_icon.isNull())
            grid_icon = QIcon::fromTheme(QStringLiteral("view-grid-details"));
        if (grid_icon.isNull())
            grid_icon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    }
    btn_grid_view->setIcon(grid_icon);
    btn_grid_view->setToolTip(tr("Grid View"));
    btn_grid_view->setCheckable(true);
    btn_grid_view->setChecked(UISettings::values.game_list_grid_view.GetValue());
    btn_grid_view->setAutoRaise(true);
    btn_grid_view->setIconSize(QSize(16, 16));
    btn_grid_view->setFixedSize(26, 26);
    btn_grid_view->setStyleSheet(QStringLiteral("QToolButton {"
                                                "  border: 1px solid #3e3e42;"
                                                "  border-radius: 4px;"
                                                "  background: #2b2b2f;"
                                                "  color: #ffffff;"
                                                "}"
                                                "QToolButton:hover {"
                                                "  background: #3e3e42;"
                                                "}"
                                                "QToolButton:checked {"
                                                "  background: #0078d4;"
                                                "  border-color: #0078d4;"
                                                "}"));
    connect(btn_grid_view, &QToolButton::clicked,
            [this]() { SetViewMode(GameList::ViewMode::Grid); });

    btn_carousel_view = new QToolButton(toolbar);
    QIcon carousel_icon(QStringLiteral(":/dist/carousel.svg"));
    if (carousel_icon.isNull()) {
        carousel_icon = QIcon::fromTheme(QStringLiteral("view-presentation"));
        if (carousel_icon.isNull())
            carousel_icon = QIcon::fromTheme(QStringLiteral("media-playlist-play"));
        if (carousel_icon.isNull())
            carousel_icon = style()->standardIcon(QStyle::SP_MediaPlay);
    }
    btn_carousel_view->setIcon(carousel_icon);
    btn_carousel_view->setToolTip(tr("Carousel View"));
    btn_carousel_view->setCheckable(true);
    btn_carousel_view->setAutoRaise(true);
    btn_carousel_view->setIconSize(QSize(16, 16));
    btn_carousel_view->setFixedSize(26, 26);
    btn_carousel_view->setStyleSheet(QStringLiteral("QToolButton {"
                                                    "  border: 1px solid #3e3e42;"
                                                    "  border-radius: 4px;"
                                                    "  background: #2b2b2f;"
                                                    "  color: #ffffff;"
                                                    "}"
                                                    "QToolButton:hover {"
                                                    "  background: #3e3e42;"
                                                    "}"
                                                    "QToolButton:checked {"
                                                    "  background: #0078d4;"
                                                    "  border-color: #0078d4;"
                                                    "}"));
    connect(btn_carousel_view, &QToolButton::clicked,
            [this]() { SetViewMode(GameList::ViewMode::Carousel); });

    // Helper to create the small mode-toggle buttons flanking the slider
    auto makeModeBtn = [&](const QString& label, const QString& tip) -> QToolButton* {
        auto* btn = new QToolButton(toolbar);
        btn->setText(label);
        btn->setToolTip(tip);
        btn->setCheckable(true);
        btn->setAutoRaise(true);
        btn->setFixedSize(26, 26);
        btn->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  border: 1px solid #3e3e42;"
            "  border-radius: 4px;"
            "  background: #2b2b2f;"
            "  color: #aaaaaa;"
            "  font-size: 9px; font-weight: bold;"
            "}"
            "QToolButton:hover { background: #3e3e42; color: #ffffff; }"
            "QToolButton:checked {"
            "  background: #1a3a5c;"
            "  border-color: #0078d4;"
            "  color: #7ec8ff;"
            "}"));
        return btn;
    };

    // Font-size mode toggle
    btn_slider_font_mode = makeModeBtn(QStringLiteral(""), tr("Slider: Font Size"));
    QIcon font_icon(QStringLiteral(":/dist/font_size.svg"));
    btn_slider_font_mode->setIcon(font_icon);
    btn_slider_font_mode->setIconSize(QSize(16, 16));
    btn_slider_font_mode->setChecked(true); // default mode

    // Icon-size mode toggle
    btn_slider_icon_mode = makeModeBtn(QStringLiteral(""), tr("Slider: Game Icon Size"));
    QIcon img_icon(QStringLiteral(":/dist/game_icon.svg"));
    btn_slider_icon_mode->setIcon(img_icon);
    btn_slider_icon_mode->setIconSize(QSize(16, 16));

    // Title/Icon size slider — range and initial value depend on active mode
    slider_title_size = new QSlider(Qt::Horizontal, toolbar);
    slider_title_size->setMinimum(8);   // font points (font mode)
    slider_title_size->setMaximum(24);
    // Initialise to current font size
    slider_title_size->setValue(tree_view->font().pointSize());
    slider_title_size->setToolTip(tr("Font Size"));
    slider_title_size->setMaximumWidth(110);
    slider_title_size->setMinimumWidth(110);
    slider_title_size->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal {"
        "  border: 1px solid palette(mid);"
        "  height: 4px;"
        "  background: palette(base);"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: palette(button);"
        "  border: 1px solid palette(mid);"
        "  width: 12px; height: 12px;"
        "  margin: -4px 0;"
        "  border-radius: 6px;"
        "}"
        "QSlider::handle:horizontal:hover { background: palette(light); }"));

    // Switch to font-size mode
    connect(btn_slider_font_mode, &QToolButton::clicked, [this]() {
        slider_icon_mode = false;
        btn_slider_font_mode->setChecked(true);
        btn_slider_icon_mode->setChecked(false);
        slider_title_size->blockSignals(true);
        slider_title_size->setRange(8, 24);
        slider_title_size->setValue(qBound(8, tree_view->font().pointSize(), 24));
        slider_title_size->setToolTip(tr("Font Size"));
        slider_title_size->blockSignals(false);
    });

    // Switch to icon-size mode
    connect(btn_slider_icon_mode, &QToolButton::clicked, [this]() {
        slider_icon_mode = true;
        btn_slider_icon_mode->setChecked(true);
        btn_slider_font_mode->setChecked(false);
        slider_title_size->blockSignals(true);
        slider_title_size->setRange(32, 256);
        slider_title_size->setValue(
            static_cast<int>(UISettings::values.game_icon_size.GetValue()));
        slider_title_size->setToolTip(tr("Game Icon Size"));
        slider_title_size->blockSignals(false);
    });

    connect(slider_title_size, &QSlider::valueChanged, [this](int value) {
        if (!slider_icon_mode) {
            // ── Font-size mode ──────────────────────────────────────────────
            QFont font = tree_view->font();
            font.setPointSize(qBound(8, value, 24));
            tree_view->setFont(font);
            tree_view->doItemsLayout();
        } else {
            // ── Icon-size mode ──────────────────────────────────────────────
            UISettings::values.game_icon_size.SetValue(static_cast<u32>(value));
            
            // Immediately force the tree view to recalculate row heights and repaint
            // so the game cards ("pills") scale dynamically without clipping.
            tree_view->doItemsLayout();

            if (grid_view) {
                grid_view->UpdateGridSize();
            }
            if (carousel_view) {
                carousel_view->update();
            }

#ifndef __linux__
            if (grid_view->isVisible()) {
                QAbstractItemModel* current_model = grid_view->model();
                if (current_model && current_model != item_model) {
                    QStandardItemModel* flat_model =
                        qobject_cast<QStandardItemModel*>(current_model);
                    if (flat_model) {
                        const u32 icon_size = static_cast<u32>(value);
                        int scroll_position =
                            grid_view->view()->verticalScrollBar()->value();
                        QModelIndex current_index = grid_view->currentIndex();

                        for (int i = 0; i < flat_model->rowCount(); ++i) {
                            QStandardItem* item = flat_model->item(i);
                            if (item) {
                                u64 program_id =
                                    item->data(GameListItemPath::ProgramIdRole).toULongLong();
                                QStandardItem* original_item = nullptr;
                                for (int folder_idx = 0; folder_idx < item_model->rowCount();
                                     ++folder_idx) {
                                    QStandardItem* folder = item_model->item(folder_idx, 0);
                                    if (!folder)
                                        continue;
                                    for (int game_idx = 0; game_idx < folder->rowCount();
                                         ++game_idx) {
                                        QStandardItem* game = folder->child(game_idx, 0);
                                        if (game && game->data(GameListItemPath::ProgramIdRole)
                                                            .toULongLong() == program_id) {
                                            original_item = game;
                                            break;
                                        }
                                    }
                                    if (original_item)
                                        break;
                                }

                                if (original_item) {
                                    QVariant orig_icon_data =
                                        original_item->data(Qt::DecorationRole);
                                    if (orig_icon_data.isValid() &&
                                        orig_icon_data.type() == QVariant::Pixmap) {
                                        QPixmap orig_pixmap =
                                            orig_icon_data.value<QPixmap>();
                                        QPixmap rounded(icon_size, icon_size);
                                        rounded.fill(Qt::transparent);
                                        QPainter painter(&rounded);
                                        painter.setRenderHint(QPainter::Antialiasing);
                                        const int radius = icon_size / 8;
                                        QPainterPath path;
                                        path.addRoundedRect(0, 0, icon_size, icon_size, radius,
                                                            radius);
                                        painter.setClipPath(path);
                                        QPixmap scaled = orig_pixmap.scaled(
                                            icon_size, icon_size, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation);
                                        painter.drawPixmap(0, 0, scaled);
                                        item->setData(rounded, Qt::DecorationRole);
                                    }
                                }
                            }
                        }
                        if (scroll_position >= 0)
                            grid_view->view()->verticalScrollBar()->setValue(scroll_position);
                        if (current_index.isValid() &&
                            current_index.row() < flat_model->rowCount())
                            grid_view->setCurrentIndex(
                                flat_model->index(current_index.row(), 0));
                    }
                } else {
                    PopulateGridView();
                }
            }
#endif
        }
    });

    // A-Z sort button - positioned after slider
    btn_sort_az = new QToolButton(toolbar);
    UpdateSortButtonIcon();
    btn_sort_az->setToolTip(tr("Sort by Name"));
    btn_sort_az->setAutoRaise(true);
    btn_sort_az->setIconSize(QSize(16, 16));
    btn_sort_az->setFixedSize(26, 26);
    btn_sort_az->setStyleSheet(QStringLiteral("QToolButton {"
                                              "  border: 1px solid #3e3e42;"
                                              "  border-radius: 4px;"
                                              "  background: #2b2b2f;"
                                              "  color: #ffffff;"
                                              "}"
                                              "QToolButton:hover {"
                                              "  background: #3e3e42;"
                                              "}"));
    connect(btn_sort_az, &QToolButton::clicked, this, &GameList::ToggleSortOrder);

    // Surprise Me button - positioned after sort button
    btn_surprise_me = new QToolButton(toolbar);
    QIcon surprise_icon(QStringLiteral(":/dist/dice.svg"));
    if (surprise_icon.isNull()) {
        // Fallback to theme icon or standard icon on Windows where SVG may not load
        surprise_icon = QIcon::fromTheme(QStringLiteral("media-playlist-shuffle"));
        if (surprise_icon.isNull()) {
            surprise_icon = QIcon::fromTheme(QStringLiteral("roll"));
        }
        if (surprise_icon.isNull()) {
            surprise_icon = style()->standardIcon(QStyle::SP_BrowserReload);
        }
    }
    btn_surprise_me->setIcon(surprise_icon);
    btn_surprise_me->setToolTip(tr("Surprise Me! (Choose Random Game)"));
    btn_surprise_me->setAutoRaise(true);
    btn_surprise_me->setIconSize(QSize(16, 16));
    btn_surprise_me->setFixedSize(26, 26);
    btn_surprise_me->setStyleSheet(QStringLiteral("QToolButton {"
                                                  "  border: 1px solid #3e3e42;"
                                                  "  border-radius: 4px;"
                                                  "  background: #2b2b2f;"
                                                  "  color: #ffffff;"
                                                  "}"
                                                  "QToolButton:hover {"
                                                  "  background: #3e3e42;"
                                                  "}"));
    connect(btn_surprise_me, &QToolButton::clicked, this, &GameList::onSurpriseMeClicked);

    // Create progress bar
    progress_bar = new QProgressBar(this);
    progress_bar->setVisible(false);
    progress_bar->setFixedHeight(4);
    progress_bar->setTextVisible(false);
    progress_bar->setStyleSheet(QStringLiteral("QProgressBar::chunk { background-color: %1; }")
                                    .arg(Theme::GetAccentColor()));

    // Add widgets to toolbar
    toolbar->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    toolbar_layout->addWidget(btn_list_view);
    toolbar_layout->addWidget(btn_grid_view);
    toolbar_layout->addWidget(btn_carousel_view);
    toolbar_layout->addWidget(btn_slider_font_mode);
    toolbar_layout->addWidget(btn_slider_icon_mode);
    toolbar_layout->addWidget(slider_title_size);
    toolbar_layout->addWidget(btn_sort_az);
    toolbar_layout->addWidget(btn_surprise_me);

    // Install event filter on toolbar for tooltip dismissal
    if (auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate())) {
        toolbar->installEventFilter(delegate);
    }

    // Controller Settings Button
    btn_controller_settings = new QToolButton(toolbar);
    QIcon ctrl_icon(QStringLiteral(":/dist/controller_navigation.svg"));
    if (ctrl_icon.isNull()) {
        ctrl_icon = QIcon::fromTheme(QStringLiteral("input-gaming"));
        if (ctrl_icon.isNull())
            ctrl_icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    btn_controller_settings->setIcon(ctrl_icon);
    btn_controller_settings->setToolTip(tr("Controller Navigation Settings"));
    btn_controller_settings->setAutoRaise(true);
    btn_controller_settings->setIconSize(QSize(16, 16));
    btn_controller_settings->setFixedSize(26, 26);
    btn_controller_settings->setStyleSheet(btn_list_view->styleSheet());
    connect(btn_controller_settings, &QToolButton::clicked, [this]() {
        if (!m_nav_overlay) {
            m_nav_overlay = new NavigationSettingsOverlay(this);
        }
        m_nav_overlay->setGeometry(rect());
        m_nav_overlay->showAnimated();
    });
    toolbar_layout->addWidget(btn_controller_settings);

    toolbar_layout->addWidget(search_field);
    search_field->setVisible(true); // Default to visible to fix the "missing" issue

    // Context menu to allow toggling the search bar
    toolbar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(toolbar, &QWidget::customContextMenuRequested, [this](const QPoint& pos) {
        QMenu menu(this);
        QAction* toggle_search = menu.addAction(tr("Show Search Bar"));
        toggle_search->setCheckable(true);
        toggle_search->setChecked(search_field->isVisible());
        connect(toggle_search, &QAction::toggled,
                [this](bool checked) { main_window->filterBarSetChecked(checked); });
        menu.exec(toolbar->mapToGlobal(pos));
    });

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    main_stack->setContentsMargins(0, 0, 0, 0);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    if (!toolbar_in_main) {
        layout->addWidget(toolbar);
    }

    UpdateProgressBarColor();
    tree_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    main_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tree_view->setFocusPolicy(Qt::StrongFocus);

    main_stack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    tree_view->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    layout->addLayout(root_layout, 1);
    setLayout(layout);

    SetViewMode(UISettings::values.game_list_grid_view.GetValue() ? GameList::ViewMode::Grid
                                                                  : GameList::ViewMode::List);

    // Selection handling is now unified in the view components

    connect(details_panel, &GameDetailsPanel::actionTriggered, this,
            [this](const QString& action, u64 program_id, const QString& pathName) {
                if (action == QStringLiteral("start")) {
                    QModelIndex current;
                    int idx = main_stack->currentIndex();
                    if (idx == 0)
                        current = tree_view->currentIndex();
                    else if (idx == 1)
                        current = grid_view->currentIndex();
                    else
                        current = carousel_view->view()->currentIndex();

                    if (!current.isValid() && !pathName.isEmpty()) {
                        // Attempt to find the index if the view lost focus
                        auto matches = item_model->match(item_model->index(0, 0),
                                                         GameListItemPath::FullPathRole, pathName,
                                                         1, Qt::MatchExactly | Qt::MatchRecursive);
                        if (!matches.isEmpty()) {
                            current = matches.first();
                        }
                    }

                    if (current.isValid()) {
                        StartLaunchAnimation(current);
                    } else if (!pathName.isEmpty()) {
                        emit BootGame(pathName, StartGameType::Normal);
                    }
                } else if (action == QStringLiteral("favorite")) {
                    ToggleFavorite(program_id);

                    // Refresh details for active item
                    QModelIndex current;
                    int idx = main_stack->currentIndex();
                    if (idx == 0)
                        current = tree_view->currentIndex();
                    else if (idx == 1)
                        current = grid_view->currentIndex();
                    else
                        current = carousel_view->view()->currentIndex();

                    if (current.isValid()) {
                        details_panel->updateDetails(current);
                    }
                } else if (action == QStringLiteral("properties")) {
                    emit OpenPerGameGeneralRequested(pathName.toStdString());
                } else if (action == QStringLiteral("save_data")) {
                    emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData,
                                             pathName.toStdString());
                } else if (action == QStringLiteral("mod_data")) {
                    emit OpenFolderRequested(program_id, GameListOpenTarget::ModData,
                                             pathName.toStdString());
                }
            });

    online_status_timer = new QTimer(this);
    connect(online_status_timer, &QTimer::timeout, this, &GameList::UpdateOnlineStatus);
    online_status_timer->start(30000);

    // Trigger an initial update shortly after startup
    QTimer::singleShot(2500, this, &GameList::UpdateOnlineStatus);

    // Configure the timer for debouncing configuration changes
    config_update_timer.setSingleShot(true);
    connect(&config_update_timer, &QTimer::timeout, this, &GameList::UpdateOnlineStatus);

    // This connection handles live updates when OK/Apply is clicked in the config window.
    connect(main_window, &GMainWindow::ConfigurationSaved, this,
            &GameList::UpdateAccentColorStyles);

    network_manager = new QNetworkAccessManager(this);

    fade_overlay = new QWidget(this);
    fade_overlay->setStyleSheet(QStringLiteral("background: black;"));
    fade_overlay->hide(); // Start hidden

    connect(main_window, &GMainWindow::EmulationStopping, this, [this]() { OnEmulationEnded(); });

    UpdateAccentColorStyles();

    // Controller Navigation Integration
    if (controller_navigation) {
        connect(controller_navigation, &ControllerNavigation::activityDetected, this,
                &GameList::SwitchToControllerMode);

        auto connectView = [this](auto* view) {
            using ViewType = std::remove_pointer_t<decltype(view)>;
            connect(controller_navigation, &ControllerNavigation::navigated, view,
                    &ViewType::onNavigated);
            connect(controller_navigation, &ControllerNavigation::activated, view,
                    &ViewType::onActivated);
            connect(controller_navigation, &ControllerNavigation::cancelled, view,
                    &ViewType::onCancelled);
        };

        connectView(tree_view);
        connectView(grid_view);

        // Connect CarouselView explicitly to resolve lint errors
        connect(controller_navigation, &ControllerNavigation::navigated, carousel_view,
                &GameCarouselView::onNavigated);
        connect(controller_navigation, &ControllerNavigation::activated, carousel_view,
                &GameCarouselView::onActivated);
        connect(controller_navigation, &ControllerNavigation::cancelled, carousel_view,
                &GameCarouselView::onCancelled);

        // Install event filter to catch shortcuts even when views have focus
        tree_view->installEventFilter(this);
        grid_view->installEventFilter(this);
        carousel_view->installEventFilter(this);

        // Connect Details Panel navigation
        connect(controller_navigation, &ControllerNavigation::navigated, details_panel,
                &GameDetailsPanel::onNavigated);
        connect(controller_navigation, &ControllerNavigation::activated, details_panel,
                &GameDetailsPanel::onActivated);
        connect(controller_navigation, &ControllerNavigation::cancelled, details_panel,
                &GameDetailsPanel::onCancelled);

        connect(controller_navigation, &ControllerNavigation::auxiliaryAction, this,
                [this](int id) {
                    if (id == 0) { // Mapping X to Alphabetical Jump
                        this->JumpToNextLetter();
                    }
                });

        connect(controller_navigation, &ControllerNavigation::focusChanged, this,
                &GameList::onControllerFocusChanged);
        connect(details_panel, &GameDetailsPanel::focusReturned, this, [this]() {
            if (controller_navigation)
                controller_navigation->setFocus(ControllerNavigation::FocusTarget::MainView);
        });
    }
}

void GameList::OnConfigurationChanged() {
    // This function debounces the update requests. Instead of starting a network
    // request immediately, it starts a 500ms timer. If another config change happens,
    // the timer is simply reset. The network request will only happen once, 500ms
    // after the *last* change was made.
    config_update_timer.start(500);
}

void GameList::SwitchToControllerMode() {
    if (m_is_controller_mode)
        return;
    m_is_controller_mode = true;
    onControllerFocusChanged(controller_navigation ? controller_navigation->currentFocus()
                                                   : ControllerNavigation::FocusTarget::MainView);
}

void GameList::SwitchToKeyboardMode() {
    if (!m_is_controller_mode)
        return;
    m_is_controller_mode = false;
    unsetCursor();
    tree_view->setControllerFocus(false);
    grid_view->setControllerFocus(false);
    carousel_view->setControllerFocus(false);
}

void GameList::JumpToNextLetter() {
    QAbstractItemModel* model = nullptr;
    QModelIndex current;
    int mode = main_stack->currentIndex();

    if (mode == 0) { // Tree
        model = tree_view->model();
        current = tree_view->selectionModel()->currentIndex();
    } else if (mode == 1) { // Grid
        model = grid_view->model();
        current = grid_view->selectionModel()->currentIndex();
    } else if (mode == 2) { // Carousel
        model = carousel_view->view()->model();
        current = carousel_view->view()->currentIndex();
    }

    if (!model || model->rowCount() == 0)
        return;
    if (!current.isValid())
        current = model->index(0, 0);

    QString current_name = current.data(Qt::DisplayRole).toString().toUpper();
    QChar current_char = current_name.isEmpty() ? QLatin1Char(' ') : current_name[0];

    int total = model->rowCount();
    int start_row = current.row();

    for (int i = 1; i <= total; ++i) {
        int next_row = (start_row + i) % total;
        QModelIndex next_idx = model->index(next_row, 0);
        QString next_name = next_idx.data(Qt::DisplayRole).toString().toUpper();
        QChar next_char = next_name.isEmpty() ? QLatin1Char(' ') : next_name[0];

        if (next_char != current_char) {
            if (mode == 0) {
                tree_view->setCurrentIndex(next_idx);
                tree_view->scrollTo(next_idx);
            } else if (mode == 1) {
                grid_view->setCurrentIndex(next_idx);
                grid_view->scrollTo(next_idx);
            } else if (mode == 2) {
                carousel_view->view()->scrollTo(next_row);
            }
            return;
        }
    }
}

void GameList::UnloadController() {
    if (controller_navigation) {
        controller_navigation->UnloadController();
    }
}

void GameList::LoadController() {
    if (controller_navigation) {
        controller_navigation->LoadController(system.HIDCore());
    }
}

GameList::~GameList() {
    UnloadController();
    // Grid and Carousel share the same flat model
    if (grid_view) {
        if (QAbstractItemModel* current_model = grid_view->model()) {
            if (current_model != item_model) {
                current_model->deleteLater();
            }
        }
    }
}

void GameList::SetFilterFocus() {
    if (tree_view->model()->rowCount() > 0) {
        search_field->setFocus();
    }
}

void GameList::SetFilterVisible(bool visibility) {
    search_field->setVisible(visibility);
}

void GameList::ClearFilter() {
    search_field->clear();
}

void GameList::WorkerEvent() {
    current_worker->ProcessEvents(this);
}

void GameList::AddDirEntry(GameListDir* entry_items) {
    if (!entry_items)
        return;

    const QString new_path = entry_items->data(GameListDir::FullPathRole).toString();

    // Check if we already have this directory folder in the root to prevent duplicates
    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* existing = item_model->item(i, 0);
        if (existing && existing->data(GameListItem::TypeRole).value<GameListItemType>() ==
                            entry_items->data(GameListItem::TypeRole).value<GameListItemType>()) {
            if (existing->data(GameListDir::FullPathRole).toString() == new_path) {
                // We already have this folder. The worker will fill its children via AddEntry.
                delete entry_items;
                return;
            }
        }
    }

    item_model->invisibleRootItem()->appendRow(entry_items);
    tree_view->setExpanded(
        entry_items->index(),
        UISettings::values.game_dirs[entry_items->data(GameListDir::GameDirRole).toInt()].expanded);
}

void GameList::AddEntry(const QList<QStandardItem*>& entry_items, const QString& parent_path) {
    if (entry_items.isEmpty())
        return;

    // Find the parent directory item in the model by its path.
    GameListDir* parent = nullptr;
    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* item = item_model->item(i, 0);
        if (item && item->data(GameListDir::FullPathRole).toString() == parent_path) {
            parent = static_cast<GameListDir*>(item);
            break;
        }
    }

    if (!parent) {
        // Parent not found (might have been removed or not added yet).
        // For safety, cleanup memory.
        for (auto* item : entry_items)
            delete item;
        return;
    }

    // Delta Update: Check if the game already exists in this folder to prevent duplicates
    // and instead update the metadata (like play-time) in place.
    const auto* path_item = static_cast<GameListItemPath*>(entry_items.at(COLUMN_NAME));
    const QString new_path = path_item->data(GameListItemPath::FullPathRole).toString();
    const u64 program_id = path_item->data(GameListItemPath::ProgramIdRole).toULongLong();
    bool found = false;

    for (int i = 0; i < parent->rowCount(); ++i) {
        auto* existing_path_item = static_cast<GameListItemPath*>(parent->child(i, COLUMN_NAME));
        if (existing_path_item &&
            (existing_path_item->data(GameListItemPath::ProgramIdRole).toULongLong() ==
                 program_id ||
             existing_path_item->data(GameListItemPath::FullPathRole).toString() == new_path)) {
            // Update existing row columns
            for (int col = 0; col < entry_items.size(); ++col) {
                QStandardItem* existing_col = parent->child(i, col);
                QStandardItem* new_col = entry_items.at(col);
                if (existing_col && new_col) {
                    existing_col->setText(new_col->text());
                    existing_col->setData(new_col->data(Qt::DisplayRole), Qt::DisplayRole);
                    existing_col->setData(new_col->data(GameListItem::SortRole),
                                          GameListItem::SortRole);

                    // Specific roles for GameListItemPath
                    if (col == 0) {
                        existing_col->setData(new_col->data(Qt::DecorationRole),
                                              Qt::DecorationRole);
                        existing_col->setData(new_col->data(GameListItemPath::HighResIconRole),
                                              GameListItemPath::HighResIconRole);
                    }

                    // Clear refreshing state on the play-time column once updated
                    if (col == COLUMN_PLAY_TIME) {
                        existing_col->setData(false, GameListItem::IsRefreshingRole);
                        existing_col->setData(new_col->data(GameListItemPlayTime::PlayTimeRole),
                                              GameListItemPlayTime::PlayTimeRole);
                    }
                }
            }
            found = true;
            break;
        }
    }

    if (!found) {
        parent->appendRow(entry_items);
        // Register the new index for bubble animation immediately
        auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate());
        if (delegate) {
            delegate->RegisterEntryAnimation(entry_items.first()->index());
        }

        // Dynamically populate grid and carousel views so games appear instantly when parsing
        QStandardItem* src = entry_items.first();
        QString path = src->data(GameListItemPath::FullPathRole).toString();
        
        if (!UISettings::values.hidden_paths.contains(path)) {
            QString title = src->data(GameListItemPath::TitleRole).toString();
            if (title.isEmpty()) {
                std::string fn;
                Common::SplitPath(path.toStdString(), nullptr, &fn, nullptr);
                title = QString::fromStdString(fn);
            }
            
            QString filter_text = search_field->filterText().toLower();
            if (filter_text.isEmpty() || ContainsAllWords(title.toLower(), filter_text)) {
                u64 pid = src->data(GameListItemPath::ProgramIdRole).toULongLong();
                bool is_fav = UISettings::values.favorited_ids.contains(pid);

                auto cloneToFlatModel = [&](QStandardItemModel* target, bool force_fav) {
                    if (!target) return;
                    QStandardItem* item = src->clone();
                    item->setText(title);
                    item->setData(title, GameListItemPath::SortRole);
                    item->setData(static_cast<int>(force_fav ? GameListItemType::Favorites : GameListItemType::Game), GameListItem::TypeRole);
                    
                    // Scale icon for flat layout
                    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
                    QVariant icon_data = item->data(Qt::DecorationRole);
                    if (icon_data.isValid() && icon_data.canConvert<QPixmap>()) {
                        QPixmap pixmap = icon_data.value<QPixmap>();
                        if (!pixmap.isNull()) {
                            item->setData(pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                        }
                    }

                    target->appendRow(item);
                    
                    // We typically want to sort grid models, but NOT the carousel (it uses its own segments)
                    if (target != static_cast<QStandardItemModel*>(carousel_view->view()->model())) {
                        target->sort(0, current_sort_order);
                    }

                    // Trigger "Pop-in" Animations for discovery
                    // Use !isHidden() instead of isVisible() to ensure it triggers during boot-time startup
                    if (grid_view && !grid_view->isHidden()) {
                        QListView* active_grid = force_fav ? grid_view->favView() : grid_view->view();
                        if (active_grid) {
                            auto* grid_delegate = qobject_cast<GameGridDelegate*>(active_grid->itemDelegate());
                            if (grid_delegate) {
                                // Find the new index in the target model after sorting to ensure animation triggers on the correct item
                                QModelIndex new_idx;
                                auto matches = target->match(target->index(0, 0), GameListItemPath::FullPathRole, path, 1, Qt::MatchExactly);
                                if (!matches.isEmpty()) {
                                    new_idx = matches.first();
                                    grid_delegate->RegisterEntryAnimation(new_idx);
                                }
                            }
                        }
                    }
                    if (carousel_view && !carousel_view->isHidden() && target == static_cast<QStandardItemModel*>(carousel_view->view()->model())) {
                        carousel_view->view()->RegisterEntryAnimation(target->index(target->rowCount() - 1, 0));
                    }
                };

                if (grid_view && grid_view->mainModel()) {
                    QStandardItemModel* target_model = static_cast<QStandardItemModel*>(is_fav ? grid_view->favModel() : grid_view->mainModel());
                    cloneToFlatModel(target_model, is_fav);
                    grid_view->UpdateGridSize();
                }
                if (carousel_view && carousel_view->view()->model()) {
                    QStandardItemModel* target_model = static_cast<QStandardItemModel*>(carousel_view->view()->model());
                    cloneToFlatModel(target_model, is_fav);
                }
            }
        }

    }

    // Auto-scroll to show new games as they appear (only on first-time or full rebuild)
    if (loading_overlay && loading_overlay->isVisible() && tree_view && !found) {
        tree_view->scrollTo(entry_items.first()->index(), QAbstractItemView::PositionAtBottom);
    }

    // Clean up temporary entry_items if we merged them
    if (found) {
        qDeleteAll(entry_items);
    }
}

void GameList::RefreshGame(u64 program_id, u64 play_time) {
    if (program_id == 0)
        return;

    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder)
            continue;

        for (int j = 0; j < folder->rowCount(); ++j) {
            auto* path_item = static_cast<GameListItemPath*>(folder->child(j, COLUMN_NAME));
            if (path_item &&
                path_item->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                QStandardItem* play_time_item = folder->child(j, COLUMN_PLAY_TIME);
                if (play_time_item) {
                    play_time_item->setData(static_cast<qulonglong>(play_time),
                                            GameListItemPlayTime::PlayTimeRole);
                    play_time_item->setData(false, GameListItem::IsRefreshingRole);
                }
                return;
            }
        }
    }
}

void GameList::ClearLaunchOverlays() {
    m_is_launching = false;
    if (fade_overlay) {
        fade_overlay->hide();
    }
    if (loading_overlay) {
        loading_overlay->FadeOut();
    }
}

void GameList::UpdateOnlineStatus() {
    // If the Online column is hidden in settings, skip all network pings and retries.
    if (!UISettings::values.show_online_column) {
        return;
    }

    auto mp_state = main_window->GetMultiplayerState();
    // If Multiplayer state or game list model isn't ready/populated yet, retry shortly.
    // This ensures we populate the "Online" column as soon as boot-up is complete.
    if (!mp_state || !item_model || item_model->rowCount() == 0) {
        QTimer::singleShot(3000, this, &GameList::UpdateOnlineStatus);
        return;
    }

    auto session = mp_state->GetSession();
    if (!session) {
        QTimer::singleShot(3000, this, &GameList::UpdateOnlineStatus);
        return;
    }

    // Skip network pings while a game is actively emulating
    if (main_window->IsEmulationRunning()) {
        return;
    }

    // A watcher gets the result back on the main thread safely
    auto online_status_watcher = new QFutureWatcher<std::map<u64, std::pair<int, int>>>(this);
    connect(online_status_watcher, &QFutureWatcher<std::map<u64, std::pair<int, int>>>::finished,
            this, [this, online_status_watcher]() {
                OnOnlineStatusUpdated(online_status_watcher->result());
                online_status_watcher->deleteLater(); // Clean up the watcher
            });

    // Run the blocking network call in a background thread using QtConcurrent
    QFuture<std::map<u64, std::pair<int, int>>> future = QtConcurrent::run([session]() {
        try {
            std::map<u64, std::pair<int, int>> stats;
            AnnounceMultiplayerRoom::RoomList room_list = session->GetRoomList();
            for (const auto& room : room_list) {
                u64 game_id = room.information.preferred_game.id;
                if (game_id != 0) {
                    stats[game_id].first += (int)room.members.size();
                    stats[game_id].second++;
                }
            }
            return stats;
        } catch (const std::exception& e) {
            LOG_ERROR(Frontend, "Exception in Online Status thread: {}", e.what());
            return std::map<u64, std::pair<int, int>>{};
        }
    });

    online_status_watcher->setFuture(future);
}

static void UpdateOnlineStatusRecursive(QStandardItem* parent,
                                        const std::map<u64, std::pair<int, int>>& online_stats) {
    if (!parent)
        return;

    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* item = parent->child(i, GameList::COLUMN_NAME);
        if (!item)
            continue;

        const auto item_type = item->data(GameListItem::TypeRole).value<GameListItemType>();

        if (item_type == GameListItemType::Game) {
            u64 program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
            QString online_text = QObject::tr("N/A");

            auto it_stats = online_stats.find(program_id);
            if (it_stats != online_stats.end()) {
                const auto& stats = it_stats->second;
                online_text =
                    QObject::tr("Players: %1 | Servers: %2").arg(stats.first).arg(stats.second);
            }

            QStandardItem* online_item = parent->child(i, GameList::COLUMN_ONLINE);
            if (online_item && online_item->data(Qt::DisplayRole).toString() != online_text) {
                online_item->setData(online_text, Qt::DisplayRole);
            }
        } else {
            // Recursive call for folders/categories
            UpdateOnlineStatusRecursive(item, online_stats);
        }
    }
}

void GameList::OnOnlineStatusUpdated(const std::map<u64, std::pair<int, int>>& online_stats) {
    if (!item_model) {
        return;
    }

    UpdateOnlineStatusRecursive(item_model->invisibleRootItem(), online_stats);
}

void GameList::ShowTechnicalInformation(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }

    const auto file_path = index.data(GameListItemPath::FullPathRole).toString();
    const auto program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    const auto format = index.data(GameListItemPath::FileTypeRole).toString();

    // Get size from the model (COLUMN_SIZE)
    const auto* model = index.model();
    const auto size_index = model->index(index.row(), COLUMN_SIZE, index.parent());
    const QString size_str = size_index.data(Qt::DisplayRole).toString();

    // Get Add-ons / Updates from the model (COLUMN_ADD_ONS)
    const auto addons_index = model->index(index.row(), COLUMN_ADD_ONS, index.parent());
    const QString updates = addons_index.data(Qt::DisplayRole).toString();

    QString technical_info =
        tr("File Path: %1\n\n"
           "Program ID: 0x%2\n\n"
           "Format: %3\n\n"
           "Size: %4\n\n"
           "Updates/Add-ons:\n%5")
            .arg(file_path)
            .arg(QString::number(program_id, 16).toUpper().mid(0, 16), 16, QLatin1Char('0'))
            .arg(format)
            .arg(size_str)
            .arg(updates.isEmpty() ? tr("None") : updates);

    QMessageBox msg_box(this);
    msg_box.setWindowTitle(tr("Game Information"));
    msg_box.setText(technical_info);
    msg_box.setIcon(QMessageBox::Information);

    const bool is_dark = UISettings::IsDarkTheme();
    const QString bg_color = is_dark ? QStringLiteral("#1c1c22") : QStringLiteral("#ffffff");
    const QString text_color = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString accent_hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    const QColor accent = QColor(accent_hex).isValid() ? QColor(accent_hex) : QColor(0, 150, 255);

    msg_box.setStyleSheet(
        QStringLiteral(
            "QMessageBox { background-color: %1; }"
            "QLabel { color: %2; font-family: 'Segoe UI', 'Roboto', sans-serif; font-size: 10pt; }"
            "QPushButton { background-color: %3; color: white; border: none; "
            "padding: 6px 20px; border-radius: 4px; font-weight: bold; min-width: 80px; }"
            "QPushButton:hover { background-color: %4; }")
            .arg(bg_color, text_color, accent.name(), accent.lighter(110).name()));

    msg_box.exec();
}

void GameList::OnSelectionChanged(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    // Filter out folders, categories, and "Add Directory" buttons
    const int type = index.data(GameListItem::TypeRole).toInt();
    if (type != static_cast<int>(GameListItemType::Game) &&
        type != static_cast<int>(GameListItemType::Favorites)) {
        return;
    }

    if (details_panel->isVisible()) {
        details_panel->updateDetails(index);
    }
}

void GameList::StartLaunchAnimation(const QModelIndex& item) {
    if (m_is_launching || !item.isValid() || !item.model()) {
        return;
    }

    const QString file_path = item.data(GameListItemPath::FullPathRole).toString();
    if (file_path.isEmpty()) {
        return;
    }

    m_is_launching = true;

    u64 program_id = item.data(GameListItemPath::ProgramIdRole).toULongLong();
    QStandardItem* original_item = nullptr;
    for (int folder_idx = 0; folder_idx < item_model->rowCount(); ++folder_idx) {
        QStandardItem* folder = item_model->item(folder_idx, 0);
        if (!folder)
            continue;
        for (int game_idx = 0; game_idx < folder->rowCount(); ++game_idx) {
            QStandardItem* game = folder->child(game_idx, 0);
            if (game && game->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                original_item = game;
                break;
            }
        }
        if (original_item)
            break;
    }

    QPixmap icon;
    if (original_item) {
        icon = original_item->data(GameListItemPath::HighResIconRole).value<QPixmap>();
        if (icon.isNull()) {
            icon = original_item->data(Qt::DecorationRole).value<QPixmap>();
        } else {
            // Apply rounded corners to the high-res icon
            icon = CreateRoundIcon(icon, 256);
        }
    } else {
        // Fallback for safety
        icon = item.data(Qt::DecorationRole).value<QPixmap>();
    }

    // If we still have no icon, launch instantly without animation
    if (icon.isNull()) {
        const auto title_id = item.data(GameListItemPath::ProgramIdRole).toULongLong();
        emit GameChosen(file_path, title_id);
        m_is_launching = false;
        return;
    }

    // --- 2. FADE GAME LIST TO BLACK ---
    fade_overlay->setGeometry(rect()); // Ensure size is correct
    fade_overlay->raise();
    fade_overlay->show();

    auto* list_fade_effect = new QGraphicsOpacityEffect(fade_overlay);
    fade_overlay->setGraphicsEffect(list_fade_effect);
    auto* list_fade_in_anim = new QPropertyAnimation(list_fade_effect, "opacity");
    list_fade_in_anim->setDuration(400); // Sync with icon zoom
    list_fade_in_anim->setStartValue(0.0f);
    list_fade_in_anim->setEndValue(1.0f);
    list_fade_in_anim->setEasingCurve(QEasingCurve::OutCubic);
    list_fade_in_anim->start(QAbstractAnimation::DeleteWhenStopped);

    // --- 3. ICON ANIMATION ---
    const auto title_id = item.data(GameListItemPath::ProgramIdRole).toULongLong();
    QRect start_geom;
    if (main_stack->currentIndex() == 0) {
        start_geom = tree_view->visualRect(item.sibling(item.row(), 0));
        start_geom.setTopLeft(tree_view->viewport()->mapTo(main_window, start_geom.topLeft()));
    } else if (main_stack->currentIndex() == 1) {
        start_geom = grid_view->visualRect(item);
        start_geom.setTopLeft(grid_view->viewport()->mapTo(main_window, start_geom.topLeft()));
    } else {
        start_geom = carousel_view->view()->visualRect(item);
        start_geom.setTopLeft(
            carousel_view->view()->viewport()->mapTo(main_window, start_geom.topLeft()));
    }

    auto* animation_label = new QLabel(main_window);
    animation_label->setPixmap(icon);
    animation_label->setScaledContents(true);
    animation_label->setGeometry(start_geom);
    animation_label->show();
    animation_label->raise();

    const int target_size = 256; // Use full 256x256 resolution
    const QPoint center_point = main_window->rect().center();

    QRect zoom_end_geom(0, 0, target_size, target_size);
    zoom_end_geom.moveCenter(center_point);
    QRect fly_end_geom = zoom_end_geom;
    fly_end_geom.moveCenter(QPoint(center_point.x(), -target_size));

    auto* zoom_anim = new QPropertyAnimation(animation_label, "geometry");
    zoom_anim->setDuration(400);
    zoom_anim->setStartValue(start_geom);
    zoom_anim->setEndValue(zoom_end_geom);
    zoom_anim->setEasingCurve(QEasingCurve::OutCubic);

    auto* fly_fade_group = new QParallelAnimationGroup;
    auto* icon_effect = new QGraphicsOpacityEffect(animation_label);
    animation_label->setGraphicsEffect(icon_effect);
    auto* fly_anim = new QPropertyAnimation(animation_label, "geometry");
    fly_anim->setDuration(350);
    fly_anim->setStartValue(zoom_end_geom);
    fly_anim->setEndValue(fly_end_geom);
    fly_anim->setEasingCurve(QEasingCurve::InQuad);
    auto* icon_fade_anim = new QPropertyAnimation(icon_effect, "opacity");
    icon_fade_anim->setDuration(350);
    icon_fade_anim->setStartValue(1.0f);
    icon_fade_anim->setEndValue(0.0f);
    icon_fade_anim->setEasingCurve(QEasingCurve::InQuad);
    fly_fade_group->addAnimation(fly_anim);
    fly_fade_group->addAnimation(icon_fade_anim);

    // --- 4. CITRON LOGO TRANSITION ---
    auto* logo_widget = new LogoAnimationWidget(main_window);
    logo_widget->setFixedSize(500, 500); // Larger container for rotation/scaling
    logo_widget->move(center_point.x() - 250, center_point.y() - 250);
    logo_widget->hide();

    auto* logo_effect = new QGraphicsOpacityEffect(logo_widget);
    logo_widget->setGraphicsEffect(logo_effect);
    logo_effect->setOpacity(0.0f);

    // Fade in animation
    auto* logo_fade_in = new QPropertyAnimation(logo_effect, "opacity");
    logo_fade_in->setDuration(600);
    logo_fade_in->setStartValue(0.0f);
    logo_fade_in->setEndValue(1.0f);
    logo_fade_in->setEasingCurve(QEasingCurve::OutCubic);

    // Initial scale-up and spin
    auto* logo_spin = new QPropertyAnimation(logo_widget, "rotation");
    logo_spin->setDuration(1200);
    logo_spin->setStartValue(0.0);
    logo_spin->setEndValue(360.0 * 2.0); // Two full spins
    logo_spin->setEasingCurve(QEasingCurve::OutQuint);

    auto* logo_scale_up = new QPropertyAnimation(logo_widget, "scale");
    logo_scale_up->setDuration(1000);
    logo_scale_up->setStartValue(0.1);
    logo_scale_up->setEndValue(1.0);
    logo_scale_up->setEasingCurve(QEasingCurve::OutBack);

    // Final "coming towards screen" and fade out
    auto* logo_final_scale = new QPropertyAnimation(logo_widget, "scale");
    logo_final_scale->setDuration(600);
    logo_final_scale->setStartValue(1.0);
    logo_final_scale->setEndValue(2.5); // Fly towards camera
    logo_final_scale->setEasingCurve(QEasingCurve::InExpo);

    auto* logo_fade_out = new QPropertyAnimation(logo_effect, "opacity");
    logo_fade_out->setDuration(500);
    logo_fade_out->setStartValue(1.0f);
    logo_fade_out->setEndValue(0.0f);
    logo_fade_out->setEasingCurve(QEasingCurve::InQuad);

    auto* final_fly_fade = new QParallelAnimationGroup;
    final_fly_fade->addAnimation(logo_final_scale);
    final_fly_fade->addAnimation(logo_fade_out);

    // Overlap the icon "fly-away" and the logo "fade-in"
    auto* overlap_group = new QParallelAnimationGroup;
    overlap_group->addAnimation(fly_fade_group);

    auto* logo_intro_group = new QParallelAnimationGroup;
    logo_intro_group->addAnimation(logo_fade_in);
    logo_intro_group->addAnimation(logo_spin);
    logo_intro_group->addAnimation(logo_scale_up);

    auto* logo_intro_seq = new QSequentialAnimationGroup;
    logo_intro_seq->addPause(100); // 100ms delay so it starts mid-fly
    logo_intro_seq->addAnimation(logo_intro_group);
    overlap_group->addAnimation(logo_intro_seq);

    auto* main_group = new QSequentialAnimationGroup(this);
    main_group->addAnimation(zoom_anim);
    main_group->addPause(50);

    // Show logo once zoom is finished, just before fly/fade starts
    connect(zoom_anim, &QPropertyAnimation::finished, [logo_widget]() {
        logo_widget->show();
        logo_widget->raise();
    });

    main_group->addAnimation(overlap_group);
    main_group->addPause(400); // Shorter pause before final effect
    main_group->addAnimation(final_fly_fade);

    // When the zoom animation is done, we already know the core can start booting
    // in the background while the rest of the animation plays out.
    // The game core will be signaled to boot when the FULL animation finishes.
    // This prevents the core's loading screen from overlapping with our pre-launch effects.

    // When the FULL animation group finishes, clean up the UI overlays safely.
    connect(main_group, &QSequentialAnimationGroup::finished, this,
            [this, animation_label, logo_widget, file_path, title_id]() {
                if (animation_label) {
                    animation_label->hide();
                    animation_label->deleteLater();
                }
                if (logo_widget) {
                    logo_widget->hide();
                    logo_widget->deleteLater();
                }

                if (fade_overlay) {
                    fade_overlay->hide();
                }

                search_field->clear();
                m_is_launching = false;
                emit GameChosen(file_path, title_id);
            });

    main_group->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameList::ValidateEntry(const QModelIndex& item) {
    if (m_is_launching || !item.isValid() || !item.model()) {
        return;
    }
    const auto selected = item.sibling(item.row(), 0);
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game:
    case GameListItemType::Favorites: {
        const QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
        if (file_path.isEmpty())
            return;
        const QFileInfo file_info(file_path);
        if (!file_info.exists())
            return;

        // If the entry is a directory (e.g., for homebrew), launch it directly without animation.
        if (file_info.isDir()) {
            m_is_launching = true;
            const QDir dir{file_path};
            const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
            if (matching_main.size() == 1) {
                emit GameChosen(dir.path() + QDir::separator() + matching_main[0]);
            }
            m_is_launching = false;
            return; // Exit here for directories
        }

        // If it's a standard game file, trigger the new launch animation.
        // The animation function will handle emitting GameChosen when it's finished.
        StartLaunchAnimation(selected);
        break;
    }
    case GameListItemType::AddDir:
        emit AddDirectory();

        if (UISettings::values.prompt_for_autoloader) {
            QMessageBox msg_box(this);
            msg_box.setWindowTitle(tr("Autoloader"));
            msg_box.setText(
                tr("Would you like to use the Autoloader to install all Updates/DLC within your "
                   "game directories?\n\n"
                   "If not now, you can always go to Emulation -> Configure -> Filesystem in order "
                   "to use this feature. Also, if you have multiple update files for a single "
                   "game, you can use the Update Manager "
                   "in File -> Install Updates with Update Manager."));
            msg_box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            QCheckBox* check_box = new QCheckBox(tr("Do not ask me again"));
            msg_box.setCheckBox(check_box);

            if (msg_box.exec() == QMessageBox::Yes) {
                emit RunAutoloaderRequested();
            }

            if (check_box->isChecked()) {
                UISettings::values.prompt_for_autoloader = false;
                emit SaveConfig();
            }
        }
        break;
    default:
        break;
    }
}

bool GameList::IsEmpty() const {
    for (int i = 0; i < item_model->rowCount(); i++) {
        const QStandardItem* child = item_model->invisibleRootItem()->child(i);
        const auto type = static_cast<GameListItemType>(child->type());
        if (!child->hasChildren() &&
            (type == GameListItemType::SdmcDir || type == GameListItemType::UserNandDir ||
             type == GameListItemType::SysNandDir)) {
            item_model->invisibleRootItem()->removeRow(child->row());
            i--;
        }
    }
    return !item_model->invisibleRootItem()->hasChildren();
}

void GameList::DonePopulating(const QStringList& watch_list) {
    if (item_model) {
        item_model->sort(COLUMN_NAME, Qt::AscendingOrder);
    }

    if (loading_overlay) {
        loading_overlay->ShowPopulated();

        // Animated scroll back to the top
        if (tree_view && tree_view->verticalScrollBar()) {
            auto* scroll_anim = new QPropertyAnimation(tree_view->verticalScrollBar(), "value");
            scroll_anim->setDuration(800);
            scroll_anim->setStartValue(tree_view->verticalScrollBar()->value());
            scroll_anim->setEndValue(0);
            scroll_anim->setEasingCurve(QEasingCurve::InOutSine);
            scroll_anim->start(QAbstractAnimation::DeleteWhenStopped);
        }

        // Give it a moment to show the success message before fading
        QTimer::singleShot(1500, this, [this]() {
            if (loading_overlay)
                loading_overlay->FadeOut();
            auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate());
            if (delegate)
                delegate->SetPopulating(false);

            // Save the newly populated index for instant loading next time.
            SaveGameListIndex();

            // Clean up worker safely outside of its execution context
            QTimer::singleShot(0, this, [this]() { current_worker.reset(); });
        });
    } else {
        auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate());
        if (delegate)
            delegate->SetPopulating(false);
        QTimer::singleShot(0, this, [this]() { current_worker.reset(); });
    }

    for (const auto& watch_dir : watch_list) {
        watcher->addPath(watch_dir);
    }
    emit PopulatingCompleted();

    if (progress_bar) {
        progress_bar->setVisible(false);
    }
    emit ShowList(!IsEmpty());
    // Only add the "Add Directory" item to the main model for List View.
    // Grid and Carousel use a flat model and explicitly skip this type.
    QStandardItem* fav_folder = nullptr;
    for (int i = 0; i < item_model->rowCount(); ++i) {
        if (item_model->item(i)->data(GameListItem::TypeRole).value<GameListItemType>() ==
            GameListItemType::Favorites) {
            fav_folder = item_model->item(i);
            break;
        }
    }
    if (!fav_folder) {
        fav_folder = new GameListFavorites();
        item_model->invisibleRootItem()->insertRow(0, fav_folder);
    } else {
        fav_folder->removeRows(0, fav_folder->rowCount());
    }
    tree_view->setRowHidden(fav_folder->row(), item_model->invisibleRootItem()->index(),
                            UISettings::values.favorited_ids.size() == 0);
    tree_view->setExpanded(fav_folder->index(), UISettings::values.favorites_expanded.GetValue());
    for (const auto id : UISettings::values.favorited_ids)
        AddFavorite(id);
    auto watch_dirs = watcher->directories();
    if (!watch_dirs.isEmpty()) {
        watcher->removePaths(watch_dirs);
    }
    constexpr int LIMIT_WATCH_DIRECTORIES = 5000;
    constexpr int SLICE_SIZE = 25;
    int len = std::min(static_cast<int>(watch_list.size()), LIMIT_WATCH_DIRECTORIES);
    tree_view->setEnabled(true);
    tree_view->setFocus();

    for (int i = 0; i < len; i += SLICE_SIZE) {
        watcher->addPaths(watch_list.mid(i, i + SLICE_SIZE));
        QCoreApplication::processEvents();
    }
    int children_total = 0;
    for (int i = 1; i < item_model->rowCount() - 1; ++i) {
        children_total += item_model->item(i, 0)->rowCount();
    }
    search_field->setFilterResult(children_total, children_total);
    if (children_total > 0) {
        search_field->setFocus();
    }
    item_model->sort(tree_view->header()->sortIndicatorSection(),
                     tree_view->header()->sortIndicatorOrder());
    if (main_stack->currentIndex() > 0) {
        // Preserve filter when repopulating
        QString filter_text = search_field->filterText();
        FilterGridView(filter_text);
    } else {
        FilterTreeView(search_field->filterText());
    }

    // Only sync if we aren't rebuilding the UI and the game isn't running.
    if (main_window && !main_window->IsConfiguring() && !system.IsPoweredOn()) {
        if (!main_window->HasPerformedInitialSync()) {
            LOG_INFO(Frontend, "Mirroring: Performing one-time startup sync...");
            system.GetFileSystemController().GetSaveDataFactory().PerformStartupMirrorSync();
            main_window->SetPerformedInitialSync(true);
        } else {
            LOG_INFO(Frontend, "Mirroring: Startup sync already performed this session. Skipping.");
        }
    } else {
        LOG_INFO(Frontend,
                 "Mirroring: Startup sync skipped (Reason: UI Busy or Game is Emulating).");
    }

    // Automatically refresh compatibility data from GitHub if enabled
    if (UISettings::values.show_compat) {
        RefreshCompatibilityList();
    }
}

void GameList::PopupContextMenu(const QPoint& menu_location) {
    QModelIndex item;
    int current = main_stack->currentIndex();
    if (current == 0) {
        item = tree_view->indexAt(menu_location);
    } else if (current == 1) {
        QListView* sending_view = qobject_cast<QListView*>(sender());
        item = sending_view ? sending_view->indexAt(menu_location)
                            : grid_view->view()->indexAt(menu_location);
    } else {
        item = carousel_view->view()->indexAt(menu_location);
    }

    if (!item.isValid())
        return;
    const auto selected = item.sibling(item.row(), 0);
    QWidget* parent_widget = nullptr;
    if (current == 0)
        parent_widget = tree_view->viewport();
    else if (current == 1)
        parent_widget = grid_view->view()->viewport();
    else
        parent_widget = carousel_view->view()->viewport();

    QMenu context_menu(parent_widget);
    context_menu.setAttribute(Qt::WA_TranslucentBackground, false);
    if (Theme::IsDarkMode()) {
        context_menu.setStyleSheet(QStringLiteral(
            "QMenu { background: #24242a; border: 1px solid #32323a; border-radius: 8px; padding: "
            "6px; "
            "color: #ffffff; }"
            "QMenu::item { padding: 6px 30px; border-radius: 4px; margin: 2px; color: #ffffff; }"
            "QMenu::item:selected { background-color: #32323a; border: 1px solid #42424a; }"
            "QMenu::separator { height: 1px; background: #32323a; margin: 4px 10px; }"));
    } else {
        context_menu.setStyleSheet(QStringLiteral(
            "QMenu { background: #ffffff; border: 1px solid #d0d0d8; border-radius: 8px; padding: "
            "6px; "
            "color: #1a1a1a; }"
            "QMenu::item { padding: 6px 30px; border-radius: 4px; margin: 2px; color: #1a1a1a; }"
            "QMenu::item:selected { background-color: #ebebf0; border: 1px solid #c8c8d0; }"
            "QMenu::separator { height: 1px; background: #d8d8e0; margin: 4px 10px; }"));
    }
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Favorites:
    case GameListItemType::Game: {
        const u64 program_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();
        const std::string path =
            selected.data(GameListItemPath::FullPathRole).toString().toStdString();
        const auto game_name = selected.data(GameListItemPath::OriginalTitleRole).toString();
        if (program_id != 0 || !path.empty()) {
            AddGamePopup(context_menu, selected, program_id, path, game_name);
            break;
        }
        [[fallthrough]];
    }
    case GameListItemType::CustomDir:
        AddPermDirPopup(context_menu, selected);
        AddCustomDirPopup(context_menu, selected, false);
        break;
    case GameListItemType::SdmcDir:
    case GameListItemType::UserNandDir:
    case GameListItemType::SysNandDir:
        AddPermDirPopup(context_menu, selected);
        break;
    default:
        break;
    }

    if (current == 0) {
        context_menu.exec(tree_view->viewport()->mapToGlobal(menu_location));
    } else if (current == 1) {
        QListView* exec_view = qobject_cast<QListView*>(sender());
        QPoint global_pt = exec_view ? exec_view->viewport()->mapToGlobal(menu_location)
                                     : grid_view->view()->viewport()->mapToGlobal(menu_location);
        context_menu.exec(global_pt);
    } else {
        context_menu.exec(carousel_view->view()->viewport()->mapToGlobal(menu_location));
    }
}

void GameList::AddGamePopup(QMenu& context_menu, const QModelIndex& index, u64 program_id,
                            const std::string& path_str, const QString& game_name) {
    const QString path = QString::fromStdString(path_str);
    const bool is_mirrored = Settings::values.mirrored_save_paths.count(program_id);
    const bool has_custom_path = Settings::values.custom_save_paths.count(program_id);
    QString mirror_base_path;

    auto calculateTotalSize = [](const QString& dirPath) -> qint64 {
        qint64 totalSize = 0;
        QDirIterator size_it(dirPath, QDirIterator::Subdirectories);
        while (size_it.hasNext()) {
            size_it.next();
            QFileInfo fileInfo = size_it.fileInfo();
            if (fileInfo.isFile()) {
                totalSize += fileInfo.size();
            }
        }
        return totalSize;
    };

    auto copyWithProgress = [calculateTotalSize](const QString& sourceDir, const QString& destDir,
                                                 QWidget* parent) -> bool {
        QProgressDialog progress(tr("Moving Save Data..."), QString(), 0, 100, parent);
        progress.setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        qint64 totalSize = calculateTotalSize(sourceDir);
        qint64 copiedSize = 0;
        QDir dir(sourceDir);
        if (!dir.exists())
            return false;
        QDir dest_dir(destDir);
        if (!dest_dir.exists())
            dest_dir.mkpath(QStringLiteral("."));
        QDirIterator dir_iter(sourceDir, QDirIterator::Subdirectories);
        while (dir_iter.hasNext()) {
            dir_iter.next();
            const QFileInfo file_info = dir_iter.fileInfo();
            const QString relative_path = dir.relativeFilePath(file_info.absoluteFilePath());
            const QString dest_path = QDir(destDir).filePath(relative_path);
            if (file_info.isDir()) {
                dest_dir.mkpath(dest_path);
            } else if (file_info.isFile()) {
                if (QFile::exists(dest_path))
                    QFile::remove(dest_path);
                if (!QFile::copy(file_info.absoluteFilePath(), dest_path))
                    return false;
                copiedSize += file_info.size();
                if (totalSize > 0) {
                    progress.setValue(static_cast<int>((copiedSize * 100) / totalSize));
                }
            }
            QCoreApplication::processEvents();
        }
        progress.setValue(100);
        return true;
    };

    QAction* favorite = context_menu.addAction(tr("Favorite"));
    QAction* hide_game = context_menu.addAction(tr("Hide Game"));
    context_menu.addSeparator();
    QAction* start_game = context_menu.addAction(tr("Start Game"));
    QAction* start_game_global =
        context_menu.addAction(tr("Start Game without Custom Configuration"));
    context_menu.addSeparator();
    QAction* open_save_location = context_menu.addAction(tr("Open Save Data Location"));
    QAction* open_nand_location = context_menu.addAction(tr("Open NAND Location"));
    QAction* open_file_location = context_menu.addAction(tr("Open File Location"));
    QAction* set_custom_save_path = context_menu.addAction(tr("Set Custom Save Path"));
    QAction* remove_custom_save_path = context_menu.addAction(tr("Revert to NAND Save Path"));
    QAction* disable_mirroring = context_menu.addAction(tr("Disable Mirroring"));
    QAction* open_mod_location = context_menu.addAction(tr("Open Mod Data Location"));
    QMenu* open_sdmc_mod_menu = context_menu.addMenu(tr("Open SDMC Mod Data Location"));
    QAction* open_current_game_sdmc =
        open_sdmc_mod_menu->addAction(tr("Open Current Game Location"));
    QAction* open_full_sdmc = open_sdmc_mod_menu->addAction(tr("Open Full Location"));
    QAction* open_transferable_shader_cache =
        context_menu.addAction(tr("Open Transferable Pipeline Cache"));
    context_menu.addSeparator();
    QMenu* remove_menu = context_menu.addMenu(tr("Remove"));
    QAction* remove_update = remove_menu->addAction(tr("Remove Installed Update"));
    QAction* remove_dlc = remove_menu->addAction(tr("Remove All Installed DLC"));
    QAction* remove_custom_config = remove_menu->addAction(tr("Remove Custom Configuration"));
    QAction* remove_play_time_data = remove_menu->addAction(tr("Remove Play Time Data"));
    QAction* remove_cache_storage = remove_menu->addAction(tr("Remove Cache Storage"));
    QAction* remove_vk_shader_cache = remove_menu->addAction(tr("Remove Vulkan Pipeline Cache"));
    remove_menu->addSeparator();
    QAction* remove_shader_cache = remove_menu->addAction(tr("Remove All Pipeline Caches"));
    QAction* remove_all_content = remove_menu->addAction(tr("Remove All Installed Contents"));
    QMenu* dump_romfs_menu = context_menu.addMenu(tr("Dump RomFS"));
    QAction* dump_romfs = dump_romfs_menu->addAction(tr("Dump RomFS"));
    QAction* dump_romfs_sdmc = dump_romfs_menu->addAction(tr("Dump RomFS to SDMC"));
    QAction* verify_integrity = context_menu.addAction(tr("Verify Integrity"));
    QAction* copy_tid = context_menu.addAction(tr("Copy Title ID to Clipboard"));
    QAction* submit_compat_report = context_menu.addAction(tr("Submit Compatibility Report"));
#if !defined(__APPLE__)
    QMenu* shortcut_menu = context_menu.addMenu(tr("Create Shortcut"));
    QAction* create_desktop_shortcut = shortcut_menu->addAction(tr("Add to Desktop"));
    QAction* create_applications_menu_shortcut =
        shortcut_menu->addAction(tr("Add to Applications Menu"));
#endif
    context_menu.addSeparator();
    QAction* edit_metadata = context_menu.addAction(tr("Edit Metadata"));
    QAction* properties = context_menu.addAction(tr("Properties"));

    connect(
        edit_metadata, &QAction::triggered, this,
        [this, program_id, game_name] {
            const u64 current_play_time = play_time_manager.GetPlayTime(program_id);
            CustomMetadataDialog dialog(this, program_id, game_name.toStdString(),
                                        current_play_time);
            if (dialog.exec() == QDialog::Accepted) {
                auto& custom_metadata = Citron::CustomMetadata::GetInstance();
                if (dialog.WasReset()) {
                    custom_metadata.RemoveCustomMetadata(program_id);
                } else {
                    custom_metadata.SetCustomTitle(program_id, dialog.GetTitle());
                    const std::string icon_path = dialog.GetIconPath();
                    if (!icon_path.empty()) {
                        custom_metadata.SetCustomIcon(program_id, icon_path);
                    }
                    play_time_manager.SetPlayTime(program_id, dialog.GetPlayTime());
                }
                // Invalidate game list cache to force re-scan of canonical titles
                const auto cache_dir =
                    Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list";
                const auto cache_file =
                    Common::FS::PathToUTF8String(cache_dir / "game_metadata_cache.json");
                Common::FS::RemoveFile(cache_file);

                if (main_window) {
                    main_window->RefreshGameList();
                }
            }
        },
        Qt::QueuedConnection);

    favorite->setVisible(program_id != 0);
    favorite->setCheckable(true);
    favorite->setChecked(UISettings::values.favorited_ids.contains(program_id));

    hide_game->setVisible(program_id != 0);
    hide_game->setCheckable(true);
    hide_game->setChecked(UISettings::values.hidden_paths.contains(path));
    if (hide_game->isChecked()) {
        hide_game->setText(tr("Unhide Game"));
    }

    open_file_location->setVisible(program_id != 0);
    open_save_location->setVisible(program_id != 0);
    open_nand_location->setVisible(is_mirrored);
    open_nand_location->setToolTip(tr("Citron uses your NAND while syncing. If you need to make "
                                      "save data modifications, do so in here."));
    set_custom_save_path->setVisible(program_id != 0 && !is_mirrored);
    remove_custom_save_path->setVisible(program_id != 0 && has_custom_path);
    disable_mirroring->setVisible(is_mirrored);
    open_mod_location->setVisible(program_id != 0);
    open_sdmc_mod_menu->menuAction()->setVisible(program_id != 0);
    open_transferable_shader_cache->setVisible(program_id != 0);
    remove_update->setVisible(program_id != 0);
    remove_dlc->setVisible(program_id != 0);
    remove_vk_shader_cache->setVisible(program_id != 0);
    remove_shader_cache->setVisible(program_id != 0);
    remove_all_content->setVisible(program_id != 0);

    if (is_mirrored) {
        const bool has_global_path = Settings::values.global_custom_save_path_enabled.GetValue() &&
                                     !Settings::values.global_custom_save_path.GetValue().empty();

        if (has_global_path) {
            open_nand_location->setText(tr("Open Global Save Path Location"));
            open_nand_location->setToolTip(
                tr("The global save path is being used as the base for save data mirroring."));
            mirror_base_path =
                QString::fromStdString(Settings::values.global_custom_save_path.GetValue());
        } else {
            open_nand_location->setToolTip(
                tr("Citron's default NAND is being used as the base for save data mirroring."));
            mirror_base_path = QString::fromStdString(
                Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir));
        }

        connect(open_nand_location, &QAction::triggered, [this, program_id, mirror_base_path]() {
            const auto user_id = system.GetProfileManager().GetLastOpenedUser().AsU128();
            const std::string relative_save_path = fmt::format(
                "user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, static_cast<uint64_t>(user_id[1]),
                static_cast<uint64_t>(user_id[0]), static_cast<uint64_t>(program_id));
            const auto full_save_path =
                std::filesystem::path(mirror_base_path.toStdString()) / relative_save_path;
            if (!std::filesystem::exists(full_save_path.parent_path())) {
                std::filesystem::create_directories(full_save_path.parent_path());
            }
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QString::fromStdString(full_save_path.string())));
        });
    }

    submit_compat_report->setToolTip(tr("Requires GitHub account."));

    connect(favorite, &QAction::triggered, [this, program_id]() { ToggleFavorite(program_id); });
    connect(hide_game, &QAction::triggered, [this, path]() { ToggleHidden(path); });
    connect(open_file_location, &QAction::triggered, [path_str]() {
        const QString qpath = QString::fromStdString(path_str);
        const QFileInfo file_info(qpath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(file_info.absolutePath()));
    });
    connect(open_save_location, &QAction::triggered, [this, program_id, path_str]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData, path_str);
    });

    connect(set_custom_save_path, &QAction::triggered, [this, program_id, copyWithProgress]() {
        const QString new_path =
            QFileDialog::getExistingDirectory(this, tr("Select Custom Save Data Location"));
        if (new_path.isEmpty())
            return;
        std::string base_save_path_str;
        if (Settings::values.global_custom_save_path_enabled.GetValue() &&
            !Settings::values.global_custom_save_path.GetValue().empty()) {
            base_save_path_str = Settings::values.global_custom_save_path.GetValue();
        } else {
            base_save_path_str = Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir);
        }
        const QString base_dir = QString::fromStdString(base_save_path_str);
        const auto user_id = system.GetProfileManager().GetLastOpenedUser().AsU128();
        const std::string relative_save_path = fmt::format(
            "user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, static_cast<uint64_t>(user_id[1]),
            static_cast<uint64_t>(user_id[0]), static_cast<uint64_t>(program_id));
        const QString internal_save_path =
            QDir(base_dir).filePath(QString::fromStdString(relative_save_path));
        bool mirroring_enabled = false;
        QString detected_emu = GetDetectedEmulatorName(new_path, program_id, base_dir);
        if (!detected_emu.isEmpty()) {
            QMessageBox::StandardButton mirror_reply =
                QMessageBox::question(this, tr("Enable Save Mirroring?"),
                                      tr("Citron has detected a %1 save structure.\n\n"
                                         "Would you like to enable 'Intelligent Mirroring'? This "
                                         "will pull the data into Citron's internal save directory "
                                         "(currently set to '%2') and keep both locations synced "
                                         "whenever you play. A backup of your existing Citron data "
                                         "will be created. BE WARNED: Please do not have both "
                                         "emulators open during this process.")
                                          .arg(detected_emu, base_dir),
                                      QMessageBox::Yes | QMessageBox::No);

            if (mirror_reply == QMessageBox::Yes) {
                mirroring_enabled = true;
            }
        }
        QDir internal_dir(internal_save_path);
        if (internal_dir.exists() && !internal_dir.isEmpty()) {
            if (mirroring_enabled) {
                QString timestamp =
                    QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss"));
                QString backup_path =
                    internal_save_path + QStringLiteral("_mirror_backup_") + timestamp;
                QDir().mkpath(QFileInfo(backup_path).absolutePath());
                if (QDir().rename(internal_save_path, backup_path)) {
                    LOG_INFO(Frontend, "Safety: Existing internal data moved to backup: {}",
                             backup_path.toStdString());
                }
            } else {
                QMessageBox::StandardButton reply = QMessageBox::question(
                    this, tr("Move Save Data"),
                    tr("You have existing save data in your internal save directory. Would you "
                       "like to move it to the new custom save path?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
                if (reply == QMessageBox::Cancel)
                    return;
                if (reply == QMessageBox::Yes) {
                    const QString full_dest_path =
                        QDir(new_path).filePath(QString::fromStdString(relative_save_path));
                    if (copyWithProgress(internal_save_path, full_dest_path, this)) {
                        QDir(internal_save_path).removeRecursively();
                        QMessageBox::information(
                            this, tr("Success"),
                            tr("Successfully moved save data to the new location."));
                    } else {
                        QMessageBox::warning(
                            this, tr("Error"),
                            tr("Failed to move save data. Please see the log for more details."));
                    }
                }
            }
        }
        if (mirroring_enabled) {
            if (copyWithProgress(new_path, internal_save_path, this)) {
                Settings::values.mirrored_save_paths.insert_or_assign(program_id,
                                                                      new_path.toStdString());
                Settings::values.custom_save_paths.erase(program_id);
                QMessageBox::information(this, tr("Success"),
                                         tr("Mirroring established. Your data has been pulled into "
                                            "the internal Citron save directory."));
            } else {
                QMessageBox::warning(this, tr("Error"),
                                     tr("Failed to pull data from the mirror source."));
                return;
            }
        } else {
            Settings::values.custom_save_paths.insert_or_assign(program_id, new_path.toStdString());
            Settings::values.mirrored_save_paths.erase(program_id);
        }
        emit SaveConfig();
    });

    connect(disable_mirroring, &QAction::triggered, [this, program_id]() {
        if (QMessageBox::question(this, tr("Disable Mirroring"),
                                  tr("Are you sure you want to disable mirroring for this "
                                     "game?\n\nThe directories will no longer be synced."),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            Settings::values.mirrored_save_paths.erase(program_id);
            emit SaveConfig();
            QMessageBox::information(this, tr("Mirroring Disabled"),
                                     tr("Mirroring has been disabled for this game. It will now "
                                        "use the save data from the NAND."));
        }
    });
    connect(open_current_game_sdmc, &QAction::triggered, [program_id]() {
        const auto sdmc_path = Common::FS::GetCitronPath(Common::FS::CitronPath::SDMCDir);
        const auto full_path = sdmc_path / "atmosphere" / "contents" /
                               fmt::format("{:016X}", static_cast<uint64_t>(program_id));
        const QString qpath = QString::fromStdString(Common::FS::PathToUTF8String(full_path));
        QDir dir(qpath);
        if (!dir.exists())
            dir.mkpath(QStringLiteral("."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
    });
    connect(open_full_sdmc, &QAction::triggered, []() {
        const auto sdmc_path = Common::FS::GetCitronPath(Common::FS::CitronPath::SDMCDir);
        const auto full_path = sdmc_path / "atmosphere" / "contents";
        const QString qpath = QString::fromStdString(Common::FS::PathToUTF8String(full_path));
        QDir dir(qpath);
        if (!dir.exists())
            dir.mkpath(QStringLiteral("."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
    });
    connect(start_game, &QAction::triggered, [this, index]() { ValidateEntry(index); });
    connect(start_game_global, &QAction::triggered, [this, path_str]() {
        emit BootGame(QString::fromStdString(path_str), StartGameType::Global);
    });
    connect(open_mod_location, &QAction::triggered, [this, program_id, path_str]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::ModData, path_str);
    });
    connect(open_transferable_shader_cache, &QAction::triggered,
            [this, program_id]() { emit OpenTransferableShaderCacheRequested(program_id); });
    connect(remove_all_content, &QAction::triggered, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::Game);
    });
    connect(remove_update, &QAction::triggered, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::Update);
    });
    connect(remove_dlc, &QAction::triggered, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::AddOnContent);
    });
    connect(remove_vk_shader_cache, &QAction::triggered, [this, program_id, path_str]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::VkShaderCache, path_str);
    });
    connect(remove_shader_cache, &QAction::triggered, [this, program_id, path_str]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::AllShaderCache, path_str);
    });
    connect(remove_custom_config, &QAction::triggered, [this, program_id, path_str]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::CustomConfiguration, path_str);
    });
    connect(remove_play_time_data, &QAction::triggered,
            [this, program_id]() { emit RemovePlayTimeRequested(program_id); });
    connect(remove_cache_storage, &QAction::triggered, [this, program_id, path_str] {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::CacheStorage, path_str);
    });

    connect(dump_romfs, &QAction::triggered, [this, program_id, path_str]() {
        emit DumpRomFSRequested(program_id, path_str, DumpRomFSTarget::Normal);
    });
    connect(dump_romfs_sdmc, &QAction::triggered, [this, program_id, path_str]() {
        emit DumpRomFSRequested(program_id, path_str, DumpRomFSTarget::SDMC);
    });
    connect(verify_integrity, &QAction::triggered,
            [this, path_str]() { emit VerifyIntegrityRequested(path_str); });
    connect(copy_tid, &QAction::triggered,
            [this, program_id]() { emit CopyTIDRequested(program_id); });
    connect(submit_compat_report, &QAction::triggered, [this, program_id, game_name]() {
        const auto reply = QMessageBox::question(
            this, tr("GitHub Account Required"),
            tr("In order to submit a compatibility report, you must have a GitHub account.\n\n"
               "If you do not have one, this feature will not work. Would you like to proceed?"),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
        const QString clean_tid =
            QStringLiteral("%1")
                .arg(static_cast<qulonglong>(program_id), 16, 16, QLatin1Char('0'))
                .toUpper();
        QUrl url(QStringLiteral("https://github.com/citron-neo/Citron-Compatability/issues/new"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("template"), QStringLiteral("compat.yml"));
        query.addQueryItem(QStringLiteral("title"), game_name);
        query.addQueryItem(QStringLiteral("title_id"), clean_tid);
        url.setQuery(query);
        QDesktopServices::openUrl(url);
    });
#if !defined(__APPLE__)
    connect(create_desktop_shortcut, &QAction::triggered, [this, program_id, path_str]() {
        emit CreateShortcut(program_id, path_str, GameListShortcutTarget::Desktop);
    });
    connect(create_applications_menu_shortcut, &QAction::triggered, [this, program_id, path_str]() {
        emit CreateShortcut(program_id, path_str, GameListShortcutTarget::Applications);
    });
#endif
    connect(
        properties, &QAction::triggered, this,
        [this, path_str]() { emit OpenPerGameGeneralRequested(path_str); }, Qt::QueuedConnection);
}

void GameList::AddCustomDirPopup(QMenu& context_menu, QModelIndex selected,
                                 bool show_hidden_action) {
    UISettings::GameDir& game_dir =
        UISettings::values.game_dirs[selected.data(GameListDir::GameDirRole).toInt()];
    if (show_hidden_action) {
        QAction* show_hidden = context_menu.addAction(tr("Show Hidden Games"));
        connect(show_hidden, &QAction::triggered, [this, selected] {
            QStandardItem* folder = item_model->itemFromIndex(selected);
            bool changed = false;
            for (int i = 0; i < folder->rowCount(); ++i) {
                const QString path =
                    folder->child(i)->data(GameListItemPath::FullPathRole).toString();
                if (UISettings::values.hidden_paths.removeOne(path)) {
                    changed = true;
                }
            }
            if (changed) {
                OnTextChanged(search_field->filterText());
                emit SaveConfig();
            }
        });
    }
    context_menu.addSeparator();
    QAction* deep_scan = context_menu.addAction(tr("Scan Subfolders"));
    QAction* delete_dir = context_menu.addAction(tr("Remove Game Directory"));
    deep_scan->setCheckable(true);
    deep_scan->setChecked(game_dir.deep_scan);
    connect(deep_scan, &QAction::triggered, [this, &game_dir] {
        game_dir.deep_scan = !game_dir.deep_scan;
        PopulateAsync(UISettings::values.game_dirs);
    });
    connect(delete_dir, &QAction::triggered, [this, &game_dir, selected] {
        UISettings::values.game_dirs.removeOne(game_dir);
        item_model->invisibleRootItem()->removeRow(selected.row());
        OnTextChanged(search_field->filterText());
    });
}

void GameList::AddPermDirPopup(QMenu& context_menu, QModelIndex selected) {
    const int game_dir_index = selected.data(GameListDir::GameDirRole).toInt();
    QAction* show_hidden = context_menu.addAction(tr("Show Hidden Games"));
    context_menu.addSeparator();
    QAction* move_up = context_menu.addAction(tr("\u25B2 Move Up"));
    QAction* move_down = context_menu.addAction(tr("\u25bc Move Down"));
    QAction* open_directory_location = context_menu.addAction(tr("Open Directory Location"));
    const int row = selected.row();
    move_up->setEnabled(row > 1);
    move_down->setEnabled(row < item_model->rowCount() - 2);
    connect(show_hidden, &QAction::triggered, [this, selected] {
        QStandardItem* folder = item_model->itemFromIndex(selected);
        bool changed = false;
        for (int i = 0; i < folder->rowCount(); ++i) {
            const QString path = folder->child(i)->data(GameListItemPath::FullPathRole).toString();
            if (UISettings::values.hidden_paths.removeOne(path)) {
                changed = true;
            }
        }
        if (changed) {
            OnTextChanged(search_field->filterText());
            emit SaveConfig();
        }
    });
    connect(move_up, &QAction::triggered, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row - 1, 0).data(GameListDir::GameDirRole).toInt();
        std::swap(UISettings::values.game_dirs[game_dir_index],
                  UISettings::values.game_dirs[other_index]);
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row - 1, 0), QVariant(game_dir_index),
                            GameListDir::GameDirRole);
        QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row - 1, item);
        tree_view->setExpanded(selected.sibling(row - 1, 0),
                               UISettings::values.game_dirs[other_index].expanded);
    });
    connect(move_down, &QAction::triggered, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row + 1, 0).data(GameListDir::GameDirRole).toInt();
        std::swap(UISettings::values.game_dirs[game_dir_index],
                  UISettings::values.game_dirs[other_index]);
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row + 1, 0), QVariant(game_dir_index),
                            GameListDir::GameDirRole);
        const QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row + 1, item);
        tree_view->setExpanded(selected.sibling(row + 1, 0),
                               UISettings::values.game_dirs[other_index].expanded);
    });
    connect(open_directory_location, &QAction::triggered, [this, game_dir_index] {
        emit OpenDirectory(
            QString::fromStdString(UISettings::values.game_dirs[game_dir_index].path));
    });
}

void GameList::AddFavoritesPopup(QMenu& context_menu) {
    QAction* clear = context_menu.addAction(tr("Clear"));
    connect(clear, &QAction::triggered, [this] {
        QList<u64> ids_to_remove(UISettings::values.favorited_ids.begin(),
                                 UISettings::values.favorited_ids.end());
        for (const auto id : ids_to_remove) {
            RemoveFavorite(id);
        }
        UISettings::values.favorited_ids.clear();
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
        FilterGridView(search_field->filterText());
    });
}

void GameList::LoadCompatibilityList() {
    // Clear existing entries to allow for a clean refresh
    compatibility_list.clear();

    // Look for a downloaded list in the config directory first
    const auto config_dir =
        QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::ConfigDir));
    const QString local_path = QDir(config_dir).filePath(QStringLiteral("compatibility_list.json"));

    QFile compat_list;
    if (QFile::exists(local_path)) {
        compat_list.setFileName(local_path);
        LOG_INFO(Frontend, "Loading compatibility list from: {}", local_path.toStdString());
    } else {
        // Fallback to the internal baked-in resource
        compat_list.setFileName(QStringLiteral(":compatibility_list/compatibility_list.json"));
        LOG_INFO(Frontend, "No local compatibility list found, using internal resource.");
    }

    if (!compat_list.open(QFile::ReadOnly | QFile::Text)) {
        LOG_ERROR(Frontend, "Unable to open game compatibility list");
        return;
    }

    const QByteArray content = compat_list.readAll();
    if (content.isEmpty()) {
        LOG_ERROR(Frontend, "Game compatibility list is empty or unreadable");
        return;
    }

    const QJsonDocument json = QJsonDocument::fromJson(content);
    const QJsonArray arr = json.array();
    for (const QJsonValue value : arr) {
        const QJsonObject game = value.toObject();
        const QString compatibility_key = QStringLiteral("compatibility");

        // Match the legacy parser logic
        if (!game.contains(compatibility_key))
            continue;

        const int compatibility = game[compatibility_key].toInt();
        const QString directory = game[QStringLiteral("directory")].toString();
        const QJsonArray ids = game[QStringLiteral("releases")].toArray();

        for (const QJsonValue id_ref : ids) {
            const QJsonObject id_object = id_ref.toObject();
            const QString id = id_object[QStringLiteral("id")].toString();
            if (id.isEmpty())
                continue;

            compatibility_list.insert_or_assign(
                id.toUpper().toStdString(),
                std::make_pair(QString::number(compatibility), directory));
        }
    }
    LOG_INFO(Frontend, "Loaded {} compatibility entries.", compatibility_list.size());
}

void GameList::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void GameList::RetranslateUI() {
    item_model->setHeaderData(COLUMN_NAME, Qt::Horizontal, tr("Name"));
    item_model->setHeaderData(COLUMN_COMPATIBILITY, Qt::Horizontal, tr("Compatibility"));
    item_model->setHeaderData(COLUMN_ADD_ONS, Qt::Horizontal, tr("Add-ons"));
    item_model->setHeaderData(COLUMN_FILE_TYPE, Qt::Horizontal, tr("File type"));
    item_model->setHeaderData(COLUMN_SIZE, Qt::Horizontal, tr("Size"));
    item_model->setHeaderData(COLUMN_PLAY_TIME, Qt::Horizontal, tr("Play time"));
    item_model->setHeaderData(COLUMN_ONLINE, Qt::Horizontal, tr("Online"));
}

void GameListSearchField::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void GameListSearchField::RetranslateUI() {
    label_filter->setText(tr("Filter:"));
    edit_filter->setPlaceholderText(tr("Enter pattern to filter"));
}

QStandardItemModel* GameList::GetModel() const {
    return item_model;
}

void GameList::PopulateAsync(QVector<UISettings::GameDir>& game_dirs, bool is_smart_update) {
    if (current_worker) {
        return;
    }

    if (loading_overlay && !is_smart_update) {
        loading_overlay->ShowLoading();
    }
    auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate());
    if (delegate) {
        delegate->SetPopulating(!is_smart_update);
    }

    if (!is_smart_update) {
        item_model->clear();
        item_model->insertColumns(0, COLUMN_COUNT);
        RetranslateUI();
        
        // Ensure flat models for Grid/Carousel are instantiated empty BEFORE async parsing starts 
        // so that games can be visually streamed straight into them during boot.
        FilterGridView(search_field->filterText());
    } else {
        // Targeted metadata updates are now handled by RefreshGame() or worker signals
        // to avoid global UI-wide 'refreshing' indicators that can feel unresponsive.
    }

    // Set columns to interactive sizing and calibrate for 720p displays
    if (tree_view && tree_view->header()) {
        auto* header = tree_view->header();

        // Ensure ALL columns are interactive and have a safe minimum floor
        header->setMinimumSectionSize(80);
        header->setStretchLastSection(true); // Fill the window space on the right

        for (int i = 0; i < COLUMN_COUNT; ++i) {
            header->setSectionResizeMode(i, QHeaderView::Interactive);
        }

        // FORCE widths for 1280x720 resolution (Total: ~1155px + Stretch)
        // We set these every time to ensure the layout remains 'perfect' on each refresh
        header->resizeSection(COLUMN_NAME, 495); // Forced as per requirement
        header->resizeSection(COLUMN_COMPATIBILITY, 110);
        header->resizeSection(COLUMN_ADD_ONS, 190);
        header->resizeSection(COLUMN_FILE_TYPE, 85);
        header->resizeSection(COLUMN_SIZE, 95);
        header->resizeSection(COLUMN_PLAY_TIME, 100);
        header->resizeSection(COLUMN_ONLINE, 80);
    }

    UpdateProgressBarColor();
    if (!is_smart_update) {
        tree_view->setEnabled(false);
        item_model->removeRows(0, item_model->rowCount());
        search_field->clear();
    }
    emit ShowList(true);
    tree_view->setColumnHidden(COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    tree_view->setColumnHidden(COLUMN_COMPATIBILITY, !UISettings::values.show_compat);
    tree_view->setColumnHidden(COLUMN_FILE_TYPE, !UISettings::values.show_types);
    tree_view->setColumnHidden(COLUMN_SIZE, !UISettings::values.show_size);
    tree_view->setColumnHidden(COLUMN_PLAY_TIME, !UISettings::values.show_play_time);
    tree_view->setColumnHidden(COLUMN_ONLINE, !UISettings::values.show_online_column);
    current_worker.reset();

    if (progress_bar) {
        progress_bar->setValue(0);
        progress_bar->setVisible(true);
    }

    current_worker = std::make_unique<GameListWorker>(
        vfs, provider, game_dirs, compatibility_list, play_time_manager, system,
        main_window->GetMultiplayerState()->GetSession());
    connect(current_worker.get(), &GameListWorker::DataAvailable, this, &GameList::WorkerEvent,
            Qt::QueuedConnection);

    if (progress_bar) {
        connect(current_worker.get(), &GameListWorker::ProgressUpdated, progress_bar,
                &QProgressBar::setValue, Qt::QueuedConnection);
    }

    QThreadPool::globalInstance()->start(current_worker.get());
}

void GameList::SaveInterfaceLayout() {
    UISettings::values.gamelist_header_state = tree_view->header()->saveState();
    UISettings::values.game_list_grid_view.SetValue(main_stack->currentIndex() > 0);
}

void GameList::LoadInterfaceLayout() {
    auto* header = tree_view->header();

    // Set modes and minimums first so they are consistent even after restore
    header->setMinimumSectionSize(80);
    header->setStretchLastSection(true);
    for (int i = 0; i < COLUMN_COUNT; ++i) {
        header->setSectionResizeMode(i, QHeaderView::Interactive);
    }

    if (header->restoreState(UISettings::values.gamelist_header_state)) {
        // After restoration, FORCE the name width on boot as requested
        header->resizeSection(COLUMN_NAME, 495);
        return;
    }

    // Default Fallback calibration for 1280x720
    header->resizeSection(COLUMN_NAME, 495);
    header->resizeSection(COLUMN_COMPATIBILITY, 110);
    header->resizeSection(COLUMN_ADD_ONS, 190);
    header->resizeSection(COLUMN_FILE_TYPE, 85);
    header->resizeSection(COLUMN_SIZE, 95);
    header->resizeSection(COLUMN_PLAY_TIME, 100);
    header->resizeSection(COLUMN_ONLINE, 80);
}

const QStringList GameList::supported_file_extensions = {
    QStringLiteral("xci"), QStringLiteral("nsp"), QStringLiteral("nso"), QStringLiteral("nro"),
    QStringLiteral("kip")};
void GameList::RefreshGameDirectory() {
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "Change detected in the games directory. Reloading game list.");
        PopulateAsync(UISettings::values.game_dirs);
    }
}

void GameList::CancelPopulation() {
    if (current_worker) {
        current_worker->Cancel();
    }
    current_worker.reset();
}

void GameList::ToggleFavorite(u64 program_id) {
    if (!UISettings::values.favorited_ids.contains(program_id)) {
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                                !search_field->filterText().isEmpty());
        UISettings::values.favorited_ids.append(program_id);
        AddFavorite(program_id);
        item_model->sort(tree_view->header()->sortIndicatorSection(),
                         tree_view->header()->sortIndicatorOrder());
    } else {
        UISettings::values.favorited_ids.removeOne(program_id);
        RemoveFavorite(program_id);
        if (UISettings::values.favorited_ids.size() == 0) {
            tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
        }
    }
    if (main_stack->currentIndex() > 0) {
        // Preserve filter when updating favorites
        QString filter_text = search_field->filterText();
        FilterGridView(filter_text);
    }
    SaveConfig();
}

void GameList::AddFavorite(u64 program_id) {
    QStandardItem* favorites_row = nullptr;
    for (int i = 0; i < item_model->rowCount(); ++i) {
        auto* folder = item_model->item(i, 0);
        if (folder && folder->data(GameListItem::TypeRole).value<GameListItemType>() ==
                          GameListItemType::Favorites) {
            favorites_row = folder;
            break;
        }
    }
    if (!favorites_row)
        return;

    for (int i = 0; i < item_model->rowCount(); i++) {
        const auto* folder = item_model->item(i);
        for (int j = 0; j < folder->rowCount(); j++) {
            if (folder->child(j)->data(GameListItemPath::ProgramIdRole).toULongLong() ==
                program_id) {
                QList<QStandardItem*> list;
                for (int k = 0; k < COLUMN_COUNT; k++) {
                    list.append(folder->child(j, k)->clone());
                }
                list[0]->setData(folder->child(j)->data(GameListItem::SortRole),
                                 GameListItem::SortRole);
                list[0]->setText(folder->child(j)->data(Qt::DisplayRole).toString());
                favorites_row->appendRow(list);
                return;
            }
        }
    }
}

void GameList::RemoveFavorite(u64 program_id) {
    auto* favorites_row = item_model->item(0);
    for (int i = 0; i < favorites_row->rowCount(); i++) {
        const auto* game = favorites_row->child(i);
        if (game->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
            favorites_row->removeRow(i);
            return;
        }
    }
}

GameListPlaceholder::GameListPlaceholder(GMainWindow* parent) : QWidget{parent} {
    connect(parent, &GMainWindow::UpdateThemedIcons, this,
            &GameListPlaceholder::onUpdateThemedIcons);
    layout = new QVBoxLayout;
    image = new QLabel;
    text = new QLabel;
    layout->setAlignment(Qt::AlignCenter);
    image->setPixmap(QIcon::fromTheme(QStringLiteral("plus_folder")).pixmap(200));
    RetranslateUI();
    QFont font = text->font();
    font.setPointSize(20);
    text->setFont(font);
    text->setAlignment(Qt::AlignHCenter);
    image->setAlignment(Qt::AlignHCenter);
    layout->addWidget(image);
    layout->addWidget(text);
    setLayout(layout);
}

GameListPlaceholder::~GameListPlaceholder() = default;

void GameListPlaceholder::onUpdateThemedIcons() {
    image->setPixmap(QIcon::fromTheme(QStringLiteral("plus_folder")).pixmap(200));
}

void GameListPlaceholder::mouseDoubleClickEvent(QMouseEvent* event) {
    emit GameListPlaceholder::AddDirectory();
}

void GameListPlaceholder::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void GameListPlaceholder::RetranslateUI() {
    text->setText(tr("Double-click to add a new folder to the game list"));
}

// UpdateCarouselSelection removed as it's now handled by GameCarouselView

void GameList::PopulateGridView() {
    FilterGridView(QString());
}

void GameList::SortAlphabetically() {
    if (main_stack->currentIndex() == 0) {
        tree_view->header()->setSortIndicator(COLUMN_NAME, current_sort_order);
        item_model->sort(COLUMN_NAME, current_sort_order);
    } else {
        // Grid/Carousel share the same flat layout which is rebuilt on FilterGridView
        FilterGridView(search_field->filterText());
    }
    UpdateSortButtonIcon();
}

void GameList::ToggleSortOrder() {
    // Toggle between ascending and descending, just like clicking the Name column header
    current_sort_order =
        (current_sort_order == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
    SortAlphabetically();
    UpdateSortButtonIcon(); // Force update immediately
}

void GameList::UpdateSortButtonIcon() {
    if (!btn_sort_az)
        return;

    QIcon sort_icon;
    if (current_sort_order == Qt::DescendingOrder) {
        // Descending (Z-A) - arrow down
        sort_icon = GetThemedIcon(QStringLiteral(":/dist/sort_descending.svg"), true);
    } else {
        // Ascending (A-Z) - arrow up
        sort_icon = GetThemedIcon(QStringLiteral(":/dist/sort_ascending.svg"), true);
    }

    if (sort_icon.isNull()) {
        // Fallback to theme if resources fail
        sort_icon = QIcon::fromTheme(current_sort_order == Qt::AscendingOrder
                                         ? QStringLiteral("view-sort-ascending")
                                         : QStringLiteral("view-sort-descending"));
    }

    btn_sort_az->setIcon(sort_icon);
}

void GameList::UpdateProgressBarColor() {
    if (!progress_bar)
        return;

    // Convert the Hex String from settings to a QColor
    QColor accent(QString::fromStdString(UISettings::values.accent_color.GetValue()));

    if (UISettings::values.enable_rainbow_mode.GetValue()) {
        progress_bar->setStyleSheet(QStringLiteral(
            "QProgressBar { border: none; background: transparent; } "
            "QProgressBar::chunk { "
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
            "stop:0 #ff0000, stop:0.16 #ffff00, stop:0.33 #00ff00, "
            "stop:0.5 #00ffff, stop:0.66 #0000ff, stop:0.83 #ff00ff, stop:1 #ff0000); "
            "}"));
    } else {
        progress_bar->setStyleSheet(
            QStringLiteral("QProgressBar { border: none; background: transparent; } "
                           "QProgressBar::chunk { background-color: %1; }")
                .arg(accent.name()));
    }
}

void GameList::RefreshCompatibilityList() {
    const QUrl url(QStringLiteral("https://raw.githubusercontent.com/citron-neo/"
                                  "Citron-Compatability/refs/heads/main/compatibility_list.json"));

    QNetworkRequest request(url);
    QNetworkReply* reply = network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray json_data = reply->readAll();

            const auto config_dir = QString::fromStdString(
                Common::FS::GetCitronPathString(Common::FS::CitronPath::ConfigDir));
            const QString local_path =
                QDir(config_dir).filePath(QStringLiteral("compatibility_list.json"));

            QFile file(local_path);
            if (file.open(QFile::WriteOnly)) {
                file.write(json_data);
                file.close();
                LOG_INFO(Frontend, "Successfully updated compatibility list from GitHub.");

                LoadCompatibilityList();

                // Refresh the UI by replacing the old compatibility items with new ones
                for (int i = 0; i < item_model->rowCount(); ++i) {
                    QStandardItem* folder = item_model->item(i, 0);
                    if (!folder)
                        continue;
                    for (int j = 0; j < folder->rowCount(); ++j) {
                        QStandardItem* game_item = folder->child(j, 0);
                        if (!game_item ||
                            game_item->data(GameListItem::TypeRole).value<GameListItemType>() !=
                                GameListItemType::Game) {
                            continue;
                        }

                        u64 program_id =
                            game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
                        auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);

                        if (it != compatibility_list.end()) {
                            folder->setChild(j, COLUMN_COMPATIBILITY,
                                             new GameListItemCompat(it->second.first));
                        }
                    }
                }
            }
        } else {
            LOG_ERROR(Frontend, "Failed to download compatibility list: {}",
                      reply->errorString().toStdString());
        }
        reply->deleteLater();
    });
}

void GameList::onSurpriseMeClicked() {
    QVector<SurpriseGame> all_games;

    // Go through the list and gather info for every game (name, icon, path)
    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder || folder->data(GameListItem::TypeRole).value<GameListItemType>() ==
                           GameListItemType::AddDir) {
            continue;
        }

        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, 0);
            if (game_item && game_item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                                 GameListItemType::Game) {
                QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                if (game_title.isEmpty()) {
                    std::string filename;
                    Common::SplitPath(
                        game_item->data(GameListItemPath::FullPathRole).toString().toStdString(),
                        nullptr, &filename, nullptr);
                    game_title = QString::fromStdString(filename);
                }

                QPixmap icon = game_item->data(Qt::DecorationRole).value<QPixmap>();
                if (icon.isNull()) {
                    // Use a generic icon if a game is missing one
                    icon = QIcon::fromTheme(QStringLiteral("application-x-executable"))
                               .pixmap(128, 128);
                }

                if (UISettings::values.hidden_paths.contains(
                        game_item->data(GameListItemPath::FullPathRole).toString())) {
                    continue;
                }

                all_games.append(
                    {game_title, game_item->data(GameListItemPath::FullPathRole).toString(),
                     static_cast<quint64>(
                         game_item->data(GameListItemPath::ProgramIdRole).toULongLong()),
                     icon});
            }
        }
    }

    if (all_games.empty()) {
        QMessageBox::information(this, tr("Surprise Me!"),
                                 tr("No games available to choose from!"));
        return;
    }

    // Create and show animated dialog
    SurpriseMeDialog dialog(all_games, this);
    const int result = dialog.exec();

    // If the user clicked "Launch Game"...
    if (result == QDialog::Accepted) {
        const SurpriseGame choice = dialog.getFinalChoice();
        if (!choice.path.isEmpty()) {
            // ...then launch the game
            emit GameChosen(choice.path, choice.title_id);
        }
    }
    // If the user just closes the window (or clicks the 'X'), nothing happens.
}

void GameList::UpdateAccentColorStyles() {
    QColor accent_color(QString::fromStdString(UISettings::values.accent_color.GetValue()));
    if (!accent_color.isValid()) {
        accent_color = palette().color(QPalette::Highlight);
    }
    const QString color_name = accent_color.name();

    // Create a semi-transparent version of the accent color for the SELECTION background
    QColor selection_background_color = accent_color;
    selection_background_color.setAlphaF(0.25f); // 25% opacity for a clear selection
    const QString selection_background_color_name = QStringLiteral("rgba(%1, %2, %3, %4)")
                                                        .arg(selection_background_color.red())
                                                        .arg(selection_background_color.green())
                                                        .arg(selection_background_color.blue())
                                                        .arg(selection_background_color.alpha());

    // Create a MORE subtle semi-transparent version for the HOVER effect
    QColor hover_background_color = accent_color;
    hover_background_color.setAlphaF(0.15f); // 15% opacity for a subtle hover
    const QString hover_background_color_name = QStringLiteral("rgba(%1, %2, %3, %4)")
                                                    .arg(hover_background_color.red())
                                                    .arg(hover_background_color.green())
                                                    .arg(hover_background_color.blue())
                                                    .arg(hover_background_color.alpha());

    const bool dark = UISettings::IsDarkTheme();

    // Header & Search Colors: Onyx (Dark) vs Silver (Light)
    const QString header_bg = dark ? QStringLiteral("#1c1c1e") : QStringLiteral("#ececf0");
    const QString header_fg = dark ? QStringLiteral("#d0d0e0") : QStringLiteral("#1a1a1e");
    const QString header_border = dark ? QStringLiteral("#2e2e34") : QStringLiteral("#d0d0d5");
    const QString header_bg_hov = dark ? QStringLiteral("#2e2e34") : QStringLiteral("#e0e0e5");
    const QString header_fg_hov = dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString search_bg = dark ? QStringLiteral("#1c1c1e") : QStringLiteral("#fdfdfd");
    const QString search_fg = dark ? QStringLiteral("#f0f0f5") : QStringLiteral("#1a1a1e");

    QString accent_style = QStringLiteral(
                               /* Tree View (List View) Selection & Hover Style */
                               "QTreeView {"
                               "    background-color: transparent;"
                               "    background: transparent;"
                               "    show-decoration-selected: 0;"
                               "    outline: 0;"
                               "    selection-background-color: transparent;"
                               "    selection-color: inherit;"
                               "}"
                               "QTreeView::viewport {"
                               "    background: transparent;"
                               "    background-color: transparent;"
                               "}"
                               "QTreeView::item {"
                               "    padding: 0px;"
                               "    border: none;"
                               "    background: transparent;"
                               "}"
                               "QTreeView::item:hover {"
                               "    background-color: transparent;"
                               "}"
                               "QTreeView::item:selected {"
                               "    background-color: transparent;"
                               "    outline: 0;"
                               "}"
                               "QTreeView::item:selected:!active {"
                               "    background-color: transparent;"
                               "    outline: 0;"
                               "}"

                               /* Modern Header Style */
                               "QHeaderView::section, QHeaderView::section:pressed {"
                               "    background-color: %2;"
                               "    color: %3;"
                               "    border: none;"
                               "    border-bottom: 1px solid %4;"
                               "    border-right: 1px solid %4;"
                               "    padding: 6px 10px;"
                               "    font-weight: bold;"
                               "    font-size: 9pt;"
                               "}"
                               "QHeaderView::section:hover {"
                               "    background-color: %5;"
                               "    color: %6;"
                               "}"

                               /* List View (Grid View) Selection Style */
                               "QListView::item:selected {"
                               "    background-color: palette(light);"
                               "    border: 3px solid %1;"
                               "    border-radius: 12px;"
                               "}"
                               "QListView::item:selected:!active {"
                               "    background-color: transparent;"
                               "    border: 3px solid palette(mid);"
                               "}"

                               /* ScrollBar Styling */
                               "QScrollBar:vertical {"
                               "    border: 1px solid black;"
                               "    background: palette(base);"
                               "    width: 12px;"
                               "    margin: 0px;"
                               "}"
                               "QScrollBar::handle:vertical {"
                               "    background: %1;"
                               "    min-height: 20px;"
                               "    border-radius: 5px;"
                               "    border: 1px solid black;"
                               "}")
                               .arg(color_name)     // %1
                               .arg(header_bg)      // %2
                               .arg(header_fg)      // %3
                               .arg(header_border)  // %4
                               .arg(header_bg_hov)  // %5
                               .arg(header_fg_hov); // %6

    // Apply the combined base styles and new accent styles to each view
    tree_view->setStyleSheet(accent_style);
    grid_view->setStyleSheet(accent_style);
    carousel_view->ApplyTheme();
    if (details_panel) {
        details_panel->ApplyTheme();
    }
    RefreshTooltips();

    // Explicitly style the header with a clean, high-fidelity theme
    tree_view->header()->setStyleSheet(
        QString::fromLatin1("QHeaderView::section { background-color: %1; color: %2; border: none; "
                            "border-bottom: 1px solid %3; padding: 6px 10px; font-weight: bold; }"
                            "QHeaderView::section:hover { background-color: %4; }")
            .arg(header_bg, header_fg, header_border, header_bg_hov));

    // Restore theme-aware button styling
    const QString button_bg = dark ? QStringLiteral("#2b2b2f") : QStringLiteral("#fdfdfd");
    const QString button_border = dark ? QStringLiteral("#3e3e42") : QStringLiteral("#d0d0d5");
    const QString button_hover = dark ? QStringLiteral("#3e3e42") : QStringLiteral("#eeeeef");
    const QString button_fg = dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");

    // Locked Dark Style for top-right buttons: consistently dark grey (#2b2b2f)
    const QString icon_btn_bg = QStringLiteral("#2b2b2f");
    const QString icon_btn_border = QStringLiteral("#3e3e42");
    const QString icon_btn_hover = QStringLiteral("#3e3e42");
    const QString icon_btn_fg = QStringLiteral("#ffffff");

    QString button_base_style = QString::fromLatin1("QToolButton {"
                                                    "  border: 1px solid %1;"
                                                    "  border-radius: 4px;"
                                                    "  background: %2;"
                                                    "  color: %3;"
                                                    "}"
                                                    "QToolButton:hover {"
                                                    "  background: %4;"
                                                    "}")
                                    .arg(button_border, button_bg, button_fg, button_hover);

    QString icon_button_style = QString::fromLatin1("QToolButton {"
                                                    "  border: 1px solid %1;"
                                                    "  border-radius: 4px;"
                                                    "  background: %2;"
                                                    "  color: %3;"
                                                    "}"
                                                    "QToolButton:hover {"
                                                    "  background: %4;"
                                                    "}")
                                    .arg(icon_btn_border, icon_btn_bg, icon_btn_fg, icon_btn_hover);

    QString button_checked_style = QString::fromLatin1("QToolButton:checked {"
                                                       "  background: %1;"
                                                       "  border-color: %1;"
                                                       "  color: #ffffff;"
                                                       "}")
                                       .arg(color_name);

    btn_list_view->setStyleSheet(icon_button_style + button_checked_style);
    btn_grid_view->setStyleSheet(icon_button_style + button_checked_style);
    if (btn_carousel_view)
        btn_carousel_view->setStyleSheet(icon_button_style + button_checked_style);

    // Apply accent style to our new slider toggles so they match the other tool buttons
    if (btn_slider_font_mode) {
        btn_slider_font_mode->setStyleSheet(icon_button_style + button_checked_style);
    }
    if (btn_slider_icon_mode) {
        btn_slider_icon_mode->setStyleSheet(icon_button_style + button_checked_style);
    }

    // Also update Sort, Surprise Me and Controller buttons with the locked dark style
    btn_sort_az->setStyleSheet(icon_button_style);
    btn_surprise_me->setStyleSheet(icon_button_style);
    btn_controller_settings->setStyleSheet(icon_button_style);

    // Update toolbar background: Onyx Grey in Dark Mode, Pure White in Light Mode
    toolbar->setStyleSheet(dark ? QStringLiteral("background: #24242a; border: none;")
                                : QStringLiteral("background: #ffffff; border: none;"));

    // Update main GameList background
    setStyleSheet(dark ? QStringLiteral("background: #161618;")
                       : QStringLiteral("background: #ffffff;"));

    search_field->setStyleSheet(QStringLiteral("QLineEdit {"
                                               "  border: 1px solid palette(mid);"
                                               "  border-radius: 6px;"
                                               "  padding: 4px 8px;"
                                               "  background: %2;"
                                               "  color: %3;"
                                               "}"
                                               "QLineEdit:focus {"
                                               "  border: 1px solid %1;"
                                               "  background: %2;"
                                               "}")
                                    .arg(color_name, search_bg, search_fg));

    // Force light icons for the locked-dark buttons even in Light Mode
    const bool force_light = true;
    btn_list_view->setIcon(GetThemedIcon(QStringLiteral(":/dist/list.svg"), force_light));
    btn_grid_view->setIcon(GetThemedIcon(QStringLiteral(":/dist/grid.svg"), force_light));
    if (btn_carousel_view)
        btn_carousel_view->setIcon(
            GetThemedIcon(QStringLiteral(":/dist/carousel.svg"), force_light));

    UpdateSortButtonIcon();
    btn_surprise_me->setIcon(GetThemedIcon(QStringLiteral(":/dist/dice.svg"), force_light));
    btn_controller_settings->setIcon(
        GetThemedIcon(QStringLiteral(":/dist/controller_navigation.svg"), force_light));

    // Standarize icon size for consistent aesthetic accountability
    const QSize icon_size(20, 20);
    btn_list_view->setIconSize(icon_size);
    btn_grid_view->setIconSize(icon_size);
    if (btn_carousel_view)
        btn_carousel_view->setIconSize(icon_size);
    btn_sort_az->setIconSize(icon_size);
    btn_surprise_me->setIconSize(icon_size);
    btn_controller_settings->setIconSize(icon_size);
}

QIcon GameList::GetThemedIcon(const QString& path, bool force_light) {
    const bool dark = UISettings::IsDarkTheme();
    const QColor icon_color = (dark || force_light) ? QColor(224, 224, 228) : QColor(32, 32, 36);

    // Load at 2x resolution for high-fidelity rendering on all displays
    const QSize base_size(24, 24);
    QPixmap pixmap = QIcon(path).pixmap(base_size * 2);
    if (pixmap.isNull())
        return QIcon(path);

    QPainter painter(&pixmap);

    // Special handling for Surprise Me (Dice) to preserve internal details (dots)
    if (path.contains(QStringLiteral("dice.svg"))) {
        // Use Multiply mode to tint the body while keeping black dots sharp
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    } else {
        // Standard tinting for flat icons
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    }

    painter.fillRect(pixmap.rect(), icon_color);
    painter.end();
    return QIcon(pixmap);
}

void GameList::SaveGameListIndex() {
    QJsonArray root_array;
    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder)
            continue;

        QJsonObject folder_obj;
        folder_obj[QStringLiteral("name")] = folder->text();
        folder_obj[QStringLiteral("type")] = folder->data(GameListItem::TypeRole).toInt();

        QJsonArray games_array;
        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game = folder->child(j, 0);
            if (!game)
                continue;

            QJsonObject game_obj;
            game_obj[QStringLiteral("path")] =
                game->data(GameListItemPath::FullPathRole).toString();
            game_obj[QStringLiteral("title")] = game->data(GameListItemPath::TitleRole).toString();
            game_obj[QStringLiteral("program_id")] =
                QString::number(game->data(GameListItemPath::ProgramIdRole).toULongLong());
            game_obj[QStringLiteral("file_type")] =
                game->data(GameListItemPath::FileTypeRole).toString();
            game_obj[QStringLiteral("original_title")] =
                game->data(GameListItemPath::OriginalTitleRole).toString();

            // Save metadata from other columns
            game_obj[QStringLiteral("compat")] = folder->child(j, COLUMN_COMPATIBILITY)
                                                     ->data(GameListItemCompat::CompatNumberRole)
                                                     .toString();
            game_obj[QStringLiteral("play_time")] =
                QString::number(folder->child(j, COLUMN_PLAY_TIME)
                                    ->data(GameListItemPlayTime::PlayTimeRole)
                                    .toULongLong());
            game_obj[QStringLiteral("size")] =
                folder->child(j, COLUMN_SIZE)->data(GameListItemSize::SizeRole).toLongLong();

            games_array.append(game_obj);
        }
        folder_obj[QStringLiteral("games")] = games_array;
        folder_obj[QStringLiteral("expanded")] = tree_view->isExpanded(folder->index());
        root_array.append(folder_obj);
    }

    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/game_list_metadata.json");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root_array).toJson());
    }
}

void GameList::LoadGameListIndex() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/game_list_metadata.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;

    QJsonArray root_array = doc.array();
    item_model->clear();
    item_model->insertColumns(0, COLUMN_COUNT);

    for (const auto& val : root_array) {
        QJsonObject folder_obj = val.toObject();
        UISettings::GameDir dir_struct;
        dir_struct.path = folder_obj[QStringLiteral("name")].toString().toStdString();

        GameListDir* folder = new GameListDir(
            dir_struct, static_cast<GameListItemType>(folder_obj[QStringLiteral("type")].toInt()));

        QJsonArray games_array = folder_obj[QStringLiteral("games")].toArray();
        for (const auto& game_val : games_array) {
            QJsonObject game_obj = game_val.toObject();

            QString game_path = game_obj[QStringLiteral("path")].toString();
            QString game_title = game_obj[QStringLiteral("title")].toString();
            u64 program_id = game_obj[QStringLiteral("program_id")].toVariant().toULongLong();
            QString file_type = game_obj[QStringLiteral("file_type")].toString();
            QString original_title = game_obj[QStringLiteral("original_title")].toString();

            QList<QStandardItem*> row;
            // Note: We leave icon data empty for the background scan to fill in, providing instant
            // text results.
            row.append(new GameListItemPath(game_path, {}, game_title, original_title, file_type,
                                            program_id));
            row.append(new GameListItemCompat(game_obj[QStringLiteral("compat")].toString()));
            row.append(new QStandardItem()); // Add-ons
            row.append(new QStandardItem(file_type));
            row.append(
                new GameListItemSize(game_obj[QStringLiteral("size")].toVariant().toULongLong()));
            row.append(new GameListItemPlayTime(
                game_obj[QStringLiteral("play_time")].toVariant().toULongLong()));
            row.append(new GameListItemOnline());

            folder->appendRow(row);
        }
        item_model->appendRow(folder);

        // Restore folder expanded state
        if (folder_obj[QStringLiteral("expanded")].toBool(true)) {
            tree_view->expand(folder->index());
        }
    }

    // Sync other view modes (Grid/Carousel)
    if (auto* gm = qobject_cast<QStandardItemModel*>(grid_view->model())) {
        gm->clear();
    }
    if (auto* cm = qobject_cast<QStandardItemModel*>(carousel_view->view()->model())) {
        cm->clear();
    }
    // (Actual synchronization logic would follow the Worker's pattern,
    // but clearing ensures no stale state while background scan runs).
}

void GameList::OnEmulationEnded() {
    m_is_launching = false; // Ensure guard is reset when emulation stops
    auto* effect = new QGraphicsOpacityEffect(fade_overlay);
    fade_overlay->setGraphicsEffect(effect);
    auto* fade_anim = new QPropertyAnimation(effect, "opacity");
    fade_anim->setDuration(300);
    fade_anim->setStartValue(1.0);
    fade_anim->setEndValue(0.0);
    fade_anim->setEasingCurve(QEasingCurve::OutQuad);
    connect(fade_anim, &QPropertyAnimation::finished, fade_overlay, &QWidget::hide);
    fade_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameList::SetViewMode(ViewMode view) {
    UISettings::values.game_list_view_mode.SetValue(static_cast<int>(view));

    if (view == ViewMode::List) {
        main_stack->setCurrentIndex(0);
        AnimateDetailsPanel(false);
        UISettings::values.game_list_grid_view.SetValue(false);
    } else if (view == ViewMode::Grid) {
        main_stack->setCurrentIndex(1);
        AnimateDetailsPanel(true);
        UISettings::values.game_list_grid_view.SetValue(true);
        FilterGridView(search_field->filterText());
    } else if (view == ViewMode::Carousel) {
        main_stack->setCurrentIndex(2);
        AnimateDetailsPanel(true);
        FilterGridView(search_field->filterText());
    }

    if (btn_list_view)
        btn_list_view->setChecked(view == ViewMode::List);
    if (btn_grid_view)
        btn_grid_view->setChecked(view == ViewMode::Grid);
    if (btn_carousel_view)
        btn_carousel_view->setChecked(view == ViewMode::Carousel);

    if (m_is_controller_mode) {
        onControllerFocusChanged(ControllerNavigation::FocusTarget::MainView);
    }

    // Sync Details Panel immediately to the new view's current game
    QModelIndex current;
    if (view == ViewMode::List) {
        current = tree_view->currentIndex();
    } else if (view == ViewMode::Grid) {
        current = grid_view->currentIndex();
    } else if (view == ViewMode::Carousel) {
        current = carousel_view->view()->currentIndex();
    }

    // If no selection exists in the new view, try to find the first game
    if (!current.isValid() && item_model->rowCount() > 0) {
        if (view == ViewMode::List) {
            // Find first game (skipping folders if necessary)
            for (int i = 0; i < item_model->rowCount(); ++i) {
                QModelIndex idx = item_model->index(i, 0);
                if (idx.data(GameListItem::TypeRole).toInt() ==
                    static_cast<int>(GameListItemType::Game)) {
                    current = idx;
                    break;
                }
                if (item_model->hasChildren(idx)) {
                    QModelIndex child = item_model->index(0, 0, idx);
                    if (child.data(GameListItem::TypeRole).toInt() ==
                        static_cast<int>(GameListItemType::Game)) {
                        current = child;
                        break;
                    }
                }
            }
        } else {
            // Grid and Carousel models are flat and only contain games
            current =
                main_stack->currentWidget()->findChild<QAbstractItemView*>()
                    ? main_stack->currentWidget()->findChild<QAbstractItemView*>()->model()->index(
                          0, 0)
                    : QModelIndex();
            // Special case for Carousel which doesn't wrap a standard QAbstractItemView directly
            if (view == ViewMode::Carousel)
                current = carousel_view->view()->currentIndex();
        }
    }

    if (current.isValid()) {
        OnSelectionChanged(current);
    }

    emit SaveConfig();
}

void GameList::ToggleViewMode() {
    ViewMode current = static_cast<ViewMode>(main_stack->currentIndex());
    if (current == ViewMode::List) {
        SetViewMode(ViewMode::Grid);
    } else if (current == ViewMode::Grid) {
        SetViewMode(ViewMode::Carousel);
    } else {
        SetViewMode(ViewMode::List);
    }
}

void GameList::ToggleHidden(const QString& path) {
    if (UISettings::values.hidden_paths.contains(path)) {
        UISettings::values.hidden_paths.removeOne(path);
    } else {
        UISettings::values.hidden_paths.append(path);
    }
    // Refresh the current view to reflect the change
    OnTextChanged(search_field->filterText());
    emit SaveConfig();
}

void GameList::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (fade_overlay) {
        fade_overlay->resize(size());
    }
    if (loading_overlay) {
        loading_overlay->resize(size());
    }
}

void GameList::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
}

void GameList::AnimateDetailsPanel(bool show) {
    if (!details_panel)
        return;

    int start_w = details_panel->width();
    int end_w = 0;
    if (show) {
        end_w = (width() > 1400) ? 380 : 280;
    }

    if (show && !details_panel->isVisible()) {
        details_panel->setFixedWidth(0);
        details_panel->show();
    }

    auto* anim = new QPropertyAnimation(details_panel, "minimumWidth");
    anim->setDuration(250);
    anim->setStartValue(start_w);
    anim->setEndValue(end_w);
    anim->setEasingCurve(show ? QEasingCurve::OutQuint : QEasingCurve::InQuint);

    auto* anim_max = new QPropertyAnimation(details_panel, "maximumWidth");
    anim_max->setDuration(250);
    anim_max->setStartValue(start_w);
    anim_max->setEndValue(end_w);
    anim_max->setEasingCurve(show ? QEasingCurve::OutQuint : QEasingCurve::InQuint);

    if (!show) {
        connect(anim, &QPropertyAnimation::finished, details_panel, &QWidget::hide);
    }

    anim->start(QAbstractAnimation::DeleteWhenStopped);
    anim_max->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameList::onControllerFocusChanged(ControllerNavigation::FocusTarget target) {
    if (target == ControllerNavigation::FocusTarget::MainView) {
        int idx = main_stack->currentIndex();
        if (idx == 0)
            tree_view->setControllerFocus(true);
        else if (idx == 1)
            grid_view->setControllerFocus(true);
        else
            carousel_view->setControllerFocus(true);
        details_panel->setControllerFocus(false);
        // Ensure panel hides in List view if focus returns to list, but stays for Grid/Carousel
        if (main_stack->currentIndex() == 0)
            AnimateDetailsPanel(false);
    } else {
        tree_view->setControllerFocus(false);
        grid_view->setControllerFocus(false);
        carousel_view->setControllerFocus(false);
        details_panel->setControllerFocus(true);

        // CRITICAL: Actually show the panel when focus moves to it!
        if (!details_panel->isVisible() || details_panel->width() < 10) {
            AnimateDetailsPanel(true);
        }
    }
}

void GameList::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();
    bool is_toggle = (key == Qt::Key_R || key == Qt::Key_Minus || key == Qt::Key_Underscore ||
                      key == Qt::Key_Equal || key == Qt::Key_Plus || key == Qt::Key_F11 ||
                      key == Qt::Key_F12 || key == Qt::Key_Semicolon);
    if (is_toggle) {
        if (m_is_controller_mode)
            return; // Deduplicate: controller button handler will handle it
        if (controller_navigation) {
            controller_navigation->toggleFocus();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

bool GameList::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* key_event = static_cast<QKeyEvent*>(event);
        const int key = key_event->key();
        bool is_toggle = (key == Qt::Key_R || key == Qt::Key_Minus || key == Qt::Key_Underscore ||
                          key == Qt::Key_Equal || key == Qt::Key_Plus || key == Qt::Key_F11 ||
                          key == Qt::Key_F12 || key == Qt::Key_Semicolon);
        if (is_toggle) {
            if (m_is_controller_mode)
                return true; // Consume but skip to avoid double-toggle
            if (controller_navigation) {
                controller_navigation->toggleFocus();
            }
            return true; // Consume event
        }
    }
    return QWidget::eventFilter(obj, event);
}

void GameList::mouseMoveEvent(QMouseEvent* event) {
    if (m_is_controller_mode) {
        SwitchToKeyboardMode();
    }
    QWidget::mouseMoveEvent(event);
}

void GameList::mousePressEvent(QMouseEvent* event) {
    if (m_is_controller_mode) {
        SwitchToKeyboardMode();
    }
    QWidget::mousePressEvent(event);
}

void GameListPlaceholder::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

void GameListSearchField::setStyleSheet(const QString& sheet) {
    edit_filter->setStyleSheet(sheet);
}

#include "game_list.moc"
