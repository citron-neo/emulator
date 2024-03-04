// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/all_system_applet_proxies_service.h"
#include "core/hle/service/am/service/library_applet_proxy.h"
#include "core/hle/service/am/service/system_applet_proxy.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IAllSystemAppletProxiesService::IAllSystemAppletProxiesService(Core::System& system_,
                                                               WindowSystem& window_system)
    : ServiceFramework{system_, "appletAE"}, m_window_system{window_system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {100, D<&IAllSystemAppletProxiesService::OpenSystemAppletProxy>, "OpenSystemAppletProxy"},
        {200, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxyOld>, "OpenLibraryAppletProxyOld"},
        {201, D<&IAllSystemAppletProxiesService::OpenLibraryAppletProxy>, "OpenLibraryAppletProxy"},
        {300, nullptr, "OpenOverlayAppletProxy"},
        {350, nullptr, "OpenSystemApplicationProxy"},
        {400, nullptr, "CreateSelfLibraryAppletCreatorForDevelop"},
        {410, nullptr, "GetSystemAppletControllerForDebug"},
        {1000, nullptr, "GetDebugFunctions"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAllSystemAppletProxiesService::~IAllSystemAppletProxiesService() = default;

Result IAllSystemAppletProxiesService::OpenSystemAppletProxy(
    Out<SharedPointer<ISystemAppletProxy>> out_system_applet_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle) {
    LOG_DEBUG(Service_AM, "called");

    if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
        *out_system_applet_proxy = std::make_shared<ISystemAppletProxy>(
            system, applet, process_handle.Get(), m_window_system);
        R_SUCCEED();
    } else {
        UNIMPLEMENTED();
        R_THROW(ResultUnknown);
    }
}

Result IAllSystemAppletProxiesService::OpenLibraryAppletProxy(
    Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle,
    InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute) {
    LOG_DEBUG(Service_AM, "called");

    if (const auto applet = this->GetAppletFromProcessId(pid); applet) {
        *out_library_applet_proxy = std::make_shared<ILibraryAppletProxy>(
            system, applet, process_handle.Get(), m_window_system);
        R_SUCCEED();
    } else {
        UNIMPLEMENTED();
        R_THROW(ResultUnknown);
    }
}

Result IAllSystemAppletProxiesService::OpenLibraryAppletProxyOld(
    Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
    InCopyHandle<Kernel::KProcess> process_handle) {
    LOG_DEBUG(Service_AM, "called");

    AppletAttribute attribute{};
    R_RETURN(
        this->OpenLibraryAppletProxy(out_library_applet_proxy, pid, process_handle, attribute));
}

std::shared_ptr<Applet> IAllSystemAppletProxiesService::GetAppletFromProcessId(
    ProcessId process_id) {
    return m_window_system.GetByAppletResourceUserId(process_id.pid);
}

} // namespace Service::AM
