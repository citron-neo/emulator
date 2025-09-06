// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/algorithm/string.hpp>
#include "common/common_types.h"
#include "common/literals.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/fs_filesystem.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/loader/nca.h"

namespace ContentManager {

enum class InstallResult {
    Success,
    Overwrite,
    Failure,
    BaseInstallAttempted,
};

enum class GameVerificationResult {
    Success,
    Failed,
    NotImplemented,
};

/**
 * \brief Removes a single installed DLC
 * \param fs_controller [FileSystemController] reference from the Core::System instance
 * \param title_id Unique title ID representing the DLC which will be removed
 * \return 'true' if successful
 */
inline bool RemoveDLC(const Service::FileSystem::FileSystemController& fs_controller,
                      const u64 title_id) {
    return fs_controller.GetUserNANDContents()->RemoveExistingEntry(title_id) ||
           fs_controller.GetSDMCContents()->RemoveExistingEntry(title_id);
}

/**
 * \brief Removes all DLC for a game
 * \param system Reference to the system instance
 * \param program_id Program ID for the game that will have all of its DLC removed
 * \return Number of DLC removed
 */
inline size_t RemoveAllDLC(Core::System& system, const u64 program_id) {
    size_t count{};
    const auto& fs_controller = system.GetFileSystemController();
    const auto dlc_entries = system.GetContentProvider().ListEntriesFilter(
        FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);
    std::vector<u64> program_dlc_entries;

    for (const auto& entry : dlc_entries) {
        if (FileSys::GetBaseTitleID(entry.title_id) == program_id) {
            program_dlc_entries.push_back(entry.title_id);
        }
    }

    for (const auto& entry : program_dlc_entries) {
        if (RemoveDLC(fs_controller, entry)) {
            ++count;
        }
    }
    return count;
}

/**
 * \brief Removes the installed update for a game
 * \param fs_controller [FileSystemController] reference from the Core::System instance
 * \param program_id Program ID for the game that will have its installed update removed
 * \return 'true' if successful
 */
inline bool RemoveUpdate(const Service::FileSystem::FileSystemController& fs_controller,
                         const u64 program_id) {
    const auto update_id = program_id | 0x800;
    return fs_controller.GetUserNANDContents()->RemoveExistingEntry(update_id) ||
           fs_controller.GetSDMCContents()->RemoveExistingEntry(update_id);
}

/**
 * \brief Removes the base content for a game
 * \param fs_controller [FileSystemController] reference from the Core::System instance
 * \param program_id Program ID for the game that will have its base content removed
 * \return 'true' if successful
 */
inline bool RemoveBaseContent(const Service::FileSystem::FileSystemController& fs_controller,
                              const u64 program_id) {
    return fs_controller.GetUserNANDContents()->RemoveExistingEntry(program_id) ||
           fs_controller.GetSDMCContents()->RemoveExistingEntry(program_id);
}

/**
 * \brief Removes a mod for a game
 * \param fs_controller [FileSystemController] reference from the Core::System instance
 * \param program_id Program ID for the game where [mod_name] will be removed
 * \param mod_name The name of a mod as given by FileSys::PatchManager::GetPatches. This corresponds
 * with the name of the mod's directory in a game's load folder.
 * \return 'true' if successful
 */
inline bool RemoveMod(const Service::FileSystem::FileSystemController& fs_controller,
                      const u64 program_id, const std::string& mod_name) {
    // Check general Mods (LayeredFS and IPS)
    const auto mod_dir = fs_controller.GetModificationLoadRoot(program_id);
    if (mod_dir != nullptr) {
        return mod_dir->DeleteSubdirectoryRecursive(mod_name);
    }

    // Check SDMC mod directory (RomFS LayeredFS)
    const auto sdmc_mod_dir = fs_controller.GetSDMCModificationLoadRoot(program_id);
    if (sdmc_mod_dir != nullptr) {
        return sdmc_mod_dir->DeleteSubdirectoryRecursive(mod_name);
    }

    return false;
}

/**
 * \brief Installs an NSP
 * \param system Reference to the system instance
 * \param vfs Reference to the VfsFilesystem instance in Core::System
 * \param filename Path to the NSP file
 * \param callback Callback to report the progress of the installation. The first size_t
 * parameter is the total size of the virtual file and the second is the current progress. If you
 * return true to the callback, it will cancel the installation as soon as possible.
 * \return [InstallResult] representing how the installation finished
 */
inline InstallResult InstallNSP(Core::System& system, FileSys::VfsFilesystem& vfs,
                                const std::string& filename,
                                const std::function<bool(size_t, size_t)>& callback) {
    const auto copy = [callback](const FileSys::VirtualFile& src, const FileSys::VirtualFile& dest,
                                 std::size_t block_size) {
        if (src == nullptr || dest == nullptr) {
            return false;
        }
        if (!dest->Resize(src->GetSize())) {
            return false;
        }

        using namespace Common::Literals;
        std::vector<u8> buffer(1_MiB);

        for (std::size_t i = 0; i < src->GetSize(); i += buffer.size()) {
            if (callback(src->GetSize(), i)) {
                dest->Resize(0);
                return false;
            }
            const auto read = src->Read(buffer.data(), buffer.size(), i);
            dest->Write(buffer.data(), read, i);
        }
        return true;
    };

    std::shared_ptr<FileSys::NSP> nsp;
    FileSys::VirtualFile file = vfs.OpenFile(filename, FileSys::OpenMode::Read);
    if (boost::to_lower_copy(file->GetName()).ends_with(std::string("nsp"))) {
        nsp = std::make_shared<FileSys::NSP>(file);
        if (nsp->IsExtractedType()) {
            return InstallResult::Failure;
        }
    } else {
        return InstallResult::Failure;
    }

    if (nsp->GetStatus() != Loader::ResultStatus::Success) {
        return InstallResult::Failure;
    }
    const auto res =
        system.GetFileSystemController().GetUserNANDContents()->InstallEntry(*nsp, true, copy);
    switch (res) {
    case FileSys::InstallResult::Success:
        return InstallResult::Success;
    case FileSys::InstallResult::OverwriteExisting:
        return InstallResult::Overwrite;
    case FileSys::InstallResult::ErrorBaseInstall:
        return InstallResult::BaseInstallAttempted;
    default:
        return InstallResult::Failure;
    }
}

/**
 * \brief Installs an NCA
 * \param vfs Reference to the VfsFilesystem instance in Core::System
 * \param filename Path to the NCA file
 * \param registered_cache Reference to the registered cache that the NCA will be installed to
 * \param title_type Type of NCA package to install
 * \param callback Callback to report the progress of the installation. The first size_t
 * parameter is the total size of the virtual file and the second is the current progress. If you
 * return true to the callback, it will cancel the installation as soon as possible.
 * \return [InstallResult] representing how the installation finished
 */
inline InstallResult InstallNCA(FileSys::VfsFilesystem& vfs, const std::string& filename,
                                FileSys::RegisteredCache& registered_cache,
                                const FileSys::TitleType title_type,
                                const std::function<bool(size_t, size_t)>& callback) {
    const auto copy = [callback](const FileSys::VirtualFile& src, const FileSys::VirtualFile& dest,
                                 std::size_t block_size) {
        if (src == nullptr || dest == nullptr) {
            return false;
        }
        if (!dest->Resize(src->GetSize())) {
            return false;
        }

        using namespace Common::Literals;
        std::vector<u8> buffer(1_MiB);

        for (std::size_t i = 0; i < src->GetSize(); i += buffer.size()) {
            if (callback(src->GetSize(), i)) {
                dest->Resize(0);
                return false;
            }
            const auto read = src->Read(buffer.data(), buffer.size(), i);
            dest->Write(buffer.data(), read, i);
        }
        return true;
    };

    const auto nca =
        std::make_shared<FileSys::NCA>(vfs.OpenFile(filename, FileSys::OpenMode::Read));
    const auto id = nca->GetStatus();

    // Game updates necessary are missing base RomFS
    if (id != Loader::ResultStatus::Success &&
        id != Loader::ResultStatus::ErrorMissingBKTRBaseRomFS) {
        return InstallResult::Failure;
    }

    const auto res = registered_cache.InstallEntry(*nca, title_type, true, copy);
    if (res == FileSys::InstallResult::Success) {
        return InstallResult::Success;
    } else if (res == FileSys::InstallResult::OverwriteExisting) {
        return InstallResult::Overwrite;
    } else {
        return InstallResult::Failure;
    }
}

/**
 * \brief Verifies the installed contents for a given ManualContentProvider
 * \param system Reference to the system instance
 * \param provider Reference to the content provider that's tracking indexed games
 * \param callback Callback to report the progress of the installation. The first size_t
 * parameter is the total size of the installed contents and the second is the current progress. If
 * you return true to the callback, it will cancel the installation as soon as possible.
 * \param firmware_only Set to true to only scan system nand NCAs (firmware), post firmware install.
 * \return A list of entries that failed to install. Returns an empty vector if successful.
 */
inline std::vector<std::string> VerifyInstalledContents(
    Core::System& system, FileSys::ManualContentProvider& provider,
    const std::function<bool(size_t, size_t)>& callback, bool firmware_only = false) {
    // Get content registries.
    auto bis_contents = system.GetFileSystemController().GetSystemNANDContents();
    auto user_contents = system.GetFileSystemController().GetUserNANDContents();

    std::vector<FileSys::RegisteredCache*> content_providers;
    if (bis_contents) {
        content_providers.push_back(bis_contents);
    }
    if (user_contents && !firmware_only) {
        content_providers.push_back(user_contents);
    }

    // Get associated NCA files.
    std::vector<FileSys::VirtualFile> nca_files;

    // Get all installed IDs.
    size_t total_size = 0;
    for (auto nca_provider : content_providers) {
        const auto entries = nca_provider->ListEntriesFilter();

        for (const auto& entry : entries) {
            auto nca_file = nca_provider->GetEntryRaw(entry.title_id, entry.type);
            if (!nca_file) {
                continue;
            }

            total_size += nca_file->GetSize();
            nca_files.push_back(std::move(nca_file));
        }
    }

    // Declare a list of file names which failed to verify.
    std::vector<std::string> failed;

    size_t processed_size = 0;
    bool cancelled = false;
    auto nca_callback = [&](size_t nca_processed, size_t nca_total) {
        cancelled = callback(total_size, processed_size + nca_processed);
        return !cancelled;
    };

    // Using the NCA loader, determine if all NCAs are valid.
    for (auto& nca_file : nca_files) {
        Loader::AppLoader_NCA nca_loader(nca_file);

        auto status = nca_loader.VerifyIntegrity(nca_callback);
        if (cancelled) {
            break;
        }
        if (status != Loader::ResultStatus::Success) {
            FileSys::NCA nca(nca_file);
            const auto title_id = nca.GetTitleId();
            std::string title_name = "unknown";

            const auto control = provider.GetEntry(FileSys::GetBaseTitleID(title_id),
                                                   FileSys::ContentRecordType::Control);
            if (control && control->GetStatus() == Loader::ResultStatus::Success) {
                const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                               provider};
                const auto [nacp, logo] = pm.ParseControlNCA(*control);
                if (nacp) {
                    title_name = nacp->GetApplicationName();
                }
            }

            if (title_id > 0) {
                failed.push_back(
                    fmt::format("{} ({:016X}) ({})", nca_file->GetName(), title_id, title_name));
            } else {
                failed.push_back(fmt::format("{} (unknown)", nca_file->GetName()));
            }
        }

        processed_size += nca_file->GetSize();
    }
    return failed;
}

