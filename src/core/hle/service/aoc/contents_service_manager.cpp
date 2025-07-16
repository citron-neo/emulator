// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/aoc/contents_service_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/kernel/k_event.h"
#include "common/logging/log.h"

namespace Service::AOC {

IContentsServiceManager::IContentsServiceManager(Core::System& system_)
    : ServiceFramework{system_, "IContentsServiceManager"},
      service_context{system_, "IContentsServiceManager"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IContentsServiceManager::RequestContentsAuthorizationToken>, "RequestContentsAuthorizationToken"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IContentsServiceManager::~IContentsServiceManager() = default;

Result IContentsServiceManager::RequestContentsAuthorizationToken(OutInterface<IAsyncData> out_async_data,
                                                                   u64 unknown,
                                                                   InBuffer<BufferAttr_HipcMapAlias> in_buffer) {
    LOG_WARNING(Service_AOC, "(STUBBED) called with unknown={:016X}, buffer_size={}", unknown, in_buffer.size());

    // Create a new IAsyncData interface to handle the authorization token request
    *out_async_data = std::make_shared<IAsyncData>(system);

    R_SUCCEED();
}

IAsyncData::IAsyncData(Core::System& system_)
    : ServiceFramework{system_, "IAsyncData"},
      service_context{system_, "IAsyncData"},
      is_complete{false} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAsyncData::GetSize>, "GetSize"},
        {1, D<&IAsyncData::Read>, "Read"},
        {2, D<&IAsyncData::Cancel>, "Cancel"},
        {3, D<&IAsyncData::GetSystemEvent>, "GetSystemEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);

    async_event = service_context.CreateEvent("IAsyncData:AsyncEvent");

    // Simulate completion immediately for stub
    data_buffer.resize(0x100, 0);
    is_complete = true;
    async_event->Signal();
}

IAsyncData::~IAsyncData() {
    service_context.CloseEvent(async_event);
}

Result IAsyncData::GetSize(Out<u64> out_size) {
    LOG_DEBUG(Service_AOC, "called");

    *out_size = data_buffer.size();

    R_SUCCEED();
}

Result IAsyncData::Read(OutBuffer<BufferAttr_HipcMapAlias> out_buffer, u64 offset, u64 size) {
    LOG_DEBUG(Service_AOC, "called with offset={:016X}, size={:016X}", offset, size);

    if (offset >= data_buffer.size()) {
        R_SUCCEED();
    }

    const u64 read_size = std::min(size, static_cast<u64>(data_buffer.size() - offset));
    const u64 copy_size = std::min(read_size, static_cast<u64>(out_buffer.size()));

    std::memcpy(out_buffer.data(), data_buffer.data() + offset, copy_size);

    R_SUCCEED();
}

Result IAsyncData::Cancel() {
    LOG_WARNING(Service_AOC, "(STUBBED) called");

    is_complete = false;
    async_event->Clear();

    R_SUCCEED();
}

Result IAsyncData::GetSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AOC, "called");

    *out_event = &async_event->GetReadableEvent();

    R_SUCCEED();
}

} // namespace Service::AOC