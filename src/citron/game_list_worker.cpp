// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QCryptographicHash>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/loader/loader.h"
#include "citron/compatibility_list.h"
#include "citron/game_list.h"
#include "citron/game_list_p.h"
#include "citron/game_list_worker.h"
#include "citron/uisettings.h"

namespace {

// Structure to hold cached game metadata
struct CachedGameMetadata {
    u64 program_id = 0;
    Loader::FileType file_type = Loader::FileType::Unknown;
    std::size_t file_size = 0;
    std::string title;
    std::vector<u8> icon;
    std::string file_path;
    std::int64_t modification_time = 0; // Unix timestamp

    bool IsValid() const {
        return program_id != 0 && file_type != Loader::FileType::Unknown;
    }
};

// In-memory cache for game metadata
static std::unordered_map<std::string, CachedGameMetadata> game_metadata_cache;

// Generate a cache key from file path
std::string GetCacheKey(const std::string& file_path) {
    // Use a hash of the normalized path as the key
    std::error_code ec;
    std::filesystem::path normalized_path;
    try {
        normalized_path = std::filesystem::canonical(std::filesystem::path(file_path), ec);
        if (ec) {
            // If canonical fails, use the original path
            normalized_path = std::filesystem::path(file_path);
        }
    } catch (...) {
        // If canonical throws, use the original path
        normalized_path = std::filesystem::path(file_path);
    }

    const auto path_str = Common::FS::PathToUTF8String(normalized_path);
    const auto hash = QCryptographicHash::hash(
        QByteArray::fromStdString(path_str),
        QCryptographicHash::Sha256);
    return hash.toHex().toStdString();
}

// Load game metadata cache from disk
void LoadGameMetadataCache() {
    if (!UISettings::values.cache_game_list) {
        return;
    }

    game_metadata_cache.clear();

    const auto cache_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list";
    const auto cache_file = Common::FS::PathToUTF8String(cache_dir / "game_metadata_cache.json");

    if (!Common::FS::Exists(cache_file)) {
        return;
    }

    QFile file(QString::fromStdString(cache_file));
    if (!file.open(QFile::ReadOnly)) {
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray entries = root[QStringLiteral("entries")].toArray();

    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const std::string key = entry[QStringLiteral("key")].toString().toStdString();

        CachedGameMetadata metadata;
        metadata.program_id = entry[QStringLiteral("program_id")].toString().toULongLong(nullptr, 16);
        metadata.file_type = static_cast<Loader::FileType>(entry[QStringLiteral("file_type")].toInt());
        metadata.file_size = static_cast<std::size_t>(entry[QStringLiteral("file_size")].toVariant().toULongLong());
        metadata.title = entry[QStringLiteral("title")].toString().toStdString();
        metadata.file_path = entry[QStringLiteral("file_path")].toString().toStdString();
        metadata.modification_time = entry[QStringLiteral("modification_time")].toVariant().toLongLong();

        const QByteArray icon_data = QByteArray::fromBase64(
            entry[QStringLiteral("icon")].toString().toUtf8());
        metadata.icon.assign(icon_data.begin(), icon_data.end());

        if (metadata.IsValid()) {
            game_metadata_cache[key] = std::move(metadata);
        }
    }
}

// Save game metadata cache to disk
void SaveGameMetadataCache() {
    if (!UISettings::values.cache_game_list) {
        return;
    }

    const auto cache_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list";
    const auto cache_file = Common::FS::PathToUTF8String(cache_dir / "game_metadata_cache.json");

    void(Common::FS::CreateParentDirs(cache_file));

    QJsonObject root;
    QJsonArray entries;

    for (const auto& [key, metadata] : game_metadata_cache) {
        QJsonObject entry;
        entry[QStringLiteral("key")] = QString::fromStdString(key);
        entry[QStringLiteral("program_id")] = QString::number(metadata.program_id, 16);
        entry[QStringLiteral("file_type")] = static_cast<int>(metadata.file_type);
        entry[QStringLiteral("file_size")] = static_cast<qint64>(metadata.file_size);
        entry[QStringLiteral("title")] = QString::fromStdString(metadata.title);
        entry[QStringLiteral("file_path")] = QString::fromStdString(metadata.file_path);
        entry[QStringLiteral("modification_time")] = static_cast<qint64>(metadata.modification_time);

        const QByteArray icon_data(reinterpret_cast<const char*>(metadata.icon.data()),
                                   static_cast<int>(metadata.icon.size()));
        entry[QStringLiteral("icon")] = QString::fromLatin1(icon_data.toBase64());

        entries.append(entry);
    }

    root[QStringLiteral("entries")] = entries;

    QFile file(QString::fromStdString(cache_file));
    if (file.open(QFile::WriteOnly)) {
        const QJsonDocument doc(root);
        file.write(doc.toJson());
    }
}

// Get cached metadata or return nullptr if not found or invalid
const CachedGameMetadata* GetCachedGameMetadata(const std::string& file_path) {
    if (!UISettings::values.cache_game_list) {
        return nullptr;
    }

    // Check if file still exists and modification time matches
    if (!Common::FS::Exists(file_path)) {
        return nullptr;
    }

    std::error_code ec;
    const auto mod_time = std::filesystem::last_write_time(file_path, ec);
    if (ec) {
        return nullptr;
    }

    const auto mod_time_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        mod_time.time_since_epoch()).count();

