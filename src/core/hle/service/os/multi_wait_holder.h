// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/intrusive_list.h"

namespace Kernel {
class KSynchronizationObject;
} // namespace Kernel

namespace Service {

class MultiWait;

class MultiWaitHolder {
public:
    explicit MultiWaitHolder(Kernel::KSynchronizationObject* native_handle)
        : m_native_handle(native_handle) {}

    void LinkToMultiWait(MultiWait* multi_wait);
    void UnlinkFromMultiWait();

    void SetUserData(uintptr_t user_data) {
        m_user_data = user_data;
    }

    uintptr_t GetUserData() const {
        return m_user_data;
    }

    Kernel::KSynchronizationObject* GetNativeHandle() const {
        return m_native_handle;
    }

private:
    friend class MultiWait;
    Common::IntrusiveListNode m_list_node{};
    MultiWait* m_multi_wait{};
    Kernel::KSynchronizationObject* m_native_handle{};
    uintptr_t m_user_data{};
};

} // namespace Service
