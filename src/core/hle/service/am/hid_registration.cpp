// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/hid_registration.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/os/process.h"
#include "core/hle/service/sm/sm.h"
#include "hid_core/resource_manager.h"

namespace Service::AM {

HidRegistration::HidRegistration(Core::System& system, Process& process) : m_process(process) {
    m_hid_server = system.ServiceManager().GetService<HID::IHidServer>("hid", true);

    if (m_process.IsInitialized()) {
        m_hid_server->GetResourceManager()->RegisterAppletResourceUserId(m_process.GetProcessId(),
                                                                         true);
        m_hid_server->GetResourceManager()->SetAruidValidForVibration(m_process.GetProcessId(),
                                                                      true);
    }
}

HidRegistration::~HidRegistration() {
    if (m_process.IsInitialized()) {
        m_hid_server->GetResourceManager()->SetAruidValidForVibration(m_process.GetProcessId(),
                                                                      false);
        m_hid_server->GetResourceManager()->UnregisterAppletResourceUserId(
            m_process.GetProcessId());
    }
}

void HidRegistration::EnableAppletToGetInput(bool enable) {
    if (m_process.IsInitialized()) {
        m_hid_server->GetResourceManager()->SetAruidValidForVibration(m_process.GetProcessId(),
                                                                      enable);
        m_hid_server->GetResourceManager()->EnableInput(m_process.GetProcessId(), enable);
    }
}

} // namespace Service::AM