    const std::string key = GetCacheKey(file_path);
    const auto it = game_metadata_cache.find(key);

    if (it == game_metadata_cache.end()) {
        return nullptr;
    }

    const auto& cached = it->second;

    // Check if file path matches and modification time matches
    if (cached.file_path != file_path || cached.modification_time != mod_time_seconds) {
        return nullptr;
    }

    return &cached;
}

// Store game metadata in cache
void CacheGameMetadata(const std::string& file_path, u64 program_id, Loader::FileType file_type,
                       std::size_t file_size, const std::string& title, const std::vector<u8>& icon) {
    if (!UISettings::values.cache_game_list) {
        return;
    }

    std::error_code ec;
    const auto mod_time = std::filesystem::last_write_time(file_path, ec);
    if (ec) {
        return;
    }

    const auto mod_time_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        mod_time.time_since_epoch()).count();

    const std::string key = GetCacheKey(file_path);

    CachedGameMetadata metadata;
    metadata.program_id = program_id;
    metadata.file_type = file_type;
    metadata.file_size = file_size;
    metadata.title = title;
    metadata.icon = icon;
    metadata.file_path = file_path;
    metadata.modification_time = mod_time_seconds;

    game_metadata_cache[key] = std::move(metadata);
}

QString GetGameListCachedObject(const std::string& filename, const std::string& ext,
                                const std::function<QString()>& generator) {
    if (!UISettings::values.cache_game_list || filename == "0000000000000000") {
        return generator();
    }

    const auto path =
        Common::FS::PathToUTF8String(Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) /
                                     "game_list" / fmt::format("{}.{}", filename, ext));

    void(Common::FS::CreateParentDirs(path));

    if (!Common::FS::Exists(path)) {
        const auto str = generator();

        QFile file{QString::fromStdString(path)};
        if (file.open(QFile::WriteOnly)) {
            file.write(str.toUtf8());
        }

        return str;
    }

    QFile file{QString::fromStdString(path)};
    if (file.open(QFile::ReadOnly)) {
        return QString::fromUtf8(file.readAll());
    }

    return generator();
}

