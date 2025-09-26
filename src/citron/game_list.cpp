// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <regex>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMenu>
#include <QThreadPool>
#include <QToolButton>
#include <fmt/format.h>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "citron/compatibility_list.h"
#include "citron/game_list.h"
#include "citron/game_list_p.h"
#include "citron/game_list_worker.h"
#include "citron/main.h"
#include "citron/uisettings.h"
#include "citron/util/controller_navigation.h"

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
        // Escape: Resets the searchfield
        case Qt::Key_Escape: {
            if (edit_filter_text_old.isEmpty()) {
                return QObject::eventFilter(obj, event);
            } else {
                gamelist->search_field->edit_filter->clear();
                edit_filter_text.clear();
            }
            break;
        }
        // Return and Enter
        // If the enter key gets pressed first checks how many and which entry is visible
        // If there is only one result launch this game
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            if (gamelist->search_field->visible == 1) {
                const QString file_path = gamelist->GetLastFilterResultItem();

                // To avoid loading error dialog loops while confirming them using enter
                // Also users usually want to run a different game after closing one
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
        QStringLiteral("QToolButton{ border: none; padding: 0px; color: "
                       "#000000; font-weight: bold; background: #F0F0F0; }"
                       "QToolButton:hover{ border: none; padding: 0px; color: "
                       "#EEEEEE; font-weight: bold; background: #E81123}"));
    connect(button_filter_close, &QToolButton::clicked, parent, &GameList::OnFilterCloseClicked);
    layout_filter->setSpacing(10);
    layout_filter->addWidget(label_filter);
    layout_filter->addWidget(edit_filter);
    layout_filter->addWidget(label_filter_result);
    layout_filter->addWidget(button_filter_close);
    setLayout(layout_filter);
    RetranslateUI();
}

/**
 * Checks if all words separated by spaces are contained in another string
 * This offers a word order insensitive search function
 *
 * @param haystack String that gets checked if it contains all words of the userinput string
 * @param userinput String containing all words getting checked
 * @return true if the haystack contains all words of userinput
 */
static bool ContainsAllWords(const QString& haystack, const QString& userinput) {
    const QStringList userinput_split = userinput.split(QLatin1Char{' '}, Qt::SkipEmptyParts);

    return std::all_of(userinput_split.begin(), userinput_split.end(),
                       [&haystack](const QString& s) { return haystack.contains(s); });
}

// Syncs the expanded state of Game Directories with settings to persist across sessions
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

// Event in order to filter the gamelist after editing the searchfield
void GameList::OnTextChanged(const QString& new_text) {
    QString edit_filter_text = new_text.toLower();

    if (list_view->isVisible()) {
        // Grid view filtering - simpler approach
        FilterGridView(edit_filter_text);
    } else {
        // Tree view filtering - existing logic
        FilterTreeView(edit_filter_text);
    }
}

