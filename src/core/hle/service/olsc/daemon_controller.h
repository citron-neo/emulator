// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/uuid.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class IDaemonController final : public ServiceFramework<IDaemonController> {
public:
    explicit IDaemonController(Core::System& system_);
    ~IDaemonController() override;

private:
    Result GetAutoTransferEnabledForAccountAndApplication(Out<bool> out_is_enabled,
                                                          Common::UUID user_id, u64 application_id);
};

} // namespace Service::OLSC