std::pair<std::vector<u8>, std::string> GetGameListCachedObject(
    const std::string& filename, const std::string& ext,
    const std::function<std::pair<std::vector<u8>, std::string>()>& generator) {
    if (!UISettings::values.cache_game_list || filename == "0000000000000000") {
        return generator();
    }

    const auto game_list_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list";
    const auto jpeg_name = fmt::format("{}.jpeg", filename);
    const auto app_name = fmt::format("{}.appname.txt", filename);

    const auto path1 = Common::FS::PathToUTF8String(game_list_dir / jpeg_name);
    const auto path2 = Common::FS::PathToUTF8String(game_list_dir / app_name);

    void(Common::FS::CreateParentDirs(path1));

    if (!Common::FS::Exists(path1) || !Common::FS::Exists(path2)) {
        const auto [icon, nacp] = generator();

        QFile file1{QString::fromStdString(path1)};
        if (!file1.open(QFile::WriteOnly)) {
            LOG_ERROR(Frontend, "Failed to open cache file.");
            return generator();
        }

        if (!file1.resize(icon.size())) {
            LOG_ERROR(Frontend, "Failed to resize cache file to necessary size.");
            return generator();
        }

        if (file1.write(reinterpret_cast<const char*>(icon.data()), icon.size()) !=
            s64(icon.size())) {
            LOG_ERROR(Frontend, "Failed to write data to cache file.");
            return generator();
        }

        QFile file2{QString::fromStdString(path2)};
        if (file2.open(QFile::WriteOnly)) {
            file2.write(nacp.data(), nacp.size());
        }

        return std::make_pair(icon, nacp);
    }

    QFile file1(QString::fromStdString(path1));
    QFile file2(QString::fromStdString(path2));

    if (!file1.open(QFile::ReadOnly)) {
        LOG_ERROR(Frontend, "Failed to open cache file for reading.");
        return generator();
    }

    if (!file2.open(QFile::ReadOnly)) {
        LOG_ERROR(Frontend, "Failed to open cache file for reading.");
        return generator();
    }

    std::vector<u8> vec(file1.size());
    if (file1.read(reinterpret_cast<char*>(vec.data()), vec.size()) !=
        static_cast<s64>(vec.size())) {
        return generator();
    }

    const auto data = file2.readAll();
    return std::make_pair(vec, data.toStdString());
}

void GetMetadataFromControlNCA(const FileSys::PatchManager& patch_manager, const FileSys::NCA& nca,
                               std::vector<u8>& icon, std::string& name) {
    std::tie(icon, name) = GetGameListCachedObject(
        fmt::format("{:016X}", patch_manager.GetTitleID()), {}, [&patch_manager, &nca] {
            const auto [nacp, icon_f] = patch_manager.ParseControlNCA(nca);
            return std::make_pair(icon_f->ReadAllBytes(), nacp->GetApplicationName());
        });
}

bool HasSupportedFileExtension(const std::string& file_name) {
    const QFileInfo file = QFileInfo(QString::fromStdString(file_name));
    return GameList::supported_file_extensions.contains(file.suffix(), Qt::CaseInsensitive);
}

bool IsExtractedNCAMain(const std::string& file_name) {
    return QFileInfo(QString::fromStdString(file_name)).fileName() == QStringLiteral("main");
}

QString FormatGameName(const std::string& physical_name) {
    const QString physical_name_as_qstring = QString::fromStdString(physical_name);
    const QFileInfo file_info(physical_name_as_qstring);

    if (IsExtractedNCAMain(physical_name)) {
        return file_info.dir().path();
    }

    return physical_name_as_qstring;
}

QString FormatPatchNameVersions(const FileSys::PatchManager& patch_manager,
                                Loader::AppLoader& loader, bool updatable = true) {
    QString out;
    FileSys::VirtualFile update_raw;
    loader.ReadUpdateRaw(update_raw);
    for (const auto& patch : patch_manager.GetPatches(update_raw)) {
        const bool is_update = patch.name == "Update";
        if (!updatable && is_update) {
            continue;
        }

        const QString type =
            QString::fromStdString(patch.enabled ? patch.name : "[D] " + patch.name);

        if (patch.version.empty()) {
            out.append(QStringLiteral("%1\n").arg(type));
        } else {
            auto ver = patch.version;

            // Display container name for packed updates
            if (is_update && ver == "PACKED") {
                ver = Loader::GetFileTypeString(loader.GetFileType());
            }

            out.append(QStringLiteral("%1 (%2)\n").arg(type, QString::fromStdString(ver)));
        }
    }

    out.chop(1);
    return out;
}

