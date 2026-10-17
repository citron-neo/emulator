// SPDX-License-Identifier: GPL-2.0-or-later
// Native acceptance: real CpuManager/KThread/KScheduler fibers, process Finalize, GPU owner,
// Vulkan cache and checked transfers. The headless renderer omits presentation only.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <functional>
#include <iostream>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "common/scope_exit.h"
#include "core/cpu_manager.h"
#include "core/device_memory.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/hle/kernel/k_hardware_timer.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/kernel.h"
#include "core/scheduler_test_access.h"
#include "video_core/host1x/host1x.h"
#include "video_core/control/channel_state.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_base.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/vulkan_common/vulkan_instance.h"
#include "video_core/vulkan_common/vulkan_library.h"

namespace {
using namespace std::chrono_literals;
using Access = Core::SchedulerTestAccess;
using Request = Core::Memory::ControlledCpuWriteRequest;
using Action = Request::SchedulerAction;
using Registration = Vulkan::RasterizerVulkan::ControlledRegistrationPtr;
constexpr u64 Footprint = 65536;

class Window final : public Core::Frontend::EmuWindow {
public:
    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<Core::Frontend::GraphicsContext>();
    }
    bool IsShown() const override { return false; }
};

class TransferRenderer final : public VideoCore::RendererBase {
public:
    TransferRenderer(Window& window, Core::System& system)
        : RendererBase(window, window.CreateSharedContext()), library{Vulkan::OpenLibrary()},
          instance{Vulkan::CreateInstance(*library, dispatch, VK_API_VERSION_1_1)},
          device{*instance, Vulkan::vk::PhysicalDevice{instance.EnumeratePhysicalDevices().at(0),
                                                     dispatch}, VK_NULL_HANDLE, dispatch},
          allocator{device}, scheduler{device, state},
          rasterizer{window, system.GPU(), system.Host1x().MemoryManager(), device, allocator,
                     state, scheduler} {}
    ~TransferRenderer() override { Shutdown(); }
    void Shutdown() override {
        if (closed) return;
        rasterizer.Shutdown();
        scheduler.Shutdown();
        {
            std::scoped_lock lock{scheduler.submit_mutex};
            if (device.GetLogical().WaitIdle() == VK_SUCCESS)
                rasterizer.RetireControlledTransfersAfterDeviceIdle();
        }
        scheduler.GetMasterSemaphore().Shutdown();
        closed = true;
    }
    void Composite(std::span<const Tegra::FramebufferConfig>) override {}
    std::vector<u8> GetAppletCaptureBuffer() override { return {}; }
    std::string GetDeviceVendor() const override { return device.GetDriverName(); }
    VideoCore::RasterizerInterface* ReadRasterizer() override { return &rasterizer; }
    std::shared_ptr<Common::DynamicLibrary> library;
    Vulkan::vk::InstanceDispatch dispatch;
    Vulkan::vk::Instance instance;
    Vulkan::Device device;
    Vulkan::MemoryAllocator allocator;
    Vulkan::StateTracker state;
    Vulkan::Scheduler scheduler;
    Vulkan::RasterizerVulkan rasterizer;
    bool closed{};
};

template <typename Predicate>
bool Until(Predicate predicate, std::chrono::milliseconds timeout = 5000ms) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

struct Process {
    explicit Process(Core::System& system_) : system{system_} {
        auto& kernel = system.Kernel();
        process = Kernel::KProcess::Create(kernel);
        REQUIRE(process);
        const Kernel::Svc::CreateProcessParameter params{
            .code_address = 0x700000, .code_num_pages = 1,
            .flags = Kernel::Svc::CreateProcessFlag::Is64Bit |
                     Kernel::Svc::CreateProcessFlag::AddressSpace32Bit,
        };
        const std::array<u32, 2> caps{7U | (63U << 4) | (4U << 10) | (3U << 24),
                                       0x7fffU | (64U << 16)};
        REQUIRE(R_SUCCEEDED(process->Initialize(params, caps, kernel.GetSystemResourceLimit(),
                                                Kernel::KMemoryManager::Pool::Application, 0)));
        Kernel::KProcess::Register(kernel, process);
        REQUIRE(R_SUCCEEDED(process->GetPageTable().SetMaxHeapSize(16 * 1024 * 1024)));
        REQUIRE(R_SUCCEEDED(process->GetPageTable().SetHeapSize(&heap, 2 * 1024 * 1024)));
        Access::StartProcess(*process);
    }
    ~Process() { Close(); }
    void Close() {
        if (!process) return;
        REQUIRE(R_SUCCEEDED(process->Terminate()));
        process->Close();
        process = nullptr;
    }
    Kernel::KThread* Start(std::function<void()> callback) {
        auto& kernel = system.Kernel();
        Kernel::KScopedResourceReservation reservation{process,
                                                       Kernel::Svc::LimitableResource::ThreadCountMax};
        REQUIRE(reservation.Succeeded());
        auto* thread = Kernel::KThread::Create(kernel);
        REQUIRE(thread);
        REQUIRE(R_SUCCEEDED(Kernel::KThread::InitializeServiceThread(system, thread,
                                      std::move(callback), 16, 3, process)));
        Kernel::KThread::Register(kernel, thread);
        reservation.Commit();
        REQUIRE(R_SUCCEEDED(thread->Run()));
        // Keep this reference for observations; Terminate/Close in the test releases it.
        return thread;
    }
    Core::System& system;
    Kernel::KProcess* process{};
    Kernel::KProcessAddress heap{};
};

void EndThread(Kernel::KThread* thread) {
    REQUIRE(R_SUCCEEDED(thread->Terminate()));
    thread->Close();
}

