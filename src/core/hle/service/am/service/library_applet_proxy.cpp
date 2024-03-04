// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/debug_functions.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/am/service/home_menu_functions.h"
#include "core/hle/service/am/service/library_applet_creator.h"
#include "core/hle/service/am/service/library_applet_proxy.h"
#include "core/hle/service/am/service/library_applet_self_accessor.h"
#include "core/hle/service/am/service/process_winding_controller.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ILibraryAppletProxy::ILibraryAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                         Kernel::KProcess* process, WindowSystem& window_system)
    : ServiceFramework{system_, "ILibraryAppletProxy"},
      m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ILibraryAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        {1, D<&ILibraryAppletProxy::GetSelfController>, "GetSelfController"},
        {2, D<&ILibraryAppletProxy::GetWindowController>, "GetWindowController"},
        {3, D<&ILibraryAppletProxy::GetAudioController>, "GetAudioController"},
        {4, D<&ILibraryAppletProxy::GetDisplayController>, "GetDisplayController"},
        {10, D<&ILibraryAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        {11, D<&ILibraryAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        {20, D<&ILibraryAppletProxy::OpenLibraryAppletSelfAccessor>, "OpenLibraryAppletSelfAccessor"},
        {21, D<&ILibraryAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        {22, D<&ILibraryAppletProxy::GetHomeMenuFunctions>, "GetHomeMenuFunctions"},
        {23, D<&ILibraryAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        {1000, D<&ILibraryAppletProxy::GetDebugFunctions>, "GetDebugFunctions"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ILibraryAppletProxy::~ILibraryAppletProxy() = default;

Result ILibraryAppletProxy::GetAudioController(
    Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_audio_controller = std::make_shared<IAudioController>(system);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetDisplayController(
    Out<SharedPointer<IDisplayController>> out_display_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetProcessWindingController(
    Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetDebugFunctions(
    Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_debug_functions = std::make_shared<IDebugFunctions>(system);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetWindowController(
    Out<SharedPointer<IWindowController>> out_window_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetSelfController(
    Out<SharedPointer<ISelfController>> out_self_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetCommonStateGetter(
    Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
    LOG_DEBUG(Service_AM, "called");
    *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetLibraryAppletCreator(
    Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
    LOG_DEBUG(Service_AM, "called");
    *out_library_applet_creator =
        std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ILibraryAppletProxy::OpenLibraryAppletSelfAccessor(
    Out<SharedPointer<ILibraryAppletSelfAccessor>> out_library_applet_self_accessor) {
    LOG_DEBUG(Service_AM, "called");
    *out_library_applet_self_accessor =
        std::make_shared<ILibraryAppletSelfAccessor>(system, m_applet);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetAppletCommonFunctions(
    Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_applet_common_functions = std::make_shared<IAppletCommonFunctions>(system, m_applet);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetHomeMenuFunctions(
    Out<SharedPointer<IHomeMenuFunctions>> out_home_menu_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_home_menu_functions =
        std::make_shared<IHomeMenuFunctions>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ILibraryAppletProxy::GetGlobalStateController(
    Out<SharedPointer<IGlobalStateController>> out_global_state_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
    R_SUCCEED();
}

} // namespace Service::AM
