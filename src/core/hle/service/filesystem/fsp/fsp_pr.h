// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FileSystem {

class FSP_PR final : public ServiceFramework<FSP_PR> {
public:
    explicit FSP_PR(Core::System& system_);
    ~FSP_PR() override;

private:
    Result RegisterProgram(u64 program_id, u64 storage_id, InBuffer<BufferAttr_HipcMapAlias> data,
                          InBuffer<BufferAttr_HipcMapAlias> meta);
    Result UnregisterProgram(u64 program_id);
    Result SetCurrentProcess(ClientProcessId process_id);
    Result SetEnabledProgramVerification(bool enabled);
};

} // namespace Service::FileSystem
