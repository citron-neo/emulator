// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::News {

class IDownloadContext final : public ServiceFramework<IDownloadContext> {
public:
    explicit IDownloadContext(Core::System& system_);
    ~IDownloadContext() override;

private:
    Result Unknown4(); // [20.0.0+]
};

} // namespace Service::News
