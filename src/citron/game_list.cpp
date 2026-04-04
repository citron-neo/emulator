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
#include <QStyle>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
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
        painter.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DashLine));
        painter.drawLine(0, widget_center_y, width(), widget_center_y);

        // Sleek small centering markers at top and bottom
        painter.setPen(QPen(QColor(0, 150, 255), 3));
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
        const int logoSize = 400;
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
        setFixedSize(850, 640);

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(15);
        layout->setContentsMargins(15, 15, 15, 15);

        // Navigation Bar
        auto* nav_layout = new QHBoxLayout();
        auto* reel_btn = new QPushButton(tr("Reel"), this);
        auto* cards_btn = new QPushButton(tr("Cards"), this);
        auto* plinko_btn = new QPushButton(tr("Plinko"), this);
        auto* blackjack_btn = new QPushButton(tr("Blackjack"), this);

        QString nav_style = QStringLiteral(
            "QPushButton { background: #333; color: #aaa; border: none; padding: 5px 15px; "
            "border-radius: 4px; }"
            "QPushButton:checked { background: #555; color: white; font-weight: bold; }");
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

        m_launch_button->setFixedHeight(35);
        m_reroll_button->setFixedHeight(35);

        QFont title_font = m_game_title_label->font();
        title_font.setPointSize(16);
        title_font.setBold(true);
        m_game_title_label->setFont(title_font);
        m_game_title_label->setAlignment(Qt::AlignCenter);

        auto* button_layout = new QHBoxLayout();
        button_layout->addStretch();
        button_layout->addWidget(m_reroll_button);
        button_layout->addWidget(m_launch_button);

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
    QStandardItemModel* hierarchical_model = item_model;
    QStandardItemModel* flat_model = nullptr;

    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    QAbstractItemModel* current_model = grid_view->view()->model();
    if (current_model && current_model != item_model) {
        QStandardItemModel* existing_flat = qobject_cast<QStandardItemModel*>(current_model);
        if (existing_flat) {
            existing_flat->clear();
            flat_model = existing_flat;
        }
    }

    if (!flat_model) {
        if (current_model && current_model != item_model) {
            current_model->deleteLater();
        }
        flat_model = new QStandardItemModel(this);
    }
    int visible_count = 0;
    int total_count = 0;
    for (int i = 0; i < hierarchical_model->rowCount(); ++i) {
        QStandardItem* folder = hierarchical_model->item(i, 0);
        if (!folder || folder->data(GameListItem::TypeRole).value<GameListItemType>() ==
                           GameListItemType::AddDir) {
            continue;
        }
        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, 0);
            if (!game_item || game_item->data(GameListItem::TypeRole).value<GameListItemType>() !=
                                  GameListItemType::Game)
                continue;

            total_count++;
            const QString full_path = game_item->data(GameListItemPath::FullPathRole).toString();
            bool should_show = !UISettings::values.hidden_paths.contains(full_path);

            if (should_show && !filter_text.isEmpty()) {
                const QString file_title =
                    game_item->data(GameListItemPath::TitleRole).toString().toLower();
                const auto program_id =
                    game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
                const QString file_program_id =
                    QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));
                const QString file_name =
                    full_path.mid(full_path.lastIndexOf(QLatin1Char{'/'}) + 1).toLower() +
                    QLatin1Char{' '} + file_title;
                should_show =
                    ContainsAllWords(file_name, filter_text) ||
                    (file_program_id.size() == 16 && file_program_id.contains(filter_text));
            }

            if (should_show) {
                QStandardItem* cloned_item = game_item->clone();
                QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                if (game_title.isEmpty()) {
                    std::string filename;
                    Common::SplitPath(full_path.toStdString(), nullptr, &filename, nullptr);
                    game_title = QString::fromStdString(filename);
                }
                cloned_item->setText(game_title);
                flat_model->appendRow(cloned_item);
                visible_count++;
            }
        }
    }
    grid_view->setModel(flat_model);
    carousel_view->setModel(flat_model);
    flat_model->setSortRole(GameListItemPath::SortRole);
    flat_model->sort(0, current_sort_order);

    for (int i = 0; i < flat_model->rowCount(); ++i) {
        QStandardItem* item = flat_model->item(i);
        if (item) {
            QVariant icon_data = item->data(Qt::DecorationRole);
            if (icon_data.isValid() && icon_data.canConvert<QPixmap>()) {
                QPixmap pixmap = icon_data.value<QPixmap>();
                if (!pixmap.isNull()) {
#ifdef __linux__
                    QPixmap scaled = pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
                    item->setData(scaled, Qt::DecorationRole);
#else
                    QPixmap rounded(icon_size, icon_size);
                    rounded.fill(Qt::transparent);
                    QPainter painter(&rounded);
                    painter.setRenderHint(QPainter::Antialiasing);
                    const int radius = icon_size / 8;
                    QPainterPath path;
                    path.addRoundedRect(0, 0, icon_size, icon_size, radius, radius);
                    painter.setClipPath(path);
                    QPixmap scaled = pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
                    painter.drawPixmap(0, 0, scaled);
                    item->setData(rounded, Qt::DecorationRole);
#endif
                }
            }
        }
    }
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
        default:
            break;
        }
    }
}