void GameList::FilterGridView(const QString& filter_text) {
    // Repopulate the grid view with filtered items
    QStandardItemModel* hierarchical_model = item_model;

    // Delete the previous flat model if it exists to prevent memory leaks
    if (QAbstractItemModel* old_model = list_view->model()) {
        if (old_model != item_model) {
            old_model->deleteLater();
        }
    }

    // Create a new flat model for grid view
    QStandardItemModel* flat_model = new QStandardItemModel(this);

    int visible_count = 0;
    int total_count = 0;

    // Collect all games from the hierarchical model
    for (int i = 0; i < hierarchical_model->rowCount(); ++i) {
        QStandardItem* folder = hierarchical_model->item(i, 0);
        if (!folder) continue;

        // Skip non-game folders in grid view, but include favorites
        const auto folder_type = folder->data(GameListItem::TypeRole).value<GameListItemType>();
        if (folder_type == GameListItemType::AddDir) {
            continue;
        }

        // Add games from this folder to the flat model
        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, 0);
            if (!game_item) continue;

            const auto game_type = game_item->data(GameListItem::TypeRole).value<GameListItemType>();
            if (game_type == GameListItemType::Game) {
                total_count++;

                bool should_show = true;

                if (!filter_text.isEmpty()) {
                    const QString file_path = game_item->data(GameListItemPath::FullPathRole).toString().toLower();
                    const QString file_title = game_item->data(GameListItemPath::TitleRole).toString().toLower();
                    const auto program_id = game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
                    const QString file_program_id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char{'0'});

                    const QString file_name = file_path.mid(file_path.lastIndexOf(QLatin1Char{'/'}) + 1) + QLatin1Char{' '} + file_title;

                    should_show = ContainsAllWords(file_name, filter_text) ||
                                 (file_program_id.size() == 16 && file_program_id.contains(filter_text));
                }

                if (should_show) {
                    // Clone the game item for the flat model
                    QStandardItem* cloned_item = game_item->clone();

                    // Set display text to just the game title for grid view
                    QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                    if (game_title.isEmpty()) {
                        // Fallback to filename if no title
                        std::string filename;
                        Common::SplitPath(game_item->data(GameListItemPath::FullPathRole).toString().toStdString(),
                                        nullptr, &filename, nullptr);
                        game_title = QString::fromStdString(filename);
                    }
                    cloned_item->setText(game_title);

                    flat_model->appendRow(cloned_item);
                    visible_count++;
                }
            }
        }
    }

    // Set the flat model for the list view
    list_view->setModel(flat_model);

    // Update grid size based on icon size
    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    list_view->setGridSize(QSize(icon_size + 60, icon_size + 80)); // More padding for round icons and text

    search_field->setFilterResult(visible_count, total_count);
}

void GameList::FilterTreeView(const QString& filter_text) {
    QStandardItem* folder;
    int children_total = 0;

    // If the searchfield is empty every item is visible
    // Otherwise the filter gets applied
    if (filter_text.isEmpty()) {
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                                UISettings::values.favorited_ids.size() == 0);
        for (int i = 1; i < item_model->rowCount() - 1; ++i) {
            folder = item_model->item(i, 0);
            const QModelIndex folder_index = folder->index();
            const int children_count = folder->rowCount();
            for (int j = 0; j < children_count; ++j) {
                ++children_total;
                tree_view->setRowHidden(j, folder_index, false);
            }
        }
        search_field->setFilterResult(children_total, children_total);
    } else {
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), true);
        int result_count = 0;
        for (int i = 1; i < item_model->rowCount() - 1; ++i) {
            folder = item_model->item(i, 0);
            const QModelIndex folder_index = folder->index();
            const int children_count = folder->rowCount();
            for (int j = 0; j < children_count; ++j) {
                ++children_total;

                const QStandardItem* child = folder->child(j, 0);

                const auto program_id = child->data(GameListItemPath::ProgramIdRole).toULongLong();

                const QString file_path =
                    child->data(GameListItemPath::FullPathRole).toString().toLower();
                const QString file_title =
                    child->data(GameListItemPath::TitleRole).toString().toLower();
                const QString file_program_id =
                    QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char{'0'});

                // Only items which filename in combination with its title contains all words
                // that are in the searchfield will be visible in the gamelist
                // The search is case insensitive because of toLower()
                // I decided not to use Qt::CaseInsensitive in containsAllWords to prevent
                // multiple conversions of edit_filter_text for each game in the gamelist
                const QString file_name =
                    file_path.mid(file_path.lastIndexOf(QLatin1Char{'/'}) + 1) + QLatin1Char{' '} +
                    file_title;
                if (ContainsAllWords(file_name, filter_text) ||
                    (file_program_id.size() == 16 && file_program_id.contains(filter_text))) {
                    tree_view->setRowHidden(j, folder_index, false);
                    ++result_count;
                } else {
                    tree_view->setRowHidden(j, folder_index, true);
                }
            }
        }
        search_field->setFilterResult(result_count, children_total);
    }
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

