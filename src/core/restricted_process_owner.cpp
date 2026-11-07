// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <thread>

#include "common/settings.h"
#include "common/terminal_fail_stop.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/kernel.h"
#include "core/restricted_process_owner.h"

namespace Core {
namespace {
// std::thread::id may be reused after a thread exits. A retained owner must not
// mistake a replacement thread for its original native orchestration thread.
u64 CurrentThreadIdentity() noexcept {
    static std::atomic<u64> next{1};
    thread_local const u64 identity = next.fetch_add(1, std::memory_order_relaxed);
    if (!identity) Common::TerminalFailStop("restricted_owner.thread", "identity_exhausted");
    return identity;
}

class RestrictedWindow final : public Frontend::EmuWindow {
public:
    std::unique_ptr<Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<Frontend::GraphicsContext>();
    }
    bool IsShown() const override { return false; }
};
}

struct RestrictedProcessOwner::State {
    const u64 thread{CurrentThreadIdentity()};
    std::shared_ptr<Observation> observation{std::make_shared<Observation>()};
    // Renderer borrows the window; all kernel children borrow System/Kernel.
    std::unique_ptr<RestrictedWindow> window;
    std::unique_ptr<Core::System> system;
    Kernel::KProcess* process{};
    Kernel::KEvent* event{};
    bool ready{};
    bool accepting{true};
    bool CurrentThread() const noexcept { return thread == CurrentThreadIdentity(); }
    ~State() noexcept {
        if (!CurrentThread()) {
            Common::TerminalFailStop("restricted_owner.release", "wrong_thread");
        }
        try {
            observation->retirement = Retirement::Event;
            if (event) {
                event->GetReadableEvent().Close();
                event->Close();
                event = nullptr;
            }
            observation->retirement = Retirement::Process;
            if (process) { process->Close(); process = nullptr; }
            observation->retirement = Retirement::System;
            if (ready) system->ShutdownMainProcess();
            system.reset();
            window.reset();
            observation->retirement = Retirement::Released;
        } catch (...) {
            // Never unwind System beneath an uncertain kernel child finalizer.
            Common::TerminalFailStop("restricted_owner.release", "host_exception");
        }
    }
};

std::optional<RestrictedProcessOwner> RestrictedProcessOwner::TryCreate() {
    if (Kernel::KernelCore::HasCurrentHostKernelBinding() ||
        !Settings::values.use_multi_core.GetValue() ||
        !Settings::values.use_asynchronous_gpu_emulation.GetValue()) {
        return std::nullopt;
    }
    auto state = std::make_shared<State>();
    state->window = std::make_unique<RestrictedWindow>();
    state->system = std::make_unique<Core::System>();
    try {
        state->system->Initialize();
        state->system->InitializeRestrictedOwner(*state->window);
        state->ready = true;
        auto& kernel = state->system->Kernel();
        state->process = Kernel::KProcess::Create(kernel);
        if (!state->process) return std::nullopt;
        const Kernel::Svc::CreateProcessParameter params{
            .code_address = 0x700000, .code_num_pages = 1,
            .flags = Kernel::Svc::CreateProcessFlag::Is64Bit |
                     Kernel::Svc::CreateProcessFlag::AddressSpace32Bit,
        };
        const std::array<u32, 2> caps{7U | (63U << 4) | (4U << 10) | (3U << 24),
                                       0x7fffU | (64U << 16)};
        if (R_FAILED(state->process->Initialize(params, caps, kernel.GetSystemResourceLimit(),
                                               Kernel::KMemoryManager::Pool::Application, 0))) {
            return std::nullopt;
        }
        Kernel::KProcess::Register(kernel, state->process);
        Kernel::KScopedResourceReservation reservation{state->process,
                                                       Kernel::Svc::LimitableResource::EventCountMax};
        if (!reservation.Succeeded()) return std::nullopt;
        state->event = Kernel::KEvent::Create(kernel);
        if (!state->event) return std::nullopt;
        state->event->Initialize(state->process);
        reservation.Commit();
        Kernel::KEvent::Register(kernel, state->event);
        return RestrictedProcessOwner{std::move(state)};
    } catch (...) {
        Common::TerminalFailStop("restricted_owner.initialize", "host_exception");
    }
}

std::optional<RestrictedProcessOwner::Lease> RestrictedProcessOwner::TryRetain() const {
    if (!owner || !owner->CurrentThread() || !owner->accepting) return std::nullopt;
    return Lease{owner};
}
bool RestrictedProcessOwner::CloseAdmission() {
    if (!owner || !owner->CurrentThread()) return false;
    owner->accepting = false;
    return true;
}
std::shared_ptr<const RestrictedProcessOwner::Observation>
RestrictedProcessOwner::ObserveRetirement() const {
    return owner ? owner->observation : nullptr;
}
bool RestrictedProcessOwner::Lease::Signal() {
    return owner && owner->CurrentThread() && R_SUCCEEDED(owner->event->Signal());
}
bool RestrictedProcessOwner::Lease::Clear() {
    return owner && owner->CurrentThread() && R_SUCCEEDED(owner->event->Clear());
}
std::optional<RestrictedProcessOwner::Counts> RestrictedProcessOwner::Lease::Observe() const {
    if (!owner || !owner->CurrentThread()) return std::nullopt;
    return Counts{owner->process->GetReferenceCount(), owner->event->GetReferenceCount(),
                  owner->process->IsInitialized()};
}
std::optional<RestrictedProcessOwner::Cohort>
RestrictedProcessOwner::Lease::TryStopCohort(u32 poll_budget) {
    if (!owner || !owner->CurrentThread() || !owner->accepting) return std::nullopt;
    auto closure = owner->process->TryStopProcessCohort(poll_budget);
    if (!closure) return std::nullopt;
    return Cohort{owner, std::move(*closure)};
}
RestrictedProcessOwner::Cohort::Cohort(std::shared_ptr<State> state,
                                     Kernel::ProcessCohortStop&& stopped)
    : owner{std::move(state)}, closure{std::move(stopped)} {}
RestrictedProcessOwner::Cohort::Cohort(Cohort&&) noexcept = default;
RestrictedProcessOwner::Cohort::~Cohort() {
    if (owner && !owner->CurrentThread()) {
        Common::TerminalFailStop("restricted_owner.cohort_release", "wrong_thread");
    }
}
auto RestrictedProcessOwner::Cohort::Poll() -> Kernel::ProcessCohortStop::Result {
    if (!owner || !owner->CurrentThread() || !closure || cancelled) {
        return Kernel::ProcessCohortStop::Result::Unknown;
    }
    return closure->Poll();
}
bool RestrictedProcessOwner::Cohort::CancelNoEffects() {
    if (!owner || !owner->CurrentThread() || !closure || cancelled) return false;
    cancelled = closure->CancelNoEffects();
    return cancelled;
}
bool RestrictedProcessOwner::Cohort::Quarantine() {
    if (!owner || !owner->CurrentThread() || !closure || cancelled) return false;
    closure->Quarantine();
    return true;
}
} // namespace Core