void GameList::OnFilterCloseClicked() {
    main_window->filterBarSetChecked(false);
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
    auto handleSelection = [this](const QModelIndex& index) {
        if (details_panel->isVisible()) {
            details_panel->updateDetails(index);
        }
    };

    connect(tree_view, &GameTreeView::itemSelectionChanged, this, handleSelection);
    connect(grid_view, &GameGridView::itemSelectionChanged, this, handleSelection);
    connect(carousel_view, &GameCarouselView::itemSelectionChanged, this, handleSelection);

    connect(tree_view, &GameTreeView::itemActivated, this, &GameList::ValidateEntry);
    connect(grid_view, &GameGridView::itemActivated, this, &GameList::ValidateEntry);
    connect(carousel_view, &GameCarouselView::itemActivated, this, &GameList::ValidateEntry);

    tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
    grid_view->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    carousel_view->view()->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);
    connect(grid_view->view(), &QListView::customContextMenuRequested, this,
            &GameList::PopupContextMenu);
    connect(carousel_view->view(), &QWidget::customContextMenuRequested, this,
            &GameList::PopupContextMenu);

    item_model->insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

    item_model->insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

    item_model->setSortRole(GameListItemPath::SortRole);

    // Apply modernized delegate
    tree_view->setItemDelegate(new GameListDelegate(tree_view, this));
    tree_view->setIndentation(0);
    tree_view->setRootIsDecorated(true);

    // Always default to List View on launch as requested
    SetViewMode(ViewMode::List);

    connect(main_window, &GMainWindow::UpdateThemedIcons, this, &GameList::OnUpdateThemedIcons);
    connect(tree_view, &QTreeView::activated, this, &GameList::ValidateEntry);
    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);
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
    QIcon list_icon = QIcon::fromTheme(QStringLiteral("view-list-details"));
    if (list_icon.isNull()) {
        list_icon = QIcon::fromTheme(QStringLiteral("view-list"));
    }
    if (list_icon.isNull()) {
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
                                                "  border: 1px solid palette(mid);"
                                                "  border-radius: 4px;"
                                                "  background: palette(button);"
                                                "}"
                                                "QToolButton:hover {"
                                                "  background: palette(light);"
                                                "}"
                                                "QToolButton:checked {"
                                                "  background: palette(highlight);"
                                                "  border-color: palette(highlight);"
                                                "}"));
    connect(btn_list_view, &QToolButton::clicked,
            [this]() { SetViewMode(GameList::ViewMode::List); });

    // Grid view button - icon-only with rounded corners
    btn_grid_view = new QToolButton(toolbar);
    QIcon grid_icon = QIcon::fromTheme(QStringLiteral("view-grid"));
    if (grid_icon.isNull()) {
        grid_icon = QIcon::fromTheme(QStringLiteral("view-grid-details"));
    }
    if (grid_icon.isNull()) {
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
                                                "  border: 1px solid palette(mid);"
                                                "  border-radius: 4px;"
                                                "  background: palette(button);"
                                                "}"
                                                "QToolButton:hover {"
                                                "  background: palette(light);"
                                                "}"
                                                "QToolButton:checked {"
                                                "  background: palette(highlight);"
                                                "  border-color: palette(highlight);"
                                                "}"));
    connect(btn_grid_view, &QToolButton::clicked,
            [this]() { SetViewMode(GameList::ViewMode::Grid); });

    btn_carousel_view = new QToolButton(toolbar);
    QIcon carousel_icon = QIcon::fromTheme(QStringLiteral("view-presentation"));
    if (carousel_icon.isNull())
        carousel_icon = QIcon::fromTheme(QStringLiteral("media-playlist-play"));
    btn_carousel_view->setIcon(carousel_icon);
    btn_carousel_view->setToolTip(tr("Carousel View"));
    btn_carousel_view->setCheckable(true);
    btn_carousel_view->setAutoRaise(true);
    btn_carousel_view->setIconSize(QSize(16, 16));
    btn_carousel_view->setFixedSize(26, 26);
    btn_carousel_view->setStyleSheet(QStringLiteral("QToolButton {"
                                                    "  border: 1px solid palette(mid);"
                                                    "  border-radius: 4px;"
                                                    "  background: palette(button);"
                                                    "}"
                                                    "QToolButton:hover {"
                                                    "  background: palette(light);"
                                                    "}"
                                                    "QToolButton:checked {"
                                                    "  background: palette(highlight);"
                                                    "  border-color: palette(highlight);"
                                                    "}"));
    connect(btn_carousel_view, &QToolButton::clicked,
            [this]() { SetViewMode(GameList::ViewMode::Carousel); });

    // Title/Icon size slider - compact with rounded corners
    slider_title_size = new QSlider(Qt::Horizontal, toolbar);
    slider_title_size->setMinimum(32);
    slider_title_size->setMaximum(256);
    slider_title_size->setValue(static_cast<int>(UISettings::values.game_icon_size.GetValue()));
    slider_title_size->setToolTip(tr("Game Icon Size"));
    slider_title_size->setMaximumWidth(120);
    slider_title_size->setMinimumWidth(120);
    slider_title_size->setStyleSheet(QStringLiteral("QSlider::groove:horizontal {"
                                                    "  border: 1px solid palette(mid);"
                                                    "  height: 4px;"
                                                    "  background: palette(base);"
                                                    "  border-radius: 2px;"
                                                    "}"
                                                    "QSlider::handle:horizontal {"
                                                    "  background: palette(button);"
                                                    "  border: 1px solid palette(mid);"
                                                    "  width: 12px;"
                                                    "  height: 12px;"
                                                    "  margin: -4px 0;"
                                                    "  border-radius: 6px;"
                                                    "}"
                                                    "QSlider::handle:horizontal:hover {"
                                                    "  background: palette(light);"
                                                    "}"));
    connect(slider_title_size, &QSlider::valueChanged, [this](int value) {
        // Update title font size in tree view
        QFont font = tree_view->font();
        font.setPointSize(qBound(8, value / 8, 24));
        tree_view->setFont(font);
        tree_view->doItemsLayout(); // Force redraw and size recalculation

#ifndef __linux__
        // On non-Linux platforms, also update game icon size and repaint grid view
        UISettings::values.game_icon_size.SetValue(static_cast<u32>(value));
        if (grid_view->isVisible()) {
            QAbstractItemModel* current_model = grid_view->model();
            if (current_model && current_model != item_model) {
                QStandardItemModel* flat_model = qobject_cast<QStandardItemModel*>(current_model);
                if (flat_model) {
                    const u32 icon_size = static_cast<u32>(value);
                    grid_view->view()->setGridSize(QSize(icon_size + 60, icon_size + 80));
                    int scroll_position = grid_view->view()->verticalScrollBar()->value();
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
                                for (int game_idx = 0; game_idx < folder->rowCount(); ++game_idx) {
                                    QStandardItem* game = folder->child(game_idx, 0);
                                    if (game &&
                                        game->data(GameListItemPath::ProgramIdRole).toULongLong() ==
                                            program_id) {
                                        original_item = game;
                                        break;
                                    }
                                }
                                if (original_item)
                                    break;
                            }

                            if (original_item) {
                                QVariant orig_icon_data = original_item->data(Qt::DecorationRole);
                                if (orig_icon_data.isValid() &&
                                    orig_icon_data.type() == QVariant::Pixmap) {
                                    QPixmap orig_pixmap = orig_icon_data.value<QPixmap>();
                                    QPixmap rounded(icon_size, icon_size);
                                    rounded.fill(Qt::transparent);
                                    QPainter painter(&rounded);
                                    painter.setRenderHint(QPainter::Antialiasing);
                                    const int radius = icon_size / 8;
                                    QPainterPath path;
                                    path.addRoundedRect(0, 0, icon_size, icon_size, radius, radius);
                                    painter.setClipPath(path);
                                    QPixmap scaled = orig_pixmap.scaled(icon_size, icon_size,
                                                                        Qt::IgnoreAspectRatio,
                                                                        Qt::SmoothTransformation);
                                    painter.drawPixmap(0, 0, scaled);
                                    item->setData(rounded, Qt::DecorationRole);
                                }
                            }
                        }
                    }
                    if (scroll_position >= 0) {
                        grid_view->view()->verticalScrollBar()->setValue(scroll_position);
                    }
                    if (current_index.isValid() && current_index.row() < flat_model->rowCount()) {
                        grid_view->setCurrentIndex(flat_model->index(current_index.row(), 0));
                    }
                }
            } else {
                PopulateGridView();
            }
        }
