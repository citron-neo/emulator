// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/final_output_recorder_manager_for_applet.h"

namespace Service::Audio {

IFinalOutputRecorderManagerForApplet::IFinalOutputRecorderManagerForApplet(Core::System& system_)
    : ServiceFramework{system_, "audrec:a"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestSuspend"},
        {1, nullptr, "RequestResume"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IFinalOutputRecorderManagerForApplet::~IFinalOutputRecorderManagerForApplet() = default;

} // namespace Service::Audio
