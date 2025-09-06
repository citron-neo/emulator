// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/vfs/vfs.h"

namespace Loader {
enum class ResultStatus : u16;
}

namespace FileSys {

class NcaReader;

/// Describes the type of content within an NCA archive.
enum class NCAContentType : u8 {
    /// Executable-related data
    Program = 0,

    /// Metadata.
    Meta = 1,

    /// Access control data.
    Control = 2,

    /// Information related to the game manual
    /// e.g. Legal information, etc.
    Manual = 3,

    /// System data.
    Data = 4,

    /// Data that can be accessed by applications.
    PublicData = 5,
};

using RightsId = std::array<u8, 0x10>;

inline bool IsDirectoryExeFS(const VirtualDir& pfs) {
    // According to switchbrew, an exefs must only contain these two files:
    return pfs->GetFile("main") != nullptr && pfs->GetFile("main.npdm") != nullptr;
}

inline bool IsDirectoryLogoPartition(const VirtualDir& pfs) {
    // NintendoLogo is the static image in the top left corner while StartupMovie is the animation
    // in the bottom right corner.
    return pfs->GetFile("NintendoLogo.png") != nullptr &&
           pfs->GetFile("StartupMovie.gif") != nullptr;
}

// An implementation of VfsDirectory that represents a Nintendo Content Archive (NCA) container.
// After construction, use GetStatus to determine if the file is valid and ready to be used.
class NCA : public ReadOnlyVfsDirectory {
public:
    explicit NCA(VirtualFile file, const NCA* base_nca = nullptr);
    ~NCA() override;

    Loader::ResultStatus GetStatus() const;

    std::vector<VirtualFile> GetFiles() const override;
    std::vector<VirtualDir> GetSubdirectories() const override;
    std::string GetName() const override;
    VirtualDir GetParentDirectory() const override;

    NCAContentType GetType() const;
    u64 GetTitleId() const;
    RightsId GetRightsId() const;
    u32 GetSDKVersion() const;
    u8 GetKeyGeneration() const;
    bool IsUpdate() const;

    VirtualFile GetRomFS() const;
    VirtualDir GetExeFS() const;

    VirtualFile GetBaseFile() const;

    VirtualDir GetLogoPartition() const;

private:
    std::vector<VirtualDir> dirs;
    std::vector<VirtualFile> files;

    VirtualFile romfs = nullptr;
    VirtualDir exefs = nullptr;
    VirtualDir logo = nullptr;
    VirtualFile file;

    Loader::ResultStatus status{};

    bool encrypted = false;
    bool is_update = false;

    Core::Crypto::KeyManager& keys;
    std::shared_ptr<NcaReader> reader;
};

} // namespace FileSys
