// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_e_u1.h"

namespace Service::Account {

ACC_E_U1::ACC_E_U1(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_,
                   Core::System& system_)
    : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e:u1") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Reserved"}, // Placeholder for empty service
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_E_U1::~ACC_E_U1() = default;

} // namespace Service::Account