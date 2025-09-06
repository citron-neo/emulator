// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "core/hle/kernel/k_auto_object.h"
#include "core/hle/kernel/slab_helpers.h"
#include "core/hle/result.h"

namespace Kernel {

class KernelCore;
class KSession;

class KClientSession final : public KAutoObject {
    KERNEL_AUTOOBJECT_TRAITS(KClientSession, KAutoObject);

public:
    explicit KClientSession(KernelCore& kernel);
    ~KClientSession() override;

    void Initialize(KSession* parent) {
        // Set member variables.
        m_parent = parent;
    }

    void Destroy() override;

    KSession* GetParent() const {
        return m_parent;
    }

    Result SendSyncRequest(uintptr_t address, size_t size);
    Result SendAsyncRequest(KEvent* event, uintptr_t address, size_t size);

    void OnServerClosed();

private:
    KSession* m_parent{};
};

} // namespace Kernel
