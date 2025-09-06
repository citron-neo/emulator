// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include "common/common_types.h"
#include "core/file_sys/fs_directory.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace FileSys {
class BISFactory;
class NCA;
class RegisteredCache;
class RegisteredCacheUnion;
class PlaceholderCache;
class RomFSFactory;
class SaveDataFactory;
class SDMCFactory;
class XCI;

enum class BisPartitionId : u32;
enum class ContentRecordType : u8;
enum class SaveDataSpaceId : u8;
enum class SaveDataType : u8;
enum class StorageId : u8;

struct SaveDataAttribute;
struct SaveDataSize;
} // namespace FileSys

namespace Service {

namespace SM {
class ServiceManager;
} // namespace SM

namespace FileSystem {

class RomFsController;
class SaveDataController;

enum class ContentStorageId : u32 {
    System,
    User,
    SdCard,
};

enum class ImageDirectoryId : u32 {
    NAND,
    SdCard,
};

using ProcessId = u64;
using ProgramId = u64;

class FileSystemController {
public:
    explicit FileSystemController(Core::System& system_);
    ~FileSystemController();

    Result RegisterProcess(ProcessId process_id, ProgramId program_id,
                           std::shared_ptr<FileSys::RomFSFactory>&& factory);
    Result OpenProcess(ProgramId* out_program_id,
                       std::shared_ptr<SaveDataController>* out_save_data_controller,
                       std::shared_ptr<RomFsController>* out_romfs_controller,
                       ProcessId process_id);
    void SetPackedUpdate(ProcessId process_id, FileSys::VirtualFile update_raw);

    std::shared_ptr<SaveDataController> OpenSaveDataController();

    Result OpenSDMC(FileSys::VirtualDir* out_sdmc) const;
    Result OpenBISPartition(FileSys::VirtualDir* out_bis_partition,
                            FileSys::BisPartitionId id) const;
    Result OpenBISPartitionStorage(FileSys::VirtualFile* out_bis_partition_storage,
                                   FileSys::BisPartitionId id) const;

    u64 GetFreeSpaceSize(FileSys::StorageId id) const;
    u64 GetTotalSpaceSize(FileSys::StorageId id) const;

    void SetGameCard(FileSys::VirtualFile file);
    FileSys::XCI* GetGameCard() const;

    FileSys::RegisteredCache* GetSystemNANDContents() const;
    FileSys::RegisteredCache* GetUserNANDContents() const;
    FileSys::RegisteredCache* GetSDMCContents() const;
    FileSys::RegisteredCache* GetGameCardContents() const;

    FileSys::PlaceholderCache* GetSystemNANDPlaceholder() const;
    FileSys::PlaceholderCache* GetUserNANDPlaceholder() const;
    FileSys::PlaceholderCache* GetSDMCPlaceholder() const;
    FileSys::PlaceholderCache* GetGameCardPlaceholder() const;

    FileSys::RegisteredCache* GetRegisteredCacheForStorage(FileSys::StorageId id) const;
    FileSys::PlaceholderCache* GetPlaceholderCacheForStorage(FileSys::StorageId id) const;

    FileSys::VirtualDir GetSystemNANDContentDirectory() const;
    FileSys::VirtualDir GetUserNANDContentDirectory() const;
    FileSys::VirtualDir GetSDMCContentDirectory() const;

    FileSys::VirtualDir GetNANDImageDirectory() const;
    FileSys::VirtualDir GetSDMCImageDirectory() const;

    FileSys::VirtualDir GetContentDirectory(ContentStorageId id) const;
    FileSys::VirtualDir GetImageDirectory(ImageDirectoryId id) const;

    FileSys::VirtualDir GetSDMCModificationLoadRoot(u64 title_id) const;
    FileSys::VirtualDir GetModificationLoadRoot(u64 title_id) const;
    FileSys::VirtualDir GetModificationDumpRoot(u64 title_id) const;

    FileSys::VirtualDir GetBCATDirectory(u64 title_id) const;

    // Creates the SaveData, SDMC, and BIS Factories. Should be called once and before any function
    // above is called.
    void CreateFactories(FileSys::VfsFilesystem& vfs, bool overwrite = true);