QList<QStandardItem*> MakeGameListEntry(
    const std::string& path, const std::string& name, const std::size_t size,
    const std::vector<u8>& icon, Loader::AppLoader& loader, u64 program_id,
    const CompatibilityList& compatibility_list, const PlayTime::PlayTimeManager& play_time_manager,
    const FileSys::PatchManager& patch,
    const std::map<u64, std::pair<int, int>>& online_stats) {
    const auto it = FindMatchingCompatibilityEntry(compatibility_list, program_id);

    // The game list uses this as compatibility number for untested games
    QString compatibility{QStringLiteral("99")};
    if (it != compatibility_list.end()) {
        compatibility = it->second.first;
    }

    const auto file_type = loader.GetFileType();
    const auto file_type_string = QString::fromStdString(Loader::GetFileTypeString(file_type));

    QString online_text = QStringLiteral("N/A");
    auto it_stats = online_stats.find(program_id);
    if (it_stats != online_stats.end()) {
        const auto& stats = it_stats->second;
        online_text = QStringLiteral("Players: %1 | Servers: %2").arg(stats.first).arg(stats.second);
    }

    QList<QStandardItem*> list{
        new GameListItemPath(FormatGameName(path), icon, QString::fromStdString(name),
                             file_type_string, program_id),
        new GameListItemCompat(compatibility),
        new GameListItem(file_type_string),
        new GameListItemSize(size),
        new GameListItemPlayTime(play_time_manager.GetPlayTime(program_id)),
        new GameListItemOnline(online_text)};

    const auto patch_versions = GetGameListCachedObject(
        fmt::format("{:016X}", patch.GetTitleID()), "pv.txt", [&patch, &loader] {
            return FormatPatchNameVersions(patch, loader, loader.IsRomFSUpdatable());
        });
    list.insert(2, new GameListItem(patch_versions));

    return list;
}
} // Anonymous namespace

GameListWorker::GameListWorker(std::shared_ptr<FileSys::VfsFilesystem> vfs_,
                               FileSys::ManualContentProvider* provider_,
                               QVector<UISettings::GameDir>& game_dirs_,
                               const CompatibilityList& compatibility_list_,
                               const PlayTime::PlayTimeManager& play_time_manager_,
                               Core::System& system_,
                               std::shared_ptr<Core::AnnounceMultiplayerSession> session_)
    : vfs{std::move(vfs_)}, provider{provider_}, game_dirs{game_dirs_},
      compatibility_list{compatibility_list_}, play_time_manager{play_time_manager_},
      system{system_}, session{session_} {
    // We want the game list to manage our lifetime.
    setAutoDelete(false);
}

GameListWorker::~GameListWorker() {
    this->disconnect();
    stop_requested.store(true);
    processing_completed.Wait();
}

void GameListWorker::ProcessEvents(GameList* game_list) {
    while (true) {
        std::function<void(GameList*)> func;
        {
            // Lock queue to protect concurrent modification.
            std::scoped_lock lk(lock);

            // If we can't pop a function, return.
            if (queued_events.empty()) {
                return;
            }

            // Pop a function.
            func = std::move(queued_events.back());
            queued_events.pop_back();
        }

        // Run the function.
        func(game_list);
    }
}

template <typename F>
void GameListWorker::RecordEvent(F&& func) {
    {
        // Lock queue to protect concurrent modification.
        std::scoped_lock lk(lock);

        // Add the function into the front of the queue.
        queued_events.emplace_front(std::move(func));
    }

    // Data now available.
    emit DataAvailable();
}

void GameListWorker::AddTitlesToGameList(GameListDir* parent_dir, const std::map<u64, std::pair<int, int>>& online_stats) {
    using namespace FileSys;

    const auto& cache = system.GetContentProviderUnion();

    auto installed_games = cache.ListEntriesFilterOrigin(std::nullopt, TitleType::Application,
                                                         ContentRecordType::Program);

    if (parent_dir->type() == static_cast<int>(GameListItemType::SdmcDir)) {
        installed_games = cache.ListEntriesFilterOrigin(
            ContentProviderUnionSlot::SDMC, TitleType::Application, ContentRecordType::Program);
    } else if (parent_dir->type() == static_cast<int>(GameListItemType::UserNandDir)) {
        installed_games = cache.ListEntriesFilterOrigin(
            ContentProviderUnionSlot::UserNAND, TitleType::Application, ContentRecordType::Program);
    } else if (parent_dir->type() == static_cast<int>(GameListItemType::SysNandDir)) {
        installed_games = cache.ListEntriesFilterOrigin(
            ContentProviderUnionSlot::SysNAND, TitleType::Application, ContentRecordType::Program);
    }

    for (const auto& [slot, game] : installed_games) {
        if (slot == ContentProviderUnionSlot::FrontendManual) {
            continue;
        }

        const auto file = cache.GetEntryUnparsed(game.title_id, game.type);
        std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(system, file);
        if (!loader) {
            continue;
        }

        std::vector<u8> icon;
        std::string name;
        u64 program_id = 0;
        const auto result = loader->ReadProgramId(program_id);

        if (result != Loader::ResultStatus::Success) {
            continue;
        }

        const PatchManager patch{program_id, system.GetFileSystemController(),
                                 system.GetContentProvider()};
        const auto control = cache.GetEntry(game.title_id, ContentRecordType::Control);
        if (control != nullptr) {
            GetMetadataFromControlNCA(patch, *control, icon, name);
        }

        auto entry = MakeGameListEntry(file->GetFullPath(), name, file->GetSize(), icon, *loader,
                                       program_id, compatibility_list, play_time_manager, patch, online_stats);
        RecordEvent([=](GameList* game_list) { game_list->AddEntry(entry, parent_dir); });
    }
}

