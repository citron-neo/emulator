// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "common/logging/log.h"
#include "common/polyfill_ranges.h"
#include "core/crypto/aes_util.h"
#include "core/crypto/ctr_encryption_layer.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/loader/loader.h"

#include "core/file_sys/fssystem/fssystem_compression_configuration.h"
#include "core/file_sys/fssystem/fssystem_crypto_configuration.h"
#include "core/file_sys/fssystem/fssystem_nca_file_system_driver.h"

namespace FileSys {

static u8 MasterKeyIdForKeyGeneration(u8 key_generation) {
    return std::max<u8>(key_generation, 1) - 1;
}

NCA::NCA(VirtualFile file_, const NCA* base_nca)
    : file(std::move(file_)), keys{Core::Crypto::KeyManager::Instance()} {
    if (file == nullptr) {
        status = Loader::ResultStatus::ErrorNullFile;
        return;
    }

    reader = std::make_shared<NcaReader>();
    if (Result rc =
            reader->Initialize(file, GetCryptoConfiguration(), GetNcaCompressionConfiguration());
        R_FAILED(rc)) {
        if (rc != ResultInvalidNcaSignature) {
            LOG_ERROR(Loader, "File reader errored out during header read: {:#x}",
                      rc.GetInnerValue());
        }
        status = Loader::ResultStatus::ErrorBadNCAHeader;
        return;
    }

    // Ensure we have the proper key area keys to continue.
    const u8 master_key_id = MasterKeyIdForKeyGeneration(reader->GetKeyGeneration());
    if (!keys.HasKey(Core::Crypto::S128KeyType::KeyArea, master_key_id, reader->GetKeyIndex())) {
        status = Loader::ResultStatus::ErrorMissingKeyAreaKey;
        return;
    }

    RightsId rights_id{};
    reader->GetRightsId(rights_id.data(), rights_id.size());
    if (rights_id != RightsId{}) {
        // External decryption key required; provide it here.
        u128 rights_id_u128;
        std::memcpy(rights_id_u128.data(), rights_id.data(), sizeof(rights_id));

        auto titlekey =
            keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id_u128[1], rights_id_u128[0]);
        if (titlekey == Core::Crypto::Key128{}) {
            status = Loader::ResultStatus::ErrorMissingTitlekey;
            return;
        }

        if (!keys.HasKey(Core::Crypto::S128KeyType::Titlekek, master_key_id)) {
            status = Loader::ResultStatus::ErrorMissingTitlekek;
            return;
        }

        auto titlekek = keys.GetKey(Core::Crypto::S128KeyType::Titlekek, master_key_id);
        Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(titlekek, Core::Crypto::Mode::ECB);
        cipher.Transcode(titlekey.data(), titlekey.size(), titlekey.data(),
                         Core::Crypto::Op::Decrypt);

        reader->SetExternalDecryptionKey(titlekey.data(), titlekey.size());
    }

    const s32 fs_count = reader->GetFsCount();
    NcaFileSystemDriver fs(base_nca ? base_nca->reader : nullptr, reader);
    std::vector<VirtualFile> filesystems(fs_count);
    for (s32 i = 0; i < fs_count; i++) {
        NcaFsHeaderReader header_reader;
        const Result rc = fs.OpenStorage(&filesystems[i], &header_reader, i);
        if (R_FAILED(rc)) {
            LOG_ERROR(Loader, "File reader errored out during read of section {}: {:#x}", i,
                      rc.GetInnerValue());
            status = Loader::ResultStatus::ErrorBadNCAHeader;
            return;
        }

        if (header_reader.GetFsType() == NcaFsHeader::FsType::RomFs) {
            files.push_back(filesystems[i]);
            romfs = files.back();
        }

        if (header_reader.GetFsType() == NcaFsHeader::FsType::PartitionFs) {
            auto npfs = std::make_shared<PartitionFilesystem>(filesystems[i]);
            if (npfs->GetStatus() == Loader::ResultStatus::Success) {
                dirs.push_back(npfs);
                if (IsDirectoryExeFS(npfs)) {
                    exefs = dirs.back();
                } else if (IsDirectoryLogoPartition(npfs)) {
                    logo = dirs.back();
                } else {
                    continue;
                }
            }
        }

        if (header_reader.GetEncryptionType() == NcaFsHeader::EncryptionType::AesCtrEx) {
            is_update = true;
        }
    }

    if (is_update && base_nca == nullptr) {
        status = Loader::ResultStatus::ErrorMissingBKTRBaseRomFS;
    } else {
        status = Loader::ResultStatus::Success;
    }
}

NCA::~NCA() = default;

Loader::ResultStatus NCA::GetStatus() const {
    return status;
}

std::vector<VirtualFile> NCA::GetFiles() const {
    if (status != Loader::ResultStatus::Success) {
        return {};
    }
    return files;
}

std::vector<VirtualDir> NCA::GetSubdirectories() const {
    if (status != Loader::ResultStatus::Success) {
        return {};
    }
    return dirs;
}

std::string NCA::GetName() const {
    return file->GetName();
}

VirtualDir NCA::GetParentDirectory() const {
    return file->GetContainingDirectory();
}

NCAContentType NCA::GetType() const {
    return static_cast<NCAContentType>(reader->GetContentType());
}

u64 NCA::GetTitleId() const {
    if (is_update) {
        return reader->GetProgramId() | 0x800;
    }
    return reader->GetProgramId();
}

RightsId NCA::GetRightsId() const {
    RightsId result;
    reader->GetRightsId(result.data(), result.size());
    return result;
}

u32 NCA::GetSDKVersion() const {
    return reader->GetSdkAddonVersion();
}

u8 NCA::GetKeyGeneration() const {
    return reader->GetKeyGeneration();
}

bool NCA::IsUpdate() const {
    return is_update;
}

VirtualFile NCA::GetRomFS() const {
    return romfs;
}

VirtualDir NCA::GetExeFS() const {
    return exefs;
}

VirtualFile NCA::GetBaseFile() const {
    return file;
}

VirtualDir NCA::GetLogoPartition() const {
    return logo;
}

} // namespace FileSys
