// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service {

namespace AM {

struct Applet;
struct AppletAttribute;
class ILibraryAppletProxy;
class ISystemAppletProxy;
class WindowSystem;

class IAllSystemAppletProxiesService final
    : public ServiceFramework<IAllSystemAppletProxiesService> {
public:
    explicit IAllSystemAppletProxiesService(Core::System& system_, WindowSystem& window_system);
    ~IAllSystemAppletProxiesService() override;

private:
    Result OpenSystemAppletProxy(Out<SharedPointer<ISystemAppletProxy>> out_system_applet_proxy,
                                 ClientProcessId pid,
                                 InCopyHandle<Kernel::KProcess> process_handle);
    Result OpenLibraryAppletProxy(Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy,
                                  ClientProcessId pid,
                                  InCopyHandle<Kernel::KProcess> process_handle,
                                  InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute);
    Result OpenLibraryAppletProxyOld(
        Out<SharedPointer<ILibraryAppletProxy>> out_library_applet_proxy, ClientProcessId pid,
        InCopyHandle<Kernel::KProcess> process_handle);

private:
    std::shared_ptr<Applet> GetAppletFromProcessId(ProcessId pid);

    WindowSystem& m_window_system;
};

} // namespace AM
} // namespace Service
