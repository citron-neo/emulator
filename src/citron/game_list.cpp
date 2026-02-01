// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <regex>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMenu>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMessageBox>
#include <QDirIterator>
#include <QPainter>
#include <QPainterPath>
#include <QProgressDialog>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <random>
#include <QScrollBar>
#include <QStyle>
#include <QThreadPool>
#include <QToolButton>
#include <QUrlQuery>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <random>
#include <vector>
#include <QSizePolicy>
#include <fmt/format.h>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/savedata_factory.h"
#include "citron/compatibility_list.h"
#include "common/fs/path_util.h"
#include "core/hle/service/acc/profile_manager.h"
#include "citron/game_list.h"
#include "citron/game_list_p.h"
#include "citron/game_list_worker.h"
#include "citron/main.h"
#include "citron/uisettings.h"
#include "citron/util/controller_navigation.h"

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
        setFixedHeight(150);
    }

    void setGameReel(const QVector<SurpriseGame>& games) {
        m_games = games;
        update();
    }

    qreal getScrollOffset() const { return m_scroll_offset; }
    void setScrollOffset(qreal offset) {
        m_scroll_offset = offset;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (m_games.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int icon_size = 128;
        const int icon_spacing = 15;
        const int total_slot_width = icon_size + icon_spacing;

        const int widget_center_x = width() / 2;
        const int widget_center_y = height() / 2;

        painter.fillRect(rect(), palette().color(QPalette::Window));
        QColor highlight_color = palette().color(QPalette::Highlight);
        painter.fillRect(widget_center_x - 2, 0, 4, height(), highlight_color);

        for (int i = 0; i < m_games.size(); ++i) {
            const qreal icon_x_position = (widget_center_x - icon_size / 2) + (i * total_slot_width) - m_scroll_offset;
            const int draw_x = static_cast<int>(icon_x_position);
            const int draw_y = widget_center_y - (icon_size / 2);

            if (draw_x + icon_size < 0 || draw_x > width()) {
                continue;
            }

            painter.save();

            QPainterPath path;
            path.addRoundedRect(draw_x, draw_y, icon_size, icon_size, 12, 12);
            painter.setClipPath(path);

            painter.drawPixmap(draw_x, draw_y, icon_size, icon_size, m_games[i].icon);

            painter.restore();
        }
    }

private:
    QVector<SurpriseGame> m_games;
    qreal m_scroll_offset;
};

// This is the main pop-up window that holds the spinning icons, title, and buttons
class SurpriseMeDialog : public QDialog {
    Q_OBJECT

public:
    explicit SurpriseMeDialog(QVector<SurpriseGame> games, QWidget* parent = nullptr)
    : QDialog(parent), m_available_games(games), m_last_choice({QString(), QString(), 0, QPixmap()}) {
        setWindowTitle(tr("Surprise Me!"));
        setModal(true);
        setFixedSize(540, 280);

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(15);
        layout->setContentsMargins(15, 15, 15, 15);

        m_reel_widget = new GameReelWidget(this);
        m_game_title_label = new QLabel(tr("Spinning..."), this);
        m_launch_button = new QPushButton(tr("Launch Game"), this);
        m_reroll_button = new QPushButton(tr("Try Again?"), this);

        m_launch_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_reroll_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_launch_button->setStyleSheet(QStringLiteral("padding: 5px;"));
        m_reroll_button->setStyleSheet(QStringLiteral("padding: 5px;"));
        m_launch_button->setMinimumHeight(35);
        m_reroll_button->setMinimumHeight(35);

        QFont title_font = m_game_title_label->font();
        title_font.setPointSize(16);
        title_font.setBold(true);
        m_game_title_label->setFont(title_font);
        m_game_title_label->setAlignment(Qt::AlignCenter);

        m_game_title_label->setWordWrap(true);

        auto* button_layout = new QHBoxLayout();
        button_layout->addWidget(m_reroll_button);
        button_layout->addWidget(m_launch_button);

        layout->addWidget(m_reel_widget);
        layout->addWidget(m_game_title_label);
        layout->addLayout(button_layout);

        m_launch_button->setEnabled(false);
        m_reroll_button->setEnabled(false);

        m_animation = new QPropertyAnimation(m_reel_widget, "scrollOffset", this);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(m_launch_button, &QPushButton::clicked, this, &SurpriseMeDialog::onLaunch);
        connect(m_reroll_button, &QPushButton::clicked, this, &SurpriseMeDialog::startRoll);

        QTimer::singleShot(100, this, &SurpriseMeDialog::startRoll);
    }

    const SurpriseGame& getFinalChoice() const { return m_last_choice; }

private slots:
    void startRoll() {
        if (m_available_games.isEmpty()) {
            m_game_title_label->setText(tr("No more games to choose!"));
            m_reroll_button->setEnabled(false);
            return;
        }

        m_game_title_label->setText(tr("Spinning..."));
        m_launch_button->setEnabled(false);
        m_reroll_button->setEnabled(false);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> full_distrib(0, m_available_games.size() - 1);
        const int winning_index = full_distrib(gen);

        const SurpriseGame winner = m_available_games.at(winning_index);
        m_available_games.removeAt(winning_index);

        QVector<SurpriseGame> reel;
        if (!m_available_games.isEmpty()) {
            std::uniform_int_distribution<> filler_distrib(0, m_available_games.size() - 1);
            for (int i = 0; i < 20; ++i) reel.push_back(m_available_games.at(filler_distrib(gen)));
            reel.push_back(winner);
            for (int i = 0; i < 20; ++i) reel.push_back(m_available_games.at(filler_distrib(gen)));
        } else {
            reel.push_back(winner);
        }

        m_reel_widget->setGameReel(reel);

        const int icon_size = 128;
        const int icon_spacing = 15;
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
            onRollFinished();
        });

        m_animation->start();
    }

    void onRollFinished() {
        m_game_title_label->setText(m_last_choice.name);
        m_launch_button->setEnabled(true);
        if (!m_available_games.isEmpty()) {
            m_reroll_button->setEnabled(true);
        }
    }

    void onLaunch() { accept(); }

private:
    QVector<SurpriseGame> m_available_games;
    SurpriseGame m_last_choice;
    GameReelWidget* m_reel_widget;
    QLabel* m_game_title_label;
    QPushButton* m_launch_button;
    QPushButton* m_reroll_button;
    QPropertyAnimation* m_animation;
};

// Static helper for Save Detection
static QString GetDetectedEmulatorName(const QString& path, u64 program_id, const QString& citron_nand_base) {
    QString abs_path = QDir(path).absolutePath();
    QString citron_abs_base = QDir(citron_nand_base).absolutePath();
    QString tid_str = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));

    // SELF-EXCLUSION
    if (abs_path.startsWith(citron_abs_base, Qt::CaseInsensitive)) {
        return QString{};
    }

    // Ryujinx
    if (abs_path.contains(QStringLiteral("bis/user/save"), Qt::CaseInsensitive)) {
        if (abs_path.contains(QStringLiteral("ryubing"), Qt::CaseInsensitive)) return QStringLiteral("Ryubing");
        if (abs_path.contains(QStringLiteral("ryujinx"), Qt::CaseInsensitive)) return QStringLiteral("Ryujinx");

        // Fallback if it's a generic Ryujinx-structure folder
        return abs_path.contains(tid_str, Qt::CaseInsensitive) ? QStringLiteral("Ryujinx/Ryubing") : QStringLiteral("Ryujinx/Ryubing (Manual Slot)");
    }

    // Fork
    if (abs_path.contains(QStringLiteral("nand/user/save"), Qt::CaseInsensitive) ||
        abs_path.contains(QStringLiteral("nand/system/Containers"), Qt::CaseInsensitive)) {

        if (abs_path.contains(QStringLiteral("eden"), Qt::CaseInsensitive)) return QStringLiteral("Eden");
        if (abs_path.contains(QStringLiteral("suyu"), Qt::CaseInsensitive)) return QStringLiteral("Suyu");
        if (abs_path.contains(QStringLiteral("sudachi"), Qt::CaseInsensitive)) return QStringLiteral("Sudachi");
        if (abs_path.contains(QStringLiteral("yuzu"), Qt::CaseInsensitive)) return QStringLiteral("Yuzu");

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
    layout_filter->setContentsMargins(8, 8, 8, 8);
    label_filter = new QLabel;
    edit_filter = new QLineEdit;
    edit_filter->clear();
    edit_filter->installEventFilter(key_release_eater);
    edit_filter->setClearButtonEnabled(true);

    connect(edit_filter, &QLineEdit::textChanged, parent, &GameList::OnTextChanged);
    label_filter_result = new QLabel;
    button_filter_close = new QToolButton(this);
    button_filter_close->setText(QStringLiteral("X"));
    button_filter_close->setCursor(Qt::ArrowCursor);
    button_filter_close->setStyleSheet(
        QStringLiteral("QToolButton{ border: 1px solid palette(mid); border-radius: 4px; padding: 4px 8px; color: "
        "palette(text); font-weight: bold; background: palette(button); }"
        "QToolButton:hover{ border: 1px solid palette(highlight); color: "
        "palette(highlighted-text); background: palette(highlight)}"));
    connect(button_filter_close, &QToolButton::clicked, parent, &GameList::OnFilterCloseClicked);
    layout_filter->setSpacing(10);
    layout_filter->addWidget(label_filter);
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
    if (list_view->isVisible()) {
        FilterGridView(edit_filter_text);
    } else {
        FilterTreeView(edit_filter_text);
    }
}

