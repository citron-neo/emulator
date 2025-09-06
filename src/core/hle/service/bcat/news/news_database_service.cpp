// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/news_database_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

INewsDatabaseService::INewsDatabaseService(Core::System& system_)
    : ServiceFramework{system_, "INewsDatabaseService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&INewsDatabaseService::GetListV1>, "GetListV1"},
        {1, D<&INewsDatabaseService::Count>, "Count"},
        {2, D<&INewsDatabaseService::CountWithKey>, "CountWithKey"},
        {3, D<&INewsDatabaseService::UpdateIntegerValue>, "UpdateIntegerValue"},
        {4, D<&INewsDatabaseService::UpdateIntegerValueWithAddition>, "UpdateIntegerValueWithAddition"},
        {5, D<&INewsDatabaseService::UpdateStringValue>, "UpdateStringValue"},
        {1000, D<&INewsDatabaseService::GetList>, "GetList"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

INewsDatabaseService::~INewsDatabaseService() = default;

Result INewsDatabaseService::Count(Out<s32> out_count,
                                   InBuffer<BufferAttr_HipcPointer> buffer_data) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, buffer_size={}", buffer_data.size());
    *out_count = 0;
    R_SUCCEED();
}

Result INewsDatabaseService::UpdateIntegerValueWithAddition(
    u32 value, InBuffer<BufferAttr_HipcPointer> buffer_data_1,
    InBuffer<BufferAttr_HipcPointer> buffer_data_2) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, value={}, buffer_size_1={}, buffer_data_2={}",
                value, buffer_data_1.size(), buffer_data_2.size());
    R_SUCCEED();
}

Result INewsDatabaseService::GetList(Out<s32> out_count, u32 value,
                                     OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data,
                                     InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                                     InBuffer<BufferAttr_HipcPointer> buffer_data_2) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, value={}, buffer_size_1={}, buffer_data_2={}",
                value, buffer_data_1.size(), buffer_data_2.size());
    *out_count = 0;
    R_SUCCEED();
}

Result INewsDatabaseService::GetListV1(Out<s32> out_count, u32 value,
                                       OutBuffer<BufferAttr_HipcMapAlias> out_buffer_data,
                                       InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                                       InBuffer<BufferAttr_HipcPointer> buffer_data_2) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, value={}, buffer_size_1={}, buffer_data_2={}",
                value, buffer_data_1.size(), buffer_data_2.size());
    *out_count = 0;
    R_SUCCEED();
}

Result INewsDatabaseService::CountWithKey(Out<s32> out_count,
                                          InBuffer<BufferAttr_HipcPointer> key_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, key_buffer_size={}", key_buffer.size());
    *out_count = 0;
    R_SUCCEED();
}

Result INewsDatabaseService::UpdateIntegerValue(u32 value,
                                                InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                                                InBuffer<BufferAttr_HipcPointer> buffer_data_2) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, value={}, buffer_size_1={}, buffer_data_2={}",
                value, buffer_data_1.size(), buffer_data_2.size());
    R_SUCCEED();
}

Result INewsDatabaseService::UpdateStringValue(InBuffer<BufferAttr_HipcPointer> buffer_data_1,
                                               InBuffer<BufferAttr_HipcPointer> buffer_data_2,
                                               InBuffer<BufferAttr_HipcPointer> buffer_data_3) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, buffer_size_1={}, buffer_size_2={}, buffer_size_3={}",
                buffer_data_1.size(), buffer_data_2.size(), buffer_data_3.size());
    R_SUCCEED();
}

} // namespace Service::News
