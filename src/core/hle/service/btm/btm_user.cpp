// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/btm/btm_user.h"
#include "core/hle/service/btm/btm_user_core.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BTM {

IBtmUser::IBtmUser(Core::System& system_) : ServiceFramework{system_, "btm:u"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IBtmUser::GetCore>, "GetCore"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IBtmUser::~IBtmUser() = default;

Result IBtmUser::GetCore(OutInterface<IBtmUserCore> out_interface) {
    LOG_WARNING(Service_BTM, "called");

    *out_interface = std::make_shared<IBtmUserCore>(system);
    R_SUCCEED();
}

} // namespace Service::BTM
