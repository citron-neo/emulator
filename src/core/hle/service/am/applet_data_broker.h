// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <deque>
#include <memory>
#include <mutex>

#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"

union Result;

namespace Service::AM {

struct Applet;
class IStorage;

class AppletStorageChannel {
public:
    explicit AppletStorageChannel(KernelHelpers::ServiceContext& ctx);
    ~AppletStorageChannel();

    void Push(std::shared_ptr<IStorage> storage);
    Result Pop(std::shared_ptr<IStorage>* out_storage);
    Kernel::KReadableEvent* GetEvent();

private:
    std::mutex m_lock{};
    std::deque<std::shared_ptr<IStorage>> m_data{};
    Event m_event;
};

class AppletDataBroker {
public:
    explicit AppletDataBroker(Core::System& system_);
    ~AppletDataBroker();

    AppletStorageChannel& GetInData() {
        return in_data;
    }

    AppletStorageChannel& GetInteractiveInData() {
        return interactive_in_data;
    }

    AppletStorageChannel& GetOutData() {
        return out_data;
    }

    AppletStorageChannel& GetInteractiveOutData() {
        return interactive_out_data;
    }

private:
    Core::System& system;
    KernelHelpers::ServiceContext context;

    AppletStorageChannel in_data;
    AppletStorageChannel interactive_in_data;
    AppletStorageChannel out_data;
    AppletStorageChannel interactive_out_data;
};

} // namespace Service::AM
