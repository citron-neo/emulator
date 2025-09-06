// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <mutex>

#include "common/common_types.h"
#include "core/hle/service/nvnflinger/hos_binder_driver_server.h"

namespace Service::Nvnflinger {

HosBinderDriverServer::HosBinderDriverServer() = default;
HosBinderDriverServer::~HosBinderDriverServer() = default;

s32 HosBinderDriverServer::RegisterBinder(std::shared_ptr<android::IBinder>&& binder) {
    std::scoped_lock lk{lock};

    last_id++;

    binders[last_id] = std::move(binder);

    return last_id;
}

void HosBinderDriverServer::UnregisterBinder(s32 binder_id) {
    std::scoped_lock lk{lock};

    binders.erase(binder_id);
}

std::shared_ptr<android::IBinder> HosBinderDriverServer::TryGetBinder(s32 id) const {
    std::scoped_lock lk{lock};

    if (auto search = binders.find(id); search != binders.end()) {
        return search->second;
    }

    return {};
}

} // namespace Service::Nvnflinger
