// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/am/service/audio_controller.h"
#include "core/hle/service/am/service/common_state_getter.h"
#include "core/hle/service/am/service/debug_functions.h"
#include "core/hle/service/am/service/display_controller.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/am/service/home_menu_functions.h"
#include "core/hle/service/am/service/library_applet_creator.h"
#include "core/hle/service/am/service/overlay_applet_proxy.h"
#include "core/hle/service/am/service/overlay_functions.h"
#include "core/hle/service/am/service/process_winding_controller.h"
#include "core/hle/service/am/service/self_controller.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/cmif_serialization.h"


namespace Service::AM {

IOverlayAppletProxy::IOverlayAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                         Kernel::KProcess* process, WindowSystem& window_system)
    : ServiceFramework{system_, "IOverlayAppletProxy"}, m_window_system{window_system},
      m_process{process}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
    {0, D<&IOverlayAppletProxy::GetCommonStateGetter>, "GetCommonStateGetter"},
    {1, D<&IOverlayAppletProxy::GetSelfController>, "GetSelfController"},
    {2, D<&IOverlayAppletProxy::GetWindowController>, "GetWindowController"},
    {3, D<&IOverlayAppletProxy::GetAudioController>, "GetAudioController"},
    {4, D<&IOverlayAppletProxy::GetDisplayController>, "GetDisplayController"},
    {10, D<&IOverlayAppletProxy::GetProcessWindingController>, "GetProcessWindingController"},
    {11, D<&IOverlayAppletProxy::GetLibraryAppletCreator>, "GetLibraryAppletCreator"},
    {20, D<&IOverlayAppletProxy::GetOverlayFunctions>, "GetOverlayFunctions"},
    {21, D<&IOverlayAppletProxy::GetAppletCommonFunctions>, "GetAppletCommonFunctions"},
    {23, D<&IOverlayAppletProxy::GetGlobalStateController>, "GetGlobalStateController"},
    {1000, D<&IOverlayAppletProxy::GetDebugFunctions>, "GetDebugFunctions"},
};
    // clang-format on

    RegisterHandlers(functions);
}

IOverlayAppletProxy::~IOverlayAppletProxy() = default;

Result IOverlayAppletProxy::GetCommonStateGetter(
    Out<SharedPointer<ICommonStateGetter>> out_common_state_getter) {
    LOG_DEBUG(Service_AM, "called");
    *out_common_state_getter = std::make_shared<ICommonStateGetter>(system, m_applet);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetSelfController(
    Out<SharedPointer<ISelfController>> out_self_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_self_controller = std::make_shared<ISelfController>(system, m_applet, m_process);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetWindowController(
    Out<SharedPointer<IWindowController>> out_window_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_window_controller = std::make_shared<IWindowController>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetAudioController(
    Out<SharedPointer<IAudioController>> out_audio_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_audio_controller = std::make_shared<IAudioController>(system);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetDisplayController(
    Out<SharedPointer<IDisplayController>> out_display_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_display_controller = std::make_shared<IDisplayController>(system, m_applet);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetLibraryAppletCreator(
    Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator) {
    LOG_DEBUG(Service_AM, "called");
    *out_library_applet_creator =
        std::make_shared<ILibraryAppletCreator>(system, m_applet, m_window_system);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetGlobalStateController(
    Out<SharedPointer<IGlobalStateController>> out_global_state_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_global_state_controller = std::make_shared<IGlobalStateController>(system);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetDebugFunctions(
    Out<SharedPointer<IDebugFunctions>> out_debug_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_debug_functions = std::make_shared<IDebugFunctions>(system);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetProcessWindingController(
    Out<SharedPointer<IProcessWindingController>> out_process_winding_controller) {
    LOG_DEBUG(Service_AM, "called");
    *out_process_winding_controller = std::make_shared<IProcessWindingController>(system, m_applet);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetOverlayFunctions(
    Out<SharedPointer<IOverlayFunctions>> out_overlay_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_overlay_functions = std::make_shared<IOverlayFunctions>(system, m_applet);
    R_SUCCEED();
}

Result IOverlayAppletProxy::GetAppletCommonFunctions(
    Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions) {
    LOG_DEBUG(Service_AM, "called");
    *out_applet_common_functions = std::make_shared<IAppletCommonFunctions>(system, m_applet);
    R_SUCCEED();
}

} // namespace Service::AM
