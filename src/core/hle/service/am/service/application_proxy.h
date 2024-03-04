// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct Applet;
class IAudioController;
class IApplicationFunctions;
class ICommonStateGetter;
class IDebugFunctions;
class IDisplayController;
class ILibraryAppletCreator;
class IProcessWindingController;
class ISelfController;
class IWindowController;
class WindowSystem;

class IApplicationProxy final : public ServiceFramework<IApplicationProxy> {
public:
    explicit IApplicationProxy(Core::System& system_, std::shared_ptr<Applet> applet,
                               Kernel::KProcess* process, WindowSystem& window_system);
    ~IApplicationProxy();

private:
    Result GetAudioController(Out<SharedPointer<IAudioController>> out_audio_controller);
    Result GetDisplayController(Out<SharedPointer<IDisplayController>> out_display_controller);
    Result GetProcessWindingController(
        Out<SharedPointer<IProcessWindingController>> out_process_winding_controller);
    Result GetDebugFunctions(Out<SharedPointer<IDebugFunctions>> out_debug_functions);
    Result GetWindowController(Out<SharedPointer<IWindowController>> out_window_controller);
    Result GetSelfController(Out<SharedPointer<ISelfController>> out_self_controller);
    Result GetCommonStateGetter(Out<SharedPointer<ICommonStateGetter>> out_common_state_getter);
    Result GetLibraryAppletCreator(
        Out<SharedPointer<ILibraryAppletCreator>> out_library_applet_creator);
    Result GetApplicationFunctions(
        Out<SharedPointer<IApplicationFunctions>> out_application_functions);

private:
    WindowSystem& m_window_system;
    Kernel::KProcess* const m_process;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