GameList::GameList(FileSys::VirtualFilesystem vfs_, FileSys::ManualContentProvider* provider_,
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

    // Configure tree view
    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_view->setStyleSheet(QStringLiteral("QTreeView{ border: none; }"));

    // Configure list view for grid display
    list_view->setViewMode(QListView::IconMode);
    list_view->setResizeMode(QListView::Adjust);
    list_view->setUniformItemSizes(true);
    list_view->setSelectionMode(QAbstractItemView::SingleSelection);
    list_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_view->setContextMenuPolicy(Qt::CustomContextMenu);
    list_view->setStyleSheet(QStringLiteral("QListView{ border: none; background: transparent; } QListView::item { text-align: center; padding: 5px; }"));
    list_view->setGridSize(QSize(140, 160)); // Initial size for round icons and text
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
    connect(list_view, &QListView::activated, this, &GameList::ValidateEntry);
    connect(list_view, &QListView::customContextMenuRequested, this, &GameList::PopupContextMenu);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent,
            [this](Qt::Key key) {
                // Avoid pressing buttons while playing
                if (system.IsPoweredOn()) {
                    return;
                }
                if (!this->isActiveWindow()) {
                    return;
                }

                // Only send events to visible and properly initialized views
                QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);

                if (tree_view->isVisible() && tree_view->model()) {
                    QCoreApplication::postEvent(tree_view, event);
                }

                if (list_view->isVisible() && list_view->model()) {
                    // Create a new event for the list view to avoid double deletion
                    QKeyEvent* list_event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                    QCoreApplication::postEvent(list_view, list_event);
                }
            });

    // We must register all custom types with the Qt Automoc system so that we are able to use
    // it with signals/slots. In this case, QList falls under the umbrells of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);
    layout->addWidget(list_view);
    layout->addWidget(search_field);
    setLayout(layout);

    // Set initial view mode
    SetViewMode(UISettings::values.game_list_grid_view.GetValue());
}

void GameList::UnloadController() {
    controller_navigation->UnloadController();
}

GameList::~GameList() {
    UnloadController();

    // Clean up any custom models that might have been created for grid view
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
    tree_view->setExpanded(
        entry_items->index(),
        UISettings::values.game_dirs[entry_items->data(GameListDir::GameDirRole).toInt()].expanded);
}

