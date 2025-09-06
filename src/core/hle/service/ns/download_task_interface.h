// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IDownloadTaskInterface final : public ServiceFramework<IDownloadTaskInterface> {
public:
    explicit IDownloadTaskInterface(Core::System& system_);
    ~IDownloadTaskInterface() override;

private:
    Result EnableAutoCommit();
    Result DisableAutoCommit();
};

} // namespace Service::NS
