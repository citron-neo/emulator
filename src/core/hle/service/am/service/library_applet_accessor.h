// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class AppletDataBroker;
struct Applet;
class IStorage;

class ILibraryAppletAccessor final : public ServiceFramework<ILibraryAppletAccessor> {
public:
    explicit ILibraryAppletAccessor(Core::System& system_, std::shared_ptr<AppletDataBroker> broker,
                                    std::shared_ptr<Applet> applet);
    ~ILibraryAppletAccessor();

private:
    Result GetAppletStateChangedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsCompleted(Out<bool> out_is_completed);
    Result GetResult();
    Result PresetLibraryAppletGpuTimeSliceZero();
    Result Start();
    Result RequestExit();
    Result Terminate();
    Result PushInData(SharedPointer<IStorage> storage);
    Result PopOutData(Out<SharedPointer<IStorage>> out_storage);
    Result PushInteractiveInData(SharedPointer<IStorage> storage);
    Result PopInteractiveOutData(Out<SharedPointer<IStorage>> out_storage);
    Result GetPopOutDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetPopInteractiveOutDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetIndirectLayerConsumerHandle(Out<u64> out_handle);

    void FrontendExecute();
    void FrontendExecuteInteractive();
    void FrontendRequestExit();

    const std::shared_ptr<AppletDataBroker> m_broker;
    const std::shared_ptr<Applet> m_applet;
};

} // namespace Service::AM
