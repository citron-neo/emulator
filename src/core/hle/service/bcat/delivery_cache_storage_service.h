// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/file_sys/vfs/vfs.h"
#include "core/hle/service/bcat/bcat_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BCAT {
class IDeliveryCacheFileService;
class IDeliveryCacheDirectoryService;

class IDeliveryCacheStorageService final : public ServiceFramework<IDeliveryCacheStorageService> {
public:
    explicit IDeliveryCacheStorageService(Core::System& system_, FileSys::VirtualDir root_);
    ~IDeliveryCacheStorageService() override;

private:
    Result CreateFileService(OutInterface<IDeliveryCacheFileService> out_interface);
    Result CreateDirectoryService(OutInterface<IDeliveryCacheDirectoryService> out_interface);
    Result EnumerateDeliveryCacheDirectory(
        Out<s32> out_directory_count,
        OutArray<DirectoryName, BufferAttr_HipcMapAlias> out_directories);

    FileSys::VirtualDir root;
    std::vector<DirectoryName> entries;
    std::size_t next_read_index = 0;
};

} // namespace Service::BCAT