TEST_CASE("Controlled writes cross real guest fibers and process finalization",
          "[controlled-cpu][native]") {
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    Settings::values.use_uma_zero_copy.SetValue(false);
    Settings::values.cpu_debug_mode.SetValue(false);
    Core::System system;
    Window window;
    system.Initialize();
    TransferRenderer* renderer{};
    Access::Initialize(system, window, [&](Core::System& owner) {
        auto result = std::make_unique<TransferRenderer>(window, owner);
        renderer = result.get();
        return result;
    });
    SCOPE_EXIT { system.ShutdownMainProcess(); };
    auto& gpu = system.GPU();
    // TryQueueCpuWrite explicitly permits pre-enqueue try-lock/full-transport
    // refusal. Retry only that no-effect boundary, never an accepted CPU request.
    const auto enqueue = [&](std::function<bool()> callback) {
        return Until([&] { return gpu.TryQueueCpuWrite(callback); });
    };
    auto& rasterizer = renderer->rasterizer;
    auto& device_memory = system.Host1x().MemoryManager();
    auto channel = gpu.AllocateChannel();
    channel->memory_manager = std::make_shared<Tegra::MemoryManager>(system);
    gpu.RunOnGpuThread([&] {
        gpu.InitChannel(*channel, 0);
        gpu.BindChannel(channel->bind_id);
    });

    // Full KProcess/heap/TLS/PTE construction and Terminate/Finalize destruction. Leave a
    // request queued while its guest CPU fiber is in AwaitControlledCpuWrite, then terminate.
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        Process owner{system};
        auto& memory = owner.process->GetMemory();
        memory.RasterizerMarkRegionCached(owner.heap, 4096, true);
        auto request = memory.PrepareControlledWrite8(owner.heap + 7, 77);
        REQUIRE(request);
        std::weak_ptr<Core::Memory::CpuWriteOwner> released_owner = request->Owner();
        auto domain = request->View().mapping.domain;
        std::atomic<bool> entered{}, interrupted{};
        auto* thread = owner.Start([&] {
            entered = true;
            interrupted = !system.GetCpuManager().AwaitControlledCpuWrite(request).has_value();
        });
        REQUIRE(Until([&] { return entered.load(); }));
        REQUIRE(Until([&] {
            Kernel::KScopedSchedulerLock lock{system.Kernel()};
            return thread->GetState() == Kernel::ThreadState::Waiting;
        }));
        std::atomic<bool> release{}, retired{};
        REQUIRE(enqueue([request, &release, &retired] {
            if (!release.load()) return false;
            (void)request->Cancel();
            retired = true;
            return true;
        }));
        owner.Close(); // Waits for guest exit; PTE is not finalized until the thread ref closes.
        EndThread(thread);
        REQUIRE(Until([&] { return !domain->alive.load(); }));
        REQUIRE_FALSE(request->View().context->alive.load());
        REQUIRE(interrupted);
        REQUIRE(request->State() == Request::Phase::Cancelled);
        REQUIRE(!request->Owner()->TryVisit([](auto&) {}));
        // This unrelated callback must retire while the old per-process continuation remains.
        std::atomic<bool> other_progress{};
        REQUIRE(enqueue([&] { other_progress = true; return true; }));
        REQUIRE(Until([&] { return other_progress.load(); }));
        release = true;
        REQUIRE(Until([&] { return retired.load(); }));
        gpu.SynchronizeGPUThread();
        request.reset();
        REQUIRE(Until([&] { return released_owner.expired(); }));
    }

    Process owner{system};
    auto& memory = owner.process->GetMemory();
    auto* keeper = owner.Start([&] {
        auto& kernel = system.Kernel();
        auto& self = Kernel::GetCurrentThread(kernel);
        while (!self.IsTerminationRequested())
            (void)self.Sleep(kernel.HardwareTimer().GetTick() + 1'000'000);
    });
    const auto asid = device_memory.RegisterProcess(&memory);
    const DAddr address = Footprint;
    device_memory.AllocateFixed(address, Footprint);
    device_memory.Map(address, GetInteger(owner.heap), Footprint, asid);
    std::vector<Core::Memory::PreparedCpuPage> views;
    for (u64 offset = 0; offset < Footprint; offset += 4096) {
        auto view = memory.PrepareControlledCpuPage(owner.heap + offset);
        REQUIRE(view);
        views.push_back(*view);
    }
    Registration operation;
    std::atomic<bool> registered{};
    REQUIRE(enqueue([&] {
        if (!operation) operation = rasterizer.BeginControlledBufferRegistration(address, 1, views);
        if (!operation) { registered = true; return true; }
        const auto phase = rasterizer.PollControlledBufferRegistration(operation);
        if (phase == Vulkan::BufferCache::ControlledRegistration::Phase::Ready) {
            registered = true;
            return true;
        }
        return false;
    }));
    REQUIRE(Until([&] { return registered.load(); }));
    REQUIRE(operation);
    gpu.RunOnGpuThread([&] { Access::FillGpu(rasterizer, operation, address, Footprint); });
    // Real deferred legacy readback on another page of the COARSE preservation footprint.
    // Native completion alone does not retire its pending guest writeback identity.
    bool legacy_pending{};
    gpu.RunOnGpuThread([&] {
        legacy_pending = Access::QueueLegacyReadback(
            rasterizer, operation, address + Footprint - 4096, 4096);
        renderer->scheduler.Finish();
    });
    REQUIRE(legacy_pending);
    auto excluded = memory.PrepareControlledWrite8(owner.heap + 7, 76);
    REQUIRE(excluded);
    std::atomic<bool> exclusion_done{};
    std::optional<Action> exclusion_action;
    auto* excluded_thread = owner.Start([&] {
        const bool enqueued = rasterizer.QueueControlledCpuWriteRequest(operation, excluded);
        if (enqueued) exclusion_action = system.GetCpuManager().AwaitControlledCpuWrite(excluded);
        exclusion_done = true;
    });
    REQUIRE(Until([&] { return exclusion_done.load(); }));
    REQUIRE(exclusion_action == Action::DoNotExecute);
    REQUIRE(excluded->State() == Request::Phase::Cancelled);
    REQUIRE(excluded->View().physical.Published()->sequence == 0);
    EndThread(excluded_thread);
    gpu.RunOnGpuThread([&] {
        Access::RetireLegacyReadback(rasterizer);
        Access::FillGpu(rasterizer, operation, address, Footprint);
    });
    auto request = memory.PrepareControlledWrite8(owner.heap + 7, 77);
    REQUIRE(request);
    REQUIRE_FALSE(system.GetCpuManager().AwaitControlledCpuWrite(request).has_value());
    REQUIRE(request->State() == Request::Phase::Prepared); // host observer cannot wait or consume it
    std::atomic<bool> producer_entered{}, consumer_done{}, queued{};
    std::optional<Action> observed;
    // Contend on REAL production sys-core and device counter locks. These must not occur
    // anywhere in the controlled store/publication lease or require the CPU to release them.
    auto sys_lock = Access::LockSysCore(memory);
    auto counter_lock = std::make_unique<Common::ScopedRangeLock>(
        Access::CounterGuard(device_memory), address, Footprint);
    std::unique_lock cache_lock{Access::Cache(rasterizer).mutex};
    auto* thread = owner.Start([&] {
        queued = rasterizer.QueueControlledCpuWriteRequest(operation, request);
        producer_entered = true;
        observed = system.GetCpuManager().AwaitControlledCpuWrite(request);
        consumer_done = true;
    });
    REQUIRE(Until([&] { return producer_entered.load(); }));
    REQUIRE(queued);
    const bool suspended = Until([&] {
        Kernel::KScopedSchedulerLock lock{system.Kernel()};
        return thread->GetState() == Kernel::ThreadState::Waiting;
    });
    std::atomic<bool> marker{};
    REQUIRE(enqueue([&] { marker = true; return true; }));
    const bool progressed = Until([&] { return marker.load(); }, 500ms);
    const auto before_unlock = request->State();
    cache_lock.unlock(); // Always break the negative-control cycle before reporting failure.
    REQUIRE(suspended);
    REQUIRE(progressed);
    REQUIRE(before_unlock == Request::Phase::Prepared);
    REQUIRE(Until([&] { return consumer_done.load(); }));
    REQUIRE(observed == Action::Complete);
    REQUIRE(request->Outcome() == Core::Memory::PreparedCpuWriteResult::Stored);
    const auto pin = system.DeviceMemory().buffer.PinPhysicalRange(
        request->View().physical.PhysicalOffset(), 4096);
    REQUIRE(pin);
    const auto bytes = pin->Bytes();
    REQUIRE(bytes[6] == 99);
    REQUIRE(bytes[7] == 77);
    REQUIRE(bytes[8] == 99);
    REQUIRE(request->View().physical.Published()->sequence == 1);
    for (unsigned poll = 0; poll < 16; ++poll)
        REQUIRE(!rasterizer.QueueControlledCpuWriteRequest(operation, request));
    REQUIRE(system.GetCpuManager().AwaitControlledCpuWrite(nullptr) == Action::DoNotExecute);
    sys_lock.unlock();
    counter_lock.reset();
    EndThread(thread);

    // Owner acceptance can certify refusal before any physical/native effects. Consume it
    // through the same guest API, never by treating enqueue acknowledgement as completion.
    memory.RasterizerMarkRegionCached(owner.heap + Footprint, 4096, true);
    auto refused = memory.PrepareControlledWrite8(owner.heap + Footprint + 7, 78);
    REQUIRE(refused);
    consumer_done = false;
    thread = owner.Start([&] {
        queued = rasterizer.QueueControlledCpuWriteRequest(operation, refused);
        observed = system.GetCpuManager().AwaitControlledCpuWrite(refused);
        consumer_done = true;
    });
    REQUIRE(Until([&] { return consumer_done.load(); }));
    REQUIRE(queued);
    REQUIRE(observed == Action::ReprepareCertified);
    REQUIRE(refused->State() == Request::Phase::Refused);
    EndThread(thread);

    // A successful enqueue can be dropped at the 64-slot owner boundary. The real guest
    // consumer must see Cancelled rather than waiting forever or reporting completion.
    std::atomic<bool> release_slots{};
    for (unsigned slot = 0; slot < 64; ++slot)
        REQUIRE(enqueue([&] { return release_slots.load(); }));
    gpu.SynchronizeGPUThread();
    auto cancelled = memory.PrepareControlledWrite8(owner.heap + 9, 78);
    REQUIRE(cancelled);
    consumer_done = false;
    thread = owner.Start([&] {
        queued = rasterizer.QueueControlledCpuWriteRequest(operation, cancelled);
        observed = system.GetCpuManager().AwaitControlledCpuWrite(cancelled);
        consumer_done = true;
    });
    REQUIRE(Until([&] { return consumer_done.load(); }));
    REQUIRE(queued);
    REQUIRE(observed == Action::DoNotExecute);
    REQUIRE(cancelled->State() == Request::Phase::Cancelled);
    release_slots = true;
    gpu.SynchronizeGPUThread();
    EndThread(thread);

    // Restore the GPU representation before another controlled request. This is an actual
    // checked upload/readback, not a fabricated completion or a native queue drain.
    bool refresh_started{};
    gpu.RunOnGpuThread([&] { refresh_started = rasterizer.RefreshControlledBufferRegistration(operation); });
    REQUIRE(refresh_started);
    registered = false;
    REQUIRE(enqueue([&] {
        if (rasterizer.PollControlledBufferRegistration(operation) ==
            Vulkan::BufferCache::ControlledRegistration::Phase::Ready) {
            registered = true;
            return true;
        }
        return false;
    }));
    REQUIRE(Until([&] { return registered.load(); }));
    auto uncertain = memory.PrepareControlledWrite8(owner.heap + 10, 79);
    REQUIRE(uncertain);
    const auto admission = uncertain->View().physical.Epoch()->ranges;
    const std::array dependency_access{Common::PhysicalRangeAdmission::Access{
        uncertain->View().physical.PhysicalOffset(), 4096,
        Common::PhysicalRangeAdmission::Mode::Write}};
    auto dependency = admission->Enqueue(dependency_access, std::make_shared<int>(1));
    REQUIRE(dependency);
    REQUIRE(admission->TryStart(dependency) == Common::PhysicalRangeAdmission::Start::Granted);
    consumer_done = false;
    producer_entered = false;
    thread = owner.Start([&] {
        queued = rasterizer.QueueControlledCpuWriteRequest(operation, uncertain);
        producer_entered = true;
        observed = system.GetCpuManager().AwaitControlledCpuWrite(uncertain);
        consumer_done = true;
    });
    REQUIRE(Until([&] { return producer_entered.load() && uncertain->State() == Request::Phase::Accepted; }));
    unsigned readback_guards{};
    gpu.RunOnGpuThread([&] {
        readback_guards = Access::ProbeLegacyReadbackGuards(
            rasterizer, operation, address, Footprint);
    });
    REQUIRE(readback_guards == 4);
    gpu.SynchronizeGPUThread(); // accepted, parked behind the real physical predecessor
    std::unique_lock pte_lock{uncertain->View().mapping.domain->mutex};
    REQUIRE(admission->Complete(dependency));
    // A CPU PTE owner waits for a later GPU callback; the GPU must refuse rather than
    // synchronously acquire this PTE lock while this CPU waits for its progress.
    marker = false;
    REQUIRE(enqueue([&] { marker = true; return true; }));
    const bool pte_progress = Until([&] { return marker.load() && consumer_done.load(); }, 500ms);
    pte_lock.unlock();
    REQUIRE(pte_progress);
    REQUIRE(queued);
    REQUIRE(observed == Action::Quarantine);
    REQUIRE(uncertain->State() == Request::Phase::Unknown);
    EndThread(thread);

    // Negative witnesses on the SAME native objects: legacy publication waits for a CPU
    // counter owner; legacy PTE mutation waits for a CPU PTE owner. A later GPU callback
    // cannot run until we explicitly break each cycle. This validates the progress probe.
    for (const bool counter : {true, false}) {
        auto range = counter ? std::make_unique<Common::ScopedRangeLock>(
                                   Access::CounterGuard(device_memory), address, Footprint)
                             : nullptr;
        std::unique_lock pte{request->View().mapping.domain->mutex, std::defer_lock};
        if (!counter) pte.lock();
        std::atomic<bool> legacy_entered{};
        marker = false;
        REQUIRE(enqueue([&] {
            legacy_entered = true;
            if (counter) Access::LegacyPublish(rasterizer, address);
            else memory.RasterizerMarkRegionCached(owner.heap, 4096, true);
            return true;
        }));
        REQUIRE(Until([&] { return legacy_entered.load(); }));
        REQUIRE(enqueue([&] { marker = true; return true; }));
        const bool bypassed_blocking_legacy = Until([&] { return marker.load(); }, 50ms);
        range.reset();
        if (pte.owns_lock()) pte.unlock();
        REQUIRE(Until([&] { return marker.load(); }));
        REQUIRE_FALSE(bypassed_blocking_legacy);
        gpu.SynchronizeGPUThread();
    }

    // Accepted native work can outlive its process without a full GPU queue drain. Hold
    // the REAL Vulkan submission mutex so preservation cannot complete during termination.
    Process pending_owner{system};
    auto& pending_memory = pending_owner.process->GetMemory();
    const auto pending_asid = device_memory.RegisterProcess(&pending_memory);
    const DAddr pending_address = 2 * Footprint;
    device_memory.AllocateFixed(pending_address, Footprint);
    device_memory.Map(pending_address, GetInteger(pending_owner.heap), Footprint, pending_asid);
    std::vector<Core::Memory::PreparedCpuPage> pending_views;
    for (u64 offset = 0; offset < Footprint; offset += 4096) {
        auto view = pending_memory.PrepareControlledCpuPage(pending_owner.heap + offset);
        REQUIRE(view);
        pending_views.push_back(*view);
    }
    Registration pending_operation;
    registered = false;
    REQUIRE(enqueue([&] {
        if (!pending_operation)
            pending_operation = rasterizer.BeginControlledBufferRegistration(
                pending_address, 1, pending_views);
        if (!pending_operation) { registered = true; return true; }
        if (rasterizer.PollControlledBufferRegistration(pending_operation) ==
            Vulkan::BufferCache::ControlledRegistration::Phase::Ready) {
            registered = true;
            return true;
        }
        return false;
    }));
    REQUIRE(Until([&] { return registered.load(); }));
    REQUIRE(pending_operation);
    gpu.RunOnGpuThread([&] {
        Access::FillGpu(rasterizer, pending_operation, pending_address, Footprint);
    });
    auto pending = pending_memory.PrepareControlledWrite8(pending_owner.heap + 7, 77);
    REQUIRE(pending);
    const auto pending_pin = system.DeviceMemory().buffer.PinPhysicalRange(
        pending->View().physical.PhysicalOffset(), 4096);
    REQUIRE(pending_pin);
    auto pending_domain = pending->View().mapping.domain;
    std::unique_lock submit_lock{renderer->scheduler.submit_mutex};
    std::atomic<bool> pending_entered{};
    auto* pending_thread = pending_owner.Start([&] {
        queued = rasterizer.QueueControlledCpuWriteRequest(pending_operation, pending);
        pending_entered = true;
        (void)system.GetCpuManager().AwaitControlledCpuWrite(pending);
    });
    REQUIRE(Until([&] { return pending_entered.load() && pending->State() == Request::Phase::Accepted; }));
    REQUIRE(queued);
    gpu.SynchronizeGPUThread(); // FIFO acknowledgement only; native submission is still blocked.
    REQUIRE_FALSE(pending->Cancel()); // accepted uncertainty cannot be certified away
    device_memory.UnregisterProcess(pending_asid);
    auto exit = std::async(std::launch::async, [&] {
        system.RegisterHostThread();
        pending_owner.Close();
        const bool terminated = R_SUCCEEDED(pending_thread->Terminate());
        pending_thread->Close(); // Last thread/process references, including actual PTE Finalize.
        return terminated && Until([&] { return !pending_domain->alive.load(); });
    });
    const bool exited_without_gpu_drain = exit.wait_for(500ms) == std::future_status::ready;
    if (!exited_without_gpu_drain) submit_lock.unlock(); // break a regression's dependency cycle
    REQUIRE(exit.get());
    REQUIRE(exited_without_gpu_drain);
    REQUIRE(Until([&] { return !pending_domain->alive.load(); }));
    REQUIRE(pending->State() == Request::Phase::Unknown);
    REQUIRE(pending->Action() == Action::Quarantine);
    REQUIRE(!pending->Owner()->TryVisit([](auto&) {}));
    // Model reuse of a returned kernel frame through its independently owned physical pin.
    // Delayed preservation must not write the former process's GPU99 over this new content.
    std::fill(pending_pin->Bytes().begin(), pending_pin->Bytes().end(), u8{55});
    marker = false;
    REQUIRE(enqueue([&] { marker = true; return true; }));
    const bool unrelated_during_native_wait = Until([&] { return marker.load(); }, 500ms);
    submit_lock.unlock();
    REQUIRE(unrelated_during_native_wait);
    gpu.SynchronizeGPUThread();
    gpu.RunOnGpuThread([&] { renderer->scheduler.Finish(); }); // trusted device drain AFTER exit
    REQUIRE(pending_pin->Bytes()[6] == 55);
    REQUIRE(pending_pin->Bytes()[7] == 55);
    REQUIRE(pending->View().physical.PublicationSequence() == 0);
    gpu.RunOnGpuThread([&] { device_memory.Unmap(pending_address, Footprint); });

    std::cout << "PASS native guest KThread/KScheduler suspend/resume, all terminal observations, "
                 "64 full process teardowns, real cache/sys-core/PTE/counter contention; "
                 "GPU99 CPU77 GPU99; accepted native teardown without queue drain\n";
    // Unregister the process while its Memory/PTE still exist. The cache is owner-drained
    // through an ordinary queue barrier here; individual teardown loops above never drain it.
    gpu.RunOnGpuThread([&] { device_memory.Unmap(address, Footprint); });
    device_memory.UnregisterProcess(asid);
    owner.Close();
    EndThread(keeper);
}
TEST_CASE("F3 ordinary staged physical aliases reproduce stale content",
          "[.][f3][native][known-defect]") {
    // KernelCore's host dummy KThread is thread_local and retains its first Kernel.
    // Run this complete System lifetime in a separate process (CTest explicitly
    // selects [f3]); the default invocation retains the original scheduler suite.
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    Settings::values.use_uma_zero_copy.SetValue(false);
    Settings::values.cpu_debug_mode.SetValue(false);
    Core::System system;
    Window window;
    system.Initialize();
    TransferRenderer* renderer{};
    Access::Initialize(system, window, [&](Core::System& initialized) {
        auto result = std::make_unique<TransferRenderer>(window, initialized);
        renderer = result.get();
        return result;
    });
    SCOPE_EXIT { system.ShutdownMainProcess(); };
    auto& gpu = system.GPU();
    auto& rasterizer = renderer->rasterizer;
    auto& device_memory = system.Host1x().MemoryManager();
    auto channel = gpu.AllocateChannel();
    channel->memory_manager = std::make_shared<Tegra::MemoryManager>(system);
    gpu.RunOnGpuThread([&] { gpu.InitChannel(*channel, 0); gpu.BindChannel(channel->bind_id); });
    Process owner{system};
    auto& memory = owner.process->GetMemory();
    auto* keeper = owner.Start([&] {
        auto& kernel = system.Kernel();
        auto& self = Kernel::GetCurrentThread(kernel);
        while (!self.IsTerminationRequested())
            (void)self.Sleep(kernel.HardwareTimer().GetTick() + 1'000'000);
    });
    const auto asid = device_memory.RegisterProcess(&memory);
    constexpr DAddr A = 0x1000000, B = 0x2000000, C = 0x3000000, D = 0x4000000;
    constexpr u64 Page = 4096;
    std::vector<u8> initial(4 * Footprint, 10);
    std::fill(initial.begin() + 2 * Footprint, initial.begin() + 3 * Footprint, u8{55});
    REQUIRE(memory.WriteBlock(owner.heap, initial.data(), initial.size()));
    for (DAddr address : {A, B, C, D}) device_memory.AllocateFixed(address, Footprint);
    device_memory.Map(A, GetInteger(owner.heap), Footprint, asid);
    device_memory.Map(B, GetInteger(owner.heap), Footprint, asid);
    device_memory.Map(C, GetInteger(owner.heap) + Page, Footprint, asid);
    device_memory.Map(D, GetInteger(owner.heap) + 3 * Footprint, Footprint, asid);
    SCOPE_EXIT {
        std::cerr << "F3 cleanup: unmap aliases\n";
        gpu.RunOnGpuThread([&] {
            for (DAddr address : {A, B, C, D}) device_memory.Unmap(address, Footprint);
        });
        std::cerr << "F3 cleanup: unregister process\n";
        device_memory.UnregisterProcess(asid);
        std::cerr << "F3 cleanup: close process\n";
        owner.Close();
        std::cerr << "F3 cleanup: close keeper\n";
        EndThread(keeper);
        std::cerr << "F3 cleanup: guest owners released\n";
    };
    auto plan_a = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, A, Footprint);
    auto plan_b = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, B, Footprint);
    auto plan_c = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, C, Footprint);
    REQUIRE(plan_a);
    REQUIRE(plan_b);
    REQUIRE(plan_c);
    REQUIRE(plan_a->Segments()[0].storage.Epoch() == plan_b->Segments()[0].storage.Epoch());
    REQUIRE(plan_a->Segments()[0].storage.PhysicalOffset() == plan_b->Segments()[0].storage.PhysicalOffset());
    REQUIRE(plan_a->Segments()[1].storage.PhysicalOffset() == plan_c->Segments()[0].storage.PhysicalOffset());

    u8 full_alias{}, same_address{}, partial_alias{}, partial_source{}, untouched{}, disjoint{};
    bool stale_rejected{}, retained_after_retirement{}, fresh_identity{};
    bool full_source_valid{}, full_alias_uniform{}, partial_source_valid{}, partial_alias_uniform{};
    bool untouched_valid{}, disjoint_valid{}, retained_valid{}, remapped_valid{};
    bool stale_representation_rejected{}, equivalent_mapping_qualified{};
    u8 remapped{}, retained_value{};
    std::exception_ptr observation_error;
    gpu.RunOnGpuThread([&] {
        try {
        // Warm independent ordinary staged resources before any GPU write. No controlled
        // first-registration or CPU-write transaction is used by this witness.
        for (DAddr address : {A, B, C, D}) {
            const auto pin = Access::OrdinaryPin(rasterizer, address, Page);
            if (Access::ReadPin(rasterizer, pin)[0] != 10)
                throw std::runtime_error("F3 initial upload control failed");
        }
        const auto old_b = Access::OrdinaryPin(rasterizer, B, Page);
        const auto domain = plan_a->Segments()[0].storage.Epoch()->ranges;
        const std::array write_access{Common::PhysicalRangeAdmission::Access{
            plan_a->Segments()[0].storage.PhysicalOffset(), Page,
            Common::PhysicalRangeAdmission::Mode::Write}};
        const std::array read_access{Common::PhysicalRangeAdmission::Access{
            plan_b->Segments()[0].storage.PhysicalOffset(), Page,
            Common::PhysicalRangeAdmission::Mode::Read}};
        auto writer = domain->Enqueue(write_access, std::make_shared<int>(1));
        auto reader = domain->Enqueue(read_access, std::make_shared<int>(2));
        if (domain->TryStart(writer) != Common::PhysicalRangeAdmission::Start::Granted ||
            domain->TryStart(reader) != Common::PhysicalRangeAdmission::Start::Park)
            throw std::runtime_error("F3 alias admission control failed");
        Access::WriteOrdinaryGpu(rasterizer, A, Page, 0x63636363);
        if (!domain->Complete(writer) ||
            domain->TryStart(reader) != Common::PhysicalRangeAdmission::Start::Granted)
            throw std::runtime_error("F3 completed producer did not release reader");
        const auto all_bytes = [](std::span<const u8> bytes, u8 expected) {
            return std::ranges::all_of(bytes, [expected](u8 value) { return value == expected; });
        };
        const auto same = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, A, Page));
        same_address = same[0];
        full_source_valid = all_bytes(same, 99);
        const auto full = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, B, Page));
        full_alias = full[0];
        full_alias_uniform = all_bytes(full, full_alias);
        if (!domain->Complete(reader)) throw std::runtime_error("F3 reader completion failed");
        Access::WriteOrdinaryGpu(rasterizer, A + Page + 128, 256, 0x63636363);
        const auto source = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, A + Page, Page));
        partial_source = source[128];
        partial_source_valid = all_bytes(std::span{source}.subspan(128, 256), 99) &&
                              all_bytes(std::span{source}.first(128), 10) &&
                              all_bytes(std::span{source}.subspan(384), 10);
        const auto partial = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, C, Page));
        partial_alias = partial[128];
        partial_alias_uniform = all_bytes(std::span{partial}.subspan(128, 256), partial_alias);
        untouched = partial[0];
        untouched_valid = all_bytes(std::span{partial}.first(128), 10) &&
                          all_bytes(std::span{partial}.subspan(384), 10);
        const auto separate = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, D, Page));
        disjoint = separate[0];
        disjoint_valid = all_bytes(separate, 10);

        // Supported retire-before-remap control. A retained native allocation is still
        // usable as an OLD resource; it must not qualify the new physical association.
        Access::RetireOrdinary(rasterizer, B);
        retained_after_retirement = old_b.IsUsable();
        const auto retained = Access::ReadPin(rasterizer, old_b);
        retained_value = retained[0];
        retained_valid = all_bytes(retained, 10);
        device_memory.Unmap(B, Footprint);
        device_memory.Map(B, GetInteger(owner.heap) + 2 * Footprint, Footprint, asid);
        stale_rejected = !plan_b->MatchesCurrentDevice(device_memory, B);
        const auto new_b = Access::OrdinaryPin(rasterizer, B, Page);
        fresh_identity = old_b.Identity() != new_b.Identity();
        const auto fresh = Access::ReadPin(rasterizer, new_b);
        remapped = fresh[0];
        remapped_valid = all_bytes(fresh, 55);
        {
            auto& cache = Access::Cache(rasterizer);
            std::scoped_lock lock{cache.mutex};
            // A resource pin is not the old physical association: the replacement
            // B cannot qualify against its pre-remap plan. Conversely, A's physical
            // geometry is unchanged by remapping B, despite the global stamp change.
            stale_representation_rejected = !cache.PinExistingDmaRepresentation(B, *plan_b);
            equivalent_mapping_qualified = bool(cache.PinExistingDmaRepresentation(A, *plan_a));
        }
        } catch (...) {
            observation_error = std::current_exception();
        }
    });
    if (observation_error) std::rethrow_exception(observation_error);
    REQUIRE(same_address == 99);
    REQUIRE(partial_source == 99);
    REQUIRE(full_source_valid);
    REQUIRE(full_alias_uniform);
    REQUIRE(partial_source_valid);
    REQUIRE(partial_alias_uniform);
    REQUIRE(untouched_valid);
    REQUIRE(disjoint_valid);
    REQUIRE(retained_valid);
    REQUIRE(remapped_valid);
    REQUIRE(stale_representation_rejected);
    REQUIRE(equivalent_mapping_qualified);
    REQUIRE(untouched == 10);
    REQUIRE(disjoint == 10);
    REQUIRE(stale_rejected);
    REQUIRE(retained_after_retirement);
    REQUIRE(retained_value == 10);
    REQUIRE(fresh_identity);
    REQUIRE(remapped == 55);
    std::cout << "F3 full-page expected=99 observed=" << unsigned(full_alias)
              << "; partial-alias expected=99 observed=" << unsigned(partial_alias)
              << "; same-DAddr=" << unsigned(same_address)
              << "; remap=" << unsigned(remapped) << '\n';
    if (std::getenv("CITRON_REQUIRE_F3_FIXED")) {
        CHECK(full_alias == 99);
        CHECK(partial_alias == 99);
    } else {
        REQUIRE(full_alias == 10);
        REQUIRE(partial_alias == 10);
        std::cout << "F3 REPRODUCED (not fixed); real ordinary cache and Vulkan transfers\n";
    }
}
TEST_CASE("Admitted staged alias adapter copies exact physical dirty intersections",
          "[.][f3-transfer]") {
    using Phase = Vulkan::ControlledAliasPhase;
    using Admission = Common::PhysicalRangeAdmission;
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    Settings::values.use_uma_zero_copy.SetValue(false);
    Settings::values.cpu_debug_mode.SetValue(false);
    Core::System system;
    Window window;
    system.Initialize();
    TransferRenderer* renderer{};
    Access::Initialize(system, window, [&](Core::System& initialized) {
        auto result = std::make_unique<TransferRenderer>(window, initialized);
        renderer = result.get();
        return result;
    });
    SCOPE_EXIT { system.ShutdownMainProcess(); };
    auto& gpu = system.GPU();
    auto& rasterizer = renderer->rasterizer;
    auto& device_memory = system.Host1x().MemoryManager();
    const auto on_gpu = [&](auto&& callback) {
        std::exception_ptr error;
        gpu.RunOnGpuThread([&] {
            try { callback(); } catch (...) { error = std::current_exception(); }
        });
        if (error) std::rethrow_exception(error);
    };
    auto channel = gpu.AllocateChannel();
    channel->memory_manager = std::make_shared<Tegra::MemoryManager>(system);
    on_gpu([&] { gpu.InitChannel(*channel, 0); gpu.BindChannel(channel->bind_id); });
    Process owner{system};
    auto& memory = owner.process->GetMemory();
    auto* keeper = owner.Start([&] {
        auto& kernel = system.Kernel();
        auto& self = Kernel::GetCurrentThread(kernel);
        while (!self.IsTerminationRequested())
            (void)self.Sleep(kernel.HardwareTimer().GetTick() + 1'000'000);
    });
    constexpr DAddr A = 0x1000000, B = 0x2000000, C = 0x3000000,
                    D = 0x4000000, E = 0x5000000, F = 0x6000000;
    constexpr u32 Page = 4096;
    std::vector<u8> initial(5 * Footprint, 10);
    std::fill(initial.begin() + 4 * Footprint, initial.end(), u8{55});
    REQUIRE(memory.WriteBlock(owner.heap, initial.data(), initial.size()));
    const auto asid = device_memory.RegisterProcess(&memory);
    for (DAddr address : {A, B, C, D, E, F}) device_memory.AllocateFixed(address, Footprint);
    device_memory.Map(A, GetInteger(owner.heap), Footprint, asid);
    device_memory.Map(B, GetInteger(owner.heap), Footprint, asid);
    device_memory.Map(C, GetInteger(owner.heap) + Page, Footprint, asid);
    device_memory.Map(D, GetInteger(owner.heap) + 3 * Footprint, Footprint, asid);
    device_memory.Map(E, GetInteger(owner.heap) + 3 * Footprint, Footprint, asid);
    device_memory.Map(F, GetInteger(owner.heap), Footprint, asid);
    SCOPE_EXIT {
        on_gpu([&] {
            for (DAddr address : {A, B, C, D, E, F}) device_memory.Unmap(address, Footprint);
        });
        device_memory.UnregisterProcess(asid);
        owner.Close();
        EndThread(keeper);
    };
    const auto read = [&](DAddr address) {
        std::vector<u8> bytes;
        on_gpu([&] { bytes = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, address, Page)); });
        return bytes;
    };
    for (DAddr address : {A, B, C, D, E, F}) REQUIRE(read(address) == std::vector<u8>(Page, 10));
    const auto begin = [&](DAddr source, DAddr target) {
        std::shared_ptr<Vulkan::ControlledAliasCopy> op;
        on_gpu([&] { op = rasterizer.BeginControlledAliasCopy(source, target, Page); });
        return op;
    };
    const auto poll = [&](const auto& op) {
        Phase phase{};
        on_gpu([&] { phase = rasterizer.PollControlledAliasCopy(op); });
        return phase;
    };
    const auto finish = [&](const auto& op, Phase expected) {
        Phase phase{};
        REQUIRE(Until([&] {
            phase = poll(op);
            return phase == Phase::Complete || phase == Phase::Refused || phase == Phase::Unknown;
        }));
        REQUIRE(phase == expected);
        REQUIRE(poll(op) == expected); // One-shot terminal state, never replay.
    };
    REQUIRE_FALSE(begin(A, B)); // Clean source is not a GPU content authority.
    on_gpu([&] {
        Access::WriteOrdinaryGpu(rasterizer, A + 128, 256, 0x63636363);
        Access::WriteOrdinaryGpu(rasterizer, B + 512, 256, 0x58585858);
        Access::WriteOrdinaryGpu(rasterizer, F + 128, 256, 0x21212121);
    });
    REQUIRE_FALSE(begin(A, B)); // A third physical alias can own newer dirty content.
    on_gpu([&] {
        Access::RetireOrdinary(rasterizer, F);
        device_memory.Unmap(F, Footprint);
        Access::WriteOrdinaryGpu(rasterizer, A + 128, 256, 0x63636363);
    });
    REQUIRE_FALSE(begin(A, A));
    REQUIRE_FALSE(begin(A, D)); // Different physical backing must not copy by DAddr shape.
    auto rewritten_target = begin(A, B);
    REQUIRE(rewritten_target);
    // Same target bytes and dirty shape, but a distinct producer intent. A
    // resource-wide revision conservatively rejects even this disjoint change.
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, B + 512, 256, 0x58585858); });
    finish(rewritten_target, Phase::Refused);
    auto target_before_copy = std::vector<u8>(Page, 10);
    std::fill(target_before_copy.begin() + 512, target_before_copy.begin() + 768, u8{88});
    REQUIRE(read(B) == target_before_copy);
    auto plan = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, A, Page);
    REQUIRE(plan);
    auto domain = plan->Segments().front().storage.Epoch()->ranges;
    const auto physical = plan->Segments().front().storage.PhysicalOffset();
    const std::array block_range{Admission::Access{physical + 128, 256, Admission::Mode::Write}};
    auto blocker = domain->Enqueue(block_range, std::make_shared<int>(1));
    REQUIRE(domain->TryStart(blocker) == Admission::Start::Granted);
    auto partial = begin(A, B);
    REQUIRE(partial);
    REQUIRE(poll(partial) == Phase::Parked);
    auto expected = std::vector<u8>(Page, 10);
    std::fill(expected.begin() + 512, expected.begin() + 768, u8{88});
    REQUIRE(read(B) == expected);
    on_gpu([&] {
        // An unrelated remap changes the global stamp, not either admitted association.
        device_memory.Unmap(E, Footprint);
        device_memory.Map(E, GetInteger(owner.heap) + 3 * Footprint, Footprint, asid);
    });
    REQUIRE(domain->Complete(blocker));
    finish(partial, Phase::Complete);
    std::fill(expected.begin() + 128, expected.begin() + 384, u8{99});
    REQUIRE(read(B) == expected); // Both untouched complements and B's dirty 88 survive.
    auto reverse = begin(B, A);
    REQUIRE(reverse);
    finish(reverse, Phase::Complete);
    REQUIRE(read(A) == expected); // Byte-disjoint reverse writer within the same page.
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, B + 128, 256, 0x4d4d4d4d); });
    REQUIRE_FALSE(begin(B, A)); // Two overlapping dirty owners: no fabricated latest version.
    REQUIRE(read(A) == expected);
    REQUIRE(read(D) == std::vector<u8>(Page, 10));

    (void)read(A + Page);
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, C, Page, 0x42424242); });
    auto rewritten_source = begin(C, A + Page);
    REQUIRE(rewritten_source);
    // Repeating exactly the dirty interval preserves its RangeSet geometry.
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, C, Page, 0x37373737); });
    finish(rewritten_source, Phase::Refused);
    REQUIRE(read(A + Page) == std::vector<u8>(Page, 10));
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, C, Page, 0x42424242); });
    auto full = begin(C, A + Page);
    REQUIRE(full);
    finish(full, Phase::Complete);
    REQUIRE(read(A + Page) == std::vector<u8>(Page, 66));
    REQUIRE(read(A) == expected);
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, C, Page, 0x37373737); });
    auto retired_target = begin(C, A + Page);
    REQUIRE(retired_target);
    std::optional<Vulkan::StagedBufferPin> retained;
    on_gpu([&] {
        retained = Access::OrdinaryPin(rasterizer, A + Page, Page);
        Access::RetireOrdinary(rasterizer, A);
    });
    finish(retired_target, Phase::Refused);
    std::vector<u8> old_bytes;
    on_gpu([&] { old_bytes = Access::ReadPin(rasterizer, *retained); });
    REQUIRE(old_bytes == std::vector<u8>(Page, 66));
    (void)read(B + Page);
    auto retired_source = begin(C, B + Page);
    REQUIRE(retired_source);
    on_gpu([&] { Access::RetireOrdinary(rasterizer, C); });
    finish(retired_source, Phase::Refused);
    REQUIRE(read(B + Page) == std::vector<u8>(Page, 10));

    (void)read(D + Page);
    (void)read(E + Page);
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, D + Page, Page, 0x2c2c2c2c); });
    std::shared_ptr<Vulkan::ControlledAliasCopy> submitted_rewrite;
    on_gpu([&] {
        submitted_rewrite = rasterizer.BeginControlledAliasCopy(D + Page, E + Page, Page);
    });
    REQUIRE(submitted_rewrite);
    REQUIRE(poll(submitted_rewrite) == Phase::Submitted);
    // Queue ordering and Finish complete the old copy/readbacks before this new
    // same-interval write finishes. Matching readbacks alone must not accept it.
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, D + Page, Page, 0x4d4d4d4d); });
    finish(submitted_rewrite, Phase::Unknown);
    REQUIRE(read(D + Page) == std::vector<u8>(Page, 77));
    REQUIRE(read(E + Page) == std::vector<u8>(Page, 44));
    auto rewritten_plan = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, D + Page, Page);
    REQUIRE(rewritten_plan);
    const std::array rewritten_range{Admission::Access{
        rewritten_plan->Segments().front().storage.PhysicalOffset(), Page, Admission::Mode::Read}};
    auto rewritten_reader = domain->Enqueue(rewritten_range, std::make_shared<int>(4));
    REQUIRE(domain->TryStart(rewritten_reader) == Admission::Start::Park);
    REQUIRE(domain->Cancel(rewritten_reader));

    (void)read(E);
    on_gpu([&] { Access::WriteOrdinaryGpu(rasterizer, D, Page, 0x2c2c2c2c); });
    auto remapped = begin(D, E);
    REQUIRE(remapped);
    REQUIRE(poll(remapped) == Phase::Submitted);
    on_gpu([&] {
        Access::RetireOrdinary(rasterizer, E);
        device_memory.Unmap(E, Footprint);
        device_memory.Map(E, GetInteger(owner.heap) + 4 * Footprint, Footprint, asid);
    });
    finish(remapped, Phase::Unknown); // Effects recorded for OLD retained allocation only.
    REQUIRE(read(E) == std::vector<u8>(Page, 55));
    auto old_plan = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, D, Page);
    REQUIRE(old_plan);
    const std::array blocked_range{Admission::Access{
        old_plan->Segments().front().storage.PhysicalOffset(), Page, Admission::Mode::Read}};
    auto blocked_reader = domain->Enqueue(blocked_range, std::make_shared<int>(2));
    REQUIRE(domain->TryStart(blocked_reader) == Admission::Start::Park);
    REQUIRE(domain->Cancel(blocked_reader));
    const std::array separate_range{Admission::Access{physical, Page, Admission::Mode::Read}};
    auto separate_reader = domain->Enqueue(separate_range, std::make_shared<int>(3));
    REQUIRE(domain->TryStart(separate_reader) == Admission::Start::Granted);
    REQUIRE(domain->Complete(separate_reader));
    on_gpu([&] {
        renderer->scheduler.Finish();
        std::scoped_lock lock{renderer->scheduler.submit_mutex};
        if (renderer->device.GetLogical().WaitIdle() != VK_SUCCESS)
            throw std::runtime_error("alias test trusted device-idle drain failed");
        rasterizer.RetireControlledTransfersAfterDeviceIdle();
    });
    REQUIRE(poll(remapped) == Phase::Unknown); // Trusted idle alone does not forgive Unknown.
    REQUIRE(poll(submitted_rewrite) == Phase::Unknown);
    std::cout << "PASS admitted alias adapter: exact partial/full physical copies, dirty complements, "
                 "parking, disjoint reverse write, overlapping-writer refusal, retained retirement, "
                 "remap Unknown and quarantine; ordinary F3 remains Open\n";
}
TEST_CASE("Checked write provenance separates emission completion and current representation",
          "[.][f3-provenance]") {
    using State = Vulkan::Scheduler::WriteState;
    Settings::values.use_multi_core.SetValue(true);
    Settings::values.use_asynchronous_gpu_emulation.SetValue(true);
    Settings::values.use_uma_zero_copy.SetValue(false);
    Settings::values.cpu_debug_mode.SetValue(false);
    Core::System system;
    Window window;
    system.Initialize();
    TransferRenderer* renderer{};
    Access::Initialize(system, window, [&](Core::System& initialized) {
        auto result = std::make_unique<TransferRenderer>(window, initialized);
        renderer = result.get();
        return result;
    });
    SCOPE_EXIT { system.ShutdownMainProcess(); };
    auto& gpu = system.GPU();
    auto& rasterizer = renderer->rasterizer;
    auto& device_memory = system.Host1x().MemoryManager();
    const auto on_gpu = [&](auto&& callback) {
        std::exception_ptr error;
        gpu.RunOnGpuThread([&] {
            try { callback(); } catch (...) { error = std::current_exception(); }
        });
        if (error) std::rethrow_exception(error);
    };
    auto channel = gpu.AllocateChannel();
    channel->memory_manager = std::make_shared<Tegra::MemoryManager>(system);
    on_gpu([&] { gpu.InitChannel(*channel, 0); gpu.BindChannel(channel->bind_id); });
    Process owner{system};
    auto& memory = owner.process->GetMemory();
    auto* keeper = owner.Start([&] {
        auto& kernel = system.Kernel();
        auto& self = Kernel::GetCurrentThread(kernel);
        while (!self.IsTerminationRequested())
            (void)self.Sleep(kernel.HardwareTimer().GetTick() + 1'000'000);
    });
    constexpr DAddr A = 0x1000000, B = 0x2000000, C = 0x4000000;
    constexpr u32 Page = 4096;
    std::vector<u8> initial(2 * Footprint, 10);
    std::fill(initial.begin() + Footprint, initial.end(), u8{55});
    REQUIRE(memory.WriteBlock(owner.heap, initial.data(), initial.size()));
    const auto asid = device_memory.RegisterProcess(&memory);
    for (DAddr address : {A, B, C}) {
        device_memory.AllocateFixed(address, Footprint);
        device_memory.Map(address, GetInteger(owner.heap), Footprint, asid);
    }
    SCOPE_EXIT {
        on_gpu([&] { for (DAddr address : {A, B, C}) device_memory.Unmap(address, Footprint); });
        device_memory.UnregisterProcess(asid);
        owner.Close();
        EndThread(keeper);
    };
    const auto read = [&](DAddr address) {
        std::vector<u8> bytes;
        on_gpu([&] { bytes = Access::ReadPin(rasterizer, Access::OrdinaryPin(rasterizer, address, Page)); });
        return bytes;
    };
    const auto poll = [&](const Vulkan::Scheduler::Write& write) {
        State state{};
        on_gpu([&] { state = renderer->scheduler.PollCheckedWrite(write); });
        return state;
    };
    const auto finish = [&](const Vulkan::Scheduler::Write& write, State expected) {
        State state{};
        REQUIRE(Until([&] {
            state = poll(write);
            return state != State::Pending && state != State::Submitted;
        }));
        REQUIRE(state == expected);
    };
    const auto fill = [&](DAddr address, u32 value, std::function<bool()> emit = {}) {
        Vulkan::Scheduler::Write write;
        on_gpu([&] { write = Access::CheckedOrdinaryFill(rasterizer, address, Page, value, std::move(emit)); });
        return write;
    };
    REQUIRE(read(A) == std::vector<u8>(Page, 10));
    REQUIRE(read(B) == std::vector<u8>(Page, 10));
    auto plan = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, A, Page);
    REQUIRE(plan);
    auto skipped = fill(A, 0x63636363, [] { return false; });
    REQUIRE(skipped);
    finish(skipped, State::Skipped); // Intent and a completed empty submission are not content.
    REQUIRE(read(A) == std::vector<u8>(Page, 10));
    auto first = fill(A, 0x42424242);
    REQUIRE(first);
    finish(first, State::Complete);
    REQUIRE(read(A) == std::vector<u8>(Page, 66));
    std::optional<VideoCommon::AliasWriteSnapshot> snapshot;
    on_gpu([&] {
        std::scoped_lock lock{Access::Cache(rasterizer).mutex};
        snapshot = Access::Cache(rasterizer).SnapshotAliasWrites(A, Page);
    });
    REQUIRE(snapshot);
    REQUIRE(first->IntentRevision() == snapshot->revision);
    auto second = fill(A, 0x4d4d4d4d);
    REQUIRE(second);
    REQUIRE(first->ResourceIdentity() == second->ResourceIdentity());
    REQUIRE(first->SubmissionIdentity() != second->SubmissionIdentity());
    REQUIRE(first->Offset() == 0);
    REQUIRE(first->Size() == Page);
    std::optional<VideoCommon::AliasWriteSnapshot> changed;
    on_gpu([&] {
        std::scoped_lock lock{Access::Cache(rasterizer).mutex};
        changed = Access::Cache(rasterizer).SnapshotAliasWrites(A, Page);
    });
    REQUIRE(changed);
    REQUIRE(changed->ranges == snapshot->ranges);
    REQUIRE(*changed != *snapshot); // Same dirty interval, distinct write intent.
    REQUIRE(second->IntentRevision() == changed->revision);
    REQUIRE(poll(first) == State::Complete); // Historical completion is not current authority.
    finish(second, State::Complete);
    REQUIRE(read(A) == std::vector<u8>(Page, 77));
    REQUIRE(read(B) == std::vector<u8>(Page, 10)); // Independent replica is never relabelled.

    auto entered = std::make_shared<std::promise<void>>();
    auto release = std::make_shared<std::promise<void>>();
    auto released = release->get_future().share();
    // Always release the worker on assertion/exception unwinding, before shutdown.
    SCOPE_EXIT { try { release->set_value(); } catch (...) {} };
    auto delayed = fill(A, 0x63636363, [entered, released] {
        entered->set_value();
        return released.wait_for(5s) == std::future_status::ready;
    });
    REQUIRE(delayed);
    REQUIRE(entered->get_future().wait_for(5s) == std::future_status::ready);
    REQUIRE(poll(delayed) == State::Pending);
    REQUIRE(poll(first) == State::Complete);
    std::optional<Vulkan::StagedBufferPin> old_pin;
    on_gpu([&] {
        // No synchronization/readback while the recording worker is deliberately parked.
        std::scoped_lock lock{Access::Cache(rasterizer).mutex};
        old_pin = Access::Cache(rasterizer).PinExistingDmaRepresentation(A, *plan);
    });
    REQUIRE(old_pin);
    on_gpu([&] {
        Access::RetireOrdinary(rasterizer, A);
        device_memory.Unmap(A, Footprint);
        device_memory.Map(A, GetInteger(owner.heap) + Footprint, Footprint, asid);
    });
    bool stale_current{};
    on_gpu([&] {
        std::scoped_lock lock{Access::Cache(rasterizer).mutex};
        stale_current = Access::Cache(rasterizer).PinExistingDmaRepresentation(A, *plan).has_value();
    });
    REQUIRE_FALSE(stale_current);
    release->set_value();
    finish(delayed, State::Complete); // Completes OLD native allocation, never the new mapping.
    REQUIRE(read(A) == std::vector<u8>(Page, 55));
    on_gpu([&] {
        std::scoped_lock lock{Access::Cache(rasterizer).mutex};
        stale_current = Access::Cache(rasterizer).PinExistingDmaRepresentation(A, *plan).has_value();
    });
    REQUIRE_FALSE(stale_current); // Reject physical mismatch even after a new allocation is warmed.
    REQUIRE(read(B) == std::vector<u8>(Page, 10));
    std::vector<u8> old_bytes;
    on_gpu([&] { old_bytes = Access::ReadPin(rasterizer, *old_pin); });
    REQUIRE(old_bytes == std::vector<u8>(Page, 99));
    REQUIRE(delayed->ResourceIdentity() == old_pin->Identity());
    Vulkan::Scheduler::Write batch_first, batch_second, next_batch;
    std::array<State, 2> batch_pending{};
    on_gpu([&] {
        batch_first = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x21212121);
        batch_second = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x22222222);
        batch_pending = {renderer->scheduler.PollCheckedWrite(batch_first),
                         renderer->scheduler.PollCheckedWrite(batch_second)};
        renderer->scheduler.Flush(); // The ordinary flush submits both observed writes.
    });
    REQUIRE(batch_first);
    REQUIRE(batch_second);
    REQUIRE(batch_first->SubmissionIdentity() == batch_second->SubmissionIdentity());
    REQUIRE(batch_first->IntentRevision() != batch_second->IntentRevision());
    REQUIRE(batch_pending[0] == State::Pending);
    REQUIRE(batch_pending[1] == State::Pending);
    finish(batch_first, State::Complete);
    finish(batch_second, State::Complete);
    REQUIRE(read(A) == std::vector<u8>(Page, 34));
    on_gpu([&] {
        next_batch = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x23232323);
        renderer->scheduler.Finish(); // Ordinary Finish also publishes the actual checked result.
    });
    REQUIRE(next_batch);
    REQUIRE(next_batch->SubmissionIdentity() != batch_first->SubmissionIdentity());
    finish(next_batch, State::Complete);
    REQUIRE(read(A) == std::vector<u8>(Page, 35));
    REQUIRE(read(B) == std::vector<u8>(Page, 10));
    // Immutable physical identity slice: real mappings/cache/queue, transfer
    // producers only. The ordinary shader binding wiring remains a fault fixture.
    auto missing_coverage = std::make_shared<VideoCommon::PhysicalWriteCoverage>();
    Vulkan::Scheduler::Write missing_origin;
    bool enabled{};
    on_gpu([&] {
        enabled = rasterizer.SetControlledStorageWriteObservation(true);
        missing_origin = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x23232323, missing_coverage);
        renderer->scheduler.Flush();
    });
    REQUIRE(enabled);
    REQUIRE(missing_origin);
    REQUIRE_FALSE(missing_origin->HistoricalPhysicalIdentity());
    REQUIRE_FALSE(missing_coverage->Intact()); // Existing allocation cannot acquire a new origin.
    missing_coverage->Seal();
    finish(missing_origin, State::Complete);
    const VideoCommon::PhysicalWriteIdentity* completed_identity{};
    on_gpu([&] { completed_identity = renderer->scheduler.CompletedPhysicalIdentity(missing_origin); });
    REQUIRE(completed_identity == nullptr);

    constexpr DAddr Alias = 0x3000000;
    device_memory.AllocateFixed(Alias, Footprint);
    on_gpu([&] { device_memory.Map(Alias, GetInteger(owner.heap) + Footprint, Footprint, asid); });
    SCOPE_EXIT { on_gpu([&] { device_memory.Unmap(Alias, Footprint); }); };
    auto physical_coverage = std::make_shared<VideoCommon::PhysicalWriteCoverage>();
    Vulkan::Scheduler::Write physical_a, physical_alias, missing_barrier, wrong_stage,
        ordered_partial, separate_submission;
    std::optional<Vulkan::StagedBufferPin> retained_physical_a;
    on_gpu([&] {
        Access::RetireOrdinary(rasterizer, A);
        physical_a = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x31313131, physical_coverage);
        retained_physical_a = Access::OrdinaryPin(rasterizer, A, Page);
        // The fill helper's internal barrier keeps native execution valid, but is
        // deliberately not enrolled as provenance. Missing evidence must refuse.
        missing_barrier = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x31313131, physical_coverage);
        renderer->scheduler.WaitForIdle(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        wrong_stage = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x31313131, physical_coverage);
        renderer->scheduler.WaitForIdle(VK_PIPELINE_STAGE_TRANSFER_BIT);
        ordered_partial = Access::ObserveOrdinaryFill(rasterizer, A + 128, 512, 0x31313131, physical_coverage);
        physical_alias = Access::ObserveOrdinaryFill(rasterizer, Alias, Page, 0x32323232, physical_coverage);
        renderer->scheduler.Flush();
        separate_submission = Access::ObserveOrdinaryFill(rasterizer, A, Page, 0x31313131, physical_coverage);
        renderer->scheduler.Flush(); // No recorded semaphore dependency between these batches.
    });
    REQUIRE(physical_a);
    REQUIRE(physical_alias);
    REQUIRE(retained_physical_a);
    const auto historical_a = physical_a->HistoricalPhysicalIdentity();
    const auto historical_alias = physical_alias->HistoricalPhysicalIdentity();
    REQUIRE(historical_a);
    REQUIRE(historical_alias);
    REQUIRE(historical_a->Representation() == physical_a->ResourceIdentity());
    REQUIRE(historical_a->NativeOffset() == physical_a->Offset());
    REQUIRE(historical_a->Size() == Page);
    REQUIRE(historical_a->Backing() == historical_alias->Backing());
    REQUIRE(historical_a->OverlapsPhysical(*historical_alias));
    REQUIRE(physical_a->ResourceIdentity() != physical_alias->ResourceIdentity());
    REQUIRE(historical_a->DeviceAddress() == A);
    REQUIRE(historical_alias->DeviceAddress() == Alias);
    REQUIRE_FALSE(physical_a->PhysicalCoverageComplete()); // Active prefix is not a complete scope.
    REQUIRE(Until([&] {
        const auto state = poll(physical_a);
        return state == State::Submitted || state == State::Complete;
    })); // Actual successful queue submission precedes the following remap.
    const std::vector<VideoCommon::PhysicalWriteIdentity::Segment> original_geometry{
        historical_a->Segments().begin(), historical_a->Segments().end()};
    on_gpu([&] {
        device_memory.Unmap(A, Footprint);
        device_memory.Map(A, GetInteger(owner.heap), Footprint, asid);
        enabled = rasterizer.SetControlledStorageWriteObservation(false);
    });
    REQUIRE(enabled);
    physical_coverage->Seal();
    finish(physical_a, State::Complete);
    finish(physical_alias, State::Complete);
    finish(missing_barrier, State::Complete);
    finish(wrong_stage, State::Complete);
    finish(ordered_partial, State::Complete);
    finish(separate_submission, State::Complete);
    std::array<bool, 6> dependencies{};
    on_gpu([&] {
        const auto physical = historical_a->Segments().front().physical_offset;
        dependencies = {
            renderer->scheduler.HasConfirmedMemoryDependency(physical_a, missing_barrier, physical, Page),
            renderer->scheduler.HasConfirmedMemoryDependency(physical_a, wrong_stage, physical, Page),
            renderer->scheduler.HasConfirmedMemoryDependency(physical_a, ordered_partial, physical + 128, 512),
            renderer->scheduler.HasConfirmedMemoryDependency(physical_a, ordered_partial, physical, Page),
            renderer->scheduler.HasConfirmedMemoryDependency(physical_a, physical_alias, physical, Page),
            renderer->scheduler.HasConfirmedMemoryDependency(physical_a, separate_submission, physical, Page)};
    });
    REQUIRE_FALSE(dependencies[0]);
    REQUIRE_FALSE(dependencies[1]);
    REQUIRE(dependencies[2]);
    REQUIRE_FALSE(dependencies[3]);
    REQUIRE_FALSE(dependencies[4]);
    REQUIRE_FALSE(dependencies[5]);
    REQUIRE(physical_a->HistoricalPhysicalIdentity() == historical_a);
    REQUIRE(std::ranges::equal(historical_a->Segments(), original_geometry));
    REQUIRE_FALSE(historical_a->MatchesCurrent(device_memory));
    REQUIRE_FALSE(historical_alias->MatchesCurrent(device_memory)); // Global mapping history changed.
    REQUIRE(historical_alias->MatchesCurrent(device_memory, false)); // Geometry alone is diagnostic.
    on_gpu([&] { completed_identity = renderer->scheduler.CompletedPhysicalIdentity(physical_a); });
    REQUIRE(completed_identity == historical_a.get()); // OLD historical identity, never current authority.
    std::vector<u8> physical_old_bytes;
    on_gpu([&] { physical_old_bytes = Access::ReadPin(rasterizer, *retained_physical_a); });
    REQUIRE(physical_old_bytes == std::vector<u8>(Page, 49));
    REQUIRE(read(Alias) == std::vector<u8>(Page, 50)); // Independent staged replica, no source selection.
    // Explicit transfer-only content history. No normal cache producer is enrolled
    // or repaired, and nothing publishes a latest physical writer.
    using Content = Vulkan::TransferContent;
    using Kind = Content::Kind;
    std::vector<Vulkan::Scheduler::TransferBuffer> content_targets;
    Vulkan::Scheduler::Transfer content_transfer;
    const std::array<Content::Command, 4> content_commands{{
        {Kind::Fill, 1, 0, 0, 0, 64, 0x0a0a0a0a},
        {Kind::Fill, 0, 0, 0, 0, 64, 0x63636363},
        {Kind::Copy, 1, 0, 16, 16, 16},
        {Kind::Fill, 0, 0, 0, 0, 64, 0x4d4d4d4d},
    }};
    on_gpu([&] {
        enabled = rasterizer.SetControlledStorageWriteObservation(true);
        Access::RetireOrdinary(rasterizer, A);
        Access::RetireOrdinary(rasterizer, B);
        content_targets.push_back(Access::ContentTransferTarget(rasterizer, A, 64));
        content_targets.push_back(Access::ContentTransferTarget(rasterizer, B, 64));
        renderer->scheduler.Finish();
        content_transfer = renderer->scheduler.RecordTransferBatch(content_targets, content_commands);
    });
    REQUIRE(enabled);
    REQUIRE(content_transfer);
    REQUIRE(content_targets[0].pin.Identity() != content_targets[1].pin.Identity());
    REQUIRE(content_targets[0].physical->OverlapsPhysical(*content_targets[1].physical));
    const auto finish_content = [&](const Vulkan::Scheduler::Transfer& transfer) {
        REQUIRE(Until([&] {
            State state{};
            on_gpu([&] { state = renderer->scheduler.PollCheckedTransfer(transfer); });
            return state == State::Complete || state == State::Unknown;
        }));
        State state{};
        on_gpu([&] { state = renderer->scheduler.PollCheckedTransfer(transfer); });
        REQUIRE(state == State::Complete);
    };
    finish_content(content_transfer);
    const Content* content{};
    on_gpu([&] { content = renderer->scheduler.TransferContentForMapping(content_transfer, device_memory); });
    REQUIRE(content);
    const Content* current_content{};
    bool current_root{};
    on_gpu([&] { current_content = renderer->scheduler.CurrentTransferContent(content_transfer, device_memory); });
    REQUIRE(current_content == content);
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(content_transfer, device_memory, 1, 16, 16, 1, 16); });
    REQUIRE(current_root);
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(content_transfer, device_memory, 1, 0, 64, 0, 0); });
    REQUIRE_FALSE(current_root); // Complete eligibility does not flatten mixed content roots.
    REQUIRE(content->HasRoot(1, 16, 16, 1, 16));
    REQUIRE(content->HasRoot(1, 0, 16, 0, 0));
    REQUIRE(content->HasRoot(1, 32, 32, 0, 32));
    REQUIRE_FALSE(content->HasRoot(1, 0, 64, 1, 0));
    REQUIRE_FALSE(content->HasRoot(1, 16, 16, 3, 16));
    REQUIRE(content->WrittenBy(2, 1, 16, 16));
    REQUIRE_FALSE(content->WrittenBy(2, 1, 0, 64));
    std::vector<u8> content_a, content_b;
    on_gpu([&] {
        content_a = Access::ReadPin(rasterizer, content_targets[0].pin);
        content_b = Access::ReadPin(rasterizer, content_targets[1].pin);
    });
    REQUIRE(content_a == std::vector<u8>(64, 77));
    auto mixed = std::vector<u8>(64, 10);
    std::fill(mixed.begin() + 16, mixed.begin() + 32, u8{99});
    REQUIRE(content_b == mixed);
    const Content *left_content{}, *middle_content{}, *right_content{};
    const auto immutable_history = content_transfer->PlannedContent();
    on_gpu([&] {
        Access::FillPinRange(rasterizer, content_targets[1].pin, 16, 16, 0xeeeeeeee);
        left_content = renderer->scheduler.CurrentTransferContentRange(content_transfer, device_memory, 1, 0, 16);
        middle_content = renderer->scheduler.CurrentTransferContentRange(content_transfer, device_memory, 1, 16, 16);
        right_content = renderer->scheduler.CurrentTransferContentRange(content_transfer, device_memory, 1, 32, 32);
        current_content = renderer->scheduler.CurrentTransferContentRange(content_transfer, device_memory, 0, 0, 64);
    });
    REQUIRE(left_content == immutable_history.get());
    REQUIRE(middle_content == nullptr);
    REQUIRE(right_content == immutable_history.get());
    REQUIRE(current_content == immutable_history.get()); // Different native replica, same physical bytes.
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(content_transfer, device_memory, 1, 0, 16, 0, 0); });
    REQUIRE(current_root);
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(content_transfer, device_memory, 1, 16, 16, 1, 16); });
    REQUIRE_FALSE(current_root); // The historical root still exists, but these bytes are no longer current.
    on_gpu([&] { content_b = Access::ReadPin(rasterizer, content_targets[1].pin); });
    std::fill(mixed.begin() + 16, mixed.begin() + 32, u8{238});
    REQUIRE(content_b == mixed);
    REQUIRE(immutable_history->HasRoot(1, 16, 16, 1, 16)); // History still records the earlier 99.
    Vulkan::Scheduler::Transfer unknown_root;
    const std::array<Content::Command, 1> copy_only{{{Kind::Copy, 1, 0, 0, 0, 64}}};
    on_gpu([&] { unknown_root = renderer->scheduler.RecordTransferBatch(content_targets, copy_only); });
    REQUIRE(unknown_root);
    finish_content(unknown_root);
    on_gpu([&] {
        current_content = renderer->scheduler.CurrentTransferContent(content_transfer, device_memory);
    });
    REQUIRE(current_content == nullptr); // Later recorded Copy revoked the earlier snapshot.
    on_gpu([&] { current_content = renderer->scheduler.CurrentTransferContent(unknown_root, device_memory); });
    REQUIRE(current_content);
    const Content* unknown_content{};
    on_gpu([&] { unknown_content = renderer->scheduler.CompletedTransferContent(unknown_root); });
    REQUIRE(unknown_content);
    REQUIRE(unknown_content->WrittenBy(0, 1, 0, 64));
    REQUIRE_FALSE(unknown_content->HasRoot(1, 0, 64, 0, 0)); // No cross-submission root import.
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(unknown_root, device_memory, 1, 0, 64, 0, 0); });
    REQUIRE_FALSE(current_root); // Current eligibility and a successful readback cannot manufacture a root.
    on_gpu([&] { content_b = Access::ReadPin(rasterizer, content_targets[1].pin); });
    REQUIRE(content_b == std::vector<u8>(64, 77));
    REQUIRE(content->HasRoot(1, 16, 16, 1, 16)); // Earlier history never becomes current content.
    on_gpu([&] { current_content = renderer->scheduler.CurrentTransferContent(unknown_root, device_memory); });
    REQUIRE(current_content); // Typed readback changes only its staging destination.
    Vulkan::Scheduler::Transfer retirement_snapshot;
    const std::array<Content::Command, 1> retirement_fill{{{Kind::Fill, 0, 0, 0, 0, 64, 0x12121212}}};
    on_gpu([&] { retirement_snapshot = renderer->scheduler.RecordTransferBatch(
        std::span{content_targets}.subspan(1, 1), retirement_fill); });
    REQUIRE(retirement_snapshot);
    finish_content(retirement_snapshot);
    const auto retirement_history = retirement_snapshot->PlannedContent();
    on_gpu([&] { current_content = renderer->scheduler.CurrentTransferContent(retirement_snapshot, device_memory); });
    REQUIRE(current_content == retirement_history.get());
    on_gpu([&] {
        Access::CopyPinRange(rasterizer, content_targets[1].pin, content_targets[0].pin, 16, 16);
        left_content = renderer->scheduler.CurrentTransferContentRange(retirement_snapshot, device_memory, 0, 0, 16);
        middle_content = renderer->scheduler.CurrentTransferContentRange(retirement_snapshot, device_memory, 0, 16, 16);
        right_content = renderer->scheduler.CurrentTransferContentRange(retirement_snapshot, device_memory, 0, 32, 32);
        content_b = Access::ReadPin(rasterizer, content_targets[1].pin);
    });
    REQUIRE(left_content == retirement_history.get());
    REQUIRE(middle_content == nullptr);
    REQUIRE(right_content == retirement_history.get());
    auto copied_middle = std::vector<u8>(64, 18);
    std::fill(copied_middle.begin() + 16, copied_middle.begin() + 32, u8{77});
    REQUIRE(content_b == copied_middle);
    REQUIRE(retirement_history->HasRoot(0, 0, 64, 0, 0));
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(retirement_snapshot, device_memory, 0, 0, 64, 0, 0); });
    REQUIRE_FALSE(current_root); // A true historical full-range root is not a current full-range root.
    on_gpu([&] { current_root = renderer->scheduler.CurrentTransferHasRoot(retirement_snapshot, device_memory, 0, 0, 16, 0, 0); });
    REQUIRE(current_root);
    on_gpu([&] {
        Access::RetireOrdinary(rasterizer, B);
        current_content = renderer->scheduler.CurrentTransferContentRange(retirement_snapshot, device_memory, 0, 0, 16);
    });
    REQUIRE(current_content == nullptr);
    on_gpu([&] { current_content = renderer->scheduler.CompletedTransferContent(retirement_snapshot); });
    REQUIRE(current_content == retirement_history.get()); // Cache retirement retains immutable history and pins.
    // Bounded same-queue copies, using private staged buffers outside ordinary cache selection.
    std::array<std::shared_ptr<Vulkan::Buffer>, 3> isolated_owners;
    std::vector<Vulkan::Scheduler::TransferBuffer> isolated_targets;
    Vulkan::Scheduler::Transfer replica_parent, replica_copy, replica_second, asynchronous_parent, asynchronous_first, asynchronous_copy;
    const std::array<Content::Command, 3> replica_initialize{{
        {Kind::Fill, 0, 0, 0, 0, 64, 0x63636363}, {Kind::Fill, 1, 0, 0, 0, 64, 0x0a0a0a0a},
        {Kind::Fill, 2, 0, 0, 0, 64, 0x14141414}}};
    on_gpu([&] {
        isolated_owners[0] = Access::IsolatedContentBuffer(rasterizer, A, 4096);
        isolated_owners[1] = Access::IsolatedContentBuffer(rasterizer, B, 4096);
        isolated_owners[2] = Access::IsolatedContentBuffer(rasterizer, C, 4096);
        for (const auto& buffer : isolated_owners) isolated_targets.push_back(Access::IsolatedContentTarget(rasterizer, buffer, 64));
        replica_parent = renderer->scheduler.RecordTransferBatch(isolated_targets, replica_initialize);
    });
    REQUIRE(isolated_targets[0].pin.Identity() != isolated_targets[1].pin.Identity());
    REQUIRE(isolated_targets[0].physical->Backing() == isolated_targets[1].physical->Backing());
    REQUIRE(isolated_targets[0].physical->OverlapsPhysical(*isolated_targets[1].physical));
    REQUIRE(replica_parent);
    finish_content(replica_parent);
    const auto replica_parent_history = replica_parent->PlannedContent();
    Vulkan::Scheduler::ReplicaCopyRequest replica_request{replica_parent, 0, 1, 16, 16, 16, 0, 1, 16, 0};
    on_gpu([&] { replica_copy = renderer->scheduler.RecordControlledReplicaCopy(device_memory, replica_request); });
    REQUIRE(replica_copy);
    finish_content(replica_copy);
    // Readback would add an unchecked submission; record the next hop first.
    on_gpu([&] {
        replica_second = renderer->scheduler.RecordControlledReplicaCopy(device_memory,
            {replica_parent, 1, 2, 20, 20, 8, 0, 2, 20, 0, replica_copy});
    });
    REQUIRE(replica_second);
    finish_content(replica_second);
    REQUIRE(replica_second->PlannedReplicaCopy()->Depth() == 2);
    REQUIRE(replica_second->PlannedReplicaCopy()->SourceCopy() == replica_copy->PlannedReplicaCopy());
    std::array<bool, 4> second_roots{};
    const auto query_second = [&] {
        second_roots[0] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_second, device_memory, replica_parent, 0, 20, 2, 0);
        second_roots[1] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_second, device_memory, replica_parent, 20, 8, 0, 20);
        second_roots[2] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_second, device_memory, replica_parent, 28, 36, 2, 28);
        second_roots[3] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_second, device_memory, replica_parent, 0, 64, 0, 0);
    };
    std::vector<u8> content_c;
    auto second_expected = std::vector<u8>(64, 20);
    std::fill(second_expected.begin() + 20, second_expected.begin() + 28, u8{99});
    on_gpu([&] { query_second();content_c = Access::ReadPin(rasterizer, isolated_targets[2].pin); });
    REQUIRE(second_roots[0]); REQUIRE(second_roots[1]); REQUIRE(second_roots[2]); REQUIRE_FALSE(second_roots[3]);
    REQUIRE(content_c == second_expected);
    std::array<bool, 4> replica_roots{};
    const auto query_replica = [&] {
        replica_roots[0] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_copy, device_memory, replica_parent, 0, 16, 1, 0);
        replica_roots[1] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_copy, device_memory, replica_parent, 16, 16, 0, 16);
        replica_roots[2] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_copy, device_memory, replica_parent, 32, 32, 1, 32);
        replica_roots[3] = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_copy, device_memory, replica_parent, 0, 64, 0, 0);
    };
    on_gpu(query_replica);
    REQUIRE(replica_roots[0]); REQUIRE(replica_roots[1]); REQUIRE(replica_roots[2]); REQUIRE_FALSE(replica_roots[3]);
    on_gpu([&] { content_b = Access::ReadPin(rasterizer, isolated_targets[1].pin); });
    auto replica_expected = std::vector<u8>(64, 10);
    std::fill(replica_expected.begin() + 16, replica_expected.begin() + 32, u8{99});
    REQUIRE(content_b == replica_expected);
    REQUIRE(replica_parent_history->HasRoot(1, 0, 64, 1, 0)); // The old target's whole history is still 10.
    on_gpu([&] {
        Access::FillPinRange(rasterizer, isolated_targets[0].pin, 16, 16, 0x4d4d4d4d);
        content_a = Access::ReadPin(rasterizer, isolated_targets[0].pin);
        query_replica();
    });
    REQUIRE(content_a[16] == 77);
    REQUIRE(replica_roots[1]); // Copy captured 99; a later source overwrite must not rebase it to 77.
    on_gpu([&] {
        Access::FillPinRange(rasterizer, isolated_targets[1].pin, 20, 4, 0x4d4d4d4d);
        content_b = Access::ReadPin(rasterizer, isolated_targets[1].pin);
        query_replica();
    });
    REQUIRE_FALSE(replica_roots[1]); REQUIRE(replica_roots[0]); REQUIRE(replica_roots[2]);
    std::fill(replica_expected.begin() + 20, replica_expected.begin() + 24, u8{77});
    REQUIRE(content_b == replica_expected);
    on_gpu([&] { query_second();content_c = Access::ReadPin(rasterizer, isolated_targets[2].pin); });
    REQUIRE(second_roots[1]); // The intermediate B overwrite cannot rewrite already copied C history.
    REQUIRE(content_c == second_expected);
    on_gpu([&] {
        Access::FillPinRange(rasterizer, isolated_targets[2].pin, 24, 4, 0x4d4d4d4d);
        content_c = Access::ReadPin(rasterizer, isolated_targets[2].pin);
        query_second();
        current_root = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_second, device_memory, replica_parent, 20, 4, 0, 20);
    });
    REQUIRE_FALSE(second_roots[1]); REQUIRE(second_roots[0]); REQUIRE(second_roots[2]); REQUIRE(current_root);
    std::fill(second_expected.begin() + 24, second_expected.begin() + 28, u8{77});
    REQUIRE(content_c == second_expected);
    on_gpu([&] { current_root = renderer->scheduler.CompletedReplicaCopyHasRoot(replica_copy, replica_parent, 16, 16, 0, 16); });
    REQUIRE(current_root); // Historical completed copy remains recorded after new target writes.
    on_gpu([&] { asynchronous_parent = renderer->scheduler.RecordTransferBatch(isolated_targets, replica_initialize); });
    REQUIRE(asynchronous_parent);
    finish_content(asynchronous_parent);
    on_gpu([&] {
        replica_request.parent = asynchronous_parent;
        asynchronous_first = renderer->scheduler.RecordControlledReplicaCopy(device_memory, replica_request);
    });
    REQUIRE(asynchronous_first);
    finish_content(asynchronous_first);
    std::weak_ptr<const void> asynchronous_payload = isolated_targets[1].pin.RetainedOwner();
    on_gpu([&] {
        asynchronous_copy = renderer->scheduler.RecordControlledReplicaCopy(device_memory,
            {asynchronous_parent, 1, 2, 20, 20, 8, 0, 2, 20, 0, asynchronous_first});
        if (!asynchronous_copy) throw std::runtime_error("asynchronous copy refused");
        isolated_owners[1]->RevokeContentEligibility();
        isolated_owners[1].reset();
        isolated_targets.clear();
    });
    REQUIRE(asynchronous_copy);
    REQUIRE_FALSE(asynchronous_payload.expired());
    REQUIRE(Until([&] {
        State status{};
        on_gpu([&] {status = renderer->scheduler.PollCheckedTransfer(asynchronous_copy);});
        return status == State::Complete || status == State::Unknown;
    }));
    on_gpu([&] {current_root = renderer->scheduler.CurrentReplicaCopyHasRoot(asynchronous_copy, device_memory, asynchronous_parent, 20, 8, 0, 20);});
    REQUIRE_FALSE(current_root);
    const auto historical_content_origin = content_transfer->HistoricalOrigins()[0];
    on_gpu([&] {
        device_memory.Unmap(A, Footprint);
        device_memory.Map(A, GetInteger(owner.heap) + Footprint, Footprint, asid);
        content = renderer->scheduler.TransferContentForMapping(content_transfer, device_memory);
        unknown_content = renderer->scheduler.TransferContentForMapping(unknown_root, device_memory);
        query_replica();
        enabled = rasterizer.SetControlledStorageWriteObservation(false);
    });
    REQUIRE(enabled);
    REQUIRE(content == nullptr);
    REQUIRE(unknown_content == nullptr);
    REQUIRE_FALSE(replica_roots[0]); REQUIRE_FALSE(replica_roots[2]); // Remap invalidates still-untouched complements too.
    REQUIRE(replica_parent->PlannedContent() == replica_parent_history);
    REQUIRE(replica_copy->PlannedReplicaCopy()->HasRoot(16, 16, 0, 16));
    REQUIRE(replica_second->PlannedReplicaCopy()->HasRoot(20, 8, 0, 20));
    on_gpu([&] { current_root = renderer->scheduler.CurrentReplicaCopyHasRoot(replica_second, device_memory, replica_parent, 20, 4, 0, 20); });
    REQUIRE_FALSE(current_root);
    REQUIRE(content_transfer->HistoricalOrigins()[0] == historical_content_origin);
    REQUIRE_FALSE(historical_content_origin->MatchesCurrent(device_memory));
    on_gpu([&] {
        renderer->scheduler.Finish();
        std::scoped_lock lock{renderer->scheduler.submit_mutex};
        if (renderer->device.GetLogical().WaitIdle() != VK_SUCCESS)
            throw std::runtime_error("provenance trusted idle failed");
        rasterizer.RetireControlledTransfersAfterDeviceIdle();
    });
    REQUIRE(delayed->ResourceIdentity() == nullptr);
    REQUIRE(poll(delayed) == State::Unknown);
    REQUIRE_FALSE(content_transfer->ResourcesRetained());
    REQUIRE(asynchronous_payload.expired());
    std::cout << "PASS checked write provenance: skipped intent, distinct actual submissions, "
                 "repeated dirty interval, historical completion versus current snapshot, "
                 "pending worker, independent replica, remap and retained old native allocation; "
                 "no physical latest-writer authority\n";
}
} // namespace
