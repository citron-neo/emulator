// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>

#include "common/swap.h"
#include "core/file_sys/system_archive/time_zone_binary.h"
#include "core/file_sys/vfs/vfs_vector.h"

#include "nx_tzdb.h"

namespace FileSys::SystemArchive {

const static std::map<std::string, const std::map<const char*, const std::vector<u8>>&>
    tzdb_zoneinfo_dirs = {{"Africa", NxTzdb::africa},
                          {"America", NxTzdb::america},
                          {"Antarctica", NxTzdb::antarctica},
                          {"Arctic", NxTzdb::arctic},
                          {"Asia", NxTzdb::asia},
                          {"Atlantic", NxTzdb::atlantic},
                          {"Australia", NxTzdb::australia},
                          {"Brazil", NxTzdb::brazil},
                          {"Canada", NxTzdb::canada},
                          {"Chile", NxTzdb::chile},
                          {"Etc", NxTzdb::etc},
                          {"Europe", NxTzdb::europe},
                          {"Indian", NxTzdb::indian},
                          {"Mexico", NxTzdb::mexico},
                          {"Pacific", NxTzdb::pacific},
                          {"US", NxTzdb::us}};

const static std::map<std::string, const std::map<const char*, const std::vector<u8>>&>
    tzdb_america_dirs = {{"Argentina", NxTzdb::america_argentina},
                         {"Indiana", NxTzdb::america_indiana},
                         {"Kentucky", NxTzdb::america_kentucky},
                         {"North_Dakota", NxTzdb::america_north_dakota}};

static void GenerateFiles(std::vector<VirtualFile>& directory,
                          const std::map<const char*, const std::vector<u8>>& files) {
    for (const auto& [filename, data] : files) {
        const auto data_copy{data};
        const std::string filename_copy{filename};
        VirtualFile file{
            std::make_shared<VectorVfsFile>(std::move(data_copy), std::move(filename_copy))};
        directory.push_back(file);
    }
}

static std::vector<VirtualFile> GenerateZoneinfoFiles() {
    std::vector<VirtualFile> zoneinfo_files;
    GenerateFiles(zoneinfo_files, NxTzdb::zoneinfo);
    return zoneinfo_files;
}

VirtualDir TimeZoneBinary() {
    std::vector<VirtualDir> america_sub_dirs;
    for (const auto& [dir_name, files] : tzdb_america_dirs) {
        std::vector<VirtualFile> vfs_files;
        GenerateFiles(vfs_files, files);
        america_sub_dirs.push_back(std::make_shared<VectorVfsDirectory>(
            std::move(vfs_files), std::vector<VirtualDir>{}, dir_name));
    }

    std::vector<VirtualDir> zoneinfo_sub_dirs;
    for (const auto& [dir_name, files] : tzdb_zoneinfo_dirs) {
        std::vector<VirtualFile> vfs_files;
        GenerateFiles(vfs_files, files);
        if (dir_name == "America") {
            zoneinfo_sub_dirs.push_back(std::make_shared<VectorVfsDirectory>(
                std::move(vfs_files), std::move(america_sub_dirs), dir_name));
        } else {
            zoneinfo_sub_dirs.push_back(std::make_shared<VectorVfsDirectory>(
                std::move(vfs_files), std::vector<VirtualDir>{}, dir_name));
        }
    }

    std::vector<VirtualDir> zoneinfo_dir{std::make_shared<VectorVfsDirectory>(
        GenerateZoneinfoFiles(), std::move(zoneinfo_sub_dirs), "zoneinfo")};
    std::vector<VirtualFile> root_files;
    GenerateFiles(root_files, NxTzdb::base);

    return std::make_shared<VectorVfsDirectory>(std::move(root_files), std::move(zoneinfo_dir),
                                                "data");
}

} // namespace FileSys::SystemArchive
