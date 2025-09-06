// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/intrusive/rbtree.hpp>

#include "common/common_funcs.h"
#include "core/hle/kernel/k_auto_object.h"
#include "core/hle/kernel/k_spin_lock.h"

namespace Kernel {

class KernelCore;
class KProcess;

class KAutoObjectWithListContainer {
public:
    CITRON_NON_COPYABLE(KAutoObjectWithListContainer);
    CITRON_NON_MOVEABLE(KAutoObjectWithListContainer);

    using ListType = boost::intrusive::rbtree<KAutoObjectWithList>;

    KAutoObjectWithListContainer(KernelCore& kernel) : m_lock(), m_object_list() {}

    void Initialize() {}
    void Finalize() {}

    void Register(KAutoObjectWithList* obj);
    void Unregister(KAutoObjectWithList* obj);
    size_t GetOwnedCount(KProcess* owner);

private:
    KSpinLock m_lock;
    ListType m_object_list;
};

} // namespace Kernel
