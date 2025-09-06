// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <mutex>

#include "core/hle/service/am/am_types.h"

namespace Core {
class System;
}

namespace Service {
class Process;
}

namespace Service::AM {

class WindowSystem;

enum class LaunchType {
    FrontendInitiated,
    ApplicationInitiated,
};

struct FrontendAppletParameters {
    ProgramId program_id{};
    AppletId applet_id{};
    AppletType applet_type{};
    LaunchType launch_type{};
    s32 program_index{};
    s32 previous_program_index{-1};
};

class AppletManager {
public:
    explicit AppletManager(Core::System& system);
    ~AppletManager();

    void CreateAndInsertByFrontendAppletParameters(std::unique_ptr<Process> process,
                                                   const FrontendAppletParameters& params);
    void RequestExit();
    void OperationModeChanged();

public:
    void SetWindowSystem(WindowSystem* window_system);

private:
    Core::System& m_system;

    std::mutex m_lock;
    std::condition_variable m_cv;

    WindowSystem* m_window_system{};

    FrontendAppletParameters m_pending_parameters{};
    std::unique_ptr<Process> m_pending_process{};
};

} // namespace Service::AM
