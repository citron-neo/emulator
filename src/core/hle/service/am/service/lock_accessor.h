// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class ILockAccessor final : public ServiceFramework<ILockAccessor> {
public:
    explicit ILockAccessor(Core::System& system_);
    ~ILockAccessor() override;

private:
    Result TryLock(Out<bool> out_is_locked, OutCopyHandle<Kernel::KReadableEvent> out_handle,
                   bool return_handle);
    Result Unlock();
    Result GetEvent(OutCopyHandle<Kernel::KReadableEvent> out_handle);
    Result IsLocked(Out<bool> out_is_locked);

private:
    KernelHelpers::ServiceContext m_context;
    Event m_event;
    std::mutex m_mutex{};
    bool m_is_locked{};
};

} // namespace Service::AM
