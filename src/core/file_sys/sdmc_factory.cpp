// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <memory>
#include <system_error>
#include "common/fs/fs.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/sdmc_factory.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/xts_archive.h"

namespace FileSys {

// Fallback capacity used only if the real host filesystem query below fails (e.g. the backing
// path is temporarily unavailable). Previously this constant was used unconditionally as the
// entire "SD card", which reports a fixed ~33.28 GiB regardless of the actual host drive - any
// user whose real sdmc/ directory (mods, saves, installed content, ARCropolis's own SD-card
// content under atmosphere/contents/, etc.) approaches or exceeds that made every subsequent
// write look like the SD card was full to the guest, independent of how much real disk space
// was actually free.
constexpr u64 SDMC_TOTAL_SIZE = 35733492472;

SDMCFactory::SDMCFactory(VirtualDir sd_dir_, VirtualDir sd_mod_dir_)
    : sd_dir(std::move(sd_dir_)), sd_mod_dir(std::move(sd_mod_dir_)),
      contents(std::make_unique<RegisteredCache>(
          GetOrCreateDirectoryRelative(sd_dir, "/Nintendo/Contents/registered"),
          [](const VirtualFile& file, const NcaID& id) {
              return NAX{file, id}.GetDecrypted();
          })),
      placeholder(std::make_unique<PlaceholderCache>(
          GetOrCreateDirectoryRelative(sd_dir, "/Nintendo/Contents/placehld"))) {}

SDMCFactory::~SDMCFactory() = default;

VirtualDir SDMCFactory::Open() const {
    return sd_dir;
}

VirtualDir SDMCFactory::GetSDMCModificationLoadRoot(u64 title_id) const {
    // LayeredFS doesn't work on updates and title id-less homebrew
    if (title_id == 0 || (title_id & 0xFFF) == 0x800) {
        return nullptr;
    }
    return GetOrCreateDirectoryRelative(sd_mod_dir, fmt::format("/{:016X}", title_id));
}

VirtualDir SDMCFactory::GetSDMCContentDirectory() const {
    return GetOrCreateDirectoryRelative(sd_dir, "/Nintendo/Contents");
}

RegisteredCache* SDMCFactory::GetSDMCContents() const {
    return contents.get();
}

PlaceholderCache* SDMCFactory::GetSDMCPlaceholder() const {
    return placeholder.get();
}

VirtualDir SDMCFactory::GetImageDirectory() const {
    return GetOrCreateDirectoryRelative(sd_dir, "/Nintendo/Album");
}

u64 SDMCFactory::GetSDMCFreeSpace() const {
    std::error_code error;
    const auto space_info = std::filesystem::space(sd_dir->GetFullPath(), error);
    if (error) {
        // Unlike Common::FS::GetFreeSpaceSize(), the error code distinguishes a failed query
        // from a legitimate zero-byte result on a full host filesystem.
        const auto used = sd_dir->GetSize();
        return used >= SDMC_TOTAL_SIZE ? 0 : SDMC_TOTAL_SIZE - used;
    }

    return space_info.free;
}

u64 SDMCFactory::GetSDMCTotalSpace() const {
    const auto host_total_space = Common::FS::GetTotalSpaceSize(sd_dir->GetFullPath());
    return host_total_space != 0 ? host_total_space : SDMC_TOTAL_SIZE;
}

} // namespace FileSys
