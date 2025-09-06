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

class IDeliveryCacheDirectoryService final
    : public ServiceFramework<IDeliveryCacheDirectoryService> {
public:
    explicit IDeliveryCacheDirectoryService(Core::System& system_, FileSys::VirtualDir root_);
    ~IDeliveryCacheDirectoryService() override;

private:
    Result Open(const DirectoryName& dir_name_raw);
    Result Read(Out<s32> out_count,
                OutArray<DeliveryCacheDirectoryEntry, BufferAttr_HipcMapAlias> out_buffer);
    Result GetCount(Out<s32> out_count);

    FileSys::VirtualDir root;
    FileSys::VirtualDir current_dir;
};

} // namespace Service::BCAT
