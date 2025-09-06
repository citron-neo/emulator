// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/applet_data_broker.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/service/library_applet_accessor.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ILibraryAppletAccessor::ILibraryAppletAccessor(Core::System& system_,
                                               std::shared_ptr<AppletDataBroker> broker,
                                               std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "ILibraryAppletAccessor"}, m_broker{std::move(broker)},
      m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ILibraryAppletAccessor::GetAppletStateChangedEvent>, "GetAppletStateChangedEvent"},
        {1, D<&ILibraryAppletAccessor::IsCompleted>, "IsCompleted"},
        {10, D<&ILibraryAppletAccessor::Start>, "Start"},
        {20, D<&ILibraryAppletAccessor::RequestExit>, "RequestExit"},
        {25, D<&ILibraryAppletAccessor::Terminate>, "Terminate"},
        {30, D<&ILibraryAppletAccessor::GetResult>, "GetResult"},
        {50, nullptr, "SetOutOfFocusApplicationSuspendingEnabled"},
        {60, D<&ILibraryAppletAccessor::PresetLibraryAppletGpuTimeSliceZero>, "PresetLibraryAppletGpuTimeSliceZero"},
        {100, D<&ILibraryAppletAccessor::PushInData>, "PushInData"},
        {101, D<&ILibraryAppletAccessor::PopOutData>, "PopOutData"},
        {102, nullptr, "PushExtraStorage"},
        {103, D<&ILibraryAppletAccessor::PushInteractiveInData>, "PushInteractiveInData"},
        {104, D<&ILibraryAppletAccessor::PopInteractiveOutData>, "PopInteractiveOutData"},
        {105, D<&ILibraryAppletAccessor::GetPopOutDataEvent>, "GetPopOutDataEvent"},
        {106, D<&ILibraryAppletAccessor::GetPopInteractiveOutDataEvent>, "GetPopInteractiveOutDataEvent"},
        {110, nullptr, "NeedsToExitProcess"},
        {120, nullptr, "GetLibraryAppletInfo"},
        {150, nullptr, "RequestForAppletToGetForeground"},
        {160, D<&ILibraryAppletAccessor::GetIndirectLayerConsumerHandle>, "GetIndirectLayerConsumerHandle"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ILibraryAppletAccessor::~ILibraryAppletAccessor() = default;

Result ILibraryAppletAccessor::GetAppletStateChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_applet->state_changed_event.GetHandle();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::IsCompleted(Out<bool> out_is_completed) {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    *out_is_completed = m_applet->is_completed;
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetResult() {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    R_RETURN(m_applet->terminate_result);
}

Result ILibraryAppletAccessor::PresetLibraryAppletGpuTimeSliceZero() {
    LOG_INFO(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ILibraryAppletAccessor::Start() {
    LOG_DEBUG(Service_AM, "called");
    m_applet->process->Run();
    FrontendExecute();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::RequestExit() {
    LOG_DEBUG(Service_AM, "called");
    {
        std::scoped_lock lk{m_applet->lock};
        m_applet->lifecycle_manager.RequestExit();
    }
    FrontendRequestExit();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::Terminate() {
    LOG_DEBUG(Service_AM, "called");
    m_applet->process->Terminate();
    FrontendRequestExit();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::PushInData(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");
    m_broker->GetInData().Push(storage);
    R_SUCCEED();
}

Result ILibraryAppletAccessor::PopOutData(Out<SharedPointer<IStorage>> out_storage) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(m_broker->GetOutData().Pop(out_storage.Get()));
}

Result ILibraryAppletAccessor::PushInteractiveInData(SharedPointer<IStorage> storage) {
    LOG_DEBUG(Service_AM, "called");
    m_broker->GetInteractiveInData().Push(storage);
    FrontendExecuteInteractive();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::PopInteractiveOutData(Out<SharedPointer<IStorage>> out_storage) {
    LOG_DEBUG(Service_AM, "called");
    R_RETURN(m_broker->GetInteractiveOutData().Pop(out_storage.Get()));
}

Result ILibraryAppletAccessor::GetPopOutDataEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_broker->GetOutData().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetPopInteractiveOutDataEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_AM, "called");
    *out_event = m_broker->GetInteractiveOutData().GetEvent();
    R_SUCCEED();
}

Result ILibraryAppletAccessor::GetIndirectLayerConsumerHandle(Out<u64> out_handle) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    // We require a non-zero handle to be valid. Using 0xdeadbeef allows us to trace if this is
    // actually used anywhere
    *out_handle = 0xdeadbeef;
    R_SUCCEED();
}

void ILibraryAppletAccessor::FrontendExecute() {
    if (m_applet->frontend) {
        m_applet->frontend->Initialize();
        m_applet->frontend->Execute();
    }
}

void ILibraryAppletAccessor::FrontendExecuteInteractive() {
    if (m_applet->frontend) {
        m_applet->frontend->ExecuteInteractive();
        m_applet->frontend->Execute();
    }
}

void ILibraryAppletAccessor::FrontendRequestExit() {
    if (m_applet->frontend) {
        m_applet->frontend->RequestExit();
    }
}

} // namespace Service::AM
