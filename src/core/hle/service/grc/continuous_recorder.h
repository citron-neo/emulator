// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::GRC {

class IContinuousRecorder final : public ServiceFramework<IContinuousRecorder> {
public:
    explicit IContinuousRecorder(Core::System& system_);
    ~IContinuousRecorder() override;

private:
    Result Unknown5(InBuffer<BufferAttr_HipcPointer> buffer); // [20.2.0+] Takes 0x20-byte input
};

} // namespace Service::GRC