void GameList::FilterGridView(const QString& filter_text) {
    QStandardItemModel* hierarchical_model = item_model;
    QStandardItemModel* flat_model = nullptr;

    QAbstractItemModel* current_model = list_view->model();
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
        if (!folder || folder->data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::AddDir) {
            continue;
        }
        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, 0);
            if (!game_item || game_item->data(GameListItem::TypeRole).value<GameListItemType>() != GameListItemType::Game) continue;

            total_count++;
            const QString full_path = game_item->data(GameListItemPath::FullPathRole).toString();
            bool should_show = !UISettings::values.hidden_paths.contains(full_path);

            if (should_show && !filter_text.isEmpty()) {
                const QString file_title = game_item->data(GameListItemPath::TitleRole).toString().toLower();
                const auto program_id = game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
                const QString file_program_id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));
                const QString file_name = full_path.mid(full_path.lastIndexOf(QLatin1Char{'/'}) + 1).toLower() + QLatin1Char{' '} + file_title;
                should_show = ContainsAllWords(file_name, filter_text) || (file_program_id.size() == 16 && file_program_id.contains(filter_text));
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
    list_view->setModel(flat_model);
    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    list_view->setGridSize(QSize(icon_size + 60, icon_size + 80));
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
                    QPixmap scaled = pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
                    QPixmap scaled = pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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

    tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), filter_text.isEmpty() ? (UISettings::values.favorited_ids.size() == 0) : true);

    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder) continue;

        const QModelIndex folder_index = folder->index();
        for (int j = 0; j < folder->rowCount(); ++j) {
            const QStandardItem* child = folder->child(j, 0);
            if (!child) continue;

            total_count++;
            const QString full_path = child->data(GameListItemPath::FullPathRole).toString();
            bool is_hidden_by_user = UISettings::values.hidden_paths.contains(full_path);
            bool matches_filter = true;

            if (!filter_text.isEmpty()) {
                const auto program_id = child->data(GameListItemPath::ProgramIdRole).toULongLong();
                const QString file_title = child->data(GameListItemPath::TitleRole).toString().toLower();
                const QString file_program_id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0'));
                const QString file_name = full_path.mid(full_path.lastIndexOf(QLatin1Char{'/'}) + 1).toLower() + QLatin1Char{' '} + file_title;
                matches_filter = ContainsAllWords(file_name, filter_text) || (file_program_id.size() == 16 && file_program_id.contains(filter_text));
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
                child->setData(QIcon::fromTheme(QStringLiteral("sd_card")).pixmap(icon_size).scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                break;
            case GameListItemType::UserNandDir:
                child->setData(QIcon::fromTheme(QStringLiteral("chip")).pixmap(icon_size).scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                break;
            case GameListItemType::SysNandDir:
                child->setData(QIcon::fromTheme(QStringLiteral("chip")).pixmap(icon_size).scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                break;
            case GameListItemType::CustomDir: {
                const UISettings::GameDir& game_dir = UISettings::values.game_dirs[child->data(GameListDir::GameDirRole).toInt()];
                const QString icon_name = QFileInfo::exists(QString::fromStdString(game_dir.path)) ? QStringLiteral("folder") : QStringLiteral("bad_folder");
                child->setData(QIcon::fromTheme(icon_name).pixmap(icon_size).scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                break;
            }
            case GameListItemType::AddDir:
                child->setData(QIcon::fromTheme(QStringLiteral("list-add")).pixmap(icon_size).scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                break;
            case GameListItemType::Favorites:
                child->setData(QIcon::fromTheme(QStringLiteral("star")).pixmap(icon_size).scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), Qt::DecorationRole);
                break;
            default:
                break;
        }
    }
}

void GameList::OnFilterCloseClicked() {
    main_window->filterBarSetChecked(false);
}

GameList::GameList(std::shared_ptr<FileSys::VfsFilesystem> vfs_, FileSys::ManualContentProvider* provider_,
                   PlayTime::PlayTimeManager& play_time_manager_, Core::System& system_,
                   GMainWindow* parent)
: QWidget{parent}, vfs{std::move(vfs_)}, provider{provider_},
play_time_manager{play_time_manager_}, system{system_} {
    watcher = new QFileSystemWatcher(this);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &GameList::RefreshGameDirectory);

    this->main_window = parent;
    layout = new QVBoxLayout;
    tree_view = new QTreeView;
    list_view = new QListView;
    controller_navigation = new ControllerNavigation(system.HIDCore(), this);
    search_field = new GameListSearchField(this);
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);
    list_view->setModel(item_model);

    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_view->setStyleSheet(QStringLiteral("QTreeView{ border: none; }"));

    list_view->setViewMode(QListView::IconMode);
    list_view->setResizeMode(QListView::Adjust);
    list_view->setUniformItemSizes(true);
    list_view->setSelectionMode(QAbstractItemView::SingleSelection);
    list_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_view->setContextMenuPolicy(Qt::CustomContextMenu);
    list_view->setStyleSheet(QStringLiteral("QListView{ border: none; background: transparent; } QListView::item { text-align: center; padding: 5px; }"));
    list_view->setGridSize(QSize(140, 160));
    list_view->setSpacing(10);
    list_view->setWordWrap(true);
    list_view->setTextElideMode(Qt::ElideRight);
    list_view->setFlow(QListView::LeftToRight);
    list_view->setWrapping(true);

    item_model->insertColumns(0, COLUMN_COUNT);
    RetranslateUI();

    tree_view->setColumnHidden(COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    tree_view->setColumnHidden(COLUMN_COMPATIBILITY, !UISettings::values.show_compat);
    tree_view->setColumnHidden(COLUMN_PLAY_TIME, !UISettings::values.show_play_time);
    item_model->setSortRole(GameListItemPath::SortRole);

    connect(main_window, &GMainWindow::UpdateThemedIcons, this, &GameList::OnUpdateThemedIcons);
    connect(tree_view, &QTreeView::activated, this, &GameList::ValidateEntry);
    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);
    connect(tree_view, &QTreeView::expanded, this, &GameList::OnItemExpanded);
    connect(tree_view, &QTreeView::collapsed, this, &GameList::OnItemExpanded);
    // Sync sort button with Name column header sort order
    connect(tree_view->header(), &QHeaderView::sortIndicatorChanged, [this](int logicalIndex, Qt::SortOrder order) {
        if (logicalIndex == COLUMN_NAME) {
            current_sort_order = order;
            UpdateSortButtonIcon();
        }
    });
    connect(list_view, &QListView::activated, this, &GameList::ValidateEntry);
    connect(list_view, &QListView::customContextMenuRequested, this, &GameList::PopupContextMenu);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent, [this](Qt::Key key) {
        if (system.IsPoweredOn() || !this->isActiveWindow()) {
            return;
        }
        QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
        if (tree_view->isVisible() && tree_view->model()) {
            QCoreApplication::postEvent(tree_view, event);
        }
        if (list_view->isVisible() && list_view->model()) {
            QKeyEvent* list_event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
            QCoreApplication::postEvent(list_view, list_event);
        }
    });

    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");
    qRegisterMetaType<std::map<u64, std::pair<int, int>>>("std::map<u64, std::pair<int, int>>");

    // Create toolbar
    toolbar = new QWidget(this);
    toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(8, 6, 8, 6);
    toolbar_layout->setSpacing(6);

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
    btn_list_view->setFixedSize(32, 32);
    btn_list_view->setStyleSheet(QStringLiteral(
        "QToolButton {"
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
        "}"
    ));
    connect(btn_list_view, &QToolButton::clicked, [this]() {
        SetViewMode(false);
        btn_list_view->setChecked(true);
        btn_grid_view->setChecked(false);
    });

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
    btn_grid_view->setFixedSize(32, 32);
    btn_grid_view->setStyleSheet(QStringLiteral(
        "QToolButton {"
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
        "}"
    ));
    connect(btn_grid_view, &QToolButton::clicked, [this]() {
        SetViewMode(true);
        btn_list_view->setChecked(false);
        btn_grid_view->setChecked(true);
    });

    // Title/Icon size slider - compact with rounded corners
    slider_title_size = new QSlider(Qt::Horizontal, toolbar);
    slider_title_size->setMinimum(32);
    slider_title_size->setMaximum(256);
    slider_title_size->setValue(static_cast<int>(UISettings::values.game_icon_size.GetValue()));
    slider_title_size->setToolTip(tr("Game Icon Size"));
    slider_title_size->setMaximumWidth(120);
    slider_title_size->setMinimumWidth(120);
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
        "  width: 12px;"
        "  height: 12px;"
        "  margin: -4px 0;"
        "  border-radius: 6px;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "  background: palette(light);"
        "}"
    ));
    connect(slider_title_size, &QSlider::valueChanged, [this](int value) {
    // Update title font size in tree view
    QFont font = tree_view->font();
    font.setPointSize(qBound(8, value / 8, 24));
    tree_view->setFont(font);

#ifndef __linux__
    // On non-Linux platforms, also update game icon size and repaint grid view
    UISettings::values.game_icon_size.SetValue(static_cast<u32>(value));
    if (list_view->isVisible()) {
        QAbstractItemModel* current_model = list_view->model();
        if (current_model && current_model != item_model) {
            QStandardItemModel* flat_model = qobject_cast<QStandardItemModel*>(current_model);
            if (flat_model) {
                const u32 icon_size = static_cast<u32>(value);
                list_view->setGridSize(QSize(icon_size + 60, icon_size + 80));
                int scroll_position = list_view->verticalScrollBar()->value();
                QModelIndex current_index = list_view->currentIndex();

                for (int i = 0; i < flat_model->rowCount(); ++i) {
                    QStandardItem* item = flat_model->item(i);
                    if (item) {
                        u64 program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
                        QStandardItem* original_item = nullptr;
                        for (int folder_idx = 0; folder_idx < item_model->rowCount(); ++folder_idx) {
                            QStandardItem* folder = item_model->item(folder_idx, 0);
                            if (!folder) continue;
                            for (int game_idx = 0; game_idx < folder->rowCount(); ++game_idx) {
                                QStandardItem* game = folder->child(game_idx, 0);
                                if (game && game->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                                    original_item = game;
                                    break;
                                }
                            }
                            if (original_item) break;
                        }

                        if (original_item) {
                            QVariant orig_icon_data = original_item->data(Qt::DecorationRole);
                            if (orig_icon_data.isValid() && orig_icon_data.type() == QVariant::Pixmap) {
                                QPixmap orig_pixmap = orig_icon_data.value<QPixmap>();
                                QPixmap rounded(icon_size, icon_size);
                                rounded.fill(Qt::transparent);
                                QPainter painter(&rounded);
                                painter.setRenderHint(QPainter::Antialiasing);
                                const int radius = icon_size / 8;
                                QPainterPath path;
                                path.addRoundedRect(0, 0, icon_size, icon_size, radius, radius);
                                painter.setClipPath(path);
                                QPixmap scaled = orig_pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                                painter.drawPixmap(0, 0, scaled);
                                item->setData(rounded, Qt::DecorationRole);
                            }
                        }
                    }
                }
                if (scroll_position >= 0) {
                    list_view->verticalScrollBar()->setValue(scroll_position);
                }
                if (current_index.isValid() && current_index.row() < flat_model->rowCount()) {
                    list_view->setCurrentIndex(flat_model->index(current_index.row(), 0));
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
    btn_sort_az->setFixedSize(32, 32);
    btn_sort_az->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 4px;"
        "  background: palette(button);"
        "}"
        "QToolButton:hover {"
        "  background: palette(light);"
        "}"
    ));
    connect(btn_sort_az, &QToolButton::clicked, this, &GameList::ToggleSortOrder);

    // Surprise Me button - positioned after sort button
    btn_surprise_me = new QToolButton(toolbar);
    QIcon surprise_icon(QStringLiteral(":/dist/dice.svg"));
    if (surprise_icon.isNull() || surprise_icon.availableSizes().isEmpty()) {
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
    btn_surprise_me->setFixedSize(32, 32);
    btn_surprise_me->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 4px;"
        "  background: palette(button);"
        "}"
        "QToolButton:hover {"
        "  background: palette(light);"
        "}"
    ));
    connect(btn_surprise_me, &QToolButton::clicked, this, &GameList::onSurpriseMeClicked);

    // Create progress bar
    progress_bar = new QProgressBar(this);
    progress_bar->setVisible(false);
    progress_bar->setFixedHeight(4);
    progress_bar->setTextVisible(false);
    progress_bar->setStyleSheet(QStringLiteral(
        "QProgressBar { border: none; background: transparent; } "
        "QProgressBar::chunk { background-color: #0078d4; }"
    ));

    // Add widgets to toolbar
    toolbar_layout->addWidget(btn_list_view);
    toolbar_layout->addWidget(btn_grid_view);
    toolbar_layout->addWidget(slider_title_size);
    toolbar_layout->addWidget(btn_sort_az);
    toolbar_layout->addWidget(btn_surprise_me);
    toolbar_layout->addStretch(); // Push search to the right
    toolbar_layout->addWidget(search_field);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(progress_bar);
    layout->addWidget(tree_view);
    layout->addWidget(list_view);
    setLayout(layout);

    SetViewMode(UISettings::values.game_list_grid_view.GetValue());

    online_status_timer = new QTimer(this);
    connect(online_status_timer, &QTimer::timeout, this, &GameList::UpdateOnlineStatus);
    online_status_timer->start(5000);

    // Configure the timer for debouncing configuration changes
    config_update_timer.setSingleShot(true);
    connect(&config_update_timer, &QTimer::timeout, this, &GameList::UpdateOnlineStatus);

    // This connection handles live updates when OK/Apply is clicked in the config window.
    connect(main_window, &GMainWindow::ConfigurationSaved, this, &GameList::UpdateAccentColorStyles);

    network_manager = new QNetworkAccessManager(this);

    fade_overlay = new QWidget(this);
    fade_overlay->setStyleSheet(QStringLiteral("background: black;"));
    fade_overlay->hide(); // Start hidden

    connect(main_window, &GMainWindow::EmulationStopping, this, [this]() { OnEmulationEnded(); });

    UpdateAccentColorStyles();
}

void GameList::OnConfigurationChanged() {
    // This function debounces the update requests. Instead of starting a network
    // request immediately, it starts a 500ms timer. If another config change happens,
    // the timer is simply reset. The network request will only happen once, 500ms
    // after the *last* change was made.
    config_update_timer.start(500);
}

void GameList::UnloadController() {
    controller_navigation->UnloadController();
}

GameList::~GameList() {
    UnloadController();
    if (QAbstractItemModel* current_model = list_view->model()) {
        if (current_model != item_model) {
            current_model->deleteLater();
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
    tree_view->setExpanded(entry_items->index(), UISettings::values.game_dirs[entry_items->data(GameListDir::GameDirRole).toInt()].expanded);
}

void GameList::AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent) {
    parent->appendRow(entry_items);
}

void GameList::UpdateOnlineStatus() {
    auto session = main_window->GetMultiplayerState()->GetSession();
    if (!session || !item_model) {
        return;
    }

    // A watcher gets the result back on the main thread safely
    auto online_status_watcher = new QFutureWatcher<std::map<u64, std::pair<int, int>>>(this);
    connect(online_status_watcher, &QFutureWatcher<std::map<u64, std::pair<int, int>>>::finished, this, [this, online_status_watcher]() {
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

void GameList::OnOnlineStatusUpdated(const std::map<u64, std::pair<int, int>>& online_stats) {
    if (!item_model) {
        return;
    }

    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder) continue;

        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, COLUMN_NAME);
            if (!game_item || game_item->data(GameListItem::TypeRole).value<GameListItemType>() != GameListItemType::Game) continue;

            u64 program_id = game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
            QString online_text = QStringLiteral("N/A");

            auto it_stats = online_stats.find(program_id);
            if (it_stats != online_stats.end()) {
                const auto& stats = it_stats->second;
                online_text = QStringLiteral("Players: %1 | Servers: %2").arg(stats.first).arg(stats.second);
            }

            QStandardItem* online_item = folder->child(j, COLUMN_ONLINE);
            if (online_item) {
                if (online_item->data(Qt::DisplayRole).toString() != online_text) {
                    online_item->setData(online_text, Qt::DisplayRole);
                }
            }
        }
    }
}

void GameList::StartLaunchAnimation(const QModelIndex& item) {
    const QString file_path = item.data(GameListItemPath::FullPathRole).toString();
    if (file_path.isEmpty()) return;

    u64 program_id = item.data(GameListItemPath::ProgramIdRole).toULongLong();
    QStandardItem* original_item = nullptr;
    for (int folder_idx = 0; folder_idx < item_model->rowCount(); ++folder_idx) {
        QStandardItem* folder = item_model->item(folder_idx, 0);
        if (!folder) continue;
        for (int game_idx = 0; game_idx < folder->rowCount(); ++game_idx) {
            QStandardItem* game = folder->child(game_idx, 0);
            if (game && game->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                original_item = game;
                break;
            }
        }
        if (original_item) break;
    }

    QPixmap icon;
    if (original_item) {
        icon = original_item->data(Qt::DecorationRole).value<QPixmap>();
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
    if (tree_view->isVisible()) {
        start_geom = tree_view->visualRect(item.sibling(item.row(), 0));
        start_geom.setTopLeft(tree_view->viewport()->mapTo(main_window, start_geom.topLeft()));
    } else {
        start_geom = list_view->visualRect(item);
        start_geom.setTopLeft(list_view->viewport()->mapTo(main_window, start_geom.topLeft()));
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
    auto* effect = new QGraphicsOpacityEffect(animation_label);
    animation_label->setGraphicsEffect(effect);
    auto* fly_anim = new QPropertyAnimation(animation_label, "geometry");
    fly_anim->setDuration(350);
    fly_anim->setStartValue(zoom_end_geom);
    fly_anim->setEndValue(fly_end_geom);
    fly_anim->setEasingCurve(QEasingCurve::InQuad);
    auto* fade_anim = new QPropertyAnimation(effect, "opacity");
    fade_anim->setDuration(350);
    fade_anim->setStartValue(1.0f);
    fade_anim->setEndValue(0.0f);
    fade_anim->setEasingCurve(QEasingCurve::InQuad);
    fly_fade_group->addAnimation(fly_anim);
    fly_fade_group->addAnimation(fade_anim);

    auto* main_group = new QSequentialAnimationGroup(animation_label);
    main_group->addAnimation(zoom_anim);
    main_group->addPause(50);
    main_group->addAnimation(fly_fade_group);

    // When the icon animation finishes, launch the game and clean up.
    // The black overlay will remain until OnEmulationEnded is called.
    connect(main_group, &QSequentialAnimationGroup::finished, this, [this, file_path, title_id, animation_label]() {
        search_field->clear();
        emit GameChosen(file_path, title_id);
        animation_label->deleteLater();
    });

    main_group->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameList::ValidateEntry(const QModelIndex& item) {
    const auto selected = item.sibling(item.row(), 0);
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
        case GameListItemType::Game: {
            const QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
            if (file_path.isEmpty()) return;
            const QFileInfo file_info(file_path);
            if (!file_info.exists()) return;

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
                    tr("Would you like to use the Autoloader to install all Updates/DLC within your game directories?\n\n"
                       "If not now, you can always go to Emulation -> Configure -> Filesystem in order to use this feature. Also, if you have multiple update files for a single game, you can use the Update Manager "
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
        if (!child->hasChildren() && (type == GameListItemType::SdmcDir || type == GameListItemType::UserNandDir || type == GameListItemType::SysNandDir)) {
            item_model->invisibleRootItem()->removeRow(child->row());
            i--;
        }
    }
    return !item_model->invisibleRootItem()->hasChildren();
}

void GameList::DonePopulating(const QStringList& watch_list) {
    if (progress_bar) {
        progress_bar->setVisible(false);
    }
    emit ShowList(!IsEmpty());
    item_model->invisibleRootItem()->appendRow(new GameListAddDir());
    item_model->invisibleRootItem()->insertRow(0, new GameListFavorites());
    tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), UISettings::values.favorited_ids.size() == 0);
    tree_view->setExpanded(item_model->invisibleRootItem()->child(0)->index(), UISettings::values.favorites_expanded.GetValue());
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
    item_model->sort(tree_view->header()->sortIndicatorSection(), tree_view->header()->sortIndicatorOrder());
    if (list_view->isVisible()) {
        // Preserve filter when repopulating
        QString filter_text = search_field->filterText();
        if (!filter_text.isEmpty()) {
            FilterGridView(filter_text);
        } else {
            PopulateGridView();
        }
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
        LOG_INFO(Frontend, "Mirroring: Startup sync skipped (Reason: UI Busy or Game is Emulating).");
    }

    // Automatically refresh compatibility data from GitHub if enabled
    if (UISettings::values.show_compat) {
        RefreshCompatibilityList();
    }

    emit PopulatingCompleted();
}

void GameList::PopupContextMenu(const QPoint& menu_location) {
    QModelIndex item;
    if (tree_view->isVisible()) {
        item = tree_view->indexAt(menu_location);
    } else {
        item = list_view->indexAt(menu_location);
    }
    if (!item.isValid()) return;
    const auto selected = item.sibling(item.row(), 0);
    QMenu context_menu;
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
        case GameListItemType::Game: {
            const u64 program_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();
            const std::string path = selected.data(GameListItemPath::FullPathRole).toString().toStdString();
            const QString game_name = selected.data(GameListItemPath::TitleRole).toString();
            AddGamePopup(context_menu, program_id, path, game_name);
            break;
        }
        case GameListItemType::CustomDir:
            AddPermDirPopup(context_menu, selected);
            AddCustomDirPopup(context_menu, selected);
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
    if (tree_view->isVisible()) {
        context_menu.exec(tree_view->viewport()->mapToGlobal(menu_location));
    } else {
        context_menu.exec(list_view->viewport()->mapToGlobal(menu_location));
    }
}

void GameList::AddGamePopup(QMenu& context_menu, u64 program_id, const std::string& path_str, const QString& game_name) {
    const QString path = QString::fromStdString(path_str);
    const bool is_mirrored = Settings::values.mirrored_save_paths.count(program_id);
    const bool has_custom_path = Settings::values.custom_save_paths.count(program_id);
    QString mirror_base_path;

    QAction* favorite = context_menu.addAction(tr("Favorite"));
    QAction* hide_game = context_menu.addAction(tr("Hide Game"));
    context_menu.addSeparator();
    QAction* start_game = context_menu.addAction(tr("Start Game"));
    QAction* start_game_global = context_menu.addAction(tr("Start Game without Custom Configuration"));
    context_menu.addSeparator();
    QAction* open_save_location = context_menu.addAction(tr("Open Save Data Location"));
    QAction* open_nand_location = context_menu.addAction(tr("Open NAND Location"));
    QAction* set_custom_save_path = context_menu.addAction(tr("Set Custom Save Path"));
    QAction* remove_custom_save_path = context_menu.addAction(tr("Revert to NAND Save Path"));
    QAction* disable_mirroring = context_menu.addAction(tr("Disable Mirroring"));
    QAction* open_mod_location = context_menu.addAction(tr("Open Mod Data Location"));
    QMenu* open_sdmc_mod_menu = context_menu.addMenu(tr("Open SDMC Mod Data Location"));
    QAction* open_current_game_sdmc = open_sdmc_mod_menu->addAction(tr("Open Current Game Location"));
    QAction* open_full_sdmc = open_sdmc_mod_menu->addAction(tr("Open Full Location"));
    QAction* open_transferable_shader_cache = context_menu.addAction(tr("Open Transferable Pipeline Cache"));
    context_menu.addSeparator();
    QMenu* remove_menu = context_menu.addMenu(tr("Remove"));
    QAction* remove_update = remove_menu->addAction(tr("Remove Installed Update"));
    QAction* remove_dlc = remove_menu->addAction(tr("Remove All Installed DLC"));
    QAction* remove_custom_config = remove_menu->addAction(tr("Remove Custom Configuration"));
    QAction* remove_play_time_data = remove_menu->addAction(tr("Remove Play Time Data"));
    QAction* remove_cache_storage = remove_menu->addAction(tr("Remove Cache Storage"));
    QAction* remove_gl_shader_cache = remove_menu->addAction(tr("Remove OpenGL Pipeline Cache"));
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
    QAction* create_applications_menu_shortcut = shortcut_menu->addAction(tr("Add to Applications Menu"));
    #endif
    context_menu.addSeparator();
    QAction* properties = context_menu.addAction(tr("Properties"));

    favorite->setVisible(program_id != 0);
    favorite->setCheckable(true);
    favorite->setChecked(UISettings::values.favorited_ids.contains(program_id));

    hide_game->setVisible(program_id != 0);
    hide_game->setCheckable(true);
    hide_game->setChecked(UISettings::values.hidden_paths.contains(path));
    if (hide_game->isChecked()) {
        hide_game->setText(tr("Unhide Game"));
    }

    open_save_location->setVisible(program_id != 0);
    open_nand_location->setVisible(is_mirrored);
    open_nand_location->setToolTip(tr("Citron uses your NAND while syncing. If you need to make save data modifications, do so in here."));
    set_custom_save_path->setVisible(program_id != 0 && !is_mirrored);
    remove_custom_save_path->setVisible(program_id != 0 && has_custom_path);
    disable_mirroring->setVisible(is_mirrored);
    open_mod_location->setVisible(program_id != 0);
    open_sdmc_mod_menu->menuAction()->setVisible(program_id != 0);
    open_transferable_shader_cache->setVisible(program_id != 0);
    remove_update->setVisible(program_id != 0);
    remove_dlc->setVisible(program_id != 0);
    remove_gl_shader_cache->setVisible(program_id != 0);
    remove_vk_shader_cache->setVisible(program_id != 0);
    remove_shader_cache->setVisible(program_id != 0);
    remove_all_content->setVisible(program_id != 0);

    if (is_mirrored) {
        const bool has_global_path = Settings::values.global_custom_save_path_enabled.GetValue() &&
                                     !Settings::values.global_custom_save_path.GetValue().empty();

        if (has_global_path) {
            open_nand_location->setText(tr("Open Global Save Path Location"));
            open_nand_location->setToolTip(tr("The global save path is being used as the base for save data mirroring."));
            mirror_base_path = QString::fromStdString(Settings::values.global_custom_save_path.GetValue());
        } else {
            open_nand_location->setToolTip(tr("Citron's default NAND is being used as the base for save data mirroring."));
            mirror_base_path = QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir));
        }

        connect(open_nand_location, &QAction::triggered, [this, program_id, mirror_base_path]() {
            const auto user_id = system.GetProfileManager().GetLastOpenedUser().AsU128();
            const std::string relative_save_path = fmt::format("user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, user_id[1], user_id[0], program_id);
            const auto full_save_path = std::filesystem::path(mirror_base_path.toStdString()) / relative_save_path;
            if (!std::filesystem::exists(full_save_path.parent_path())) {
                std::filesystem::create_directories(full_save_path.parent_path());
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(full_save_path.string())));
        });
    }

    submit_compat_report->setToolTip(tr("Requires GitHub account."));

    connect(favorite, &QAction::triggered, [this, program_id]() { ToggleFavorite(program_id); });
    connect(hide_game, &QAction::triggered, [this, path]() { ToggleHidden(path); });
    connect(open_save_location, &QAction::triggered, [this, program_id, path_str]() { emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData, path_str); });

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

    auto copyWithProgress = [calculateTotalSize](const QString& sourceDir, const QString& destDir, QWidget* parent) -> bool {
        QProgressDialog progress(tr("Moving Save Data..."), QString(), 0, 100, parent);
        progress.setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setValue(0);
        qint64 totalSize = calculateTotalSize(sourceDir);
        qint64 copiedSize = 0;
        QDir dir(sourceDir);
        if (!dir.exists()) return false;
        QDir dest_dir(destDir);
        if (!dest_dir.exists()) dest_dir.mkpath(QStringLiteral("."));
        QDirIterator dir_iter(sourceDir, QDirIterator::Subdirectories);
        while (dir_iter.hasNext()) {
            dir_iter.next();
            const QFileInfo file_info = dir_iter.fileInfo();
            const QString relative_path = dir.relativeFilePath(file_info.absoluteFilePath());
            const QString dest_path = QDir(destDir).filePath(relative_path);
            if (file_info.isDir()) {
                dest_dir.mkpath(dest_path);
            } else if (file_info.isFile()) {
                if (QFile::exists(dest_path)) QFile::remove(dest_path);
                if (!QFile::copy(file_info.absoluteFilePath(), dest_path)) return false;
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

    connect(set_custom_save_path, &QAction::triggered, [this, program_id, copyWithProgress]() {
        const QString new_path = QFileDialog::getExistingDirectory(this, tr("Select Custom Save Data Location"));
        if (new_path.isEmpty()) return;
        std::string base_save_path_str;
        if (Settings::values.global_custom_save_path_enabled.GetValue() &&
            !Settings::values.global_custom_save_path.GetValue().empty()) {
            base_save_path_str = Settings::values.global_custom_save_path.GetValue();
        } else {
            base_save_path_str = Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir);
        }
        const QString base_dir = QString::fromStdString(base_save_path_str);
        const auto user_id = system.GetProfileManager().GetLastOpenedUser().AsU128();
        const std::string relative_save_path = fmt::format("user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, user_id[1], user_id[0], program_id);
        const QString internal_save_path = QDir(base_dir).filePath(QString::fromStdString(relative_save_path));
        bool mirroring_enabled = false;
        QString detected_emu = GetDetectedEmulatorName(new_path, program_id, base_dir);
        if (!detected_emu.isEmpty()) {
            QMessageBox::StandardButton mirror_reply = QMessageBox::question(this, tr("Enable Save Mirroring?"),
                tr("Citron has detected a %1 save structure.\n\n"
                   "Would you like to enable 'Intelligent Mirroring'? This will pull the data into Citron's internal save directory "
                   "(currently set to '%2') and keep both locations synced whenever you play. A backup of your existing Citron data "
                   "will be created. BE WARNED: Please do not have both emulators open during this process.").arg(detected_emu, base_dir),
                QMessageBox::Yes | QMessageBox::No);

            if (mirror_reply == QMessageBox::Yes) {
                mirroring_enabled = true;
            }
        }
        QDir internal_dir(internal_save_path);
        if (internal_dir.exists() && !internal_dir.isEmpty()) {
            if (mirroring_enabled) {
                QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss"));
                QString backup_path = internal_save_path + QStringLiteral("_mirror_backup_") + timestamp;
                QDir().mkpath(QFileInfo(backup_path).absolutePath());
                if (QDir().rename(internal_save_path, backup_path)) {
                    LOG_INFO(Frontend, "Safety: Existing internal data moved to backup: {}", backup_path.toStdString());
                }
            } else {
                QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Move Save Data"),
                    tr("You have existing save data in your internal save directory. Would you like to move it to the new custom save path?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
                if (reply == QMessageBox::Cancel) return;
                if (reply == QMessageBox::Yes) {
                    const QString full_dest_path = QDir(new_path).filePath(QString::fromStdString(relative_save_path));
                    if (copyWithProgress(internal_save_path, full_dest_path, this)) {
                        QDir(internal_save_path).removeRecursively();
                        QMessageBox::information(this, tr("Success"), tr("Successfully moved save data to the new location."));
                    } else {
                        QMessageBox::warning(this, tr("Error"), tr("Failed to move save data. Please see the log for more details."));
                    }
                }
            }
        }
        if (mirroring_enabled) {
            if (copyWithProgress(new_path, internal_save_path, this)) {
                Settings::values.mirrored_save_paths.insert_or_assign(program_id, new_path.toStdString());
                Settings::values.custom_save_paths.erase(program_id);
                QMessageBox::information(this, tr("Success"), tr("Mirroring established. Your data has been pulled into the internal Citron save directory."));
            } else {
                QMessageBox::warning(this, tr("Error"), tr("Failed to pull data from the mirror source."));
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
                                  tr("Are you sure you want to disable mirroring for this game?\n\nThe directories will no longer be synced."),
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            Settings::values.mirrored_save_paths.erase(program_id);
            emit SaveConfig();
            QMessageBox::information(this, tr("Mirroring Disabled"),
                                     tr("Mirroring has been disabled for this game. It will now use the save data from the NAND."));
        }
    });
    connect(open_current_game_sdmc, &QAction::triggered, [program_id]() {
        const auto sdmc_path = Common::FS::GetCitronPath(Common::FS::CitronPath::SDMCDir);
        const auto full_path = sdmc_path / "atmosphere" / "contents" / fmt::format("{:016X}", program_id);
        const QString qpath = QString::fromStdString(Common::FS::PathToUTF8String(full_path));
        QDir dir(qpath);
        if (!dir.exists()) dir.mkpath(QStringLiteral("."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
    });
    connect(open_full_sdmc, &QAction::triggered, []() {
        const auto sdmc_path = Common::FS::GetCitronPath(Common::FS::CitronPath::SDMCDir);
        const auto full_path = sdmc_path / "atmosphere" / "contents";
        const QString qpath = QString::fromStdString(Common::FS::PathToUTF8String(full_path));
        QDir dir(qpath);
        if (!dir.exists()) dir.mkpath(QStringLiteral("."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
    });
    connect(start_game, &QAction::triggered, [this, path_str]() { emit BootGame(QString::fromStdString(path_str), StartGameType::Normal); });
    connect(start_game_global, &QAction::triggered, [this, path_str]() { emit BootGame(QString::fromStdString(path_str), StartGameType::Global); });
    connect(open_mod_location, &QAction::triggered, [this, program_id, path_str]() { emit OpenFolderRequested(program_id, GameListOpenTarget::ModData, path_str); });
    connect(open_transferable_shader_cache, &QAction::triggered, [this, program_id]() { emit OpenTransferableShaderCacheRequested(program_id); });
    connect(remove_all_content, &QAction::triggered, [this, program_id]() { emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::Game); });
    connect(remove_update, &QAction::triggered, [this, program_id]() { emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::Update); });
    connect(remove_dlc, &QAction::triggered, [this, program_id]() { emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::AddOnContent); });
    connect(remove_gl_shader_cache, &QAction::triggered, [this, program_id, path_str]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::GlShaderCache, path_str); });
    connect(remove_vk_shader_cache, &QAction::triggered, [this, program_id, path_str]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::VkShaderCache, path_str); });
    connect(remove_shader_cache, &QAction::triggered, [this, program_id, path_str]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::AllShaderCache, path_str); });
    connect(remove_custom_config, &QAction::triggered, [this, program_id, path_str]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::CustomConfiguration, path_str); });
    connect(remove_play_time_data, &QAction::triggered, [this, program_id]() { emit RemovePlayTimeRequested(program_id); });
    connect(remove_cache_storage, &QAction::triggered, [this, program_id, path_str] { emit RemoveFileRequested(program_id, GameListRemoveTarget::CacheStorage, path_str); });
    connect(dump_romfs, &QAction::triggered, [this, program_id, path_str]() { emit DumpRomFSRequested(program_id, path_str, DumpRomFSTarget::Normal); });
    connect(dump_romfs_sdmc, &QAction::triggered, [this, program_id, path_str]() { emit DumpRomFSRequested(program_id, path_str, DumpRomFSTarget::SDMC); });
    connect(verify_integrity, &QAction::triggered, [this, path_str]() { emit VerifyIntegrityRequested(path_str); });
    connect(copy_tid, &QAction::triggered, [this, program_id]() { emit CopyTIDRequested(program_id); });
    connect(submit_compat_report, &QAction::triggered, [this, program_id, game_name]() {
        const auto reply = QMessageBox::question(this, tr("GitHub Account Required"),
            tr("In order to submit a compatibility report, you must have a GitHub account.\n\n"
               "If you do not have one, this feature will not work. Would you like to proceed?"),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
        const QString clean_tid = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper();
        QUrl url(QStringLiteral("https://github.com/CollectingW/Citron-Compatability/issues/new"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("template"), QStringLiteral("compat.yml"));
        query.addQueryItem(QStringLiteral("title"), game_name);
        query.addQueryItem(QStringLiteral("title_id"), clean_tid);
        url.setQuery(query);
        QDesktopServices::openUrl(url);
    });
    #if !defined(__APPLE__)
    connect(create_desktop_shortcut, &QAction::triggered, [this, program_id, path_str]() { emit CreateShortcut(program_id, path_str, GameListShortcutTarget::Desktop); });
    connect(create_applications_menu_shortcut, &QAction::triggered, [this, program_id, path_str]() { emit CreateShortcut(program_id, path_str, GameListShortcutTarget::Applications); });
    #endif
    connect(properties, &QAction::triggered, [this, path_str]() { emit OpenPerGameGeneralRequested(path_str); });
}

void GameList::AddCustomDirPopup(QMenu& context_menu, QModelIndex selected) {
    UISettings::GameDir& game_dir = UISettings::values.game_dirs[selected.data(GameListDir::GameDirRole).toInt()];
    QAction* show_hidden = context_menu.addAction(tr("Show Hidden Games"));
    context_menu.addSeparator();
    QAction* deep_scan = context_menu.addAction(tr("Scan Subfolders"));
    QAction* delete_dir = context_menu.addAction(tr("Remove Game Directory"));
    deep_scan->setCheckable(true);
    deep_scan->setChecked(game_dir.deep_scan);
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
        std::swap(UISettings::values.game_dirs[game_dir_index], UISettings::values.game_dirs[other_index]);
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row - 1, 0), QVariant(game_dir_index), GameListDir::GameDirRole);
        QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row - 1, item);
        tree_view->setExpanded(selected.sibling(row - 1, 0), UISettings::values.game_dirs[other_index].expanded);
    });
    connect(move_down, &QAction::triggered, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row + 1, 0).data(GameListDir::GameDirRole).toInt();
        std::swap(UISettings::values.game_dirs[game_dir_index], UISettings::values.game_dirs[other_index]);
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row + 1, 0), QVariant(game_dir_index), GameListDir::GameDirRole);
        const QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row + 1, item);
        tree_view->setExpanded(selected.sibling(row + 1, 0), UISettings::values.game_dirs[other_index].expanded);
    });
    connect(open_directory_location, &QAction::triggered, [this, game_dir_index] { emit OpenDirectory(QString::fromStdString(UISettings::values.game_dirs[game_dir_index].path)); });
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
    const auto config_dir = QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::ConfigDir));
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
        if (!game.contains(compatibility_key)) continue;

        const int compatibility = game[compatibility_key].toInt();
        const QString directory = game[QStringLiteral("directory")].toString();
        const QJsonArray ids = game[QStringLiteral("releases")].toArray();

        for (const QJsonValue id_ref : ids) {
            const QJsonObject id_object = id_ref.toObject();
            const QString id = id_object[QStringLiteral("id")].toString();
            if (id.isEmpty()) continue;

            compatibility_list.insert_or_assign(id.toUpper().toStdString(),
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

    current_worker = std::make_unique<GameListWorker>(vfs, provider, game_dirs, compatibility_list, play_time_manager, system, main_window->GetMultiplayerState()->GetSession());
    connect(current_worker.get(), &GameListWorker::DataAvailable, this, &GameList::WorkerEvent, Qt::QueuedConnection);

    if (progress_bar) {
        connect(current_worker.get(), &GameListWorker::ProgressUpdated, progress_bar, &QProgressBar::setValue, Qt::QueuedConnection);
    }

    QThreadPool::globalInstance()->start(current_worker.get());
}

void GameList::SaveInterfaceLayout() {
    UISettings::values.gamelist_header_state = tree_view->header()->saveState();
    UISettings::values.game_list_grid_view.SetValue(list_view->isVisible());
}

void GameList::LoadInterfaceLayout() {
    auto* header = tree_view->header();
    if (header->restoreState(UISettings::values.gamelist_header_state)) {
        return;
    }
    header->resizeSection(COLUMN_NAME, header->width());
}

const QStringList GameList::supported_file_extensions = {
    QStringLiteral("xci"), QStringLiteral("nsp"),
    QStringLiteral("nso"), QStringLiteral("nro"), QStringLiteral("kip")
};

void GameList::RefreshGameDirectory() {
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "Change detected in the games directory. Reloading game list.");
        PopulateAsync(UISettings::values.game_dirs);
    }
}

    void GameList::ToggleFavorite(u64 program_id) {
        if (!UISettings::values.favorited_ids.contains(program_id)) {
            tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), !search_field->filterText().isEmpty());
            UISettings::values.favorited_ids.append(program_id);
            AddFavorite(program_id);
            item_model->sort(tree_view->header()->sortIndicatorSection(), tree_view->header()->sortIndicatorOrder());
        } else {
            UISettings::values.favorited_ids.removeOne(program_id);
            RemoveFavorite(program_id);
            if (UISettings::values.favorited_ids.size() == 0) {
                tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
            }
        }
        if (list_view->isVisible()) {
            // Preserve filter when updating favorites
            QString filter_text = search_field->filterText();
            if (!filter_text.isEmpty()) {
                FilterGridView(filter_text);
            } else {
                PopulateGridView();
            }
        }
        SaveConfig();
    }

    void GameList::AddFavorite(u64 program_id) {
        auto* favorites_row = item_model->item(0);
        for (int i = 1; i < item_model->rowCount() - 1; i++) {
            const auto* folder = item_model->item(i);
            for (int j = 0; j < folder->rowCount(); j++) {
                if (folder->child(j)->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                    QList<QStandardItem*> list;
                    for (int k = 0; k < COLUMN_COUNT; k++) {
                        list.append(folder->child(j, k)->clone());
                    }
                    list[0]->setData(folder->child(j)->data(GameListItem::SortRole), GameListItem::SortRole);
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
        connect(parent, &GMainWindow::UpdateThemedIcons, this, &GameListPlaceholder::onUpdateThemedIcons);
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

    void GameList::SetViewMode(bool grid_view) {
        if (grid_view) {
            // Check if there's an active filter - if so, use FilterGridView instead
            QString filter_text = search_field->filterText();
            if (!filter_text.isEmpty()) {
                FilterGridView(filter_text);
            } else {
                PopulateGridView();
            }
            tree_view->setVisible(false);
            list_view->setVisible(true);
            if (list_view->model() && list_view->model()->rowCount() > 0) {
                list_view->setCurrentIndex(list_view->model()->index(0, 0));
            }
        } else {
            list_view->setVisible(false);
            tree_view->setVisible(true);
            if (item_model && item_model->rowCount() > 0) {
                tree_view->setCurrentIndex(item_model->index(0, 0));
            }
        }
        // Update button states
        if (btn_list_view && btn_grid_view) {
            btn_list_view->setChecked(!grid_view);
            btn_grid_view->setChecked(grid_view);
        }
    }

    void GameList::PopulateGridView() {
        QStandardItemModel* hierarchical_model = item_model;
        if (QAbstractItemModel* old_model = list_view->model()) {
            if (old_model != item_model) {
                old_model->deleteLater();
            }
        }
        QStandardItemModel* flat_model = new QStandardItemModel(this);
        flat_model->setSortRole(GameListItemPath::SortRole);
        for (int i = 0; i < hierarchical_model->rowCount(); ++i) {
            QStandardItem* folder = hierarchical_model->item(i, 0);
            if (!folder) continue;
            const auto folder_type = folder->data(GameListItem::TypeRole).value<GameListItemType>();
            if (folder_type == GameListItemType::AddDir) {
                continue;
            }
            for (int j = 0; j < folder->rowCount(); ++j) {
                QStandardItem* game_item = folder->child(j, 0);
                if (!game_item) continue;
                const auto game_type = game_item->data(GameListItem::TypeRole).value<GameListItemType>();
                if (game_type == GameListItemType::Game) {
                    QStandardItem* cloned_item = game_item->clone();
                    QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                    if (game_title.isEmpty()) {
                        std::string filename;
                        Common::SplitPath(game_item->data(GameListItemPath::FullPathRole).toString().toStdString(), nullptr, &filename, nullptr);
                        game_title = QString::fromStdString(filename);
                    }
                    cloned_item->setText(game_title);
                    flat_model->appendRow(cloned_item);
                }
            }
        }
        list_view->setModel(flat_model);
        const u32 icon_size = UISettings::values.game_icon_size.GetValue();
        list_view->setGridSize(QSize(icon_size + 60, icon_size + 80));
        // Sort the grid view using current sort order
        flat_model->sort(0, current_sort_order);
        // Update icon sizes in the model - ensure all icons are consistently sized with rounded corners
        for (int i = 0; i < flat_model->rowCount(); ++i) {
            QStandardItem* item = flat_model->item(i);
            if (item) {
                QVariant icon_data = item->data(Qt::DecorationRole);
                if (icon_data.isValid() && icon_data.canConvert<QPixmap>()) {
                    QPixmap pixmap = icon_data.value<QPixmap>();
                    if (!pixmap.isNull()) {
                        #ifdef __linux__
                        // On Linux, use simple scaling to avoid QPainter bugs
                        QPixmap scaled = pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        item->setData(scaled, Qt::DecorationRole);
                        #else
                        // On other platforms, use the QPainter method for rounded corners
                        QPixmap rounded(icon_size, icon_size);
                        rounded.fill(Qt::transparent);

                        QPainter painter(&rounded);
                        painter.setRenderHint(QPainter::Antialiasing);

                        const int radius = icon_size / 8;
                        QPainterPath path;
                        path.addRoundedRect(0, 0, icon_size, icon_size, radius, radius);
                        painter.setClipPath(path);

                        QPixmap scaled = pixmap.scaled(icon_size, icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        painter.drawPixmap(0, 0, scaled);

                        item->setData(rounded, Qt::DecorationRole);
                        #endif
                    }
                }
            }
        }
    }

    void GameList::ToggleViewMode() {
        bool current_grid_view = UISettings::values.game_list_grid_view.GetValue();
        UISettings::values.game_list_grid_view.SetValue(!current_grid_view);
        SetViewMode(!current_grid_view);
        // Button states are updated in SetViewMode
    }

    void GameList::SortAlphabetically() {
        if (tree_view->isVisible()) {
            // Sort tree view by name column using current sort order
            tree_view->header()->setSortIndicator(COLUMN_NAME, current_sort_order);
            item_model->sort(COLUMN_NAME, current_sort_order);
        } else if (list_view->isVisible()) {
            // Sort grid view alphabetically using current sort order
            QAbstractItemModel* current_model = list_view->model();
            if (current_model && current_model != item_model) {
                // Sort the flat model used by list view (filtered or unfiltered)
                QStandardItemModel* flat_model = qobject_cast<QStandardItemModel*>(current_model);
                if (flat_model) {
                    // Use SortRole for proper alphabetical sorting
                    flat_model->setSortRole(GameListItemPath::SortRole);
                    flat_model->sort(0, current_sort_order);
                }
            } else {
                // If using item_model directly, repopulate grid view to apply sort
                // Preserve filter if active
                QString filter_text = search_field->filterText();
                if (!filter_text.isEmpty()) {
                    FilterGridView(filter_text);
                } else {
                    PopulateGridView();
                }
            }
        }
        UpdateSortButtonIcon();
    }

    void GameList::ToggleSortOrder() {
        // Toggle between ascending and descending, just like clicking the Name column header
        current_sort_order = (current_sort_order == Qt::AscendingOrder)
                           ? Qt::DescendingOrder
                           : Qt::AscendingOrder;
        SortAlphabetically();
    }

    void GameList::UpdateSortButtonIcon() {
        if (!btn_sort_az) return;

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
    if (!progress_bar) return;

    // Convert the Hex String from settings to a QColor
    QColor accent(QString::fromStdString(UISettings::values.accent_color.GetValue()));

    if (UISettings::values.enable_rainbow_mode.GetValue()) {
        progress_bar->setStyleSheet(QStringLiteral(
            "QProgressBar { border: none; background: transparent; } "
            "QProgressBar::chunk { "
            "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
            "stop:0 #ff0000, stop:0.16 #ffff00, stop:0.33 #00ff00, "
            "stop:0.5 #00ffff, stop:0.66 #0000ff, stop:0.83 #ff00ff, stop:1 #ff0000); "
            "}"
        ));
    } else {
        progress_bar->setStyleSheet(QStringLiteral(
            "QProgressBar { border: none; background: transparent; } "
            "QProgressBar::chunk { background-color: %1; }"
        ).arg(accent.name()));
    }
}

void GameList::RefreshCompatibilityList() {
    const QUrl url(QStringLiteral("https://raw.githubusercontent.com/CollectingW/Citron-Compatability/refs/heads/main/compatibility_list.json"));

    QNetworkRequest request(url);
    QNetworkReply* reply = network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray json_data = reply->readAll();

            const auto config_dir = QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::ConfigDir));
            const QString local_path = QDir(config_dir).filePath(QStringLiteral("compatibility_list.json"));

            QFile file(local_path);
            if (file.open(QFile::WriteOnly)) {
                file.write(json_data);
                file.close();
                LOG_INFO(Frontend, "Successfully updated compatibility list from GitHub.");

                LoadCompatibilityList();

                // Refresh the UI by replacing the old compatibility items with new ones
                for (int i = 0; i < item_model->rowCount(); ++i) {
                    QStandardItem* folder = item_model->item(i, 0);
                    if (!folder) continue;
                    for (int j = 0; j < folder->rowCount(); ++j) {
                        QStandardItem* game_item = folder->child(j, 0);
                        if (!game_item || game_item->data(GameListItem::TypeRole).value<GameListItemType>() != GameListItemType::Game) {
                            continue;
                        }

                        u64 program_id = game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
                        auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);

                        if (it != compatibility_list.end()) {
                            folder->setChild(j, COLUMN_COMPATIBILITY, new GameListItemCompat(it->second.first));
                        }
                    }
                }
            }
        } else {
            LOG_ERROR(Frontend, "Failed to download compatibility list: {}", reply->errorString().toStdString());
        }
        reply->deleteLater();
    });
}