#endif
    });

    // A-Z sort button - positioned after slider
    btn_sort_az = new QToolButton(toolbar);
    UpdateSortButtonIcon();
    btn_sort_az->setToolTip(tr("Sort by Name"));
    btn_sort_az->setAutoRaise(true);
    btn_sort_az->setIconSize(QSize(16, 16));
    btn_sort_az->setFixedSize(26, 26);
    btn_sort_az->setStyleSheet(QStringLiteral("QToolButton {"
                                              "  border: 1px solid palette(mid);"
                                              "  border-radius: 4px;"
                                              "  background: palette(button);"
                                              "}"
                                              "QToolButton:hover {"
                                              "  background: palette(light);"
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
                                                  "  border: 1px solid palette(mid);"
                                                  "  border-radius: 4px;"
                                                  "  background: palette(button);"
                                                  "}"
                                                  "QToolButton:hover {"
                                                  "  background: palette(light);"
                                                  "}"));
    connect(btn_surprise_me, &QToolButton::clicked, this, &GameList::onSurpriseMeClicked);

    // Create progress bar
    progress_bar = new QProgressBar(this);
    progress_bar->setVisible(false);
    progress_bar->setFixedHeight(4);
    progress_bar->setTextVisible(false);
    progress_bar->setStyleSheet(
        QStringLiteral("QProgressBar { border: none; background: transparent; } "
                       "QProgressBar::chunk { background-color: #0078d4; }"));

    // Add widgets to toolbar
    toolbar->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    toolbar_layout->addWidget(btn_list_view);
    toolbar_layout->addWidget(btn_grid_view);
    toolbar_layout->addWidget(btn_carousel_view);
    toolbar_layout->addWidget(slider_title_size);
    toolbar_layout->addWidget(btn_sort_az);
    toolbar_layout->addWidget(btn_surprise_me);

    // Controller Settings Button
    auto* btn_controller_settings = new QToolButton(toolbar);
    btn_controller_settings->setIcon(QIcon::fromTheme(QStringLiteral("input-gaming")));
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

    setStyleSheet(QStringLiteral("background: #161618;"));
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

                    if (current.isValid()) {
                        StartLaunchAnimation(current);
                    } else {
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
    item_model->invisibleRootItem()->appendRow(entry_items);
    tree_view->setExpanded(
        entry_items->index(),
        UISettings::values.game_dirs[entry_items->data(GameListDir::GameDirRole).toInt()].expanded);
}

