// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/am/frontend/applets.h"

namespace Core {
class System;
}

namespace Service::AM::Frontend {

// Applet-specific input pushed after CommonArguments.
struct DataEraseAppletInput {
    Common::UUID user_id;
    u32 mode;
    INSERT_PADDING_BYTES(4);
};
static_assert(sizeof(DataEraseAppletInput) == 0x18, "DataEraseAppletInput has incorrect size.");

// Output returned via PopOutData. Games compare free vs required space.
struct DataEraseAppletOutput {
    Result result;
    INSERT_PADDING_BYTES(4);
    u64 free_space_size;
    u64 required_size;
};
static_assert(sizeof(DataEraseAppletOutput) == 0x18, "DataEraseAppletOutput has incorrect size.");

class DataErase final : public FrontendApplet {
public:
    explicit DataErase(Core::System& system_, std::shared_ptr<Applet> applet_,
                       LibraryAppletMode applet_mode_);
    ~DataErase() override;

    void Initialize() override;
    Result GetStatus() const override;
    void ExecuteInteractive() override;
    void Execute() override;
    Result RequestExit() override;

private:
    void Complete(Result result, u64 free_space_size, u64 required_size);

    Common::UUID user_id{};
    u32 mode{};
    u64 program_id{};
    bool complete{};
    Result status{ResultSuccess};
};

} // namespace Service::AM::Frontend
