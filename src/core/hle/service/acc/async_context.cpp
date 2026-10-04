// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/acc/async_context.h"
#include "core/hle/service/ipc_helpers.h"

// [UNITY-FIX] undef Win32 macros shadowing ServiceContext methods.
#undef CreateEvent
#undef CreateMutex
#undef CreateSemaphore

namespace Service::Account {
IAsyncContext::IAsyncContext(Core::System& system_)
    : ServiceFramework{system_, "IAsyncContext"}, service_context{system_, "IAsyncContext"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &IAsyncContext::GetSystemEvent, "GetSystemEvent"},
        {1, &IAsyncContext::Cancel, "Cancel"},
        {2, &IAsyncContext::HasDone, "HasDone"},
        {3, &IAsyncContext::GetResult, "GetResult"},
    };
    // clang-format on

    RegisterHandlers(functions);

    completion_event = service_context.CreateEvent("IAsyncContext:CompletionEvent");
}

IAsyncContext::~IAsyncContext() {
    service_context.CloseEvent(completion_event);
}

void IAsyncContext::GetSystemEvent(HLERequestContext& ctx) {
    LOG_INFO(Service_ACC, "called, complete={}", IsComplete());

    if (IsComplete()) {
        completion_event->Signal();
    }

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(completion_event->GetReadableEvent());
}

void IAsyncContext::Cancel(HLERequestContext& ctx) {
    LOG_DEBUG(Service_ACC, "called");

    Cancel();
    MarkComplete();

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void IAsyncContext::HasDone(HLERequestContext& ctx) {
    is_complete.store(IsComplete());
    LOG_INFO(Service_ACC, "called, done={}", is_complete.load());

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(is_complete.load());
}

void IAsyncContext::GetResult(HLERequestContext& ctx) {
    const Result result = GetResult();
    LOG_INFO(Service_ACC, "called, result=0x{:08X}", result.raw);

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result);
}

void IAsyncContext::SignalCompletion() {
    MarkComplete();
}

void IAsyncContext::MarkComplete() {
    is_complete.store(true);
    completion_event->Signal();
}

} // namespace Service::Account
