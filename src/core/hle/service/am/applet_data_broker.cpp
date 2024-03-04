// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"

#include "core/core.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/applet_manager.h"

namespace Service::AM {

AppletStorageChannel::AppletStorageChannel(KernelHelpers::ServiceContext& context)
    : m_event(context) {}
AppletStorageChannel::~AppletStorageChannel() = default;

void AppletStorageChannel::Push(std::shared_ptr<IStorage> storage) {
    std::scoped_lock lk{m_lock};

    m_data.emplace_back(std::move(storage));
    m_event.Signal();
}

Result AppletStorageChannel::Pop(std::shared_ptr<IStorage>* out_storage) {
    std::scoped_lock lk{m_lock};

    SCOPE_EXIT {
        if (m_data.empty()) {
            m_event.Clear();
        }
    };

    R_UNLESS(!m_data.empty(), AM::ResultNoDataInChannel);

    *out_storage = std::move(m_data.front());
    m_data.pop_front();

    R_SUCCEED();
}

Kernel::KReadableEvent* AppletStorageChannel::GetEvent() {
    return m_event.GetHandle();
}

AppletDataBroker::AppletDataBroker(Core::System& system_)
    : system(system_), context(system_, "AppletDataBroker"), in_data(context),
      interactive_in_data(context), out_data(context), interactive_out_data(context) {}

AppletDataBroker::~AppletDataBroker() = default;

} // namespace Service::AM
