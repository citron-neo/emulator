// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;

class IAppletCommonFunctions final : public ServiceFramework<IAppletCommonFunctions> {
public:
    explicit IAppletCommonFunctions(Core::System& system_, std::shared_ptr<Applet> applet_);
    ~IAppletCommonFunctions() override;

private:
    Result SetHomeButtonDoubleClickEnabled(bool home_button_double_click_enabled);
    Result GetHomeButtonDoubleClickEnabled(Out<bool> out_home_button_double_click_enabled);
    Result SetCpuBoostRequestPriority(s32 priority);
    Result GetCurrentApplicationId(Out<u64> out_application_id);

    const std::shared_ptr<Applet> applet;
};

} // namespace Service::AM