void GameList::AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent) {
    parent->appendRow(entry_items);

    // Register the new index for bubble animation immediately
    auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate());
    if (delegate) {
        delegate->RegisterEntryAnimation(entry_items.first()->index());
    }

    // Auto-scroll to show new games as they appear
    if (loading_overlay && loading_overlay->isVisible() && tree_view) {
        tree_view->scrollTo(entry_items.first()->index(), QAbstractItemView::PositionAtBottom);
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

void GameList::StartLaunchAnimation(const QModelIndex& item) {
    const QString file_path = item.data(GameListItemPath::FullPathRole).toString();
    if (file_path.isEmpty())
        return;

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

    // When the animation finishes, launch the game and clean up.
    connect(main_group, &QSequentialAnimationGroup::finished, this,
            [this, file_path, title_id, animation_label, logo_widget]() {
                search_field->clear();
                emit GameChosen(file_path, title_id);
                animation_label->deleteLater();
                logo_widget->deleteLater();
            });

    main_group->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameList::ValidateEntry(const QModelIndex& item) {
    const auto selected = item.sibling(item.row(), 0);
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game: {
        const QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
        if (file_path.isEmpty())
            return;
        const QFileInfo file_info(file_path);
        if (!file_info.exists())
            return;

        // If the entry is a directory (e.g., for homebrew), launch it directly without animation.
        if (file_info.isDir()) {
            const QDir dir{file_path};
            const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
            if (matching_main.size() == 1) {
                emit GameChosen(dir.path() + QDir::separator() + matching_main[0]);
            }
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
    item_model->invisibleRootItem()->appendRow(new GameListAddDir());
    item_model->invisibleRootItem()->insertRow(0, new GameListFavorites());
    tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                            UISettings::values.favorited_ids.size() == 0);
    tree_view->setExpanded(item_model->invisibleRootItem()->child(0)->index(),
                           UISettings::values.favorites_expanded.GetValue());
    for (const auto id : UISettings::values.favorited_ids) {
        AddFavorite(id);
    }
    auto watch_dirs = watcher->directories();
    if (!watch_dirs.isEmpty()) {
        watcher->removePaths(watch_dirs);
    }
    constexpr int LIMIT_WATCH_DIRECTORIES = 5000;
    constexpr int SLICE_SIZE = 25;
    int len = std::min(static_cast<int>(watch_list.size()), LIMIT_WATCH_DIRECTORIES);
    for (int i = 0; i < len; i += SLICE_SIZE) {
        watcher->addPaths(watch_list.mid(i, i + SLICE_SIZE));
        QCoreApplication::processEvents();
    }
    tree_view->setEnabled(true);
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
        item = grid_view->indexAt(menu_location);
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
    context_menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #24242a; border: 1px solid #32323a; border-radius: 8px; padding: 6px; color: #ffffff; }"
        "QMenu::item { padding: 6px 30px; border-radius: 4px; margin: 2px; color: #ffffff; }"
        "QMenu::item:selected { background-color: #32323a; border: 1px solid #42424a; }"
        "QMenu::separator { height: 1px; background: #32323a; margin: 4px 10px; }"
    ));
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game: {
        const u64 program_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();
        const std::string path =
            selected.data(GameListItemPath::FullPathRole).toString().toStdString();
        const QString game_name = selected.data(GameListItemPath::TitleRole).toString();
        AddGamePopup(context_menu, program_id, path, game_name);
        break;
    }
    case GameListItemType::CustomDir:
        AddPermDirPopup(context_menu, selected);
        AddCustomDirPopup(context_menu, selected,
                          false); // Pass false to skip adding "Show Hidden Games"
        break;
    case GameListItemType::SdmcDir:
    case GameListItemType::UserNandDir:
    case GameListItemType::SysNandDir:
        AddPermDirPopup(context_menu, selected);
        break;
    case GameListItemType::Favorites:
        AddFavoritesPopup(context_menu);
        break;
    default:
        break;
    }

    if (current == 0) {
        context_menu.exec(tree_view->viewport()->mapToGlobal(menu_location));
    } else if (current == 1) {
        context_menu.exec(grid_view->view()->viewport()->mapToGlobal(menu_location));
    } else {
        context_menu.exec(carousel_view->view()->viewport()->mapToGlobal(menu_location));
    }
}

