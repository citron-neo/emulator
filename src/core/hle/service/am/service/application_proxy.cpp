// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/am/service/application_functions.h"
#include "core/hle/service/am/service/application_proxy.h"
#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/debug_functions.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/am/service/library_applet_creator.h"
#include "core/hle/service/am/service/process_winding_controller.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IApplicationProxy::IApplicationProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                     Kernel::KProcess* process, WindowSystem& window_system)
    : ServiceFramework{system_, "IApplicationProxy"},
      m_window_system{window_system}, m_process{process}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IApplicationProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
        {1, D<&IApplicationProxy::GetSelfController>, "GetSelfController"},
        {2, D<&IApplicationProxy::GetWindowController>, "GetWindowController"},
        {3, D<&IApplicationProxy::GetAudioController>, "GetAudioController"},
        {4, D<&IApplicationProxy::GetDisplayController>, "GetDisplayController"},
        {10, D<&IApplicationProxy::GetProcessWindingController>, "GetProcessWindingController"},
        {11, D<&IApplicationProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
        {20, D<&IApplicationProxy::GetApplicationFunctions>, "GetApplicationFunctions"},
        {1000, D<&IApplicationProxy::GetDebugFunctions>, "GetDebugFunctions"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationProxy::~IApplicationProxy() = default;

Result IApplicationProxy::GetAudioController(
    Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_audio_controller = std::make_shared<IAudioController>(system);
    R_SUCCEED();
}

Result IApplicationProxy::GetDisplayController(
    Out<SharedPointer<IDisplayController>> out_display_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
    R_SUCCEED();
}

Result IApplicationProxy::GetProcessWindingController(
    Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
    R_SUCCEED();
}

Result IApplicationProxy::GetDebugFunctions(
    Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_debug_functions = std::make_shared<IDebugFunctions>(system);
    R_SUCCEED();
}

Result IApplicationProxy::GetWindowController(
    Out<SharedPointer<IWindowController>> out_window_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result IApplicationProxy::GetSelfController(
    Out<SharedPointer<ISelfController>> out_self_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
    R_SUCCEED();
}

Result IApplicationProxy::GetCommonStateGetter(
    Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
    LOG_DEBUG(Service_AM, "called");
    *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
    R_SUCCEED();
}

Result IApplicationProxy::GetLibraryAppletCreator(
    Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
    LOG_DEBUG(Service_AM, "called");
    *out_library_applet_creator =
        std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result IApplicationProxy::GetApplicationFunctions(
    Out<SharedPointer<IApplicationFunctions>> out_application_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_application_functions = std::make_shared<IApplicationFunctions>(system, m_applet);
    R_SUCCEED();
}

} // namespace Service::AM
