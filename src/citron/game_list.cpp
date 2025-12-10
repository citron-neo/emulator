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
#include <QScrollBar>
#include <QStyle>
#include <QThreadPool>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <fmt/format.h>
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "citron/compatibility_list.h"
#include "common/fs/path_util.h"
#include "core/hle/service/acc/profile_manager.h"
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
    // Add rounded corners styling to the search field
    edit_filter->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  background: palette(base);"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid palette(highlight);"
        "  background: palette(base);"
        "}"
    ));
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

    // Check if we can reuse the existing model
    QAbstractItemModel* current_model = list_view->model();
    if (current_model && current_model != item_model) {
        QStandardItemModel* existing_flat = qobject_cast<QStandardItemModel*>(current_model);
        if (existing_flat) {
            // Clear existing model instead of deleting it to avoid view flicker
            existing_flat->clear();
            flat_model = existing_flat;
        }
    }

    if (!flat_model) {
        // Delete old model if it exists and create new one
        if (current_model && current_model != item_model) {
            current_model->deleteLater();
        }
        flat_model = new QStandardItemModel(this);
    }
    int visible_count = 0;
    int total_count = 0;
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
                total_count++;
                bool should_show = true;
                if (!filter_text.isEmpty()) {
                    const QString file_path = game_item->data(GameListItemPath::FullPathRole).toString().toLower();
                    const QString file_title = game_item->data(GameListItemPath::TitleRole).toString().toLower();
                    const auto program_id = game_item->data(GameListItemPath::ProgramIdRole).toULongLong();
                    const QString file_program_id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char{'0'});
                    const QString file_name = file_path.mid(file_path.lastIndexOf(QLatin1Char{'/'}) + 1) + QLatin1Char{' '} + file_title;
                    should_show = ContainsAllWords(file_name, filter_text) || (file_program_id.size() == 16 && file_program_id.contains(filter_text));
                }
                if (should_show) {
                    QStandardItem* cloned_item = game_item->clone();
                    QString game_title = game_item->data(GameListItemPath::TitleRole).toString();
                    if (game_title.isEmpty()) {
                        std::string filename;
                        Common::SplitPath(game_item->data(GameListItemPath::FullPathRole).toString().toStdString(), nullptr, &filename, nullptr);
                        game_title = QString::fromStdString(filename);
                    }
                    cloned_item->setText(game_title);
                    flat_model->appendRow(cloned_item);
                    visible_count++;
                }
            }
        }
    }
    list_view->setModel(flat_model);
    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    list_view->setGridSize(QSize(icon_size + 60, icon_size + 80));
    // Set sort role and sort the filtered model
    flat_model->setSortRole(GameListItemPath::SortRole);
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
    search_field->setFilterResult(visible_count, total_count);
}

