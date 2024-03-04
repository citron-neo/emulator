// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/event_observer.h"
#include "core/hle/service/am/window_system.h"

namespace Service::AM {

enum class UserDataTag : u32 {
    WakeupEvent,
    AppletProcess,
};

EventObserver::EventObserver(Core::System& system, WindowSystem& window_system)
    : m_system(system), m_context(system, "am:EventObserver"), m_window_system(window_system),
      m_wakeup_event(m_context), m_wakeup_holder(m_wakeup_event.GetHandle()) {
    m_window_system.SetEventObserver(this);
    m_wakeup_holder.SetUserData(static_cast<uintptr_t>(UserDataTag::WakeupEvent));
    m_wakeup_holder.LinkToMultiWait(std::addressof(m_multi_wait));
    m_thread = std::thread([&] { this->ThreadFunc(); });
}

EventObserver::~EventObserver() {
    // Signal thread and wait for processing to finish.
    m_stop_source.request_stop();
    m_wakeup_event.Signal();
    m_thread.join();

    // Free remaining owned sessions.
    auto it = m_process_holder_list.begin();
    while (it != m_process_holder_list.end()) {
        // Get the holder.
        auto* const holder = std::addressof(*it);

        // Remove from the list.
        it = m_process_holder_list.erase(it);

        // Free the holder.
        delete holder;
    }
}

void EventObserver::TrackAppletProcess(Applet& applet) {
    // Don't observe dummy processes.
    if (!applet.process->IsInitialized()) {
        return;
    }

    // Allocate new holder.
    auto* holder = new ProcessHolder(applet, *applet.process);
    holder->SetUserData(static_cast<uintptr_t>(UserDataTag::AppletProcess));

    // Insert into list.
    {
        std::scoped_lock lk{m_lock};
        m_process_holder_list.push_back(*holder);
        holder->LinkToMultiWait(std::addressof(m_deferred_wait_list));
    }

    // Signal wakeup.
    m_wakeup_event.Signal();
}

void EventObserver::RequestUpdate() {
    m_wakeup_event.Signal();
}

void EventObserver::LinkDeferred() {
    std::scoped_lock lk{m_lock};
    m_multi_wait.MoveAll(std::addressof(m_deferred_wait_list));
}

MultiWaitHolder* EventObserver::WaitSignaled() {
    while (true) {
        this->LinkDeferred();

        // If we're done, return before we start waiting.
        if (m_stop_source.stop_requested()) {
            return nullptr;
        }

        auto* selected = m_multi_wait.WaitAny(m_system.Kernel());
        if (selected != std::addressof(m_wakeup_holder)) {
            // Unlink the process.
            selected->UnlinkFromMultiWait();
        }

        return selected;
    }
}

void EventObserver::Process(MultiWaitHolder* holder) {
    switch (static_cast<UserDataTag>(holder->GetUserData())) {
    case UserDataTag::WakeupEvent:
        this->OnWakeupEvent(holder);
        break;
    case UserDataTag::AppletProcess:
        this->OnProcessEvent(static_cast<ProcessHolder*>(holder));
        break;
    default:
        UNREACHABLE();
    }
}

void EventObserver::OnWakeupEvent(MultiWaitHolder* holder) {
    m_wakeup_event.Clear();

    // Perform recalculation.
    m_window_system.Update();
}

void EventObserver::OnProcessEvent(ProcessHolder* holder) {
    // Check process state.
    auto& applet = holder->GetApplet();
    auto& process = holder->GetProcess();

    {
        std::scoped_lock lk{m_lock, applet.lock};
        if (process.IsTerminated()) {
            // Destroy the holder.
            this->DestroyAppletProcessHolderLocked(holder);
        } else {
            // Reset signaled state.
            process.ResetSignal();

            // Relink wakeup event.
            holder->LinkToMultiWait(std::addressof(m_deferred_wait_list));
        }

        // Set running.
        applet.is_process_running = process.IsRunning();
    }

    // Perform recalculation.
    m_window_system.Update();
}

void EventObserver::DestroyAppletProcessHolderLocked(ProcessHolder* holder) {
    // Remove from owned list.
    m_process_holder_list.erase(m_process_holder_list.iterator_to(*holder));

    // Destroy and free.
    delete holder;
}

void EventObserver::ThreadFunc() {
    Common::SetCurrentThreadName("am:EventObserver");

    while (true) {
        auto* signaled_holder = this->WaitSignaled();
        if (!signaled_holder) {
            break;
        }

        this->Process(signaled_holder);
    }
}

} // namespace Service::AM
