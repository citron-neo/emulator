// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Service::Capture {

// This is nn::album::ImageOrientation
enum class AlbumImageOrientation {
    None,
    Rotate90,
    Rotate180,
    Rotate270,
};

// This is nn::album::AlbumReportOption
enum class AlbumReportOption : s32 {
    Disable,
    Enable,
    Unknown2,
    Unknown3,
};

enum class ContentType : u8 {
    Screenshot = 0,
    Movie = 1,
    ExtraMovie = 3,
};

enum class AlbumStorage : u8 {
    Nand,
    Sd,
};

enum class ScreenShotDecoderFlag : u64 {
    None = 0,
    EnableFancyUpsampling = 1 << 0,
    EnableBlockSmoothing = 1 << 1,
};

enum class ShimLibraryVersion : u64 {
    Version1 = 1,
};

// This is nn::capsrv::AlbumFileDateTime
struct AlbumFileDateTime {
    s16 year{};
    s8 month{};
    s8 day{};
    s8 hour{};
    s8 minute{};
    s8 second{};
    s8 unique_id{};

    friend constexpr bool operator==(const AlbumFileDateTime&, const AlbumFileDateTime&) = default;
    friend constexpr bool operator>(const AlbumFileDateTime& a, const AlbumFileDateTime& b) {
        if (a.year > b.year) {
            return true;
        }
        if (a.month > b.month) {
            return true;
        }
        if (a.day > b.day) {
            return true;
        }
        if (a.hour > b.hour) {
            return true;
        }
        if (a.minute > b.minute) {
            return true;
        }
        return a.second > b.second;
    };
    friend constexpr bool operator<(const AlbumFileDateTime& a, const AlbumFileDateTime& b) {
        if (a.year < b.year) {
            return true;
        }
        if (a.month < b.month) {
            return true;
        }
        if (a.day < b.day) {
            return true;
        }
        if (a.hour < b.hour) {
            return true;
        }
        if (a.minute < b.minute) {
            return true;
        }
        return a.second < b.second;
    };
};
static_assert(sizeof(AlbumFileDateTime) == 0x8, "AlbumFileDateTime has incorrect size.");

// This is nn::album::AlbumEntry
struct AlbumFileEntry {
    u64 size{}; // Size of the entry
    u64 hash{}; // AES256 with hardcoded key over AlbumEntry
    AlbumFileDateTime datetime{};
    AlbumStorage storage{};
    ContentType content{};
    INSERT_PADDING_BYTES(5);
    u8 unknown{}; // Set to 1 on official SW
};
static_assert(sizeof(AlbumFileEntry) == 0x20, "AlbumFileEntry has incorrect size.");

struct AlbumFileId {
    u64 application_id{};
    AlbumFileDateTime date{};
    AlbumStorage storage{};
    ContentType type{};
    INSERT_PADDING_BYTES(0x5);
    u8 unknown{};

    friend constexpr bool operator==(const AlbumFileId&, const AlbumFileId&) = default;
};
static_assert(sizeof(AlbumFileId) == 0x18, "AlbumFileId is an invalid size");

// This is nn::capsrv::AlbumEntry
struct AlbumEntry {
    u64 entry_size{};
    AlbumFileId file_id{};
};
static_assert(sizeof(AlbumEntry) == 0x20, "AlbumEntry has incorrect size.");

// This is nn::capsrv::ApplicationAlbumEntry
struct ApplicationAlbumEntry {
    u64 size{}; // Size of the entry
    u64 hash{}; // AES256 with hardcoded key over AlbumEntry
    AlbumFileDateTime datetime{};
    AlbumStorage storage{};
    ContentType content{};
    INSERT_PADDING_BYTES(5);
    u8 unknown{1}; // Set to 1 on official SW
};
static_assert(sizeof(ApplicationAlbumEntry) == 0x20, "ApplicationAlbumEntry has incorrect size.");

// This is nn::capsrv::ApplicationAlbumFileEntry
struct ApplicationAlbumFileEntry {
    ApplicationAlbumEntry entry{};
    AlbumFileDateTime datetime{};
    u64 unknown{};
};
static_assert(sizeof(ApplicationAlbumFileEntry) == 0x30,
              "ApplicationAlbumFileEntry has incorrect size.");

struct ApplicationData {
    std::array<u8, 0x400> data;
    u32 data_size;
};
static_assert(sizeof(ApplicationData) == 0x404, "ApplicationData is an invalid size");
static_assert(std::is_trivial_v<ApplicationData>,
              "ApplicationData type must be trivially copyable.");

struct ScreenShotAttribute {
    u32 unknown_0;
    AlbumImageOrientation orientation;
    u32 unknown_1;
    u32 unknown_2;
    INSERT_PADDING_BYTES_NOINIT(0x30);
};
static_assert(sizeof(ScreenShotAttribute) == 0x40, "ScreenShotAttribute is an invalid size");
static_assert(std::is_trivial_v<ScreenShotAttribute>,
              "ScreenShotAttribute type must be trivially copyable.");

struct ScreenShotDecodeOption {
    ScreenShotDecoderFlag flags{};
    INSERT_PADDING_BYTES(0x18);
};
static_assert(sizeof(ScreenShotDecodeOption) == 0x20, "ScreenShotDecodeOption is an invalid size");

struct LoadAlbumScreenShotImageOutput {
    s64 width;
    s64 height;
    ScreenShotAttribute attribute;
    INSERT_PADDING_BYTES_NOINIT(0x400);
};
static_assert(sizeof(LoadAlbumScreenShotImageOutput) == 0x450,
              "LoadAlbumScreenShotImageOutput is an invalid size");
static_assert(std::is_trivial_v<LoadAlbumScreenShotImageOutput>,
              "LoadAlbumScreenShotImageOutput type must be trivially copyable.");

struct LoadAlbumScreenShotImageOutputForApplication {
    s64 width{};
    s64 height{};
    ScreenShotAttribute attribute{};
    ApplicationData data{};
    INSERT_PADDING_BYTES(0xAC);
};
static_assert(sizeof(LoadAlbumScreenShotImageOutputForApplication) == 0x500,
              "LoadAlbumScreenShotImageOutput is an invalid size");

} // namespace Service::Capture
