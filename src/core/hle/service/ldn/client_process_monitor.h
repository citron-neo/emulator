// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::LDN {

// IClientProcessMonitor - Added in firmware 18.0.0+
// Returned by CreateClientProcessMonitor command in ldn:s and ldn:u services
// Used during the final stages of LDN (Local Network) initialization
// Reference: https://switchbrew.org/wiki/LDN_services#IClientProcessMonitor
class IClientProcessMonitor final : public ServiceFramework<IClientProcessMonitor> {
public:
    explicit IClientProcessMonitor(Core::System& system_);
    ~IClientProcessMonitor() override;

private:
    // RegisterClient - Command 0
    // Input: ClientProcessId (PID), u64 placeholder
    // Registers a client process for LDN monitoring and reference counting
    Result RegisterClient(ClientProcessId process_id, u64 placeholder);
};

} // namespace Service::LDN
