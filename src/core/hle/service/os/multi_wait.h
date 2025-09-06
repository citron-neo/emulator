// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/os/multi_wait_holder.h"

namespace Kernel {
class KernelCore;
}

namespace Service {

class MultiWait final {
public:
    explicit MultiWait();
    ~MultiWait();

public:
    MultiWaitHolder* WaitAny(Kernel::KernelCore& kernel);
    MultiWaitHolder* TryWaitAny(Kernel::KernelCore& kernel);
    MultiWaitHolder* TimedWaitAny(Kernel::KernelCore& kernel, s64 timeout_ns);
    // TODO: SdkReplyAndReceive?

    void MoveAll(MultiWait* other);

private:
    MultiWaitHolder* TimedWaitImpl(Kernel::KernelCore& kernel, s64 timeout_tick);

private:
    friend class MultiWaitHolder;
    using ListType = Common::IntrusiveListMemberTraits<&MultiWaitHolder::m_list_node>::ListType;
    ListType m_wait_list{};
};

} // namespace Service