void GameList::AddGamePopup(QMenu& context_menu, u64 program_id, const std::string& path_str,
                            const QString& game_name) {
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

    connect(edit_metadata, &QAction::triggered, [this, program_id, game_name] {
        const u64 current_play_time = play_time_manager.GetPlayTime(program_id);
        CustomMetadataDialog dialog(this, program_id, game_name.toStdString(), current_play_time);
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
            if (main_window) {
                main_window->RefreshGameList();
            }
        }
    });

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
                "user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, user_id[1], user_id[0], program_id);
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
            "user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, user_id[1], user_id[0], program_id);
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
        const auto full_path =
            sdmc_path / "atmosphere" / "contents" / fmt::format("{:016X}", program_id);
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
    connect(start_game, &QAction::triggered, [this, path_str]() {
        emit BootGame(QString::fromStdString(path_str), StartGameType::Normal);
    });
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
            QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper();
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
    connect(properties, &QAction::triggered,
            [this, path_str]() { emit OpenPerGameGeneralRequested(path_str); });
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
        for (const auto id : UISettings::values.favorited_ids) {
            RemoveFavorite(id);
        }
        UISettings::values.favorited_ids.clear();
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
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

void GameList::PopulateAsync(QVector<UISettings::GameDir>& game_dirs) {
    if (current_worker) {
        return;
    }

    if (loading_overlay) {
        loading_overlay->ShowLoading();
    }
    auto* delegate = qobject_cast<GameListDelegate*>(tree_view->itemDelegate());
    if (delegate) {
        delegate->SetPopulating(true);
    }

    item_model->clear();
    item_model->insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

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
    tree_view->setEnabled(false);
    emit ShowList(true);
    tree_view->setColumnHidden(COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    tree_view->setColumnHidden(COLUMN_COMPATIBILITY, !UISettings::values.show_compat);
    tree_view->setColumnHidden(COLUMN_FILE_TYPE, !UISettings::values.show_types);
    tree_view->setColumnHidden(COLUMN_SIZE, !UISettings::values.show_size);
    tree_view->setColumnHidden(COLUMN_PLAY_TIME, !UISettings::values.show_play_time);
    tree_view->setColumnHidden(COLUMN_ONLINE, !UISettings::values.show_online_column);
    current_worker.reset();
    item_model->removeRows(0, item_model->rowCount());
    search_field->clear();

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
    auto* favorites_row = item_model->item(0);
    for (int i = 1; i < item_model->rowCount() - 1; i++) {
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
}

void GameList::UpdateSortButtonIcon() {
    if (!btn_sort_az)
        return;

    QIcon sort_icon;
    if (current_sort_order == Qt::AscendingOrder) {
        // Ascending (A-Z) - arrow up
        sort_icon = QIcon::fromTheme(QStringLiteral("view-sort-ascending"));
        if (sort_icon.isNull()) {
            sort_icon = QIcon::fromTheme(QStringLiteral("sort-ascending"));
        }
        if (sort_icon.isNull()) {
            sort_icon = style()->standardIcon(QStyle::SP_ArrowUp);
        }
    } else {
        // Descending (Z-A) - arrow down
        sort_icon = QIcon::fromTheme(QStringLiteral("view-sort-descending"));
        if (sort_icon.isNull()) {
            sort_icon = QIcon::fromTheme(QStringLiteral("sort-descending"));
        }
        if (sort_icon.isNull()) {
            sort_icon = style()->standardIcon(QStyle::SP_ArrowDown);
        }
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
    const QString header_bg = dark ? QStringLiteral("#24242a") : QStringLiteral("#dddde2");
    const QString header_fg = dark ? QStringLiteral("#9898a4") : QStringLiteral("#444450");
    const QString header_border = dark ? QStringLiteral("#32323a") : QStringLiteral("#c8c8d0");
    const QString header_bg_hov = dark ? QStringLiteral("#2e2e34") : QStringLiteral("#cdcdd4");
    const QString header_fg_hov = dark ? QStringLiteral("#d0d0e0") : QStringLiteral("#222230");

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

    // Update the toolbar buttons as before
    QString button_base_style = QStringLiteral("QToolButton {"
                                               "  border: 1px solid palette(mid);"
                                               "  border-radius: 4px;"
                                               "  background: palette(button);"
                                               "}"
                                               "QToolButton:hover {"
                                               "  background: palette(light);"
                                               "}");
    QString button_checked_style = QStringLiteral("QToolButton:checked {"
                                                  "  background: %1;"
                                                  "  border-color: %1;"
                                                  "}")
                                       .arg(color_name);

    btn_list_view->setStyleSheet(button_base_style + button_checked_style);
    btn_grid_view->setStyleSheet(button_base_style + button_checked_style);
    if (btn_carousel_view)
        btn_carousel_view->setStyleSheet(button_base_style + button_checked_style);

    search_field->setStyleSheet(QStringLiteral("QLineEdit {"
                                               "  border: 1px solid palette(mid);"
                                               "  border-radius: 6px;"
                                               "  padding: 4px 8px;"
                                               "  background: palette(base);"
                                               "}"
                                               "QLineEdit:focus {"
                                               "  border: 1px solid %1;"
                                               "  background: palette(base);"
                                               "}")
                                    .arg(color_name));
}

void GameList::OnEmulationEnded() {
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
        if (main_stack->currentIndex() == 0) AnimateDetailsPanel(false);
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
