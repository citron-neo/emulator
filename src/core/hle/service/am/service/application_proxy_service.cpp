// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/application_proxy.h"
#include "core/hle/service/am/service/application_proxy_service.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IApplicationProxyService::IApplicationProxyService(Core::System& system_,
                                                   WindowSystem& window_system)
    : ServiceFramework{system_, "appletOE"}, m_window_system{window_system} {
    static const FunctionInfo functions[] = {
        {0, D<&IApplicationProxyService::OpenApplicationProxy>, "OpenApplicationProxy"},
    };
    RegisterHandlers(functions);
}

IApplicationProxyService::~IApplicationProxyService() = default;

Result IApplicationProxyService::OpenApplicationProxy(
    Out<SharedPointer<IApplicationProxy>> out_application_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle) {
    LOG_DEBUG(Service_AM, "called");

    if (const auto applet = this->GetAppletFromProcessId(pid)) {
        *out_application_proxy = std::make_shared<IApplicationProxy>(
            system, applet, process_handle.Get(), m_window_system);
        R_SUCCEED();
    } else {
        UNIMPLEMENTED();
        R_THROW(ResultUnknown);
    }
}

std::shared_ptr<Applet> IApplicationProxyService::GetAppletFromProcessId(ProcessId process_id) {
    return m_window_system.GetByAppletResourceUserId(process_id.pid);
}

} // namespace Service::AM
