// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class ISaveDataImporter final : public ServiceFramework<ISaveDataImporter> {
public:
    explicit ISaveDataImporter(Core::System& system_);
    ~ISaveDataImporter() override;

private:
    Result GetSaveDataInfo(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_info);
    Result GetRestSize(Out<u64> out_size);
    Result Push(InBuffer<BufferAttr_HipcMapAlias> buffer);
    Result Finalize();
};

} // namespace Service::FileSystem