// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::News {

class INewsDataService final : public ServiceFramework<INewsDataService> {
public:
    explicit INewsDataService(Core::System& system_);
    ~INewsDataService() override;

private:
    Result Open(InBuffer<BufferAttr_HipcPointer> news_id);
    Result OpenWithNewsRecordV1(InBuffer<BufferAttr_HipcPointer> news_record_v1);
    Result Read(Out<u64> out_read_size, u64 offset, OutBuffer<BufferAttr_HipcMapAlias> out_buffer);
    Result GetSize(Out<u64> out_size);
    Result OpenWithNewsRecord(InBuffer<BufferAttr_HipcPointer> news_record);
};

} // namespace Service::News
