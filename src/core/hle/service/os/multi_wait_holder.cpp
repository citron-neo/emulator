// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/os/multi_wait.h"
#include "core/hle/service/os/multi_wait_holder.h"

namespace Service {

void MultiWaitHolder::LinkToMultiWait(MultiWait* multi_wait) {
    if (m_multi_wait != nullptr) {
        UNREACHABLE();
    }

    m_multi_wait = multi_wait;
    m_multi_wait->m_wait_list.push_back(*this);
}

void MultiWaitHolder::UnlinkFromMultiWait() {
    if (m_multi_wait) {
        m_multi_wait->m_wait_list.erase(m_multi_wait->m_wait_list.iterator_to(*this));
        m_multi_wait = nullptr;
    }
}

} // namespace Service