    void Reset();

private:
    std::shared_ptr<FileSys::SaveDataFactory> CreateSaveDataFactory(ProgramId program_id);

    struct Registration {
        ProgramId program_id;
        std::shared_ptr<FileSys::RomFSFactory> romfs_factory;
        std::shared_ptr<FileSys::SaveDataFactory> save_data_factory;
    };

    std::mutex registration_lock;
    std::map<ProcessId, Registration> registrations;

    std::unique_ptr<FileSys::SDMCFactory> sdmc_factory;
    std::unique_ptr<FileSys::BISFactory> bis_factory;

    std::unique_ptr<FileSys::XCI> gamecard;
    std::unique_ptr<FileSys::RegisteredCache> gamecard_registered;
    std::unique_ptr<FileSys::PlaceholderCache> gamecard_placeholder;

    Core::System& system;
};

void LoopProcess(Core::System& system);

// A class that wraps a VfsDirectory with methods that return ResultVal and Result instead of
// pointers and booleans. This makes using a VfsDirectory with switch services much easier and
// avoids repetitive code.
class VfsDirectoryServiceWrapper {
public:
    explicit VfsDirectoryServiceWrapper(FileSys::VirtualDir backing);
    ~VfsDirectoryServiceWrapper();

    /**
     * Get a descriptive name for the archive (e.g. "RomFS", "SaveData", etc.)
     */
    std::string GetName() const;

    /**
     * Create a file specified by its path
     * @param path Path relative to the Archive
     * @param size The size of the new file, filled with zeroes
     * @return Result of the operation
     */
    Result CreateFile(const std::string& path, u64 size) const;

    /**
     * Delete a file specified by its path
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    Result DeleteFile(const std::string& path) const;

    /**
     * Create a directory specified by its path
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    Result CreateDirectory(const std::string& path) const;

    /**
     * Delete a directory specified by its path
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    Result DeleteDirectory(const std::string& path) const;

    /**
     * Delete a directory specified by its path and anything under it
     * @param path Path relative to the archive
     * @return Result of the operation
     */
    Result DeleteDirectoryRecursively(const std::string& path) const;

    /**
     * Cleans the specified directory. This is similar to DeleteDirectoryRecursively,
     * in that it deletes all the contents of the specified directory, however, this
     * function does *not* delete the directory itself. It only deletes everything
     * within it.
     *
     * @param path Path relative to the archive.
     *
     * @return Result of the operation.
     */
    Result CleanDirectoryRecursively(const std::string& path) const;

    /**
     * Rename a File specified by its path
     * @param src_path Source path relative to the archive
     * @param dest_path Destination path relative to the archive
     * @return Result of the operation
     */
    Result RenameFile(const std::string& src_path, const std::string& dest_path) const;

    /**
     * Rename a Directory specified by its path
     * @param src_path Source path relative to the archive
     * @param dest_path Destination path relative to the archive
     * @return Result of the operation
     */
    Result RenameDirectory(const std::string& src_path, const std::string& dest_path) const;

    /**
     * Open a file specified by its path, using the specified mode
     * @param path Path relative to the archive
     * @param mode Mode to open the file with
     * @return Opened file, or error code
     */
    Result OpenFile(FileSys::VirtualFile* out_file, const std::string& path,
                    FileSys::OpenMode mode) const;

    /**
     * Open a directory specified by its path
     * @param path Path relative to the archive
     * @return Opened directory, or error code
     */
    Result OpenDirectory(FileSys::VirtualDir* out_directory, const std::string& path);

    /**
     * Get the type of the specified path
     * @return The type of the specified path or error code
     */
    Result GetEntryType(FileSys::DirectoryEntryType* out_entry_type, const std::string& path) const;

    /**
     * Get the timestamp of the specified path
     * @return The timestamp of the specified path or error code
     */
    Result GetFileTimeStampRaw(FileSys::FileTimeStampRaw* out_time_stamp_raw,
                               const std::string& path) const;

private:
    FileSys::VirtualDir backing;
};

} // namespace FileSystem
} // namespace Service
