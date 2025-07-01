// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service {

namespace AM {

struct Applet;
struct AppletAttribute;
class IDebugFunctions;
class ILibraryAppletCreator;
class ILibraryAppletProxy;
class IOverlayAppletProxy;
class ISystemAppletProxy;
class ISystemApplicationProxy;
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
    Result OpenOverlayAppletProxy(Out<SharedPointer<IOverlayAppletProxy>> out_overlay_applet_proxy,
                                  ClientProcessId pid,
                                  InCopyHandle<Kernel::KProcess> process_handle,
                                  InLargeData<AppletAttribute, BufferAttr_HipcMapAlias> attribute);
    Result OpenSystemApplicationProxy(Out<SharedPointer<ISystemApplicationProxy>> out_system_application_proxy,
                                      ClientProcessId pid,
                                      InCopyHandle<Kernel::KProcess> process_handle);
    Result CreateSelfLibraryAppletCreatorForDevelop(Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator,
                                                     ClientProcessId pid,
                                                     InCopyHandle<Kernel::KProcess> process_handle);
    Result GetSystemAppletControllerForDebug();
    Result GetDebugFunctions(Out<SharedPointer<IDebugFunctions>> out_debug_functions);

private:
    std::shared_ptr<Applet> GetAppletFromProcessId(ProcessId pid);

    WindowSystem& m_window_system;
};

} // namespace AM
} // namespace Service
