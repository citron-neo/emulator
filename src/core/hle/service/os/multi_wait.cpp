// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_hardware_timer.h"
#include "core/hle/kernel/k_synchronization_object.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_common.h"
#include "core/hle/service/os/multi_wait.h"

namespace Service {

MultiWait::MultiWait() = default;
MultiWait::~MultiWait() = default;

MultiWaitHolder* MultiWait::WaitAny(Kernel::KernelCore& kernel) {
    return this->TimedWaitImpl(kernel, -1);
}

MultiWaitHolder* MultiWait::TryWaitAny(Kernel::KernelCore& kernel) {
    return this->TimedWaitImpl(kernel, 0);
}

MultiWaitHolder* MultiWait::TimedWaitAny(Kernel::KernelCore& kernel, s64 timeout_ns) {
    return this->TimedWaitImpl(kernel, kernel.HardwareTimer().GetTick() + timeout_ns);
}

MultiWaitHolder* MultiWait::TimedWaitImpl(Kernel::KernelCore& kernel, s64 timeout_tick) {
    std::array<MultiWaitHolder*, Kernel::Svc::ArgumentHandleCountMax> holders{};
    std::array<Kernel::KSynchronizationObject*, Kernel::Svc::ArgumentHandleCountMax> objects{};

    s32 out_index = -1;
    s32 num_objects = 0;

    for (auto it = m_wait_list.begin(); it != m_wait_list.end(); it++) {
        ASSERT(num_objects < Kernel::Svc::ArgumentHandleCountMax);
        holders[num_objects] = std::addressof(*it);
        objects[num_objects] = it->GetNativeHandle();
        num_objects++;
    }

    Kernel::KSynchronizationObject::Wait(kernel, std::addressof(out_index), objects.data(),
                                         num_objects, timeout_tick);

    if (out_index == -1) {
        return nullptr;
    } else {
        return holders[out_index];
    }
}

void MultiWait::MoveAll(MultiWait* other) {
    while (!other->m_wait_list.empty()) {
        MultiWaitHolder& holder = other->m_wait_list.front();
        holder.UnlinkFromMultiWait();
        holder.LinkToMultiWait(this);
    }
}

} // namespace Service