/**
 * \brief Verifies the contents of a given game
 * \param system Reference to the system instance
 * \param game_path Patch to the game file
 * \param callback Callback to report the progress of the installation. The first size_t
 * parameter is the total size of the installed contents and the second is the current progress. If
 * you return true to the callback, it will cancel the installation as soon as possible.
 * \return GameVerificationResult representing how the verification process finished
 */
inline GameVerificationResult VerifyGameContents(
    Core::System& system, const std::string& game_path,
    const std::function<bool(size_t, size_t)>& callback) {
    const auto loader = Loader::GetLoader(
        system, system.GetFilesystem()->OpenFile(game_path, FileSys::OpenMode::Read));
    if (loader == nullptr) {
        return GameVerificationResult::NotImplemented;
    }

    bool cancelled = false;
    auto loader_callback = [&](size_t processed, size_t total) {
        cancelled = callback(total, processed);
        return !cancelled;
    };

    const auto status = loader->VerifyIntegrity(loader_callback);
    if (cancelled || status == Loader::ResultStatus::ErrorIntegrityVerificationNotImplemented) {
        return GameVerificationResult::NotImplemented;
    }

    if (status == Loader::ResultStatus::ErrorIntegrityVerificationFailed) {
        return GameVerificationResult::Failed;
    }
    return GameVerificationResult::Success;
}

/**
 * Checks if the keys required for decrypting firmware and games are available
 */
inline bool AreKeysPresent() {
    return !Core::Crypto::KeyManager::Instance().BaseDeriveNecessary();
}

} // namespace ContentManager
