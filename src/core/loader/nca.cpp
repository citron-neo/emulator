// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include "common/hex_util.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nca.h"
#include "mbedtls/sha256.h"

namespace Loader {

AppLoader_NCA::AppLoader_NCA(FileSys::VirtualFile file_)
    : AppLoader(std::move(file_)), nca(std::make_unique<FileSys::NCA>(file)) {}

AppLoader_NCA::~AppLoader_NCA() = default;

FileType AppLoader_NCA::IdentifyType(const FileSys::VirtualFile& nca_file) {
    const FileSys::NCA nca(nca_file);

    if (nca.GetStatus() == ResultStatus::Success &&
        nca.GetType() == FileSys::NCAContentType::Program) {
        return FileType::NCA;
    }

    return FileType::Error;
}

AppLoader_NCA::LoadResult AppLoader_NCA::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    const auto result = nca->GetStatus();
    if (result != ResultStatus::Success) {
        return {result, {}};
    }

    if (nca->GetType() != FileSys::NCAContentType::Program) {
        return {ResultStatus::ErrorNCANotProgram, {}};
    }

    auto exefs = nca->GetExeFS();
    if (exefs == nullptr) {
        LOG_INFO(Loader, "No ExeFS found in NCA, looking for ExeFS from update");

        // This NCA may be a sparse base of an installed title.
        // Try to fetch the ExeFS from the installed update.
        const auto& installed = system.GetContentProvider();
        const auto update_nca = installed.GetEntry(FileSys::GetUpdateTitleID(nca->GetTitleId()),
                                                   FileSys::ContentRecordType::Program);

        if (update_nca) {
            exefs = update_nca->GetExeFS();
        }

        if (exefs == nullptr) {
            return {ResultStatus::ErrorNoExeFS, {}};
        }
    }

    directory_loader = std::make_unique<AppLoader_DeconstructedRomDirectory>(exefs, true);

    const auto load_result = directory_loader->Load(process, system);
    if (load_result.first != ResultStatus::Success) {
        return load_result;
    }

    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), nca->GetTitleId(),
        std::make_shared<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                system.GetFileSystemController()));

    is_loaded = true;
    return load_result;
}

ResultStatus AppLoader_NCA::VerifyIntegrity(std::function<bool(size_t, size_t)> progress_callback) {
    using namespace Common::Literals;

    constexpr size_t NcaFileNameWithHashLength = 36;
    constexpr size_t NcaFileNameHashLength = 32;
    constexpr size_t NcaSha256HashLength = 32;
    constexpr size_t NcaSha256HalfHashLength = NcaSha256HashLength / 2;

    // Get the file name.
    const auto name = file->GetName();

    // We won't try to verify meta NCAs.
    if (name.ends_with(".cnmt.nca")) {
        return ResultStatus::Success;
    }

    // Check if we can verify this file. NCAs should be named after their hashes.
    if (!name.ends_with(".nca") || name.size() != NcaFileNameWithHashLength) {
        LOG_WARNING(Loader, "Unable to validate NCA with name {}", name);
        return ResultStatus::ErrorIntegrityVerificationNotImplemented;
    }

    // Get the expected truncated hash of the NCA.
    const auto input_hash =
        Common::HexStringToVector(file->GetName().substr(0, NcaFileNameHashLength), false);

    // Declare buffer to read into.
    std::vector<u8> buffer(4_MiB);

    // Initialize sha256 verification context.
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);

    // Ensure we maintain a clean state on exit.
    SCOPE_EXIT {
        mbedtls_sha256_free(&ctx);
    };

    // Declare counters.
    const size_t total_size = file->GetSize();
    size_t processed_size = 0;

    // Begin iterating the file.
    while (processed_size < total_size) {
        // Refill the buffer.
        const size_t intended_read_size = std::min(buffer.size(), total_size - processed_size);
        const size_t read_size = file->Read(buffer.data(), intended_read_size, processed_size);

        // Update the hash function with the buffer contents.
        mbedtls_sha256_update_ret(&ctx, buffer.data(), read_size);

        // Update counters.
        processed_size += read_size;

        // Call the progress function.
        if (!progress_callback(processed_size, total_size)) {
            return ResultStatus::ErrorIntegrityVerificationFailed;
        }
    }

    // Finalize context and compute the output hash.
    std::array<u8, NcaSha256HashLength> output_hash;
    mbedtls_sha256_finish_ret(&ctx, output_hash.data());

    // Compare to expected.
    if (std::memcmp(input_hash.data(), output_hash.data(), NcaSha256HalfHashLength) != 0) {
        LOG_ERROR(Loader, "NCA hash mismatch detected for file {}", name);
        return ResultStatus::ErrorIntegrityVerificationFailed;
    }

    // File verified.
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadRomFS(FileSys::VirtualFile& dir) {
    if (nca == nullptr) {
        return ResultStatus::ErrorNotInitialized;
    }

    if (nca->GetRomFS() == nullptr || nca->GetRomFS()->GetSize() == 0) {
        return ResultStatus::ErrorNoRomFS;
    }

    dir = nca->GetRomFS();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadProgramId(u64& out_program_id) {
    if (nca == nullptr || nca->GetStatus() != ResultStatus::Success) {
        return ResultStatus::ErrorNotInitialized;
    }

    out_program_id = nca->GetTitleId();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadBanner(std::vector<u8>& buffer) {
    if (nca == nullptr || nca->GetStatus() != ResultStatus::Success) {
        return ResultStatus::ErrorNotInitialized;
    }

    const auto logo = nca->GetLogoPartition();
    if (logo == nullptr) {
        return ResultStatus::ErrorNoIcon;
    }

    buffer = logo->GetFile("StartupMovie.gif")->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadLogo(std::vector<u8>& buffer) {
    if (nca == nullptr || nca->GetStatus() != ResultStatus::Success) {
        return ResultStatus::ErrorNotInitialized;
    }

    const auto logo = nca->GetLogoPartition();
    if (logo == nullptr) {
        return ResultStatus::ErrorNoIcon;
    }

    buffer = logo->GetFile("NintendoLogo.png")->ReadAllBytes();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NCA::ReadNSOModules(Modules& modules) {
    if (directory_loader == nullptr) {
        return ResultStatus::ErrorNotInitialized;
    }

    return directory_loader->ReadNSOModules(modules);
}

} // namespace Loader
