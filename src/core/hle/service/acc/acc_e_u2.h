// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/acc/acc.h"

namespace Service::Account {

class ACC_E_U2 final : public Module::Interface {
public:
    explicit ACC_E_U2(std::shared_ptr<Module> module_,
                      std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_);
    ~ACC_E_U2() override;
};

} // namespace Service::Account