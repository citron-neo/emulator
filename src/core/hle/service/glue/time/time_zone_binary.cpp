// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/glue/time/time_zone_binary.h"

namespace Service::Glue::Time {
namespace {
constexpr u64 TimeZoneBinaryId = 0x10000000000080E;

static FileSys::VirtualDir g_time_zone_binary_romfs{};
static Result g_time_zone_binary_mount_result{ResultUnknown};
static std::vector<u8> g_time_zone_scratch_space(0x2800, 0);

Result TimeZoneReadBinary(size_t& out_read_size, std::span<u8> out_buffer, size_t out_buffer_size,
                          std::string_view path) {
    R_UNLESS(g_time_zone_binary_mount_result == ResultSuccess, g_time_zone_binary_mount_result);

    auto vfs_file{g_time_zone_binary_romfs->GetFileRelative(path)};
    R_UNLESS(vfs_file, ResultUnknown);

    auto file_size{vfs_file->GetSize()};
    R_UNLESS(file_size > 0, ResultUnknown);

    R_UNLESS(file_size <= out_buffer_size, Service::PSC::Time::ResultFailed);

    out_read_size = vfs_file->Read(out_buffer.data(), file_size);
    R_UNLESS(out_read_size > 0, ResultUnknown);

    R_SUCCEED();
}
} // namespace

void ResetTimeZoneBinary() {
    g_time_zone_binary_romfs = {};
    g_time_zone_binary_mount_result = ResultUnknown;
    g_time_zone_scratch_space.clear();
    g_time_zone_scratch_space.resize(0x2800, 0);
}

Result MountTimeZoneBinary(Core::System& system) {
    ResetTimeZoneBinary();

    auto& fsc{system.GetFileSystemController()};
    std::unique_ptr<FileSys::NCA> nca{};

    auto* bis_system = fsc.GetSystemNANDContents();

    R_UNLESS(bis_system, ResultUnknown);

    nca = bis_system->GetEntry(TimeZoneBinaryId, FileSys::ContentRecordType::Data);

    if (nca) {
        g_time_zone_binary_romfs = FileSys::ExtractRomFS(nca->GetRomFS());
    }

    if (g_time_zone_binary_romfs) {
        // Validate that the romfs is readable, using invalid firmware keys can cause this to get
        // set but the files to be garbage. In that case, we want to hit the next path and
        // synthesise them instead.
        g_time_zone_binary_mount_result = ResultSuccess;
        Service::PSC::Time::LocationName name{"Etc/GMT"};
        if (!IsTimeZoneBinaryValid(name)) {
            ResetTimeZoneBinary();
        }
    }

    if (!g_time_zone_binary_romfs) {
        g_time_zone_binary_romfs = FileSys::ExtractRomFS(
            FileSys::SystemArchive::SynthesizeSystemArchive(TimeZoneBinaryId));
    }

    R_UNLESS(g_time_zone_binary_romfs, ResultUnknown);

    g_time_zone_binary_mount_result = ResultSuccess;
    R_SUCCEED();
}

void GetTimeZoneBinaryListPath(std::string& out_path) {
    if (g_time_zone_binary_mount_result != ResultSuccess) {
        return;
    }
    // out_path = fmt::format("{}:/binaryList.txt", "TimeZoneBinary");
    out_path = "/binaryList.txt";
}

void GetTimeZoneBinaryVersionPath(std::string& out_path) {
    if (g_time_zone_binary_mount_result != ResultSuccess) {
        return;
    }
    // out_path = fmt::format("{}:/version.txt", "TimeZoneBinary");
    out_path = "/version.txt";
}

void GetTimeZoneZonePath(std::string& out_path, const Service::PSC::Time::LocationName& name) {
    if (g_time_zone_binary_mount_result != ResultSuccess) {
        return;
    }
    // out_path = fmt::format("{}:/zoneinfo/{}", "TimeZoneBinary", name);
    out_path = fmt::format("/zoneinfo/{}", name.data());
}

bool IsTimeZoneBinaryValid(const Service::PSC::Time::LocationName& name) {
    std::string path{};
    GetTimeZoneZonePath(path, name);

    auto vfs_file{g_time_zone_binary_romfs->GetFileRelative(path)};
    if (!vfs_file) {
        LOG_INFO(Service_Time, "Could not find timezone file {}", path);
        return false;
    }
    return vfs_file->GetSize() != 0;
}

u32 GetTimeZoneCount() {
    std::string path{};
    GetTimeZoneBinaryListPath(path);

    size_t bytes_read{};
    if (TimeZoneReadBinary(bytes_read, g_time_zone_scratch_space, 0x2800, path) != ResultSuccess) {
        return 0;
    }
    if (bytes_read == 0) {
        return 0;
    }

    auto chars = std::span(reinterpret_cast<char*>(g_time_zone_scratch_space.data()), bytes_read);
    u32 count{};
    for (auto chr : chars) {
        if (chr == '\n') {
            count++;
        }
    }
    return count;
}

Result GetTimeZoneVersion(Service::PSC::Time::RuleVersion& out_rule_version) {
    std::string path{};
    GetTimeZoneBinaryVersionPath(path);

    auto rule_version_buffer{std::span(reinterpret_cast<u8*>(&out_rule_version),
                                       sizeof(Service::PSC::Time::RuleVersion))};
    size_t bytes_read{};
    R_TRY(TimeZoneReadBinary(bytes_read, rule_version_buffer, rule_version_buffer.size_bytes(),
                             path));

    rule_version_buffer[bytes_read] = 0;
    R_SUCCEED();
}

Result GetTimeZoneRule(std::span<const u8>& out_rule, size_t& out_rule_size,
                       const Service::PSC::Time::LocationName& name) {
    std::string path{};
    GetTimeZoneZonePath(path, name);

    size_t bytes_read{};
    R_TRY(TimeZoneReadBinary(bytes_read, g_time_zone_scratch_space,
                             g_time_zone_scratch_space.size(), path));

    out_rule = std::span(g_time_zone_scratch_space.data(), bytes_read);
    out_rule_size = bytes_read;
    R_SUCCEED();
}

Result GetTimeZoneLocationList(u32& out_count,
                               std::span<Service::PSC::Time::LocationName> out_names,
                               size_t max_names, u32 index) {
    std::string path{};
    GetTimeZoneBinaryListPath(path);

    size_t bytes_read{};
    R_TRY(TimeZoneReadBinary(bytes_read, g_time_zone_scratch_space,
                             g_time_zone_scratch_space.size(), path));

    out_count = 0;
    R_SUCCEED_IF(bytes_read == 0);

    Service::PSC::Time::LocationName current_name{};
    size_t current_name_len{};
    std::span<const u8> chars{g_time_zone_scratch_space};
    u32 name_count{};

    for (auto chr : chars) {
        if (chr == '\r') {
            continue;
        }

        if (chr == '\n') {
            if (name_count >= index) {
                out_names[out_count] = current_name;
                out_count++;
                if (out_count >= max_names) {
                    break;
                }
            }
            name_count++;
            current_name_len = 0;
            current_name = {};
            continue;
        }

        if (chr == '\0') {
            break;
        }

        R_UNLESS(current_name_len <= current_name.size() - 2, Service::PSC::Time::ResultFailed);

        current_name[current_name_len++] = chr;
    }

    R_SUCCEED();
}

} // namespace Service::Glue::Time
