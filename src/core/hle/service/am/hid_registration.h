// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

namespace Core {
class System;
}

namespace Service::HID {
class IHidServer;
}

namespace Service {
class Process;
}

namespace Service::AM {

class HidRegistration {
public:
    explicit HidRegistration(Core::System& system, Process& process);
    ~HidRegistration();

    void EnableAppletToGetInput(bool enable);

private:
    Process& m_process;
    std::shared_ptr<Service::HID::IHidServer> m_hid_server;
};

} // namespace Service::AM
