// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include "common/common_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KSharedMemory;
}

namespace Service::HID {
class ResourceManager;

class IAppletResource final : public ServiceFramework<IAppletResource> {
public:
    explicit IAppletResource(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                             u64 applet_resource_user_id);
    ~IAppletResource() override;

private:
    Result GetSharedMemoryHandle(OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle);

    u64 aruid{};
    std::shared_ptr<ResourceManager> resource_manager;
};

} // namespace Service::HID
