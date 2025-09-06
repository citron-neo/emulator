// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/lock_accessor.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ILockAccessor::ILockAccessor(Core::System& system_)
    : ServiceFramework{system_, "ILockAccessor"}, m_context{system_, "ILockAccessor"},
      m_event{m_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, D<&ILockAccessor::TryLock>, "TryLock"},
        {2, D<&ILockAccessor::Unlock>, "Unlock"},
        {3, D<&ILockAccessor::GetEvent>, "GetEvent"},
        {4, D<&ILockAccessor::IsLocked>, "IsLocked"},
    };
    // clang-format on

    RegisterHandlers(functions);

    m_event.Signal();
}

ILockAccessor::~ILockAccessor() = default;

Result ILockAccessor::TryLock(Out<bool> out_is_locked,
                              OutCopyHandle<Kernel::KReadableEvent> out_handle,
                              bool return_handle) {
    LOG_INFO(Service_AM, "called, return_handle={}", return_handle);

    {
        std::scoped_lock lk{m_mutex};
        if (m_is_locked) {
            *out_is_locked = false;
        } else {
            m_is_locked = true;
            *out_is_locked = true;
        }
    }

    if (return_handle) {
        *out_handle = m_event.GetHandle();
    }

    R_SUCCEED();
}

Result ILockAccessor::Unlock() {
    LOG_INFO(Service_AM, "called");

    {
        std::scoped_lock lk{m_mutex};
        m_is_locked = false;
    }

    m_event.Signal();
    R_SUCCEED();
}

Result ILockAccessor::GetEvent(OutCopyHandle<Kernel::KReadableEvent> out_handle) {
    LOG_INFO(Service_AM, "called");
    *out_handle = m_event.GetHandle();
    R_SUCCEED();
}

Result ILockAccessor::IsLocked(Out<bool> out_is_locked) {
    LOG_INFO(Service_AM, "called");
    std::scoped_lock lk{m_mutex};
    *out_is_locked = m_is_locked;
    R_SUCCEED();
}

} // namespace Service::AM
