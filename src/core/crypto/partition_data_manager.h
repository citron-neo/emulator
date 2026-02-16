// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>
#include "common/common_types.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace Core::Crypto {

enum class Package2Type {
    NormalMain,
    NormalSub,
    SafeModeMain,
    SafeModeSub,
    RepairMain,
    RepairSub,
};

class PartitionDataManager {
public:
    static const u8 MAX_KEYBLOB_SOURCE_HASH;
    static constexpr std::size_t NUM_ENCRYPTED_KEYBLOBS = 32;
    static constexpr std::size_t ENCRYPTED_KEYBLOB_SIZE = 0xB0;

    using EncryptedKeyBlob = std::array<u8, ENCRYPTED_KEYBLOB_SIZE>;
    using EncryptedKeyBlobs = std::array<EncryptedKeyBlob, NUM_ENCRYPTED_KEYBLOBS>;

    explicit PartitionDataManager(const FileSys::VirtualDir& sysdata_dir);
    ~PartitionDataManager();

    // BOOT0
    bool HasBoot0() const;
    FileSys::VirtualFile GetBoot0Raw() const;
    EncryptedKeyBlob GetEncryptedKeyblob(std::size_t index) const;
    EncryptedKeyBlobs GetEncryptedKeyblobs() const;
    std::vector<u8> GetSecureMonitor() const;
    std::vector<u8> GetPackage1Decrypted() const;

    // Fuses
    bool HasFuses() const;
    FileSys::VirtualFile GetFusesRaw() const;
    std::array<u8, 0x10> GetSecureBootKey() const;

    // K-Fuses
    bool HasKFuses() const;
    FileSys::VirtualFile GetKFusesRaw() const;

    // Package2
    bool HasPackage2(Package2Type type = Package2Type::NormalMain) const;
    FileSys::VirtualFile GetPackage2Raw(Package2Type type = Package2Type::NormalMain) const;
    void DecryptPackage2(const std::array<std::array<u8, 16>, 0x20>& package2_keys,
                         Package2Type type);
    const std::vector<u8>& GetPackage2FSDecompressed(
        Package2Type type = Package2Type::NormalMain) const;
    const std::vector<u8>& GetPackage2SPLDecompressed(
        Package2Type type = Package2Type::NormalMain) const;

    // PRODINFO
    bool HasProdInfo() const;
    FileSys::VirtualFile GetProdInfoRaw() const;
    void DecryptProdInfo(std::array<u8, 0x20> bis_key);
    FileSys::VirtualFile GetDecryptedProdInfo() const;
    std::array<u8, 0x240> GetETicketExtendedKek() const;

private:
    FileSys::VirtualFile boot0;
    FileSys::VirtualFile fuses;
    FileSys::VirtualFile kfuses;
    std::array<FileSys::VirtualFile, 6> package2;
    FileSys::VirtualFile prodinfo;
    FileSys::VirtualFile secure_monitor;
    FileSys::VirtualFile package1_decrypted;

    // Processed
    std::array<FileSys::VirtualFile, 6> package2_decrypted;
    FileSys::VirtualFile prodinfo_decrypted;
    std::vector<u8> secure_monitor_bytes;
    std::vector<u8> package1_decrypted_bytes;
    std::array<std::vector<u8>, 6> package2_fs;
    std::array<std::vector<u8>, 6> package2_spl;
};

} // namespace Core::Crypto
