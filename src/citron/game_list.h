// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <utility>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QPushButton>
#include <QSlider>
#include <QStandardItemModel>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "common/common_types.h"
#include "core/core.h"
#include "uisettings.h"
#include "citron/compatibility_list.h"
#include "citron/play_time_manager.h"
#include "citron/multiplayer/state.h"

class ControllerNavigation;
class GameListWorker;
class GameListSearchField;
class GameListDir;
class GMainWindow;
enum class AmLaunchType;
enum class StartGameType;

namespace FileSys {
    class ManualContentProvider;
    class VfsFilesystem;
} // namespace FileSys

enum class GameListOpenTarget {
    SaveData,
    ModData,
};

enum class GameListRemoveTarget {
    GlShaderCache,
    VkShaderCache,
    AllShaderCache,
    CustomConfiguration,
    CacheStorage,
};

enum class DumpRomFSTarget {
    Normal,
    SDMC,
};

enum class GameListShortcutTarget {
    Desktop,
    Applications,
};

enum class InstalledEntryType {
    Game,
    Update,
    AddOnContent,
};

class GameList : public QWidget {
    Q_OBJECT

public:
    enum {
        COLUMN_NAME,
        COLUMN_COMPATIBILITY,
        COLUMN_ADD_ONS,
        COLUMN_FILE_TYPE,
        COLUMN_SIZE,
        COLUMN_PLAY_TIME,
        COLUMN_ONLINE,
        COLUMN_COUNT, // Number of columns
    };

    explicit GameList(std::shared_ptr<FileSys::VfsFilesystem> vfs_,
                      FileSys::ManualContentProvider* provider_,
                      PlayTime::PlayTimeManager& play_time_manager_, Core::System& system_,
                      GMainWindow* parent = nullptr);
    ~GameList() override;

    QString GetLastFilterResultItem() const;
    void ClearFilter();
    void SetFilterFocus();
    void SetFilterVisible(bool visibility);
    bool IsEmpty() const;

    void LoadCompatibilityList();
    void PopulateAsync(QVector<UISettings::GameDir>& game_dirs);

    void SaveInterfaceLayout();
    void LoadInterfaceLayout();

    void SetViewMode(bool grid_view);
    void ToggleViewMode();
    void SortAlphabetically();
    void ToggleSortOrder();

    QStandardItemModel* GetModel() const;

    /// Disables events from the emulated controller
    void UnloadController();

    static const QStringList supported_file_extensions;

signals:
    void BootGame(const QString& game_path, StartGameType type);
    void GameChosen(const QString& game_path, const u64 title_id = 0);
    void OpenFolderRequested(u64 program_id, GameListOpenTarget target,
                             const std::string& game_path);
    void OpenTransferableShaderCacheRequested(u64 program_id);
    void RemoveInstalledEntryRequested(u64 program_id, InstalledEntryType type);
    void RemoveFileRequested(u64 program_id, GameListRemoveTarget target,
                             const std::string& game_path);
    void RemovePlayTimeRequested(u64 program_id);
    void DumpRomFSRequested(u64 program_id, const std::string& game_path, DumpRomFSTarget target);
    void VerifyIntegrityRequested(const std::string& game_path);
    void CopyTIDRequested(u64 program_id);
    void CreateShortcut(u64 program_id, const std::string& game_path,
                        GameListShortcutTarget target);
    void NavigateToGamedbEntryRequested(u64 program_id,
                                        const CompatibilityList& compatibility_list);
    void OpenPerGameGeneralRequested(const std::string& file);
    void OpenDirectory(const QString& directory);
    void AddDirectory();
    void ShowList(bool show);
    void PopulatingCompleted();
    void SaveConfig();
    void RunAutoloaderRequested();

public slots:
    void OnConfigurationChanged();

private slots:
    void OnItemExpanded(const QModelIndex& item);
    void OnTextChanged(const QString& new_text);
    void OnFilterCloseClicked();
    void OnUpdateThemedIcons();
    void UpdateOnlineStatus();
    void OnOnlineStatusUpdated(const std::map<u64, std::pair<int, int>>& online_stats);

private:
    friend class GameListWorker;
    void WorkerEvent();

    void AddDirEntry(GameListDir* entry_items);
    void AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent);
    void DonePopulating(const QStringList& watch_list);

private:
    void ValidateEntry(const QModelIndex& item);

    void RefreshGameDirectory();

    void ToggleFavorite(u64 program_id);
    void AddFavorite(u64 program_id);
    void RemoveFavorite(u64 program_id);

    void PopulateGridView();

    void FilterGridView(const QString& filter_text);
    void FilterTreeView(const QString& filter_text);

    void PopupContextMenu(const QPoint& menu_location);
    void AddGamePopup(QMenu& context_menu, u64 program_id, const std::string& path);
    void AddCustomDirPopup(QMenu& context_menu, QModelIndex selected);
    void AddPermDirPopup(QMenu& context_menu, QModelIndex selected);
    void AddFavoritesPopup(QMenu& context_menu);

    void changeEvent(QEvent*) override;
    void RetranslateUI();
    void UpdateSortButtonIcon();

    std::shared_ptr<FileSys::VfsFilesystem> vfs;
    FileSys::ManualContentProvider* provider;
    GameListSearchField* search_field;
    GMainWindow* main_window = nullptr;
    QVBoxLayout* layout = nullptr;
    QWidget* toolbar = nullptr;
    QHBoxLayout* toolbar_layout = nullptr;
    QToolButton* btn_list_view = nullptr;
    QToolButton* btn_grid_view = nullptr;
    QSlider* slider_title_size = nullptr;
    QToolButton* btn_sort_az = nullptr;
    Qt::SortOrder current_sort_order = Qt::AscendingOrder;
    QTreeView* tree_view = nullptr;
    QListView* list_view = nullptr;
    QStandardItemModel* item_model = nullptr;
    std::unique_ptr<GameListWorker> current_worker;
    QFileSystemWatcher* watcher = nullptr;
    ControllerNavigation* controller_navigation = nullptr;
    CompatibilityList compatibility_list;
    QTimer* online_status_timer;
    QTimer config_update_timer;

    friend class GameListSearchField;

    const PlayTime::PlayTimeManager& play_time_manager;
    Core::System& system;
};

class GameListPlaceholder : public QWidget {
    Q_OBJECT
public:
    explicit GameListPlaceholder(GMainWindow* parent = nullptr);
    ~GameListPlaceholder();

signals:
    void AddDirectory();

private slots:
    void onUpdateThemedIcons();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    QVBoxLayout* layout = nullptr;
    QLabel* image = nullptr;
    QLabel* text = nullptr;
};
