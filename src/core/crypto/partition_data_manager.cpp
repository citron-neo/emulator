// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// NOTE TO FUTURE MAINTAINERS:
// When a new version of switch cryptography is released,
// hash the new keyblob source and master key and add the hashes to
// the arrays below.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <mbedtls/sha256.h>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/crypto/key_manager.h"
#include "core/crypto/partition_data_manager.h"
#include "core/crypto/xts_encryption_layer.h"
#include "core/file_sys/kernel_executable.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/loader/loader.h"

using Common::AsArray;

namespace Core::Crypto {

struct Package2Header {
    std::array<u8, 0x100> signature;
    Key128 header_ctr;
    std::array<Key128, 4> section_ctr;
    u32_le magic;
    u32_le base_offset;
    INSERT_PADDING_BYTES(4);
    u8 version_max;
    u8 version_min;
    INSERT_PADDING_BYTES(2);
    std::array<u32_le, 4> section_size;
    std::array<u32_le, 4> section_offset;
    std::array<SHA256Hash, 4> section_hash;
};
static_assert(sizeof(Package2Header) == 0x200, "Package2Header has incorrect size.");

const u8 PartitionDataManager::MAX_KEYBLOB_SOURCE_HASH = 32;

static FileSys::VirtualFile FindFileInDirWithNames(const FileSys::VirtualDir& dir,
                                                   const std::string& name) {
    const auto upper = Common::ToUpper(name);

    for (const auto& fname : {name, name + ".bin", upper, upper + ".BIN"}) {
        if (dir->GetFile(fname) != nullptr) {
            return dir->GetFile(fname);
        }
    }

    return nullptr;
}

PartitionDataManager::PartitionDataManager(const FileSys::VirtualDir& sysdata_dir)
    : boot0(FindFileInDirWithNames(sysdata_dir, "BOOT0")),
      fuses(FindFileInDirWithNames(sysdata_dir, "fuses")),
      kfuses(FindFileInDirWithNames(sysdata_dir, "kfuses")),
      package2({
          FindFileInDirWithNames(sysdata_dir, "BCPKG2-1-Normal-Main"),
          FindFileInDirWithNames(sysdata_dir, "BCPKG2-2-Normal-Sub"),
          FindFileInDirWithNames(sysdata_dir, "BCPKG2-3-SafeMode-Main"),
          FindFileInDirWithNames(sysdata_dir, "BCPKG2-4-SafeMode-Sub"),
          FindFileInDirWithNames(sysdata_dir, "BCPKG2-5-Repair-Main"),
          FindFileInDirWithNames(sysdata_dir, "BCPKG2-6-Repair-Sub"),
      }),
      prodinfo(FindFileInDirWithNames(sysdata_dir, "PRODINFO")),
      secure_monitor(FindFileInDirWithNames(sysdata_dir, "secmon")),
      package1_decrypted(FindFileInDirWithNames(sysdata_dir, "pkg1_decr")),
      secure_monitor_bytes(secure_monitor == nullptr ? std::vector<u8>{}
                                                     : secure_monitor->ReadAllBytes()),
      package1_decrypted_bytes(package1_decrypted == nullptr ? std::vector<u8>{}
                                                             : package1_decrypted->ReadAllBytes()) {
}

PartitionDataManager::~PartitionDataManager() = default;

bool PartitionDataManager::HasBoot0() const {
    return boot0 != nullptr;
}

FileSys::VirtualFile PartitionDataManager::GetBoot0Raw() const {
    return boot0;
}

PartitionDataManager::EncryptedKeyBlob PartitionDataManager::GetEncryptedKeyblob(
    std::size_t index) const {
    if (HasBoot0() && index < NUM_ENCRYPTED_KEYBLOBS)
        return GetEncryptedKeyblobs()[index];
    return {};
}

PartitionDataManager::EncryptedKeyBlobs PartitionDataManager::GetEncryptedKeyblobs() const {
    if (!HasBoot0())
        return {};

    EncryptedKeyBlobs out{};
    for (size_t i = 0; i < out.size(); ++i)
        boot0->Read(out[i].data(), out[i].size(), 0x180000 + i * 0x200);
    return out;
}

std::vector<u8> PartitionDataManager::GetSecureMonitor() const {
    return secure_monitor_bytes;
}

std::vector<u8> PartitionDataManager::GetPackage1Decrypted() const {
    return package1_decrypted_bytes;
}

bool PartitionDataManager::HasFuses() const {
    return fuses != nullptr;
}

FileSys::VirtualFile PartitionDataManager::GetFusesRaw() const {
    return fuses;
}

std::array<u8, 16> PartitionDataManager::GetSecureBootKey() const {
    if (!HasFuses())
        return {};
    Key128 out{};
    fuses->Read(out.data(), out.size(), 0xA4);
    return out;
}

bool PartitionDataManager::HasKFuses() const {
    return kfuses != nullptr;
}

FileSys::VirtualFile PartitionDataManager::GetKFusesRaw() const {
    return kfuses;
}

bool PartitionDataManager::HasPackage2(Package2Type type) const {
    return package2.at(static_cast<size_t>(type)) != nullptr;
}

FileSys::VirtualFile PartitionDataManager::GetPackage2Raw(Package2Type type) const {
    return package2.at(static_cast<size_t>(type));
}

static bool AttemptDecrypt(const std::array<u8, 16>& key, Package2Header& header) {
    Package2Header temp = header;
    AESCipher<Key128> cipher(key, Mode::CTR);
    cipher.SetIV(header.header_ctr);
    cipher.Transcode(&temp.header_ctr, sizeof(Package2Header) - sizeof(Package2Header::signature),
                     &temp.header_ctr, Op::Decrypt);
    if (temp.magic == Common::MakeMagic('P', 'K', '2', '1')) {
        header = temp;
        return true;
    }

    return false;
}

void PartitionDataManager::DecryptPackage2(const std::array<Key128, 0x20>& package2_keys,
                                           Package2Type type) {
    FileSys::VirtualFile file = std::make_shared<FileSys::OffsetVfsFile>(
        package2[static_cast<size_t>(type)],
        package2[static_cast<size_t>(type)]->GetSize() - 0x4000, 0x4000);

    Package2Header header{};
    if (file->ReadObject(&header) != sizeof(Package2Header))
        return;

    std::size_t revision = 0xFF;
    if (header.magic != Common::MakeMagic('P', 'K', '2', '1')) {
        for (std::size_t i = 0; i < package2_keys.size(); ++i) {
            if (AttemptDecrypt(package2_keys[i], header)) {
                revision = i;
            }
        }
    }

    if (header.magic != Common::MakeMagic('P', 'K', '2', '1'))
        return;

    const auto a = std::make_shared<FileSys::OffsetVfsFile>(
        file, header.section_size[1], header.section_size[0] + sizeof(Package2Header));

    auto c = a->ReadAllBytes();

    AESCipher<Key128> cipher(package2_keys[revision], Mode::CTR);
    cipher.SetIV(header.section_ctr[1]);
    cipher.Transcode(c.data(), c.size(), c.data(), Op::Decrypt);

    const auto ini_file = std::make_shared<FileSys::VectorVfsFile>(c);
    const FileSys::INI ini{ini_file};
    if (ini.GetStatus() != Loader::ResultStatus::Success)
        return;

    for (const auto& kip : ini.GetKIPs()) {
        if (kip.GetStatus() != Loader::ResultStatus::Success)
            return;

        if (kip.GetName() != "FS" && kip.GetName() != "spl") {
            continue;
        }

        const auto& text = kip.GetTextSection();
        const auto& rodata = kip.GetRODataSection();
        const auto& data = kip.GetDataSection();

        std::vector<u8> out;
        out.reserve(text.size() + rodata.size() + data.size());
        out.insert(out.end(), text.begin(), text.end());
        out.insert(out.end(), rodata.begin(), rodata.end());
        out.insert(out.end(), data.begin(), data.end());

        if (kip.GetName() == "FS")
            package2_fs[static_cast<size_t>(type)] = std::move(out);
        else if (kip.GetName() == "spl")
            package2_spl[static_cast<size_t>(type)] = std::move(out);
    }
}

const std::vector<u8>& PartitionDataManager::GetPackage2FSDecompressed(Package2Type type) const {
    return package2_fs.at(static_cast<size_t>(type));
}

const std::vector<u8>& PartitionDataManager::GetPackage2SPLDecompressed(Package2Type type) const {
    return package2_spl.at(static_cast<size_t>(type));
}

bool PartitionDataManager::HasProdInfo() const {
    return prodinfo != nullptr;
}

FileSys::VirtualFile PartitionDataManager::GetProdInfoRaw() const {
    return prodinfo;
}

void PartitionDataManager::DecryptProdInfo(std::array<u8, 0x20> bis_key) {
    if (prodinfo == nullptr)
        return;

    prodinfo_decrypted = std::make_shared<XTSEncryptionLayer>(prodinfo, bis_key);
}

FileSys::VirtualFile PartitionDataManager::GetDecryptedProdInfo() const {
    return prodinfo_decrypted;
}

std::array<u8, 576> PartitionDataManager::GetETicketExtendedKek() const {
    std::array<u8, 0x240> out{};
    if (prodinfo_decrypted != nullptr)
        prodinfo_decrypted->Read(out.data(), out.size(), 0x3890);
    return out;
}
} // namespace Core::Crypto
