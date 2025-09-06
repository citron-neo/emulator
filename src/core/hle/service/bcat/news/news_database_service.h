// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::News {

class INewsDatabaseService final : public ServiceFramework<INewsDatabaseService> {
public:
    explicit INewsDatabaseService(Core::System& system_);
    ~INewsDatabaseService() override;

private:
    Result Count(Out<s32> out_count, InBuffer<BufferAttr_HipcPointer> buffer_data);

    Result UpdateIntegerValueWithAddition(u32 value, InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                                          InBuffer<BufferAttr_HipcPointer> buffer_data_2);

    Result GetList(Out<s32> out_count, u32 value,
                   OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data,
                   InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                   InBuffer<BufferAttr_HipcPointer> buffer_data_2);

    // Additional News Database functions
    Result GetListV1(Out<s32> out_count, u32 value,
                     OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data,
                     InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                     InBuffer<BufferAttr_HipcPointer> buffer_data_2);
    Result CountWithKey(Out<s32> out_count, InBuffer<BufferAttr_HipcPointer> key_buffer);
    Result UpdateIntegerValue(u32 value, InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                             InBuffer<BufferAttr_HipcPointer> buffer_data_2);
    Result UpdateStringValue(InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                            InBuffer<BufferAttr_HipcPointer> buffer_data_2,
                            InBuffer<BufferAttr_HipcPointer> buffer_data_3);
};

} // namespace Service::News
