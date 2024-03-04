// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/os/multi_wait_holder.h"

namespace Service {
class Process;
}

namespace Service::AM {

struct Applet;

class ProcessHolder : public MultiWaitHolder, public Common::IntrusiveListBaseNode<ProcessHolder> {
public:
    explicit ProcessHolder(Applet& applet, Process& process);
    ~ProcessHolder();

    Applet& GetApplet() const {
        return m_applet;
    }

    Process& GetProcess() const {
        return m_process;
    }

private:
    Applet& m_applet;
    Process& m_process;
};

} // namespace Service::AM
