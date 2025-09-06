// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class ISaveDataExporter final : public ServiceFramework<ISaveDataExporter> {
public:
    explicit ISaveDataExporter(Core::System& system_);
    ~ISaveDataExporter() override;

private:
    Result GetSaveDataInfo(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_info);
    Result GetRestSize(Out<u64> out_size);
    Result Pull(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result PullInitialData(OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
};

} // namespace Service::FileSystem