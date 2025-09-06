// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/hid/applet_resource.h"
#include "hid_core/resource_manager.h"

namespace Service::HID {

IAppletResource::IAppletResource(Core::System& system_, std::shared_ptr<ResourceManager> resource,
                                 u64 applet_resource_user_id)
    : ServiceFramework{system_, "IAppletResource"}, aruid{applet_resource_user_id},
      resource_manager{resource} {
    static const FunctionInfo functions[] = {
        {0, C<&IAppletResource::GetSharedMemoryHandle>, "GetSharedMemoryHandle"},
    };
    RegisterHandlers(functions);
}

IAppletResource::~IAppletResource() {
    resource_manager->FreeAppletResourceId(aruid);
}

Result IAppletResource::GetSharedMemoryHandle(
    OutCopyHandle<Kernel::KSharedMemory> out_shared_memory_handle) {
    const auto result = resource_manager->GetSharedMemoryHandle(out_shared_memory_handle, aruid);

    LOG_DEBUG(Service_HID, "called, applet_resource_user_id={}, result=0x{:X}", aruid, result.raw);
    R_RETURN(result);
}

} // namespace Service::HID