void GameList::onSurpriseMeClicked() {
    QVector<SurpriseGame> all_games;

    // Go through the list and gather info for every game (name, icon, path)
    for (int i = 0; i < item_model->rowCount(); ++i) {
        QStandardItem* folder = item_model->item(i, 0);
        if (!folder || folder->data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::AddDir) {
            continue;
        }

        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, 0);
            if (game_item && game_item->data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::Game) {
                QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                if (game_title.isEmpty()) {
                    std::string filename;
                    Common::SplitPath(game_item->data(GameListItemPath::FullPathRole).toString().toStdString(), nullptr, &filename, nullptr);
                    game_title = QString::fromStdString(filename);
                }

                QPixmap icon = game_item->data(Qt::DecorationRole).value<QPixmap>();
                if (icon.isNull()) {
                    // Use a generic icon if a game is missing one
                    icon = QIcon::fromTheme(QStringLiteral("application-x-executable")).pixmap(128, 128);
                }

                all_games.append({
                    game_title,
                    game_item->data(GameListItemPath::FullPathRole).toString(),
                    static_cast<quint64>(game_item->data(GameListItemPath::ProgramIdRole).toULongLong()),
                    icon
                });
            }
        }
    }

    if (all_games.empty()) {
        QMessageBox::information(this, tr("Surprise Me!"), tr("No games available to choose from!"));
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

    QString accent_style = QStringLiteral(
        /* Tree View (List View) Selection & Hover Style */
        "QTreeView::item:hover {"
        "    background-color: %3;"                 /* Use the new accent-based hover color */
        "    border-radius: 4px;"                   /* Add a subtle rounding to the hover effect */
        "}"
        "QTreeView::item:selected {"
        "    background-color: %2;"                 /* Use the accent-based selection color */
        "    color: palette(text);"
        "    border: none;"                         /* NO BORDER */
        "    border-radius: 4px;"                   /* Keep rounding consistent */
        "}"
        "QTreeView::item:selected:!active {"
        "    background-color: palette(light);"     /* Use a muted color when window is not focused */
        "    border: none;"                         /* NO BORDER */
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
        "}"
    ).arg(color_name, selection_background_color_name, hover_background_color_name);

    // Apply the combined base styles and new accent styles to each view
    tree_view->setStyleSheet(QStringLiteral("QTreeView{ border: none; }") + accent_style);
    list_view->setStyleSheet(QStringLiteral("QListView{ border: none; background: transparent; } QListView::item { text-align: center; padding: 5px; }") + accent_style);

    // Update the toolbar buttons as before
    QString button_base_style = QStringLiteral(
        "QToolButton {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 4px;"
        "  background: palette(button);"
        "}"
        "QToolButton:hover {"
        "  background: palette(light);"
        "}"
    );
    QString button_checked_style = QStringLiteral(
        "QToolButton:checked {"
        "  background: %1;"
        "  border-color: %1;"
        "}"
    ).arg(color_name);

    btn_list_view->setStyleSheet(button_base_style + button_checked_style);
    btn_grid_view->setStyleSheet(button_base_style + button_checked_style);

    search_field->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  background: palette(base);"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid %1;"
        "  background: palette(base);"
        "}"
    ).arg(color_name));
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
    // Ensure the overlay always perfectly covers the game list widget
    fade_overlay->setGeometry(rect());
}

void GameListPlaceholder::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
}

void GameList::OnEmulationEnded() {
    // This function is called when the emulator returns to the game list.
    // We now fade the black overlay back out.
    auto* effect = new QGraphicsOpacityEffect(fade_overlay);
    fade_overlay->setGraphicsEffect(effect);

    auto* fade_out_anim = new QPropertyAnimation(effect, "opacity");
    fade_out_anim->setDuration(300);
    fade_out_anim->setStartValue(1.0f);
    fade_out_anim->setEndValue(0.0f);
    fade_out_anim->setEasingCurve(QEasingCurve::OutQuad);

    // When the fade-out is complete, hide the overlay widget
    connect(fade_out_anim, &QPropertyAnimation::finished, this, [this]() {
        fade_overlay->hide();
    });

    fade_out_anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameListSearchField::setStyleSheet(const QString& sheet) {
    edit_filter->setStyleSheet(sheet);
}

#include "game_list.moc"