void GameList::AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent) {
    parent->appendRow(entry_items);
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

        if (file_info.isDir()) {
            const QDir dir{file_path};
            const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
            if (matching_main.size() == 1) {
                emit GameChosen(dir.path() + QDir::separator() + matching_main[0]);
            }
            return;
        }

        const auto title_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();

        // Users usually want to run a different game after closing one
        search_field->clear();
        emit GameChosen(file_path, title_id);
        break;
    }
    case GameListItemType::AddDir:
        emit AddDirectory();
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
    emit ShowList(!IsEmpty());

    item_model->invisibleRootItem()->appendRow(new GameListAddDir());

    // Add favorites row
    item_model->invisibleRootItem()->insertRow(0, new GameListFavorites());
    tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(),
                            UISettings::values.favorited_ids.size() == 0);
    tree_view->setExpanded(item_model->invisibleRootItem()->child(0)->index(),
                           UISettings::values.favorites_expanded.GetValue());
    for (const auto id : UISettings::values.favorited_ids) {
        AddFavorite(id);
    }

    // Clear out the old directories to watch for changes and add the new ones
    auto watch_dirs = watcher->directories();
    if (!watch_dirs.isEmpty()) {
        watcher->removePaths(watch_dirs);
    }
    // Workaround: Add the watch paths in chunks to allow the gui to refresh
    // This prevents the UI from stalling when a large number of watch paths are added
    // Also artificially caps the watcher to a certain number of directories
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

    // Update grid view if it's currently active
    if (list_view->isVisible()) {
        PopulateGridView();
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

    if (!item.isValid())
        return;

    const auto selected = item.sibling(item.row(), 0);
    QMenu context_menu;
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game:
        AddGamePopup(context_menu, selected.data(GameListItemPath::ProgramIdRole).toULongLong(),
                     selected.data(GameListItemPath::FullPathRole).toString().toStdString());
        break;
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

void GameList::AddGamePopup(QMenu& context_menu, u64 program_id, const std::string& path) {
    QAction* favorite = context_menu.addAction(tr("Favorite"));
    context_menu.addSeparator();
    QAction* start_game = context_menu.addAction(tr("Start Game"));
    QAction* start_game_global =
        context_menu.addAction(tr("Start Game without Custom Configuration"));
    context_menu.addSeparator();
    QAction* open_save_location = context_menu.addAction(tr("Open Save Data Location"));
    QAction* open_mod_location = context_menu.addAction(tr("Open Mod Data Location"));
    QAction* open_transferable_shader_cache =
        context_menu.addAction(tr("Open Transferable Pipeline Cache"));
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
    QAction* navigate_to_gamedb_entry = context_menu.addAction(tr("Navigate to GameDB entry"));
// TODO: Implement shortcut creation for macOS
#if !defined(__APPLE__)
    QMenu* shortcut_menu = context_menu.addMenu(tr("Create Shortcut"));
    QAction* create_desktop_shortcut = shortcut_menu->addAction(tr("Add to Desktop"));
    QAction* create_applications_menu_shortcut =
        shortcut_menu->addAction(tr("Add to Applications Menu"));
#endif
    context_menu.addSeparator();
    QAction* properties = context_menu.addAction(tr("Properties"));

    favorite->setVisible(program_id != 0);
    favorite->setCheckable(true);
    favorite->setChecked(UISettings::values.favorited_ids.contains(program_id));
    open_save_location->setVisible(program_id != 0);
    open_mod_location->setVisible(program_id != 0);
    open_transferable_shader_cache->setVisible(program_id != 0);
    remove_update->setVisible(program_id != 0);
    remove_dlc->setVisible(program_id != 0);
    remove_gl_shader_cache->setVisible(program_id != 0);
    remove_vk_shader_cache->setVisible(program_id != 0);
    remove_shader_cache->setVisible(program_id != 0);
    remove_all_content->setVisible(program_id != 0);
    auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);
    navigate_to_gamedb_entry->setVisible(it != compatibility_list.end() && program_id != 0);

    connect(favorite, &QAction::triggered, [this, program_id]() { ToggleFavorite(program_id); });
    connect(open_save_location, &QAction::triggered, [this, program_id, path]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData, path);
    });
    connect(start_game, &QAction::triggered,
            [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Normal); });
    connect(start_game_global, &QAction::triggered,
            [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Global); });
    connect(open_mod_location, &QAction::triggered, [this, program_id, path]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::ModData, path);
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
    connect(remove_gl_shader_cache, &QAction::triggered, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::GlShaderCache, path);
    });
    connect(remove_vk_shader_cache, &QAction::triggered, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::VkShaderCache, path);
    });
    connect(remove_shader_cache, &QAction::triggered, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::AllShaderCache, path);
    });
    connect(remove_custom_config, &QAction::triggered, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::CustomConfiguration, path);
    });
    connect(remove_play_time_data, &QAction::triggered,
            [this, program_id]() { emit RemovePlayTimeRequested(program_id); });
    connect(remove_cache_storage, &QAction::triggered, [this, program_id, path] {
        emit RemoveFileRequested(program_id, GameListRemoveTarget::CacheStorage, path);
    });
    connect(dump_romfs, &QAction::triggered, [this, program_id, path]() {
        emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::Normal);
    });
    connect(dump_romfs_sdmc, &QAction::triggered, [this, program_id, path]() {
        emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::SDMC);
    });
    connect(verify_integrity, &QAction::triggered,
            [this, path]() { emit VerifyIntegrityRequested(path); });
    connect(copy_tid, &QAction::triggered,
            [this, program_id]() { emit CopyTIDRequested(program_id); });
    connect(navigate_to_gamedb_entry, &QAction::triggered, [this, program_id]() {
        emit NavigateToGamedbEntryRequested(program_id, compatibility_list);
    });
// TODO: Implement shortcut creation for macOS
#if !defined(__APPLE__)
    connect(create_desktop_shortcut, &QAction::triggered, [this, program_id, path]() {
        emit CreateShortcut(program_id, path, GameListShortcutTarget::Desktop);
    });
    connect(create_applications_menu_shortcut, &QAction::triggered, [this, program_id, path]() {
        emit CreateShortcut(program_id, path, GameListShortcutTarget::Applications);
    });
