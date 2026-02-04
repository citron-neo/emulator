// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/am/service/overlay_functions.h"

namespace Kernel {
class KProcess;
}

namespace Service::AM {

struct Applet;
class IAudioController;
class ICommonStateGetter;
class IDebugFunctions;
class IDisplayController;
class IGlobalStateController;
class IHomeMenuFunctions;
class ILibraryAppletCreator;
class IOverlayAppletFunctions;
class IProcessWindingController;
class ISelfController;
class IWindowController;
class WindowSystem;
class IOverlayFunctions;
class IAppletCommonFunctions;

class IOverlayAppletProxy final : public ServiceFramework<IOverlayAppletProxy> {
public:
    explicit IOverlayAppletProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                                Kernel::KProcess* process, WindowSystem& window_system);
    ~IOverlayAppletProxy() override;

private:
    Result GetCommonStateGetter(Out<SharedPointer<ICommonStateGetter>> out_common_state_getter);
    Result GetSelfController(Out<SharedPointer<ISelfController>> out_self_controller);
    Result GetWindowController(Out<SharedPointer<IWindowController>> out_window_controller);
    Result GetAudioController(Out<SharedPointer<IAudioController>> out_audio_controller);
    Result GetDisplayController(Out<SharedPointer<IDisplayController>> out_display_controller);
    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller);
    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator);
    Result GetOverlayFunctions(Out<SharedPointer<IOverlayFunctions>> out_overlay_functions);
    Result GetAppletCommonFunctions(
        Out<SharedPointer<IAppletCommonFunctions>> out_applet_common_functions);
    Result GetGlobalStateController(
        Out<SharedPointer<IGlobalStateController>> out_global_state_controller);
    Result GetDebugFunctions(Out<SharedPointer<IDebugFunctions>> out_debug_functions);

    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
