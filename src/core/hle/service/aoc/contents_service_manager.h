// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
class KReadableEvent;
}

namespace Service::AOC {

class IAsyncData;

class IContentsServiceManager final : public ServiceFramework<IContentsServiceManager> {
public:
    explicit IContentsServiceManager(Core::System& system_);
    ~IContentsServiceManager() override;

    Result RequestContentsAuthorizationToken(OutInterface<IAsyncData> out_async_data,
                                              u64 unknown,
                                              InBuffer<BufferAttr_HipcMapAlias> in_buffer);

private:
    KernelHelpers::ServiceContext service_context;
};

class IAsyncData final : public ServiceFramework<IAsyncData> {
public:
    explicit IAsyncData(Core::System& system_);
    ~IAsyncData() override;

    Result GetSize(Out<u64> out_size);
    Result Read(OutBuffer<BufferAttr_HipcMapAlias> out_buffer, u64 offset, u64 size);
    Result Cancel();
    Result GetSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);

private:
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* async_event;
    std::vector<u8> data_buffer;
    bool is_complete;
};

} // namespace Service::AOC