#endif
    connect(properties, &QAction::triggered,
            [this, path]() { emit OpenPerGameGeneralRequested(path); });
};

void GameList::AddCustomDirPopup(QMenu& context_menu, QModelIndex selected) {
    UISettings::GameDir& game_dir =
        UISettings::values.game_dirs[selected.data(GameListDir::GameDirRole).toInt()];

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

    QAction* move_up = context_menu.addAction(tr("\u25B2 Move Up"));
    QAction* move_down = context_menu.addAction(tr("\u25bc Move Down"));
    QAction* open_directory_location = context_menu.addAction(tr("Open Directory Location"));

    const int row = selected.row();

    move_up->setEnabled(row > 1);
    move_down->setEnabled(row < item_model->rowCount() - 2);

    connect(move_up, &QAction::triggered, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row - 1, 0).data(GameListDir::GameDirRole).toInt();
        // swap the items in the settings
        std::swap(UISettings::values.game_dirs[game_dir_index],
                  UISettings::values.game_dirs[other_index]);
        // swap the indexes held by the QVariants
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row - 1, 0), QVariant(game_dir_index),
                            GameListDir::GameDirRole);
        // move the treeview items
        QList<QStandardItem*> item = item_model->takeRow(row);
        item_model->invisibleRootItem()->insertRow(row - 1, item);
        tree_view->setExpanded(selected.sibling(row - 1, 0),
                               UISettings::values.game_dirs[other_index].expanded);
    });

    connect(move_down, &QAction::triggered, [this, selected, row, game_dir_index] {
        const int other_index = selected.sibling(row + 1, 0).data(GameListDir::GameDirRole).toInt();
        // swap the items in the settings
        std::swap(UISettings::values.game_dirs[game_dir_index],
                  UISettings::values.game_dirs[other_index]);
        // swap the indexes held by the QVariants
        item_model->setData(selected, QVariant(other_index), GameListDir::GameDirRole);
        item_model->setData(selected.sibling(row + 1, 0), QVariant(game_dir_index),
                            GameListDir::GameDirRole);
        // move the treeview items
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
    QFile compat_list{QStringLiteral(":compatibility_list/compatibility_list.json")};

    if (!compat_list.open(QFile::ReadOnly | QFile::Text)) {
        LOG_ERROR(Frontend, "Unable to open game compatibility list");
        return;
    }

    if (compat_list.size() == 0) {
        LOG_WARNING(Frontend, "Game compatibility list is empty");
        return;
    }

    const QByteArray content = compat_list.readAll();
    if (content.isEmpty()) {
        LOG_ERROR(Frontend, "Unable to completely read game compatibility list");
        return;
    }

    const QJsonDocument json = QJsonDocument::fromJson(content);
    const QJsonArray arr = json.array();

    for (const QJsonValue value : arr) {
        const QJsonObject game = value.toObject();
        const QString compatibility_key = QStringLiteral("compatibility");

        if (!game.contains(compatibility_key) || !game[compatibility_key].isDouble()) {
            continue;
        }

        const int compatibility = game[compatibility_key].toInt();
        const QString directory = game[QStringLiteral("directory")].toString();
        const QJsonArray ids = game[QStringLiteral("releases")].toArray();

        for (const QJsonValue id_ref : ids) {
            const QJsonObject id_object = id_ref.toObject();
            const QString id = id_object[QStringLiteral("id")].toString();

            compatibility_list.emplace(id.toUpper().toStdString(),
                                       std::make_pair(QString::number(compatibility), directory));
        }
    }
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
    tree_view->setEnabled(false);

    // Update the columns in case UISettings has changed
    tree_view->setColumnHidden(COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    tree_view->setColumnHidden(COLUMN_COMPATIBILITY, !UISettings::values.show_compat);
    tree_view->setColumnHidden(COLUMN_FILE_TYPE, !UISettings::values.show_types);
    tree_view->setColumnHidden(COLUMN_SIZE, !UISettings::values.show_size);
    tree_view->setColumnHidden(COLUMN_PLAY_TIME, !UISettings::values.show_play_time);

    // Cancel any existing worker.
    current_worker.reset();

    // Delete any rows that might already exist if we're repopulating
    item_model->removeRows(0, item_model->rowCount());
    search_field->clear();

    current_worker = std::make_unique<GameListWorker>(vfs, provider, game_dirs, compatibility_list,
                                                      play_time_manager, system);

    // Get events from the worker as data becomes available
    connect(current_worker.get(), &GameListWorker::DataAvailable, this, &GameList::WorkerEvent,
            Qt::QueuedConnection);

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

    // We are using the name column to display icons and titles
    // so make it as large as possible as default.
    header->resizeSection(COLUMN_NAME, header->width());
}

const QStringList GameList::supported_file_extensions = {
    QStringLiteral("nso"), QStringLiteral("nro"), QStringLiteral("nca"),
    QStringLiteral("xci"), QStringLiteral("nsp"), QStringLiteral("kip")};

void GameList::RefreshGameDirectory() {
    if (!UISettings::values.game_dirs.empty() && current_worker != nullptr) {
        LOG_INFO(Frontend, "Change detected in the games directory. Reloading game list.");
        PopulateAsync(UISettings::values.game_dirs);
    }
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

    // Update grid view if it's currently active
    if (list_view->isVisible()) {
        PopulateGridView();
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

void GameList::SetViewMode(bool grid_view) {
    if (grid_view) {
        // Create a flat model for grid view showing only games
        PopulateGridView();
        tree_view->setVisible(false);
        list_view->setVisible(true);
        // Only set current index if the model has items
        if (list_view->model() && list_view->model()->rowCount() > 0) {
            list_view->setCurrentIndex(list_view->model()->index(0, 0));
        }
    } else {
        // Restore the hierarchical model for tree view
        list_view->setVisible(false);
        tree_view->setVisible(true);
        // Only set current index if the model has items
        if (item_model && item_model->rowCount() > 0) {
            tree_view->setCurrentIndex(item_model->index(0, 0));
        }
    }
}

void GameList::PopulateGridView() {
    // Store the current hierarchical model
    QStandardItemModel* hierarchical_model = item_model;

    // Delete the previous flat model if it exists to prevent memory leaks
    if (QAbstractItemModel* old_model = list_view->model()) {
        if (old_model != item_model) {
            old_model->deleteLater();
        }
    }

    // Create a new flat model for grid view
    QStandardItemModel* flat_model = new QStandardItemModel(this);

    // Collect all games from the hierarchical model
    for (int i = 0; i < hierarchical_model->rowCount(); ++i) {
        QStandardItem* folder = hierarchical_model->item(i, 0);
        if (!folder) continue;

        // Skip non-game folders in grid view, but include favorites
        const auto folder_type = folder->data(GameListItem::TypeRole).value<GameListItemType>();
        if (folder_type == GameListItemType::AddDir) {
            continue;
        }

        // Add games from this folder to the flat model
        for (int j = 0; j < folder->rowCount(); ++j) {
            QStandardItem* game_item = folder->child(j, 0);
            if (!game_item) continue;

            const auto game_type = game_item->data(GameListItem::TypeRole).value<GameListItemType>();
            if (game_type == GameListItemType::Game) {
                // Clone the game item for the flat model
                QStandardItem* cloned_item = game_item->clone();

                // Set display text to just the game title for grid view
                QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                if (game_title.isEmpty()) {
                    // Fallback to filename if no title
                    std::string filename;
                    Common::SplitPath(game_item->data(GameListItemPath::FullPathRole).toString().toStdString(),
                                    nullptr, &filename, nullptr);
                    game_title = QString::fromStdString(filename);
                }
                cloned_item->setText(game_title);

                flat_model->appendRow(cloned_item);
            }
        }
    }

    // Set the flat model for the list view
    list_view->setModel(flat_model);

    // Update grid size based on icon size
    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    list_view->setGridSize(QSize(icon_size + 60, icon_size + 80)); // More padding for round icons and text
}

void GameList::ToggleViewMode() {
    bool current_grid_view = UISettings::values.game_list_grid_view.GetValue();
    UISettings::values.game_list_grid_view.SetValue(!current_grid_view);
    SetViewMode(!current_grid_view);
}