void GameList::FilterTreeView(const QString& filter_text) {
    QStandardItem* folder;
    int children_total = 0;
    if (filter_text.isEmpty()) {
        tree_view->setRowHidden(0, item_model->invisibleRootItem()->index(), UISettings::values.favorited_ids.size() == 0);
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
                const QString file_path = child->data(GameListItemPath::FullPathRole).toString().toLower();
                const QString file_title = child->data(GameListItemPath::TitleRole).toString().toLower();
                const QString file_program_id = QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char{'0'});
                const QString file_name = file_path.mid(file_path.lastIndexOf(QLatin1Char{'/'}) + 1) + QLatin1Char{' '} + file_title;
                if (ContainsAllWords(file_name, filter_text) || (file_program_id.size() == 16 && file_program_id.contains(filter_text))) {
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

    // Add widgets to toolbar
    toolbar_layout->addWidget(btn_list_view);
    toolbar_layout->addWidget(btn_grid_view);
    toolbar_layout->addWidget(slider_title_size);
    toolbar_layout->addWidget(btn_sort_az);
    toolbar_layout->addStretch(); // Push search to the right
    toolbar_layout->addWidget(search_field);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(tree_view);
    layout->addWidget(list_view);
    setLayout(layout);

    SetViewMode(UISettings::values.game_list_grid_view.GetValue());

    online_status_timer = new QTimer(this);
    connect(online_status_timer, &QTimer::timeout, this, &GameList::UpdateOnlineStatus);
    online_status_timer->start(5000); // Your refresh interval

    // Configure the new timer for debouncing configuration changes
    config_update_timer.setSingleShot(true);
    connect(&config_update_timer, &QTimer::timeout, this, &GameList::UpdateOnlineStatus);
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
        std::map<u64, std::pair<int, int>> stats;
        AnnounceMultiplayerRoom::RoomList room_list = session->GetRoomList();
        for (const auto& room : room_list) {
            u64 game_id = room.information.preferred_game.id;
            if (game_id != 0) {
                stats[game_id].first += room.members.size();
                stats[game_id].second++;
            }
        }
        return stats;
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

void GameList::ValidateEntry(const QModelIndex& item) {
    const auto selected = item.sibling(item.row(), 0);
    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
        case GameListItemType::Game: {
            const QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
            if (file_path.isEmpty()) return;
            const QFileInfo file_info(file_path);
            if (!file_info.exists()) return;
            if (file_info.isDir()) {
                const QDir dir{file_path};
                const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
                if (matching_main.size() == 1) {
                    emit GameChosen(dir.path() + QDir::separator() + matching_main[0]);
                }
                return;
            }
            const auto title_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();
            search_field->clear();
            emit GameChosen(file_path, title_id);
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
        case GameListItemType::Game:
            AddGamePopup(context_menu, selected.data(GameListItemPath::ProgramIdRole).toULongLong(), selected.data(GameListItemPath::FullPathRole).toString().toStdString());
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
    QAction* start_game_global = context_menu.addAction(tr("Start Game without Custom Configuration"));
    context_menu.addSeparator();
    QAction* open_save_location = context_menu.addAction(tr("Open Save Data Location"));
    QAction* set_custom_save_path = context_menu.addAction(tr("Set Custom Save Path"));
    QAction* remove_custom_save_path = context_menu.addAction(tr("Revert to NAND Save Path"));
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
    QAction* navigate_to_gamedb_entry = context_menu.addAction(tr("Navigate to GameDB entry"));
    #if !defined(__APPLE__)
    QMenu* shortcut_menu = context_menu.addMenu(tr("Create Shortcut"));
    QAction* create_desktop_shortcut = shortcut_menu->addAction(tr("Add to Desktop"));
    QAction* create_applications_menu_shortcut = shortcut_menu->addAction(tr("Add to Applications Menu"));
    #endif
    context_menu.addSeparator();
    QAction* properties = context_menu.addAction(tr("Properties"));

    const bool has_custom_path = Settings::values.custom_save_paths.count(program_id);

    favorite->setVisible(program_id != 0);
    favorite->setCheckable(true);
    favorite->setChecked(UISettings::values.favorited_ids.contains(program_id));
    open_save_location->setVisible(program_id != 0);
    set_custom_save_path->setVisible(program_id != 0);
    remove_custom_save_path->setVisible(program_id != 0 && has_custom_path);
    open_mod_location->setVisible(program_id != 0);
    open_sdmc_mod_menu->menuAction()->setVisible(program_id != 0);
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
    connect(open_save_location, &QAction::triggered, [this, program_id, path]() { emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData, path); });

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

        const auto nand_dir = QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir));
        const auto user_id = system.GetProfileManager().GetLastOpenedUser().AsU128();
        const std::string relative_save_path = fmt::format("user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, user_id[1], user_id[0], program_id);
        const QString old_save_path = QDir(nand_dir).filePath(QString::fromStdString(relative_save_path));

        QDir old_dir(old_save_path);
        if (old_dir.exists() && !old_dir.isEmpty()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Move Save Data"),
                tr("You have existing save data in the NAND. Would you like to move it to the new custom save path?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

            if (reply == QMessageBox::Cancel) return;

            if (reply == QMessageBox::Yes) {
                const QString full_dest_path = QDir(new_path).filePath(QString::fromStdString(relative_save_path));
                if (copyWithProgress(old_save_path, full_dest_path, this)) {
                    QDir(old_save_path).removeRecursively();
                    QMessageBox::information(this, tr("Success"), tr("Successfully moved save data to the new location."));
                } else {
                    QMessageBox::warning(this, tr("Error"), tr("Failed to move save data. Please see the log for more details."));
                }
            }
        }

        Settings::values.custom_save_paths.insert_or_assign(program_id, new_path.toStdString());
        emit SaveConfig();
    });

    connect(remove_custom_save_path, &QAction::triggered, [this, program_id, copyWithProgress]() {
        const QString custom_path_root = QString::fromStdString(Settings::values.custom_save_paths.at(program_id));
        const auto nand_dir = QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir));
        const auto user_id = system.GetProfileManager().GetLastOpenedUser().AsU128();
        const std::string relative_save_path = fmt::format("user/save/{:016X}/{:016X}{:016X}/{:016X}", 0, user_id[1], user_id[0], program_id);

        const QString custom_game_save_path = QDir(custom_path_root).filePath(QString::fromStdString(relative_save_path));
        const QString nand_save_path = QDir(nand_dir).filePath(QString::fromStdString(relative_save_path));

        QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Move Save Data"),
            tr("Would you like to move the save data from the custom path back to the NAND?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) return;

        if (reply == QMessageBox::Yes) {
            if (copyWithProgress(custom_game_save_path, nand_save_path, this)) {
                QDir(custom_game_save_path).removeRecursively();
                QMessageBox::information(this, tr("Success"), tr("Successfully moved save data back to the NAND."));
            } else {
                QMessageBox::warning(this, tr("Error"), tr("Failed to move save data. Please see the log for more details."));
            }
        }

        Settings::values.custom_save_paths.erase(program_id);
        emit SaveConfig();
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

    connect(start_game, &QAction::triggered, [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Normal); });
    connect(start_game_global, &QAction::triggered, [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Global); });
    connect(open_mod_location, &QAction::triggered, [this, program_id, path]() { emit OpenFolderRequested(program_id, GameListOpenTarget::ModData, path); });
    connect(open_transferable_shader_cache, &QAction::triggered, [this, program_id]() { emit OpenTransferableShaderCacheRequested(program_id); });
    connect(remove_all_content, &QAction::triggered, [this, program_id]() { emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::Game); });
    connect(remove_update, &QAction::triggered, [this, program_id]() { emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::Update); });
    connect(remove_dlc, &QAction::triggered, [this, program_id]() { emit RemoveInstalledEntryRequested(program_id, InstalledEntryType::AddOnContent); });
    connect(remove_gl_shader_cache, &QAction::triggered, [this, program_id, path]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::GlShaderCache, path); });
    connect(remove_vk_shader_cache, &QAction::triggered, [this, program_id, path]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::VkShaderCache, path); });
    connect(remove_shader_cache, &QAction::triggered, [this, program_id, path]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::AllShaderCache, path); });
    connect(remove_custom_config, &QAction::triggered, [this, program_id, path]() { emit RemoveFileRequested(program_id, GameListRemoveTarget::CustomConfiguration, path); });
    connect(remove_play_time_data, &QAction::triggered, [this, program_id]() { emit RemovePlayTimeRequested(program_id); });
    connect(remove_cache_storage, &QAction::triggered, [this, program_id, path] { emit RemoveFileRequested(program_id, GameListRemoveTarget::CacheStorage, path); });
    connect(dump_romfs, &QAction::triggered, [this, program_id, path]() { emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::Normal); });
    connect(dump_romfs_sdmc, &QAction::triggered, [this, program_id, path]() { emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::SDMC); });
    connect(verify_integrity, &QAction::triggered, [this, path]() { emit VerifyIntegrityRequested(path); });
    connect(copy_tid, &QAction::triggered, [this, program_id]() { emit CopyTIDRequested(program_id); });
    connect(navigate_to_gamedb_entry, &QAction::triggered, [this, program_id]() { emit NavigateToGamedbEntryRequested(program_id, compatibility_list); });
    #if !defined(__APPLE__)
    connect(create_desktop_shortcut, &QAction::triggered, [this, program_id, path]() { emit CreateShortcut(program_id, path, GameListShortcutTarget::Desktop); });
    connect(create_applications_menu_shortcut, &QAction::triggered, [this, program_id, path]() { emit CreateShortcut(program_id, path, GameListShortcutTarget::Applications); });
    #endif
    connect(properties, &QAction::triggered, [this, path]() { emit OpenPerGameGeneralRequested(path); });
}

void GameList::AddCustomDirPopup(QMenu& context_menu, QModelIndex selected) {
    UISettings::GameDir& game_dir = UISettings::values.game_dirs[selected.data(GameListDir::GameDirRole).toInt()];
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
            compatibility_list.emplace(id.toUpper().toStdString(), std::make_pair(QString::number(compatibility), directory));
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
    tree_view->setEnabled(false);
    tree_view->setColumnHidden(COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    tree_view->setColumnHidden(COLUMN_COMPATIBILITY, !UISettings::values.show_compat);
    tree_view->setColumnHidden(COLUMN_FILE_TYPE, !UISettings::values.show_types);
    tree_view->setColumnHidden(COLUMN_SIZE, !UISettings::values.show_size);
    tree_view->setColumnHidden(COLUMN_PLAY_TIME, !UISettings::values.show_play_time);
    current_worker.reset();
    item_model->removeRows(0, item_model->rowCount());
    search_field->clear();
    current_worker = std::make_unique<GameListWorker>(vfs, provider, game_dirs, compatibility_list, play_time_manager, system, main_window->GetMultiplayerState()->GetSession());
    connect(current_worker.get(), &GameListWorker::DataAvailable, this, &GameList::WorkerEvent, Qt::QueuedConnection);
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