void GameListWorker::ScanFileSystem(ScanTarget target, const std::string& dir_path, bool deep_scan,
                                    GameListDir* parent_dir, const std::map<u64, std::pair<int, int>>& online_stats) {
    const auto callback = [this, target, parent_dir, &online_stats](const std::filesystem::path& path) -> bool {
        if (stop_requested) {
            // Breaks the callback loop.
            return false;
        }

        const auto physical_name = Common::FS::PathToUTF8String(path);
        const auto is_dir = Common::FS::IsDir(path);

        if (!is_dir &&
            (HasSupportedFileExtension(physical_name) || IsExtractedNCAMain(physical_name))) {
            // Try to get cached metadata first
            const auto* cached_metadata = GetCachedGameMetadata(physical_name);

            const auto file = vfs->OpenFile(physical_name, FileSys::OpenMode::Read);
            if (!file) {
                return true;
            }

            auto loader = Loader::GetLoader(system, file);
            if (!loader) {
                return true;
            }

            const auto file_type = loader->GetFileType();
            if (file_type == Loader::FileType::Unknown || file_type == Loader::FileType::Error) {
                return true;
            }

            u64 program_id = 0;
            const auto res2 = loader->ReadProgramId(program_id);

            if (target == ScanTarget::FillManualContentProvider) {
                if (res2 == Loader::ResultStatus::Success && file_type == Loader::FileType::NCA) {
                    provider->AddEntry(FileSys::TitleType::Application,
                                       FileSys::GetCRTypeFromNCAType(FileSys::NCA{file}.GetType()),
                                       program_id, file);
                } else if (res2 == Loader::ResultStatus::Success &&
                           (file_type == Loader::FileType::XCI ||
                            file_type == Loader::FileType::NSP)) {
                    const auto nsp = file_type == Loader::FileType::NSP
                                         ? std::make_shared<FileSys::NSP>(file)
                                         : FileSys::XCI{file}.GetSecurePartitionNSP();
                    for (const auto& title : nsp->GetNCAs()) {
                        for (const auto& entry : title.second) {
                            provider->AddEntry(entry.first.first, entry.first.second, title.first,
                                               entry.second->GetBaseFile());
                        }
                    }
                }
            } else {
                std::vector<u64> program_ids;
                loader->ReadProgramIds(program_ids);

                if (res2 == Loader::ResultStatus::Success && program_ids.size() > 1 &&
                    (file_type == Loader::FileType::XCI || file_type == Loader::FileType::NSP)) {
                    for (const auto id : program_ids) {
                        loader = Loader::GetLoader(system, file, id);
                        if (!loader) {
                            continue;
                        }

                        std::vector<u8> icon;
                        [[maybe_unused]] const auto res1 = loader->ReadIcon(icon);

                        std::string name = " ";
                        [[maybe_unused]] const auto res3 = loader->ReadTitle(name);

                        const FileSys::PatchManager patch{id, system.GetFileSystemController(),
                                                          system.GetContentProvider()};

                        auto entry = MakeGameListEntry(
                            physical_name, name, Common::FS::GetSize(physical_name), icon, *loader,
                            id, compatibility_list, play_time_manager, patch, online_stats);

                        RecordEvent(
                            [=](GameList* game_list) { game_list->AddEntry(entry, parent_dir); });
                    }
                } else {
                    // Use cached metadata if available, otherwise read from file
                    std::vector<u8> icon;
                    std::string name = " ";
                    std::size_t file_size = 0;

                    if (cached_metadata && cached_metadata->program_id == program_id) {
                        // Use cached data
                        icon = cached_metadata->icon;
                        name = cached_metadata->title;
                        file_size = cached_metadata->file_size;
                    } else {
                        // Read from file
                        [[maybe_unused]] const auto res1 = loader->ReadIcon(icon);
                        [[maybe_unused]] const auto res3 = loader->ReadTitle(name);
                        file_size = Common::FS::GetSize(physical_name);

                        // Cache it for next time
                        if (res2 == Loader::ResultStatus::Success) {
                            CacheGameMetadata(physical_name, program_id, file_type, file_size, name, icon);
                        }
                    }

                    const FileSys::PatchManager patch{program_id, system.GetFileSystemController(),
                                                      system.GetContentProvider()};

                    auto entry = MakeGameListEntry(
                        physical_name, name, file_size, icon, *loader,
                        program_id, compatibility_list, play_time_manager, patch, online_stats);

                    RecordEvent(
                        [=](GameList* game_list) { game_list->AddEntry(entry, parent_dir); });
                }
            }
        } else if (is_dir) {
            watch_list.append(QString::fromStdString(physical_name));
        }

        return true;
    };

    if (deep_scan) {
        Common::FS::IterateDirEntriesRecursively(dir_path, callback,
                                                 Common::FS::DirEntryFilter::All);
    } else {
        Common::FS::IterateDirEntries(dir_path, callback, Common::FS::DirEntryFilter::File);
    }
}

