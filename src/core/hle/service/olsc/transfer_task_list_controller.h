// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class INativeHandleHolder;
class IRemoteStorageController;

class ITransferTaskListController final : public ServiceFramework<ITransferTaskListController> {
public:
    explicit ITransferTaskListController(Core::System& system_);
    ~ITransferTaskListController() override;

private:
    Result GetNativeHandleHolder(Out<SharedPointer<INativeHandleHolder>> out_holder);
    Result GetRemoteStorageController(Out<SharedPointer<IRemoteStorageController>> out_controller);
    Result Unknown20();
    Result GetCurrentTransferTaskInfo(Out<std::array<u8, 0x30>> out_info, u8 unknown);
    Result FindTransferTaskInfo(Out<std::array<u8, 0x30>> out_info,
                                InBuffer<BufferAttr_HipcAutoSelect> in);
};

} // namespace Service::OLSC
