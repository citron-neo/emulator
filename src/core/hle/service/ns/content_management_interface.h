// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IContentManagementInterface final : public ServiceFramework<IContentManagementInterface> {
public:
    explicit IContentManagementInterface(Core::System& system_);
    ~IContentManagementInterface() override;

public:
    Result CalculateApplicationOccupiedSize(Out<ApplicationOccupiedSize> out_size,
                                            u64 application_id);
    Result CheckSdCardMountStatus();
    Result GetTotalSpaceSize(Out<s64> out_total_space_size, FileSys::StorageId storage_id);
    Result GetFreeSpaceSize(Out<s64> out_free_space_size, FileSys::StorageId storage_id);
};

} // namespace Service::NS