void GameListWorker::run() {
    // Load cached game metadata at the start
    LoadGameMetadataCache();

    std::map<u64, std::pair<int, int>> online_stats; // Game ID -> {player_count, server_count}
    if (session) {
        AnnounceMultiplayerRoom::RoomList room_list = session->GetRoomList();
        for (const auto& room : room_list) {
            u64 game_id = room.information.preferred_game.id;
            if (game_id != 0) {
                online_stats[game_id].first += room.members.size();
                online_stats[game_id].second++;
            }
        }
    }

    watch_list.clear();
    provider->ClearAllEntries();

    const auto DirEntryReady = [&](GameListDir* game_list_dir) {
        RecordEvent([=](GameList* game_list) { game_list->AddDirEntry(game_list_dir); });
    };

    for (UISettings::GameDir& game_dir : game_dirs) {
        if (stop_requested) {
            break;
        }

        if (game_dir.path == std::string("SDMC")) {
            auto* const game_list_dir = new GameListDir(game_dir, GameListItemType::SdmcDir);
            DirEntryReady(game_list_dir);
            AddTitlesToGameList(game_list_dir, online_stats);
        } else if (game_dir.path == std::string("UserNAND")) {
            auto* const game_list_dir = new GameListDir(game_dir, GameListItemType::UserNandDir);
            DirEntryReady(game_list_dir);
            AddTitlesToGameList(game_list_dir, online_stats);
        } else if (game_dir.path == std::string("SysNAND")) {
            auto* const game_list_dir = new GameListDir(game_dir, GameListItemType::SysNandDir);
            DirEntryReady(game_list_dir);
            AddTitlesToGameList(game_list_dir, online_stats);
        } else {
            watch_list.append(QString::fromStdString(game_dir.path));
            auto* const game_list_dir = new GameListDir(game_dir);
            DirEntryReady(game_list_dir);
            ScanFileSystem(ScanTarget::FillManualContentProvider, game_dir.path, game_dir.deep_scan,
                           game_list_dir, online_stats);
            ScanFileSystem(ScanTarget::PopulateGameList, game_dir.path, game_dir.deep_scan,
                           game_list_dir, online_stats);
        }
    }

    RecordEvent([this](GameList* game_list) { game_list->DonePopulating(watch_list); });

    // Save cached game metadata at the end
    SaveGameMetadataCache();

    processing_completed.Set();
}
