// SPDX-FileCopyrightText: 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: 2022 Skyline Team and Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <deque>
#include <mutex>
#include <stdexcept>

#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/heap_mapper.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/memory.h"
#include "video_core/host1x/host1x.h"

namespace Service::Nvidia::NvCore {

// Process-lifetime terminal ledger. No automatic destruction or Close retry:
// only a future separately proven terminal protocol may retire these owners.
static std::atomic<ProcessReleaseFailure*> failed_process_releases{nullptr};

static void ReleaseProcessOwner(Kernel::KScopedAutoObject<Kernel::KProcess>& owner,
    Common::RetainedBackingStorage& backing, std::unique_ptr<ProcessReleaseFailure>& failure) noexcept {
    auto* process = owner.ReleasePointerUnsafe();
    if (!process) return;
    const auto retain_failure = [&] {
        // Close may already have reached zero and entered Finalize. Do not
        // resurrect/retry it, or release the backing needed by failed cleanup.
        failure->process_identity = process;
        failure->backing.emplace(std::move(backing));
        if (failure->domain) failure->domain->Seal();
        auto* record = failure.release();
        auto* head = failed_process_releases.load(std::memory_order_acquire);
        do {
            record->next = head;
        } while (!failed_process_releases.compare_exchange_weak(
            head, record, std::memory_order_acq_rel, std::memory_order_acquire));
    };
    // A late external lease must not call a Kernel facade after its physical
    // backing epoch has died. Retain opaque identity rather than dereference it.
    if (!backing.OwnerAlive()) {
        retain_failure();
        return;
    }
    try {
        process->Close();
    } catch (...) {
        retain_failure();
    }
}

Container::SessionLifetime::SessionLifetime(Common::RetainedBackingStorage&& backing_,
    Kernel::KScopedAutoObject<Kernel::KProcess>&& owner_, std::shared_ptr<Common::ProducerDrain> domain)
    : process_failure{std::make_unique<ProcessReleaseFailure>()}, backing{std::move(backing_)}, process{std::move(owner_)} {
    process_failure->domain = std::move(domain);
}
Container::SessionLifetime::~SessionLifetime() {
    ReleaseProcessOwner(process, backing, process_failure);
}

Session::Session(SessionId id_, Kernel::KScopedAutoObject<Kernel::KProcess>&& owner_, u64 pid_,
                 Core::Asid asid_, Common::RetainedBackingStorage&& backing_, std::unique_ptr<ProcessReleaseFailure> failure_)
    : process_failure{std::move(failure_)}, backing{std::move(backing_)}, process_owner{std::move(owner_)},
      id{id_}, process{process_owner.GetPointerUnsafe()}, pid{pid_}, asid{asid_}, has_preallocated_area{}, mapper{},
      is_active{} {}

Session::~Session() {
    mapper.reset();
    if (backing) ReleaseProcessOwner(process_owner, *backing, process_failure);
}

Session& Session::operator=(Session&& other) {
    if (this != &other) {
        // Default member assignment would replace backing/Process before the
        // old mapper is destroyed. Retire the complete old session first.
        this->~Session();
        std::construct_at(this, std::move(other));
    }
    return *this;
}

struct ContainerImpl {
    explicit ContainerImpl(Container& core, Tegra::Host1x::Host1x& host1x_)
        : host1x{host1x_}, file{core, host1x_}, manager{host1x_}, device_file_data{} {}
    Tegra::Host1x::Host1x& host1x;
    // Keep session Process/backing owners until nvmap handle state is gone.
    std::deque<Session> sessions;
    NvMap file;
    SyncpointManager manager;
    Container::Host1xDeviceFileData device_file_data;
    size_t new_ids{};
    std::deque<size_t> id_pool;
    std::mutex session_guard;
};

Container::Container(Tegra::Host1x::Host1x& host1x_) {
    impl = std::make_unique<ContainerImpl>(*this, host1x_);
}

Container::~Container() = default;

SessionId Container::OpenSession(Kernel::KProcess* process) {
    using namespace Common::Literals;

    // Prepare all lifetime/failure owners before ASID publication or taking the
    // session lock. Both failed initialization and a reused old session can run
    // final Process release; their destruction must occur after unlocking.
    auto process_failure = std::make_unique<ProcessReleaseFailure>();
    process_failure->domain = impl->host1x.System().Kernel().NvdrvDispatchDomain();
    auto backing = impl->host1x.System().DeviceMemory().buffer.RetainBackingStorage();
    if (!backing || !backing->OwnerAlive())
        throw std::runtime_error("nvdrv session backing unavailable");
    auto owner = Kernel::KScopedAutoObject<Kernel::KProcess>::TryOpen(process);
    if (!owner) throw std::runtime_error("nvdrv session owner unavailable");
    Session pending{SessionId{0}, std::move(*owner), process->GetProcessId(), Core::Asid{},
                    std::move(*backing), std::move(process_failure)};
    std::optional<Session> retired;
    std::scoped_lock lk(impl->session_guard);
    for (auto& session : impl->sessions) {
        if (!session.is_active) {
            continue;
        }
        if (session.process == process) {
            if (session.pid != process->GetProcessId()) {
                LOG_WARNING(Service_NVDRV,
                            "Container::OpenSession: Stale session detected! PID mismatch (old={}, "
                            "new={}). Marking as inactive.",
                            session.pid, process->GetProcessId());
                // Force close stale session logic
                // NOTE: We do NOT unmap handles or free memory here because it causes
                // KSynchronizationObject::UnlinkNode segfaults if threads are still waiting on
                // events associated with this session. We effectively leak the old session's
                // resources to ensure stability.
                // impl->file.UnmapAllHandles(session.id);

                // auto& smmu_mgr = impl->host1x.MemoryManager();
                /*
                if (session.has_preallocated_area) {
                    const DAddr region_start = session.mapper->GetRegionStart();
                    const size_t region_size = session.mapper->GetRegionSize();
                    session.mapper.reset();
                    smmu_mgr.Free(region_start, region_size);
                    session.has_preallocated_area = false;
                }
                */
                session.is_active = false;
                session.ref_count = 0;
                // smmu_mgr.UnregisterProcess(session.asid);
                // impl->id_pool.emplace_front(session.id.id);
                // Continue searching to clean up other stale sessions if any?
                // Or proceed to create new one. Stale one is now inactive.
                continue;
            } else {
                session.ref_count++;
                return session.id;
            }
        }
    }
    size_t new_id{};
    auto* memory_interface = &process->GetMemory();
    auto& smmu = impl->host1x.MemoryManager();
    auto asid = smmu.RegisterProcess(memory_interface);
    if (!impl->id_pool.empty()) {
        new_id = impl->id_pool.front();
        impl->id_pool.pop_front();
        retired.emplace(std::move(impl->sessions[new_id]));
        pending.id = SessionId{new_id};
        pending.asid = asid;
        impl->sessions[new_id] = std::move(pending);
    } else {
        new_id = impl->new_ids++;
        pending.id = SessionId{new_id};
        pending.asid = asid;
        impl->sessions.emplace_back(std::move(pending));
    }
    auto& session = impl->sessions[new_id];
    session.is_active = true;
    session.ref_count = 1;
    // Optimization
    if (process->IsApplication()) {
        auto& page_table = process->GetPageTable().GetBasePageTable();
        auto heap_start = page_table.GetHeapRegionStart();

        Kernel::KProcessAddress cur_addr = heap_start;
        size_t region_size = 0;
        VAddr region_start = 0;
        while (true) {
            Kernel::KMemoryInfo mem_info{};
            Kernel::Svc::PageInfo page_info{};
            R_ASSERT(page_table.QueryInfo(std::addressof(mem_info), std::addressof(page_info),
                                          cur_addr));
            auto svc_mem_info = mem_info.GetSvcMemoryInfo();

            // Check if this memory block is heap.
            if (svc_mem_info.state == Kernel::Svc::MemoryState::Normal) {
                if (region_start + region_size == svc_mem_info.base_address) {
                    region_size += svc_mem_info.size;
                } else if (svc_mem_info.size > region_size) {
                    region_size = svc_mem_info.size;
                    region_start = svc_mem_info.base_address;
                }
            }

            // Check if we're done.
            const uintptr_t next_address = svc_mem_info.base_address + svc_mem_info.size;
            if (next_address <= GetInteger(cur_addr)) {
                break;
            }

            cur_addr = next_address;
        }
        session.has_preallocated_area = false;
        auto start_region = region_size >= 32_MiB ? smmu.Allocate(region_size) : 0;
        if (start_region != 0) {
            session.mapper = std::make_unique<HeapMapper>(region_start, start_region, region_size,
                                                          asid, impl->host1x);
            smmu.TrackContinuity(start_region, region_start, region_size, asid);
            session.has_preallocated_area = true;
            LOG_DEBUG(Debug, "Preallocation created!");
        }
    }
    return SessionId{new_id};
}

void Container::CloseSession(SessionId session_id, bool is_shutdown) {
    std::scoped_lock lk(impl->session_guard);
    if (session_id.id >= impl->sessions.size())
        throw std::runtime_error("nvdrv session retirement id unavailable");
    auto& session = impl->sessions[session_id.id];
    if (!session.is_active || session.ref_count <= 0)
        throw std::runtime_error("nvdrv session already retired");
    if (--session.ref_count > 0) {
        return;
    }

    if (is_shutdown) {
        session.is_active = false;
        return;
    }

    impl->file.UnmapAllHandles(session_id);
    auto& smmu = impl->host1x.MemoryManager();
    if (session.has_preallocated_area) {
        const DAddr region_start = session.mapper->GetRegionStart();
        const size_t region_size = session.mapper->GetRegionSize();
        session.mapper.reset();
        smmu.Free(region_start, region_size);
        session.has_preallocated_area = false;
    }
    session.is_active = false;
    smmu.UnregisterProcess(impl->sessions[session_id.id].asid);
    impl->id_pool.emplace_front(session_id.id);
}

Session* Container::GetSession(SessionId session_id) {
    std::atomic_thread_fence(std::memory_order_acquire);
    return &impl->sessions[session_id.id];
}

std::optional<Container::SessionLifetime> Container::RetainSession(SessionId id) {
    std::scoped_lock lock{impl->session_guard};
    if (id.id >= impl->sessions.size()) return {};
    const auto& session = impl->sessions[id.id];
    if (!session.is_active || session.ref_count <= 0 ||
        !session.backing || !session.backing->OwnerAlive()) return {};
    auto process = Kernel::KScopedAutoObject<Kernel::KProcess>::TryOpen(session.process);
    auto backing = impl->host1x.System().DeviceMemory().buffer.RetainBackingStorage();
    if (!process || !backing || backing->Epoch() != session.backing->Epoch()) return {};
    return SessionLifetime{std::move(*backing), std::move(*process), impl->host1x.System().Kernel().NvdrvDispatchDomain()};
}

NvMap& Container::GetNvMapFile() {
    return impl->file;
}

const NvMap& Container::GetNvMapFile() const {
    return impl->file;
}

Container::Host1xDeviceFileData& Container::Host1xDeviceFile() {
    return impl->device_file_data;
}

const Container::Host1xDeviceFileData& Container::Host1xDeviceFile() const {
    return impl->device_file_data;
}

SyncpointManager& Container::GetSyncpointManager() {
    return impl->manager;
}

const SyncpointManager& Container::GetSyncpointManager() const {
    return impl->manager;
}

} // namespace Service::Nvidia::NvCore
