// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <zlib.h>

#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "common/swap.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

const std::array<const char*, static_cast<size_t>(Language::Count)> LANGUAGE_NAMES{{
    "AmericanEnglish",
    "BritishEnglish",
    "Japanese",
    "French",
    "German",
    "LatinAmericanSpanish",
    "Spanish",
    "Italian",
    "Dutch",
    "CanadianFrench",
    "Portuguese",
    "Russian",
    "Korean",
    "TraditionalChinese",
    "SimplifiedChinese",
    "BrazilianPortuguese",
    "Polish",
    "Thai",
}};

std::string LanguageEntry::GetApplicationName() const {
    return Common::StringFromFixedZeroTerminatedBuffer(application_name.data(),
                                                       application_name.size());
}

std::string LanguageEntry::GetDeveloperName() const {
    return Common::StringFromFixedZeroTerminatedBuffer(developer_name.data(),
                                                       developer_name.size());
}

constexpr std::array<Language, 18> language_to_codes = {{
    Language::Japanese,
    Language::AmericanEnglish,
    Language::French,
    Language::German,
    Language::Italian,
    Language::Spanish,
    Language::SimplifiedChinese,
    Language::Korean,
    Language::Dutch,
    Language::Portuguese,
    Language::Russian,
    Language::TraditionalChinese,
    Language::BritishEnglish,
    Language::CanadianFrench,
    Language::LatinAmericanSpanish,
    Language::SimplifiedChinese,
    Language::TraditionalChinese,
    Language::BrazilianPortuguese,
}};

NACP::NACP() = default;

NACP::NACP(VirtualFile file) {
    file->ReadObject(&raw);
    DecompressTitleBlock();
}

NACP::~NACP() = default;

const LanguageEntry& NACP::GetLanguageEntry() const {
    Language language =
        language_to_codes[static_cast<s32>(Settings::values.language_index.GetValue())];

    {
        const auto& language_entry = language_entries_[static_cast<u8>(language)];
        if (!language_entry.GetApplicationName().empty())
            return language_entry;
    }

    for (u8 i = 0; i < static_cast<u8>(Language::Count); ++i) {
        const auto& language_entry = language_entries_[i];
        if (!language_entry.GetApplicationName().empty())
            return language_entry;
    }

    return language_entries_[static_cast<u8>(Language::AmericanEnglish)];
}

void NACP::DecompressTitleBlock() {
    if (raw.title_compression == 0) {
        std::memcpy(language_entries_.data(), raw.language_entries.data(),
                     sizeof(LanguageEntry) * UncompressedLanguageCount);
        return;
    }

    constexpr size_t decompressed_size = sizeof(LanguageEntry) * MaxLanguageEntries;
    const u16 compressed_size = raw.compressed_title.compressed_size;

    if (compressed_size == 0 || compressed_size > raw.compressed_title.compressed_data.size()) {
        LOG_ERROR(Service_FS, "Invalid compressed title block size: 0x{:X}", compressed_size);
        return;
    }

    z_stream zstrm{};
    int ret = inflateInit2(&zstrm, -15);
    if (ret != Z_OK) {
        LOG_ERROR(Service_FS, "inflateInit2 failed: {}", ret);
        return;
    }

    std::array<u8, decompressed_size> decompressed{};
    zstrm.next_in = raw.compressed_title.compressed_data.data();
    zstrm.avail_in = static_cast<uInt>(compressed_size);
    zstrm.next_out = decompressed.data();
    zstrm.avail_out = static_cast<uInt>(decompressed_size);

    ret = inflate(&zstrm, Z_FINISH);
    inflateEnd(&zstrm);

    if (ret != Z_STREAM_END) {
        LOG_ERROR(Service_FS, "Failed to decompress NACP title block: zlib error {}", ret);
        return;
    }

    std::memcpy(language_entries_.data(), decompressed.data(), decompressed_size);
}

std::string NACP::GetApplicationName() const {
    return GetLanguageEntry().GetApplicationName();
}

std::string NACP::GetDeveloperName() const {
    return GetLanguageEntry().GetDeveloperName();
}

u64 NACP::GetTitleId() const {
    return raw.save_data_owner_id;
}

u64 NACP::GetDLCBaseTitleId() const {
    return raw.dlc_base_title_id;
}

std::string NACP::GetVersionString() const {
    return Common::StringFromFixedZeroTerminatedBuffer(raw.version_string.data(),
                                                       raw.version_string.size());
}

u64 NACP::GetDefaultNormalSaveSize() const {
    return raw.user_account_save_data_size;
}

u64 NACP::GetDefaultJournalSaveSize() const {
    return raw.user_account_save_data_journal_size;
}

bool NACP::GetUserAccountSwitchLock() const {
    return raw.user_account_switch_lock != 0;
}

u32 NACP::GetSupportedLanguages() const {
    return raw.supported_languages;
}

u64 NACP::GetDeviceSaveDataSize() const {
    return raw.device_save_data_size;
}

u32 NACP::GetParentalControlFlag() const {
    return raw.parental_control;
}

const std::array<u8, 0x20>& NACP::GetRatingAge() const {
    return raw.rating_age;
}

std::vector<u8> NACP::GetRawBytes() const {
    std::vector<u8> out(sizeof(RawNACP));
    std::memcpy(out.data(), &raw, sizeof(RawNACP));
    return out;
}
} // namespace FileSys
