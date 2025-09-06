// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/application_functions.h"
#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/debug_functions.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/am/service/home_menu_functions.h"
#include "core/hle/service/am/service/library_applet_creator.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/system_application_proxy.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ISystemApplicationProxy::ISystemApplicationProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                                 Kernel::KProcess* process, WindowSystem& window_system)
    : ServiceFramework{system_, "ISystemApplicationProxy"},
      m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISystemApplicationProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        {1, D<&ISystemApplicationProxy::GetSelfController>, "GetSelfController"},
        {2, D<&ISystemApplicationProxy::GetWindowController>, "GetWindowController"},
        {3, D<&ISystemApplicationProxy::GetAudioController>, "GetAudioController"},
        {4, D<&ISystemApplicationProxy::GetDisplayController>, "GetDisplayController"},
        {11, D<&ISystemApplicationProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        {20, D<&ISystemApplicationProxy::GetHomeMenuFunctions>, "GetHomeMenuFunctions"},
        {21, D<&ISystemApplicationProxy::GetGlobalStateController>, "GetGlobalStateController"},
        {22, D<&ISystemApplicationProxy::GetApplicationFunctions>, "GetApplicationFunctions"},
        {1000, D<&ISystemApplicationProxy::GetDebugFunctions>, "GetDebugFunctions"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemApplicationProxy::~ISystemApplicationProxy() = default;

Result ISystemApplicationProxy::GetCommonStateGetter(
    Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
    LOG_DEBUG(Service_AM, "called");
    *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetSelfController(
    Out<SharedPointer<ISelfController>> out_self_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetWindowController(
    Out<SharedPointer<IWindowController>> out_window_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetAudioController(
    Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_audio_controller = std::make_shared<IAudioController>(system);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetDisplayController(
    Out<SharedPointer<IDisplayController>> out_display_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetLibraryAppletCreator(
    Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
    LOG_DEBUG(Service_AM, "called");
    *out_library_applet_creator =
        std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetHomeMenuFunctions(
    Out<SharedPointer<IHomeMenuFunctions>> out_home_menu_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_home_menu_functions =
        std::make_shared<IHomeMenuFunctions>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetGlobalStateController(
    Out<SharedPointer<IGlobalStateController>> out_global_state_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetApplicationFunctions(
    Out<SharedPointer<IApplicationFunctions>> out_application_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_application_functions = std::make_shared<IApplicationFunctions>(system, m_applet);
    R_SUCCEED();
}

Result ISystemApplicationProxy::GetDebugFunctions(
    Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_debug_functions = std::make_shared<IDebugFunctions>(system);
    R_SUCCEED();
}

} // namespace Service::AM