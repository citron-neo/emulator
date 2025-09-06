// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/news_data_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

INewsDataService::INewsDataService(Core::System& system_)
    : ServiceFramework{system_, "INewsDataService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&INewsDataService::Open>, "Open"},
        {1, D<&INewsDataService::OpenWithNewsRecordV1>, "OpenWithNewsRecordV1"},
        {2, D<&INewsDataService::Read>, "Read"},
        {3, D<&INewsDataService::GetSize>, "GetSize"},
        {1001, D<&INewsDataService::OpenWithNewsRecord>, "OpenWithNewsRecord"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

INewsDataService::~INewsDataService() = default;

Result INewsDataService::Open(InBuffer<BufferAttr_HipcPointer> news_id) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, news_id_size={}", news_id.size());
    R_SUCCEED();
}

Result INewsDataService::OpenWithNewsRecordV1(InBuffer<BufferAttr_HipcPointer> news_record_v1) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, news_record_v1_size={}", news_record_v1.size());
    R_SUCCEED();
}

Result INewsDataService::Read(Out<u64> out_read_size, u64 offset, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, offset={:016X}, buffer_size={:016X}", offset, out_buffer.size());
    *out_read_size = 0;
    R_SUCCEED();
}

Result INewsDataService::GetSize(Out<u64> out_size) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    *out_size = 0;
    R_SUCCEED();
}

Result INewsDataService::OpenWithNewsRecord(InBuffer<BufferAttr_HipcPointer> news_record) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called, news_record_size={}", news_record.size());
    R_SUCCEED();
}

} // namespace Service::News
