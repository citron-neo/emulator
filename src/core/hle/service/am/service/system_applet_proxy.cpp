// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/am/service/application_creator.h"
#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/debug_functions.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/am/service/home_menu_functions.h"
#include "core/hle/service/am/service/library_applet_creator.h"
#include "core/hle/service/am/service/process_winding_controller.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/system_applet_proxy.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ISystemAppletProxy::ISystemAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                       Kernel::KProcess* process, WindowSystem& window_system)
    : ServiceFramework{system_, "ISystemAppletProxy"},
      m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISystemAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        {1, D<&ISystemAppletProxy::GetSelfController>, "GetSelfController"},
        {2, D<&ISystemAppletProxy::GetWindowController>, "GetWindowController"},
        {3, D<&ISystemAppletProxy::GetAudioController>, "GetAudioController"},
        {4, D<&ISystemAppletProxy::GetDisplayController>, "GetDisplayController"},
        {10, D<&ISystemAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
        {11, D<&ISystemAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        {20, D<&ISystemAppletProxy::GetHomeMenuFunctions>, "GetHomeMenuFunctions"},
        {21, D<&ISystemAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
        {22, D<&ISystemAppletProxy::GetApplicationCreator>, "GetApplicationCreator"},
        {23, D<&ISystemAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
        {1000, D<&ISystemAppletProxy::GetDebugFunctions>, "GetDebugFunctions"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemAppletProxy::~ISystemAppletProxy() = default;

Result ISystemAppletProxy::GetAudioController(
    Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_audio_controller = std::make_shared<IAudioController>(system);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetDisplayController(
    Out<SharedPointer<IDisplayController>> out_display_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetProcessWindingController(
    Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetDebugFunctions(
    Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_debug_functions = std::make_shared<IDebugFunctions>(system);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetWindowController(
    Out<SharedPointer<IWindowController>> out_window_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetSelfController(
    Out<SharedPointer<ISelfController>> out_self_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetCommonStateGetter(
    Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
    LOG_DEBUG(Service_AM, "called");
    *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetLibraryAppletCreator(
    Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
    LOG_DEBUG(Service_AM, "called");
    *out_library_applet_creator =
        std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetApplicationCreator(
    Out<SharedPointer<IApplicationCreator>> out_application_creator) {
    LOG_DEBUG(Service_AM, "called");
    *out_application_creator = std::make_shared<IApplicationCreator>(system, m_window_system);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetAppletCommonFunctions(
    Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_applet_common_functions = std::make_shared<IAppletCommonFunctions>(system, m_applet);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetHomeMenuFunctions(
    Out<SharedPointer<IHomeMenuFunctions>> out_home_menu_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_home_menu_functions =
        std::make_shared<IHomeMenuFunctions>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ISystemAppletProxy::GetGlobalStateController(
    Out<SharedPointer<IGlobalStateController>> out_global_state_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
    R_SUCCEED();
}

} // namespace Service::AM
