// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <numeric>
#include <stdexcept>

#include "common/alignment.h"
#include "common/range_sets.inc"
#include "video_core/buffer_cache/buffer_cache_base.h"
#include "video_core/buffer_cache/controlled_conflict.h"
#include "video_core/cache_memory_pressure.h"
#include "video_core/guest_memory.h"
#include "video_core/host1x/gpu_device_memory_manager.h"

namespace VideoCommon {

using Core::DEVICE_PAGESIZE;

template <class P>
BufferCache<P>::BufferCache(Tegra::MaxwellDeviceMemoryManager& device_memory_, Runtime& runtime_)
    : runtime{runtime_}, device_memory{device_memory_}, memory_tracker{device_memory},
      write_heat{CACHING_PAGE_COUNT}, page_table{CACHING_PAGE_COUNT} {
    // Ensure the first slot is used for the null buffer
    void(slot_buffers.insert(runtime, NullBufferParams{}));
    gpu_modified_ranges.Clear();
    inline_buffer_id = NULL_BUFFER_ID;
}

template <class P>
BufferCache<P>::~BufferCache() = default;

template <class P>
auto BufferCache<P>::BeginControlledFirstRegistration(
    DAddr address, u32 size, std::span<const Core::Memory::PreparedCpuPage> views)
    -> ControlledRegistrationPtr {
    if constexpr (!requires {
                      runtime.SupportsControlledTransfers();
                      runtime.PrepareControlledTransfer(std::declval<const Buffer&>());
                  }) {
        return {};
    } else {
        if (!runtime.SupportsControlledTransfers() || views.empty() || views.size() > 64 ||
            !IsRegionPageTableAddressable(address, size) || scatter_busy ||
            std::ranges::any_of(gather_buffers,
                                [](const auto& item) { return item.second.scatter_pending; }))
            return {};
        const u64 begin = Common::AlignDown(address, CACHING_PAGESIZE);
        const u64 length = Common::AlignUp(address + size, CACHING_PAGESIZE) - begin;
        if (length > 64 * DEVICE_PAGESIZE) return {};
        for (u64 page = begin / CACHING_PAGESIZE; page < (begin + length) / CACHING_PAGESIZE;
             ++page)
            if (GetPageBufferId(page)) return {}; // Overlap expansion is deliberately ineligible.
        std::erase_if(controlled_registrations, [](const auto& entry) {
            return entry.use_count() == 1 &&
                   (entry->phase == ControlledRegistration::Phase::Ready ||
                    entry->phase == ControlledRegistration::Phase::Cancelled);
        });
        if (controlled_registrations.size() >= 64) return {};
        auto plan = Tegra::PhysicalAccessPlan::BuildDeviceRange(device_memory, begin, length);
        if (!plan) return {};
        std::vector<Common::PreparedPhysicalCpuPage> pages;
        for (const auto& segment : plan->Segments()) {
            const auto& pin = segment.storage;
            auto page =
                device_memory.PrepareGuestPhysicalCpuPage(pin.PhysicalOffset(), pin.Epoch());
            if (!page || std::ranges::none_of(views, [&](const auto& view) {
                    return view.physical.GateIdentity() == page->GateIdentity() &&
                           view.IsCurrentForRegistration();
                }))
                return {};
            pages.push_back(std::move(*page));
        }
        auto operation =
            ControlledRegistrationPtr{new ControlledRegistration{std::move(*plan), views}};
        operation->address = begin;
        operation->id =
            slot_buffers.insert(runtime, begin, length); // Hidden: no page-table/LRU entry.
        try {
            operation->transfer = runtime.PrepareControlledTransfer(slot_buffers[operation->id]);
            if (!operation->transfer) {
                slot_buffers.erase(operation->id);
                return {};
            }
            // Retain the session before gate closure, so caller drop cannot strand a hidden slot.
            controlled_registrations.push_back(operation);
            auto admission = Common::PhysicalRegistrationBundle::Begin(pages, operation->transfer);
            if (!admission) {
                controlled_registrations.pop_back();
                slot_buffers.erase(operation->id);
                return {};
            }
            operation->admission.emplace(std::move(*admission));
        } catch (...) {
            if (!controlled_registrations.empty() && controlled_registrations.back() == operation)
                controlled_registrations.pop_back();
            slot_buffers.erase(operation->id);
            throw;
        }
        return operation;
    }
}

template <class P>
BufferId BufferCache<P>::ControlledReadyBuffer(const ControlledRegistrationPtr& operation) {
    return ControlledBufferId(operation, true);
}

template <class P>
BufferId BufferCache<P>::ControlledBufferId(const ControlledRegistrationPtr& operation,
                                            bool require_clean, bool nonblocking_mapping) {
    if constexpr (!requires(const Buffer& buffer) { buffer.PinStagedRepresentation(0, 1); }) {
        return NULL_BUFFER_ID;
    } else {
        if (!operation ||
            (operation->phase != ControlledRegistration::Phase::Ready &&
             (require_clean || operation->phase != ControlledRegistration::Phase::Writing)) ||
            std::ranges::find(controlled_registrations, operation) ==
                controlled_registrations.end() ||
            !slot_buffers.contains(operation->id))
            return NULL_BUFFER_ID;
        if (nonblocking_mapping) {
            // The immutable plan's epoch/generation certifies the captured translations.
            // A current stamp requires no blocking rewalk of the device page table.
            if (!device_memory.WithControlledMapping(operation->plan.DeviceEpoch(),
                    operation->plan.DeviceGeneration(), [](const auto&) {})) return NULL_BUFFER_ID;
        } else if (!operation->plan.MatchesCurrentDevice(device_memory, operation->address)) {
            return NULL_BUFFER_ID;
        }
        if (require_clean &&
            memory_tracker.IsRegionCpuModified(operation->address, operation->plan.Size()))
            return NULL_BUFFER_ID;
        const auto& buffer = slot_buffers[operation->id];
        auto pin = buffer.PinStagedRepresentation(0, buffer.SizeBytes());
        return pin && pin->Identity() == operation->transfer->DestinationIdentity()
                   ? operation->id
                   : NULL_BUFFER_ID;
    }
}

template <class P>
template <typename Store>
Core::Memory::PreparedCpuWriteResult BufferCache<P>::CommitControlledCachedWrite(
    const ControlledRegistrationPtr& operation, const Core::Memory::PreparedCpuPage& view,
    Common::PhysicalRegistrationBundle::CachedWrite& write, u64 address, u64 size, Store&& store) {
    using Result = Core::Memory::PreparedCpuWriteResult;
    if (ControlledBufferId(operation, false) == NULL_BUFFER_ID || !write.Granted() ||
        !operation->admission->OwnsCachedWrite(write) || !size ||
        size > DEVICE_PAGESIZE - address % DEVICE_PAGESIZE ||
        view.physical.Epoch()->generation != operation->plan.BackingGeneration())
        return Result::NeedSlow;
    if (std::ranges::none_of(operation->plan.Segments(), [&](const auto& segment) {
            return segment.storage.Epoch() == view.physical.Epoch() &&
                   segment.storage.PhysicalOffset() == view.physical.PhysicalOffset();
        }))
        return Result::Invalid;
    Result result = Result::Busy;
    try {
        const bool qualified = device_memory.WithControlledPhysicalAliases(
            view.physical.PhysicalOffset() + address % DEVICE_PAGESIZE,
            operation->plan.DeviceEpoch(), operation->plan.DeviceGeneration(), [&](auto aliases) {
                // Include ALL device aliases, even those with no buffer yet. They must not keep
                // clean tracker state after the physical store. Preservation remains fail-closed.
                if (scatter_busy || std::ranges::any_of(gather_buffers, [](const auto& entry) {
                        return entry.second.scatter_pending;
                    })) {
                    result = Result::NeedSlow;
                    return;
                }
                for (const auto alias : aliases)
                    if (memory_tracker.IsRegionGpuModified(alias, size)) {
                        result = Result::NeedSlow;
                        return;
                    }
                result = store(); // Actual prepared Memory cached Write, same write capability.
                if (result != Result::Stored) return;
                for (const auto alias : aliases) {
                    NotifyGatherWrite(alias, size);
                    memory_tracker.MarkRegionAsCpuModified(alias, size);
                }
            });
        return qualified ? result : Result::Busy;
    } catch (...) {
        (void)write.MarkUnknown();
        operation->phase = ControlledRegistration::Phase::Unknown;
        throw;
    }
}

template <class P>
auto BufferCache<P>::BeginControlledCpuWrite(
    const ControlledRegistrationPtr& operation, const Core::Memory::PreparedCpuPage& view,
    u64 address, u64 size, std::shared_ptr<const void> extra_owner) -> ControlledCpuWritePtr {
    if constexpr (!requires { runtime.PrepareControlledTransfer(std::declval<const Buffer&>()); }) {
        return {};
    } else {
        address &= 0xffffffffffffULL;
        if (!operation || operation->phase != ControlledRegistration::Phase::Ready ||
            ControlledBufferId(operation, false, bool(extra_owner)) == NULL_BUFFER_ID ||
            (size != 1 && size != 2 && size != 4 && size != 8) ||
            size > DEVICE_PAGESIZE - address % DEVICE_PAGESIZE ||
            (address & ~(DEVICE_PAGESIZE - 1)) != view.mapping.virtual_page ||
            view.mapping.type != Common::PageType::RasterizerCachedMemory ||
            !view.IsCurrentForRegistration() ||
            std::ranges::none_of(operation->plan.Segments(), [&](const auto& segment) {
                return segment.storage.Epoch() == view.physical.Epoch() &&
                       segment.storage.PhysicalOffset() == view.physical.PhysicalOffset();
            }))
            return {};
        auto preservation = runtime.PrepareControlledTransfer(slot_buffers[operation->id]);
        if (!preservation) return {};
        struct Owners {
            ControlledRegistrationPtr operation;
            std::shared_ptr<ControlledBufferTransfer> preservation;
            std::shared_ptr<const void> extra;
        };
        auto write = operation->admission->BeginCachedWrite(
            std::make_shared<const Owners>(Owners{operation, preservation, std::move(extra_owner)}));
        if (!write) return {};
        auto transaction = ControlledCpuWritePtr{new ControlledCpuWrite{
            operation, view, address, size, std::move(preservation), std::move(*write)}};
        operation->phase = ControlledRegistration::Phase::Writing;
        return transaction;
    }
}

template <class P>
auto BufferCache<P>::BeginControlledCpuWriteRequest(
    const ControlledRegistrationPtr& operation,
    const Core::Memory::ControlledCpuWriteRequestPtr& request) -> ControlledCpuWritePtr {
    using Request = Core::Memory::ControlledCpuWriteRequest;
    if (!request) return {};
    // A queued legacy copyback has no request execution identity. It must retire before
    // acceptance, including work addressed through another device alias of this footprint.
    // This check certifies only buffer-readback exclusion, never the other producer families.
    if (!ControlledReadbacksIdle(operation)) {
        (void)request->Cancel();
        return {};
    }
    if (!request->TryAccept(request)) return {};
    if (!request->Owner()->TryVisit([](auto&) {})) {
        request->RefuseBeforeEffects();
        return {};
    }
    try {
        auto transaction = BeginControlledCpuWrite(operation, request->View(), request->Address(),
                                                    request->Size(), request);
        if (!transaction) {
            request->RefuseBeforeEffects();
            return {};
        }
        transaction->request = request;
        return transaction;
    } catch (...) {
        request->Publish(Request::Phase::Unknown);
        throw;
    }
}

template <class P>
template <typename Execute>
Core::Memory::PreparedCpuWriteResult BufferCache<P>::CommitControlledCpuWriteRequest(
    const ControlledCpuWritePtr& transaction, Execute&& execute) {
    using Result = Core::Memory::PreparedCpuWriteResult;
    if (!transaction || !transaction->request || transaction->executing_request)
        return Result::Invalid;
    if (transaction->phase != ControlledCpuWrite::Phase::Preserved) return Result::NeedSlow;
    // The GPU owner may not wait behind a CPU device mapper, even outside the Memory visit.
    if (ControlledBufferId(transaction->operation, false, true) == NULL_BUFFER_ID) {
        AbandonControlledCpuWrite(transaction);
        return Result::NeedSlow;
    }
    // Request-backed writes may commit only their immutable accepted payload, never a later
    // caller-supplied scalar via the old typed endpoint. No binding survives this short call.
    transaction->executing_request = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{transaction->executing_request};
    return std::visit([&](const auto& value) {
        return CommitControlledCpuTransaction(transaction, sizeof(value),
            [&](const auto& view, auto& write, u64 address) {
                Result result = Result::NeedSlow;
                // Only the ordinary store and its nonblocking context/PTE qualification
                // can touch Memory. Tracker/device callbacks are outside this visit.
                (void)transaction->request->Owner()->TryVisit([&](auto& memory) {
                    result = execute(memory, view, write, address, value);
                });
                return result;
            });
    }, transaction->request->Payload());
}

template <class P>
void BufferCache<P>::AbandonControlledCpuWrite(const ControlledCpuWritePtr& transaction) {
    if (!transaction || transaction->phase == ControlledCpuWrite::Phase::Committed ||
        transaction->phase == ControlledCpuWrite::Phase::Cancelled) return;
    (void)transaction->write.MarkUnknown();
    transaction->phase = ControlledCpuWrite::Phase::Unknown;
    transaction->operation->phase = ControlledRegistration::Phase::Unknown;
    if (transaction->request)
        transaction->request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Unknown);
}

template <class P>
auto BufferCache<P>::PollControlledCpuWrite(const ControlledCpuWritePtr& transaction)
    -> typename ControlledCpuWrite::Phase {
    using Phase = typename ControlledCpuWrite::Phase;
    if (!transaction) return Phase::Unknown;
    auto& tx = *transaction;
    if (tx.phase != Phase::Waiting && tx.phase != Phase::Submitted) return tx.phase;
    if (tx.request && tx.request->State() != Core::Memory::ControlledCpuWriteRequest::Phase::Accepted) {
        AbandonControlledCpuWrite(transaction);
        return tx.phase;
    }
    auto& op = *tx.operation;
    const auto unknown = [&] {
        (void)tx.write.MarkUnknown();
        op.phase = ControlledRegistration::Phase::Unknown;
        if (tx.request) tx.request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Unknown);
        return tx.phase = Phase::Unknown;
    };
    try {
        if (tx.phase == Phase::Waiting && !tx.write.Granted()) {
            const auto start = tx.write.TryStart();
            if (start == Common::PhysicalRangeAdmission::Start::Park) return tx.phase;
            if (start != Common::PhysicalRangeAdmission::Start::Granted) return unknown();
        }
        if (ControlledBufferId(tx.operation, false, bool(tx.request)) == NULL_BUFFER_ID ||
            tx.preservation->DestinationIdentity() != op.transfer->DestinationIdentity() ||
            !tx.view.IsCurrentForRegistration() || scatter_busy ||
            std::ranges::any_of(gather_buffers, [](const auto& item) {
                return item.second.scatter_pending;
            }))
            return unknown();
        const bool submitted = tx.phase == Phase::Submitted;
        if (submitted) {
            const auto result = tx.preservation->PollPreserve();
            if (result == ControlledTransferPoll::Pending) return tx.phase;
            if (result != ControlledTransferPoll::Complete) return unknown();
        }
        bool qualified = true;
        const bool locked = device_memory.WithControlledMapping(
            op.plan.DeviceEpoch(), op.plan.DeviceGeneration(), [&](const auto& aliases_for) {
                std::array<DAddr, 64> aliases{};
                // Whole-buffer readback is allowed only for a clean CPU footprint with a
                // single authoritative GPU representation. Mixed/other GPU aliases refuse.
                for (const auto& segment : op.plan.Segments()) {
                    size_t count{};
                    if (!segment.storage.OwnerAlive() ||
                        !aliases_for(segment.storage.PhysicalOffset(), aliases, count)) {
                        qualified = false;
                        return;
                    }
                    for (size_t i = 0; i < count; ++i) {
                        const auto alias = aliases[i];
                        if ((IsRegionRegistered(alias, DEVICE_PAGESIZE) &&
                             memory_tracker.IsRegionCpuModified(alias, DEVICE_PAGESIZE)) ||
                            (alias != op.address + segment.logical_offset &&
                             memory_tracker.IsRegionGpuModified(alias, DEVICE_PAGESIZE))) {
                            qualified = false;
                            return;
                        }
                    }
                }
                if (!submitted) return; // Never record/submit while holding this guard.
                const auto bytes = tx.preservation->PreservedBytes();
                if (bytes.size() != op.plan.Size()) {
                    qualified = false;
                    return;
                }
                // All destinations validated BEFORE the first copy. Only immutable owned pins
                // are written; remapping cannot redirect preservation to another allocation.
                const auto copy = [&] {
                    for (const auto& segment : op.plan.Segments()) {
                        const auto target = segment.storage.Bytes();
                        std::memcpy(target.data(), bytes.data() + segment.logical_offset, target.size());
                    }
                };
                // Process finalization may return kernel pages to its allocator. Close must
                // also exclude this bounded backing copy, not only the later scalar store.
                if (tx.request) {
                    qualified = tx.request->Owner()->TryVisit([&](auto&) { copy(); });
                } else {
                    copy();
                }
                // Backing now contains every GPU byte. Keep GPU dirty state AND exact download
                // ranges until the CPU store and every alias's tracker publication succeed.
            });
        if (!locked) return tx.phase;
        if (!qualified) return unknown();
        if (submitted) return tx.phase = Phase::Preserved;
        if (!tx.preservation->SubmitPreserve()) return unknown();
        return tx.phase = Phase::Submitted;
    } catch (...) {
        return unknown();
    }
}

template <class P>
template <typename Store>
Core::Memory::PreparedCpuWriteResult BufferCache<P>::CommitControlledCpuTransaction(
    const ControlledCpuWritePtr& transaction, u64 size, Store&& store) {
    using Result = Core::Memory::PreparedCpuWriteResult;
    using Phase = typename ControlledCpuWrite::Phase;
    if (!transaction || transaction->phase != Phase::Preserved) return Result::NeedSlow;
    if (transaction->request && !transaction->executing_request) return Result::Invalid;
    if (transaction->size != size) return Result::Invalid;
    auto& tx = *transaction;
    auto& op = *tx.operation;
    try {
        Result result = Result::Busy;
        // A request's slot was qualified immediately before entering the lifetime visit on
        // this same GPU owner. Do not acquire a blocking device snapshot lock in that visit.
        const bool valid = (tx.executing_request || ControlledBufferId(tx.operation, false) != NULL_BUFFER_ID) && tx.write.Granted();
        const bool qualified = valid && device_memory.WithControlledPhysicalAliases(
            tx.view.physical.PhysicalOffset() + tx.address % DEVICE_PAGESIZE,
            op.plan.DeviceEpoch(), op.plan.DeviceGeneration(), [&](auto aliases) {
                // The preservation continuation owns the complete footprint. Unlike the old
                // adapter, it need not clear GPU dirtiness merely to permit this actual store.
                result = store(tx.view, tx.write, tx.address);
                if (result != Result::Stored) return;
                for (const auto alias : aliases) {
                    NotifyGatherWrite(alias, tx.size);
                    if (tx.request) {
                        if (!memory_tracker.MarkRegionAsCpuModifiedRetainingTracking(alias, tx.size))
                            throw std::runtime_error("controlled tracker manager lost");
                    } else {
                        memory_tracker.MarkRegionAsCpuModified(alias, tx.size);
                    }
                }
            });
        if (!qualified) result = Result::Busy;
        if (result != Result::Stored) {
            // Prepared refusal has no store effects; successful preservation is already in
            // backing. Do not expose the old GPU representation after a mapping change.
            (void)tx.write.MarkUnknown();
            op.phase = ControlledRegistration::Phase::Unknown;
            tx.phase = Phase::Unknown;
            if (tx.request) tx.request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Unknown);
            return result;
        }
        // Cache range containers may reclaim allocations. They need no Memory pointer and
        // must not lengthen the process execution lease, even when highly fragmented.
        ClearDownload(op.address, op.plan.Size());
        gpu_modified_ranges.Subtract(op.address, op.plan.Size());
        memory_tracker.UnmarkRegionAsGpuModified(op.address, op.plan.Size());
        const auto finish = [&] {
            // Only bounded range-ticket finalization and terminal publication here. No
            // cache-container reclamation, tracker/device/PTE callbacks or blocking locks.
            if (!(tx.request ? tx.write.TryCompleteAfterTrackerPublication()
                             : tx.write.CompleteAfterTrackerPublication())) return false;
            tx.phase = Phase::Committed;
            op.phase = ControlledRegistration::Phase::Ready;
            if (tx.request) tx.request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Stored, result);
            return true;
        };
        bool finished = false;
        if (tx.request) {
            (void)tx.request->Owner()->TryVisit([&](auto&) { finished = finish(); });
        } else {
            finished = finish();
        }
        if (!finished) {
            AbandonControlledCpuWrite(transaction); // potentially contended metadata, OUTSIDE lease
            return Result::NeedSlow;
        }
        return result;
    } catch (...) {
        (void)tx.write.MarkUnknown();
        op.phase = ControlledRegistration::Phase::Unknown;
        tx.phase = Phase::Unknown;
        if (tx.request) tx.request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Unknown);
        throw;
    }
}

template <class P>
bool BufferCache<P>::BeginControlledRefresh(const ControlledRegistrationPtr& operation) {
    if constexpr (!requires { runtime.PrepareControlledTransfer(std::declval<const Buffer&>()); }) {
        return false;
    } else {
        if (!operation || operation->phase != ControlledRegistration::Phase::Ready ||
            ControlledBufferId(operation, false) == NULL_BUFFER_ID || !runtime.SupportsControlledTransfers() ||
            !operation->plan.MatchesCurrentDevice(device_memory, operation->address))
            return false;
        // Refresh into a NEW hidden allocation. Existing bindings cannot read uninitialized data.
        const auto id = slot_buffers.insert(runtime, operation->address, operation->plan.Size());
        try {
            auto transfer = runtime.PrepareControlledTransfer(slot_buffers[id]);
            if (!transfer) {
                slot_buffers.erase(id);
                return false;
            }
            auto admission = operation->admission->BeginCachedWrite(transfer);
            if (!admission) {
                slot_buffers.erase(id);
                return false;
            }
            operation->previous = operation->id;
            operation->previous_identity = operation->transfer->DestinationIdentity();
            operation->id = id;
            operation->transfer = std::move(transfer);
            operation->refresh.emplace(std::move(*admission));
            operation->phase = ControlledRegistration::Phase::Waiting;
            return true;
        } catch (...) {
            slot_buffers.erase(id);
            throw;
        }
    }
}

template <class P>
auto BufferCache<P>::PollControlledRegistration(const ControlledRegistrationPtr& operation) ->
    typename ControlledRegistration::Phase {
    using Phase = typename ControlledRegistration::Phase;
    if (!operation ||
        std::ranges::find(controlled_registrations, operation) == controlled_registrations.end())
        return Phase::Unknown;
    auto& op = *operation;
    const auto unknown = [&] {
        if (op.refresh)
            (void)op.refresh->MarkUnknown();
        else
            (void)op.admission->MarkUnknown();
        return op.phase = Phase::Unknown;
    };
    const auto membership_valid = [&] {
        if constexpr (requires(const Buffer& buffer) { buffer.PinStagedRepresentation(0, 1); }) {
            if (!slot_buffers.contains(op.id)) return false;
            auto target = slot_buffers[op.id].PinStagedRepresentation(0, op.plan.Size());
            if (!target || target->Identity() != op.transfer->DestinationIdentity()) return false;
            if (op.previous) {
                if (!slot_buffers.contains(op.previous)) return false;
                auto previous =
                    slot_buffers[op.previous].PinStagedRepresentation(0, op.plan.Size());
                if (!previous || previous->Identity() != op.previous_identity) return false;
            }
        } else {
            return false;
        }
        for (u64 page = op.address / CACHING_PAGESIZE;
             page < (op.address + op.plan.Size()) / CACHING_PAGESIZE; ++page)
            if (GetPageBufferId(page) != op.previous) return false;
        return true;
    };
    if (op.phase == Phase::Ready)
        return ControlledBufferId(operation, false) != NULL_BUFFER_ID
                   ? Phase::Ready : (op.phase = Phase::Cancelled);
    if (op.phase == Phase::Writing || op.phase == Phase::Unknown || op.phase == Phase::Cancelled)
        return op.phase;
    bool registered = false;
    try {
        if (op.phase == Phase::Waiting || op.phase == Phase::Admitted) {
            if (op.phase == Phase::Waiting) {
                if (op.refresh) {
                    const auto start = op.refresh->TryStart();
                    if (start == Common::PhysicalRangeAdmission::Start::Park) return op.phase;
                    if (start != Common::PhysicalRangeAdmission::Start::Granted) return unknown();
                } else {
                    const auto start = op.admission->TryStart();
                    if (start == Common::PhysicalRegistrationBundle::Start::Draining ||
                        start == Common::PhysicalRegistrationBundle::Start::Park)
                        return op.phase;
                    if (start != Common::PhysicalRegistrationBundle::Start::Granted)
                        return unknown();
                }
                op.phase = Phase::Admitted;
            }
            if (!op.plan.DeviceEpoch()->alive.load() ||
                op.plan.DeviceEpoch()->generation.load() != op.plan.DeviceGeneration() ||
                !membership_valid() ||
                (!op.refresh && std::ranges::any_of(op.views, [](const auto& view) {
                    return !view.IsCurrentForRegistration();
                }))) {
                if (op.refresh)
                    (void)op.refresh->CompleteAfterTrackerPublication();
                else
                    (void)op.admission->CancelBeforeEffects();
                slot_buffers.erase(op.id);
                return op.phase = Phase::Cancelled;
            }
            if (scatter_busy ||
                std::ranges::any_of(
                    gather_buffers,
                    [](const auto& entry) { return entry.second.scatter_pending; }) ||
                memory_tracker.IsRegionGpuModified(op.address, op.plan.Size()))
                return unknown();
            // Actual tracker/protection body, after ALL entered writers have published.
            bool views_current = true;
            const bool marked = device_memory.WithControlledMapping(
                op.plan.DeviceEpoch(), op.plan.DeviceGeneration(), [&](const auto& aliases_for) {
                    views_current =
                        op.refresh || std::ranges::none_of(op.views, [](const auto& view) {
                            return !view.IsCurrentForRegistration();
                        });
                    std::array<DAddr, 64> aliases{};
                    for (const auto& segment : op.plan.Segments()) {
                        size_t count{};
                        if (!aliases_for(segment.storage.PhysicalOffset(), aliases, count)) {
                            views_current = false;
                            break;
                        }
                        for (size_t i = 0; i < count; ++i)
                            if (memory_tracker.IsRegionGpuModified(aliases[i], DEVICE_PAGESIZE))
                                views_current = false;
                    }
                    if (views_current)
                        memory_tracker.ForEachUploadRange(op.address, op.plan.Size(),
                                                          [](u64, u64) {});
                });
            if (!marked)
                return op.phase; // Full admission retained; no mapping/cache/PTE lock retained.
            if (!views_current) {
                if (op.refresh)
                    (void)op.refresh->CompleteAfterTrackerPublication();
                else
                    (void)op.admission->CancelBeforeEffects();
                slot_buffers.erase(op.id);
                return op.phase = Phase::Cancelled;
            }
            if (!op.transfer->Submit(op.plan)) return unknown();
            return op.phase = Phase::Submitted;
        }
        const auto result = op.transfer->Poll(op.plan);
        if (result == ControlledTransferPoll::Pending) return op.phase;
        if (result != ControlledTransferPoll::Complete ||
            !op.plan.MatchesCurrentDevice(device_memory, op.address) || !membership_valid() ||
            memory_tracker.IsRegionCpuModified(op.address, op.plan.Size()) ||
            memory_tracker.IsRegionGpuModified(op.address, op.plan.Size()))
            return unknown();
        // Revalidate and publish under the same short device-mapping guard. Never bind old
        // physical contents to a remapped live DAddr between the check and Register.
        bool published = false;
        const bool locked = device_memory.WithControlledMapping(
            op.plan.DeviceEpoch(), op.plan.DeviceGeneration(), [&](const auto&) {
                if (!membership_valid() ||
                    memory_tracker.IsRegionCpuModified(op.address, op.plan.Size()) ||
                    memory_tracker.IsRegionGpuModified(op.address, op.plan.Size()))
                    return;
                if (op.previous) DeleteBuffer(op.previous, true);
                Register(op.id);
                registered = true;
                MarkGpuUse(slot_buffers[op.id], 0, op.plan.Size());
                published = op.refresh ? op.refresh->CompleteAfterTrackerPublication()
                                       : op.admission->CompleteWithExternalEvidence();
                if (!published) {
                    Unregister(op.id);
                    registered = false;
                }
            });
        if (!locked) return op.phase;
        return published ? (op.phase = Phase::Ready) : unknown();
    } catch (...) {
        if (registered) {
            for (u64 page = op.address / CACHING_PAGESIZE;
                 page < (op.address + op.plan.Size()) / CACHING_PAGESIZE; ++page)
                SetPageBufferId(page, BufferId{});
        }
        return unknown();
    }
}

template <class P>
bool BufferCache<P>::RunGarbageCollector(bool aggressive_gc) {
    const u64 ticks_to_destroy = aggressive_gc ? 60 : 120;
    const size_t max_evictions = aggressive_gc ? 64 : 32;
    const size_t max_scans = aggressive_gc ? 128 : 64;
    constexpr u64 max_download_bytes = 32_MiB;
    struct Download {
        BufferId id;
        size_t first_copy;
        size_t num_copies;
    };
    // Fixed metadata capacity prevents an unbounded allocation before reclaim.
    std::array<Download, 64> downloads{};
    std::array<BufferCopy, 256> copies{};
    size_t num_downloads = 0;
    size_t num_copies = 0;
    size_t num_evictions = 0;
    u64 download_bytes = 0;
    const auto clean_up = [&](BufferId buffer_id) {
        auto& buffer = slot_buffers[buffer_id];
        CheckControlledSynchronization(buffer.CpuAddr(), buffer.SizeBytes());
        if (IsDirectGuest(buffer) && memory_tracker.IsRegionGpuModified(buffer.CpuAddr(), buffer.SizeBytes())) {
            DownloadBufferMemory(buffer);
        }
        if (!memory_tracker.IsRegionGpuModified(buffer.CpuAddr(), buffer.SizeBytes())) {
            DeleteBuffer(buffer_id);
            ++num_evictions;
        } else {
            // Preserve whole dirty buffers atomically. Oversized buffers stay owned;
            // do not clear page-granular tracking after a partial readback.
            if (buffer.SizeBytes() > max_download_bytes || num_downloads == max_evictions) {
                return false;
            }
            const size_t first_copy = num_copies;
            const u64 initial_bytes = download_bytes;
            bool fits = true;
            memory_tracker.ForEachDownloadRange(
                buffer.CpuAddr(), buffer.SizeBytes(), false,
                [&](DAddr addr, u64 size) {
                    gpu_modified_ranges.ForEachInRange(addr, size, [&](DAddr start, DAddr end) {
                        const u64 aligned_size = Common::AlignUp<u64>(end - start, 64);
                        if (!fits || num_copies == copies.size() ||
                            aligned_size > max_download_bytes - download_bytes) {
                            fits = false;
                            return;
                        }
                        copies[num_copies++] = BufferCopy{
                            .src_offset = start - buffer.CpuAddr(),
                            .dst_offset = download_bytes,
                            .size = end - start,
                        };
                        download_bytes += aligned_size;
                    });
                });
            if (!fits) {
                num_copies = first_copy;
                download_bytes = initial_bytes;
                return false;
            }
            downloads[num_downloads++] = {buffer_id, first_copy, num_copies - first_copy};
        }
        return num_evictions == max_evictions;
    };
    lru_cache.ForEachItemBelow(frame_tick - ticks_to_destroy, max_scans, clean_up);
    // Clean resources have priority. Keep the combined actual eviction count bounded.
    num_downloads = std::min(num_downloads, max_evictions - num_evictions);
    if (num_downloads == 0) {
        return num_evictions != 0;
    }
    const auto& last_download = downloads[num_downloads - 1];
    num_copies = last_download.first_copy + last_download.num_copies;
    download_bytes = num_copies == 0
                         ? 0
                         : copies[num_copies - 1].dst_offset +
                               Common::AlignUp<u64>(copies[num_copies - 1].size, 64);
    if (download_bytes != 0) {
        // Retain staging until Finish and all mapped reads below have completed.
        const auto staging = runtime.TryGCDownloadStagingBuffer(download_bytes);
        if (!staging) {
            // Dirty tracking and ownership are unchanged. Rate-limit failed attempts too.
            return true;
        }
        for (size_t i = 0; i < num_downloads; ++i) {
            const auto& download = downloads[i];
            auto& buffer = slot_buffers[download.id];
            auto buffer_copies =
                std::span{copies}.subspan(download.first_copy, download.num_copies);
            for (auto& copy : buffer_copies) {
                copy.dst_offset += staging->offset;
                buffer.MarkUsage(copy.src_offset, copy.size, runtime.CurrentTick());
            }
            if (!buffer_copies.empty()) {
                runtime.CopyBuffer(staging->buffer, buffer, buffer_copies, true);
            }
        }
        runtime.Finish(); // One wait for the entire dirty-buffer batch.
        for (size_t i = 0; i < num_downloads; ++i) {
            const auto& download = downloads[i];
            const auto& buffer = slot_buffers[download.id];
            for (const auto& copy : std::span{copies}.subspan(download.first_copy,
                                                            download.num_copies)) {
                WriteBackDownload(buffer.CpuAddr() + copy.src_offset,
                                  staging->mapped_span.data() + copy.dst_offset - staging->offset,
                                  copy.size);
            }
        }
    }
    for (size_t i = 0; i < num_downloads; ++i) {
        const auto& buffer = slot_buffers[downloads[i].id];
        memory_tracker.ForEachDownloadRangeAndClear(
            buffer.CpuAddr(), buffer.SizeBytes(), [&](DAddr addr, u64 size) {
                ClearDownload(addr, size);
                gpu_modified_ranges.Subtract(addr, size);
            });
        DeleteBuffer(downloads[i].id);
    }
    return true;
}

template <class P>
void BufferCache<P>::TickFrame(u64 cache_usage, u64 cache_target,
                              std::span<const HeapMemoryBudget> heaps) {
    // Homebrew console apps don't create or bind any channels, so this will be nullptr.
    if (!channel_state) {
        return;
    }
    ResolveScatter(0, 0);
    runtime.TickFrame(slot_buffers);

    // Calculate hits and shots and move hit bits to the right
    const u32 hits = std::reduce(channel_state->uniform_cache_hits.begin(),
                                 channel_state->uniform_cache_hits.end());
    const u32 shots = std::reduce(channel_state->uniform_cache_shots.begin(),
                                  channel_state->uniform_cache_shots.end());
    std::copy_n(channel_state->uniform_cache_hits.begin(),
                channel_state->uniform_cache_hits.size() - 1,
                channel_state->uniform_cache_hits.begin() + 1);
    std::copy_n(channel_state->uniform_cache_shots.begin(),
                channel_state->uniform_cache_shots.size() - 1,
                channel_state->uniform_cache_shots.begin() + 1);
    channel_state->uniform_cache_hits[0] = 0;
    channel_state->uniform_cache_shots[0] = 0;

    const bool skip_preferred = hits * 256 < shots * 251;
    channel_state->uniform_buffer_skip_cache_size = skip_preferred ? DEFAULT_SKIP_CACHE_SIZE : 0;

    memory_pressure.Update(cache_usage, cache_target, heaps);
    TrimGatherBuffers(memory_pressure.IsCritical());
    if (memory_pressure.CanReclaim(frame_tick, TICKS_TO_DESTROY) &&
        RunGarbageCollector(memory_pressure.IsCritical())) {
        memory_pressure.Reclaimed(frame_tick);
    }
    ++frame_tick;
    delayed_destruction_ring.Tick();

    for (auto& buffer : async_buffers_death_ring) {
        runtime.FreeDeferredStagingBuffer(buffer);
    }
    async_buffers_death_ring.clear();
}

template <class P>
bool BufferCache<P>::CpuWriteNeedsGpuThread(DAddr address, u64 size) const {
    if (has_direct_guest_buffers) {
        return true;
    }
    for (const auto gpu_addr : scatter_pending) {
        const auto it = gather_buffers.find(gpu_addr);
        if (it != gather_buffers.end() && std::ranges::any_of(it->second.chunks,
            [&](const GatherChunk& chunk) {
                return size == 0 || (address < chunk.device_addr + chunk.size &&
                                    chunk.device_addr < address + size);
            })) {
            return true;
        }
    }
    return false;
}

template <class P>
void BufferCache<P>::WaitGuestBacking(DAddr address, u64 size) {
    if (!has_direct_guest_buffers) { return; }
    bool shared = false;
    ForEachBufferInRange(address, size, [&](BufferId, Buffer& buffer) {
        shared |= IsDirectGuest(buffer) && NeedsGuestCpuAccess(buffer);
    });
    if (shared) {
        GuestMemoryBarrier(true);
        // EndGuestCpuAccess precedes every GPU use of a direct backing. An fd
        // already in its CPU interval has no intervening GPU reads or writes.
        runtime.Finish(); // Also wait for outstanding GPU *reads* on ownership transfer.
        ForEachBufferInRange(address, size, [&](BufferId, Buffer& buffer) {
            if (IsDirectGuest(buffer)) { BeginGuestCpuAccess(buffer); }
        });
    }
}

template <class P>
void BufferCache<P>::WriteMemory(DAddr device_addr, u64 size) {
    ResolveScatter(device_addr, size);
    WaitGuestBacking(device_addr, size);
    if (size <= DEVICE_PAGESIZE) {
        size = WidenHotWrite(device_addr, size);
    }
    NotifyGatherWrite(device_addr, size);
    if (memory_tracker.IsRegionGpuModified(device_addr, size)) {
        ClearDownload(device_addr, size);
        gpu_modified_ranges.Subtract(device_addr, size);
    }
    memory_tracker.MarkRegionAsCpuModified(device_addr, size);
}

template <class P>
template <typename Store>
Core::Memory::PreparedCpuWriteResult
BufferCache<P>::CommitControlledCpuWrite(Common::PhysicalRegistrationBundle::CachedWrite& write,
                                         std::span<const DAddr> device_aliases, u64 size,
                                         Store&& store) {
    using Result = Core::Memory::PreparedCpuWriteResult;
    if (!write.Granted() || !size || size > DEVICE_PAGESIZE || device_aliases.empty() ||
        device_aliases.size() > Common::PhysicalRangeAdmission::MaxAccesses || scatter_busy ||
        std::ranges::any_of(gather_buffers,
                            [](const auto& item) { return item.second.scatter_pending; }))
        return Result::NeedSlow;
    for (const auto address : device_aliases) {
        if (!IsRegionPageTableAddressable(address, size) ||
            size > DEVICE_PAGESIZE - address % DEVICE_PAGESIZE ||
            !IsRegionRegistered(address, size) || memory_tracker.IsRegionGpuModified(address, size))
            return Result::NeedSlow;
    }
    try {
        const auto result = store();
        if (result != Result::Stored) return result;
        // The real store has completed. Admission still excludes registration/uploads here.
        // Never use pre-store dirty notification as proof of post-store publication.
        for (const auto address : device_aliases) {
            NotifyGatherWrite(address, size);
            memory_tracker.MarkRegionAsCpuModified(address, size);
        }
    } catch (...) {
        // Store or publication may already have effects. Catching the exception must not let
        // a caller complete this obligation or expose an incompletely dirtied alias as clean.
        (void)write.MarkUnknown();
        throw;
    }
    // The caller may release only after observing Stored from this whole adapter.
    return Result::Stored;
}

template <class P>
u64 BufferCache<P>::WidenHotWrite(DAddr device_addr, u64 size) {
    // A write only says something about the guest when it landed on a page being watched
    if (memory_tracker.IsRegionCpuModified(device_addr, size)) {
        return size;
    }
    const u64 block = device_addr >> CACHING_PAGEBITS;
    if (block >= CACHING_PAGE_COUNT) {
        return size;
    }
    // Each entry holds the frame of the last write in the upper half and a count in the lower.
    const u16 now = static_cast<u16>(frame_tick);
    const auto is_recent = [now](u32 heat) {
        return static_cast<u16>(now - static_cast<u16>(heat >> 16)) <= WRITE_HEAT_FRAMES;
    };
    const auto is_hot = [&is_recent](u32 heat) {
        return static_cast<u16>(heat) >= HOT_WRITE_COUNT && is_recent(heat);
    };
    u32& heat = write_heat[block];
    u16 count = static_cast<u16>(heat);
    if (!is_recent(heat)) {
        count /= 2;
    }
    if (count < HOT_WRITE_COUNT) {
        heat = (u32{now} << 16) | static_cast<u16>(count + 1);
        return size;
    }
    count = std::min<u16>(static_cast<u16>(count + 1), HOT_WRITE_COUNT * 2);
    heat = (u32{now} << 16) | count;

    // The guest keeps rewriting this caching page. Each write into a watched page costs a fault
    // on the guest thread, so stop watching the rest of it now, and the busy caching pages that
    // follow it. Pages that end up unchanged are only uploaded again if something binds them.
    const u64 last_block =
        std::min<u64>(CACHING_PAGE_COUNT, block + MAX_WIDENED_WRITE / CACHING_PAGESIZE);
    u64 end_block = block + 1;
    while (end_block < last_block) {
        if (!is_hot(write_heat[end_block])) {
            break;
        }
        ++end_block;
    }
    const u64 wide_size = (end_block << CACHING_PAGEBITS) - device_addr;
    if (wide_size <= size) {
        // The write runs past the end of the hot caching pages. Never mark less than was written.
        return size;
    }
    // Pages the host GPU wrote hold data guest memory does not have yet. Leave those alone.
    if (memory_tracker.IsRegionGpuModified(device_addr, wide_size) ||
        IsRegionGpuModified(device_addr, wide_size)) {
        return size;
    }
    return wide_size;
}

template <class P>
void BufferCache<P>::CachedWriteMemory(DAddr device_addr, u64 size) {
    const bool is_dirty = IsRegionRegistered(device_addr, size);
    if (!is_dirty) {
        return;
    }
    DAddr aligned_start = Common::AlignDown(device_addr, DEVICE_PAGESIZE);
    DAddr aligned_end = Common::AlignUp(device_addr + size, DEVICE_PAGESIZE);
    if (!IsRegionGpuModified(aligned_start, aligned_end - aligned_start)) {
        WriteMemory(device_addr, size);
        return;
    }

    tmp_buffer.resize_destructive(size);
    device_memory.ReadBlockUnsafe(device_addr, tmp_buffer.data(), size, "BufferCache.InlineMemory");

    InlineMemoryImplementation(device_addr, size, tmp_buffer);
}

template <class P>
bool BufferCache<P>::OnCPUWrite(DAddr device_addr, u64 size) {
    WaitGuestBacking(device_addr, size);
    const bool is_dirty = IsRegionRegistered(device_addr, size);
    if (!is_dirty) {
        return false;
    }
    if (memory_tracker.IsRegionGpuModified(device_addr, size)) {
        bool staged = false;
        ForEachBufferInRange(device_addr, size, [&](BufferId, Buffer& buffer) {
            staged |= !IsDirectGuest(buffer);
        });
        if (staged) {
            const auto started = std::chrono::steady_clock::now();
            // This notification precedes the CPU store. Preserve the other GPU
            // bytes before coarse deferred CPU dirty tracking can discard them.
            ResolveScatter(device_addr, size);
            WaitGuestBacking(device_addr, size);
            ForEachBufferInRange(device_addr, size, [&](BufferId, Buffer& buffer) {
                if (!IsDirectGuest(buffer)) {
                    const DAddr begin = std::max(device_addr, buffer.CpuAddr());
                    const DAddr end = std::min(device_addr + size, buffer.CpuAddr() + buffer.SizeBytes());
                    DownloadBufferMemory(buffer, begin, end - begin);
                }
            });
            const u64 elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count();
            ++cpu_write_preserve_count;
            cpu_write_preserve_time_ns += elapsed;
            if (cpu_write_preserve_count <= 8 || (cpu_write_preserve_count % 64) == 0) {
                LOG_INFO(HW_Memory, "Staged CPU write preservation: count={} time_ns={} last_time_ns={}",
                         cpu_write_preserve_count, cpu_write_preserve_time_ns, elapsed);
            }
        }
        if (memory_tracker.IsRegionGpuModified(device_addr, size)) {
            return true;
        }
    }
    WriteMemory(device_addr, size);
    return false;
}

template <class P>
std::optional<VideoCore::RasterizerDownloadArea> BufferCache<P>::GetFlushArea(DAddr device_addr,
                                                                              u64 size) {
    std::optional<VideoCore::RasterizerDownloadArea> area{};
    area.emplace();
    DAddr device_addr_start_aligned = Common::AlignDown(device_addr, Core::DEVICE_PAGESIZE);
    DAddr device_addr_end_aligned = Common::AlignUp(device_addr + size, Core::DEVICE_PAGESIZE);
    area->start_address = device_addr_start_aligned;
    area->end_address = device_addr_end_aligned;
    area->preemtive = !IsRegionGpuModified(device_addr_start_aligned,
                                           device_addr_end_aligned - device_addr_start_aligned);
    memory_tracker.MarkRegionAsPreflushable(device_addr_start_aligned,
                                            device_addr_end_aligned - device_addr_start_aligned);
    return area;
}

template <class P>
void BufferCache<P>::DownloadMemory(DAddr device_addr, u64 size) {
    CheckControlledSynchronization(device_addr, size);
    ResolveScatter(device_addr, size);
    ForEachBufferInRange(device_addr, size, [&](BufferId, Buffer& buffer) {
        DownloadBufferMemory(buffer, device_addr, size);
    });
}

template <class P>
void BufferCache<P>::ClearDownload(DAddr device_addr, u64 size) {
    async_downloads.DeleteAll(device_addr, size);
    uncommitted_gpu_modified_ranges.Subtract(device_addr, size);
    for (auto& interval_set : committed_gpu_modified_ranges) {
        interval_set.Subtract(device_addr, size);
    }
}

template <class P>
bool BufferCache<P>::DMACopy(GPUVAddr src_address, GPUVAddr dest_address, u64 amount) {
    if (const auto direct = TryDirectDMACopy(src_address, dest_address, amount)) {
        return *direct;
    }
    return DMACopyImpl(src_address, dest_address, amount);
}

template <class P>
bool BufferCache<P>::DMACopyImpl(GPUVAddr src_address, GPUVAddr dest_address, u64 amount) {
    // The copy below treats [src, +amount) and [dest, +amount) as single device ranges. When either
    // side is split across non-contiguous device memory, copy it piece by piece instead.
    if (gpu_memory->MaxContinuousRange(src_address, amount) < amount ||
        gpu_memory->MaxContinuousRange(dest_address, amount) < amount) {
        return DMACopySplit(src_address, dest_address, amount);
    }
    const std::optional<DAddr> cpu_src_address = gpu_memory->GpuToCpuAddress(src_address);
    const std::optional<DAddr> cpu_dest_address = gpu_memory->GpuToCpuAddress(dest_address);
    if (!cpu_src_address || !cpu_dest_address) {
        return false;
    }
    const bool source_dirty = IsRegionRegistered(*cpu_src_address, amount);
    const bool dest_dirty = IsRegionRegistered(*cpu_dest_address, amount);
    if (!source_dirty && !dest_dirty) {
        return false;
    }

    BufferId buffer_a;
    BufferId buffer_b;
    do {
        channel_state->has_deleted_buffers = false;
        buffer_a = FindBuffer(*cpu_src_address, static_cast<u32>(amount));
        buffer_b = FindBuffer(*cpu_dest_address, static_cast<u32>(amount));
    } while (channel_state->has_deleted_buffers);
    auto& src_buffer = slot_buffers[buffer_a];
    auto& dest_buffer = slot_buffers[buffer_b];
    if (IsDirectGuest(src_buffer) || IsDirectGuest(dest_buffer)) {
        DownloadMemory(*cpu_src_address, amount);
        DownloadMemory(*cpu_dest_address, amount);
        GuestMemoryBarrier(true);
        runtime.Finish();
        return false;
    }
    ClearDownload(*cpu_dest_address, amount);
    SynchronizeBuffer(src_buffer, *cpu_src_address, static_cast<u32>(amount));
    SynchronizeBuffer(dest_buffer, *cpu_dest_address, static_cast<u32>(amount));
    std::array copies{BufferCopy{
        .src_offset = src_buffer.Offset(*cpu_src_address),
        .dst_offset = dest_buffer.Offset(*cpu_dest_address),
        .size = amount,
    }};

    boost::container::small_vector<std::pair<DAddr, size_t>, 4> tmp_intervals;
    auto mirror = [&](DAddr base_address, DAddr base_address_end) {
        const u64 size = base_address_end - base_address;
        const DAddr diff = base_address - *cpu_src_address;
        const DAddr new_base_address = *cpu_dest_address + diff;
        tmp_intervals.push_back({new_base_address, size});
        uncommitted_gpu_modified_ranges.Add(new_base_address, size);
    };
    gpu_modified_ranges.ForEachInRange(*cpu_src_address, amount, mirror);
    // This subtraction in this order is important for overlapping copies.
    gpu_modified_ranges.Subtract(*cpu_dest_address, amount);
    const bool has_new_downloads = tmp_intervals.size() != 0;
    for (const auto& pair : tmp_intervals) {
        gpu_modified_ranges.Add(pair.first, pair.second);
    }
    const auto& copy = copies[0];
    MarkGpuUse(src_buffer, copy.src_offset, copy.size);
    MarkGpuUse(dest_buffer, copy.dst_offset, copy.size);
    dest_buffer.gpu_write_revision.Mark();
    runtime.CopyBuffer(dest_buffer, src_buffer, copies, true);
    NotifyGatherWrite(*cpu_dest_address, amount);
    if (has_new_downloads) {
        memory_tracker.MarkRegionAsGpuModified(*cpu_dest_address, amount);
    }

    Tegra::Memory::DeviceGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::UnsafeReadWrite>
        tmp(device_memory, *cpu_src_address, amount, &tmp_buffer);
    tmp.SetAddressAndSize(*cpu_dest_address, amount);
    return true;
}

template <class P>
std::optional<bool> BufferCache<P>::TryDirectDMACopy(GPUVAddr src_address, GPUVAddr dest_address,
                                                   u64 amount) {
    if constexpr (requires { runtime.TryDirectGuestBuffer(slot_buffers[NULL_BUFFER_ID], Common::GuestBackingRange{}); }) {
        if ((!Settings::values.use_uma_zero_copy.GetValue() && !has_direct_guest_buffers) || amount == 0 ||
            amount > std::numeric_limits<u32>::max() ||
            src_address > std::numeric_limits<u64>::max() - amount ||
            dest_address > std::numeric_limits<u64>::max() - amount) {
            return std::nullopt;
        }
        const GuestMappingIdentity before{gpu_memory, gpu_memory->GetID(), gpu_memory->MapGeneration()};
        const auto device_generation = device_memory.MapGeneration();
        if (!before.Stable() || (device_generation & 1)) { return std::nullopt; }
        struct Piece {
            DAddr src, dst;
            u32 size;
            Common::GuestBackingRange src_backing, dst_backing;
            BufferId src_id{}, dst_id{};
        };
        std::vector<Piece> pieces;
        // No copy, CPU mirror write or dirty-state change until the whole plan passes.
        for (u64 offset = 0; offset < amount;) {
            const auto src = gpu_memory->GpuToCpuAddress(src_address + offset);
            const auto dst = gpu_memory->GpuToCpuAddress(dest_address + offset);
            const u64 size = std::min<u64>({amount - offset,
                gpu_memory->MaxContinuousRange(src_address + offset, amount - offset),
                gpu_memory->MaxContinuousRange(dest_address + offset, amount - offset)});
            if (!src || !dst || !size || ((*src | *dst | size) & 3) ||
                !IsRegionPageTableAddressable(*src, size) || !IsRegionPageTableAddressable(*dst, size)) {
                return std::nullopt;
            }
            const auto a = device_memory.GetDriverBackingRange(*src, size);
            const auto b = device_memory.GetDriverBackingRange(*dst, size);
            if (!a || !b) {
                return std::nullopt;
            }
            pieces.push_back({*src, *dst, static_cast<u32>(size), *a, *b});
            offset += size;
        }
        // Different guest VAs/DAddrs can alias the same external allocation.
        // Guest-memory fallback snapshots the source and implements memmove semantics.
        for (const auto& a : pieces) {
            for (const auto& b : pieces) {
                if (a.src_backing.fd == b.dst_backing.fd && GuestRangesOverlap(
                    a.src_backing.offset, a.size, b.dst_backing.offset, b.size)) {
                    GuestMemoryBarrier(true);
                    runtime.Finish();
                    return false;
                }
            }
        }
        // Resolve all overlapping cache allocations before inspecting eligibility or
        // retaining ids; allocating a later piece can replace an earlier piece's buffer.
        do {
            channel_state->has_deleted_buffers = false;
            for (auto& piece : pieces) {
                piece.src_id = FindBuffer(piece.src, piece.size);
                piece.dst_id = FindBuffer(piece.dst, piece.size);
            }
        } while (channel_state->has_deleted_buffers);
        bool any_direct = false, all_direct = true;
        for (const auto& piece : pieces) {
            any_direct |= IsDirectGuest(slot_buffers[piece.src_id]) || IsDirectGuest(slot_buffers[piece.dst_id]);
            all_direct &= IsDirectGuest(slot_buffers[piece.src_id]) && IsDirectGuest(slot_buffers[piece.dst_id]);
        }
        if (!any_direct) { return std::nullopt; }
        if (!all_direct) {
            // Mixed ownership: flush the entire source/destination before letting the
            // guest-memory implementation perform the whole operation.
            for (const auto& piece : pieces) {
                DownloadMemory(piece.src, piece.size);
                DownloadMemory(piece.dst, piece.size);
            }
            GuestMemoryBarrier(true);
            runtime.Finish();
            return false;
        }
        const GuestMappingIdentity after{gpu_memory, gpu_memory->GetID(), gpu_memory->MapGeneration()};
        if (before != after || device_generation != device_memory.MapGeneration()) { return std::nullopt; }
        for (const auto& piece : pieces) {
            const auto a = piece.src_id;
            const auto b = piece.dst_id;
            auto& src = slot_buffers[a];
            auto& dst = slot_buffers[b];
            SynchronizeBuffer(src, piece.src, piece.size);
            SynchronizeBuffer(dst, piece.dst, piece.size);
            ClearDownload(piece.dst, piece.size);
            const std::array copies{BufferCopy{src.Offset(piece.src), dst.Offset(piece.dst), piece.size}};
            MarkGpuUse(src, copies[0].src_offset, piece.size);
            MarkGpuUse(dst, copies[0].dst_offset, piece.size);
            runtime.CopyBuffer(dst, src, copies, true);
            MarkWrittenBuffer(b, piece.dst, piece.size);
        }
        return true;
    }
    return std::nullopt;
}

template <class P>
bool BufferCache<P>::DMACopySplit(GPUVAddr src_address, GPUVAddr dest_address, u64 amount) {
    struct Piece {
        GPUVAddr src;
        GPUVAddr dst;
        u64 size;
    };
    boost::container::small_vector<Piece, 8> pieces;
    for (u64 offset = 0; offset < amount;) {
        const u64 left = amount - offset;
        const u64 size =
            std::min<u64>({gpu_memory->MaxContinuousRange(src_address + offset, left),
                           gpu_memory->MaxContinuousRange(dest_address + offset, left), left});
        const std::optional<DAddr> src = gpu_memory->GpuToCpuAddress(src_address + offset);
        const std::optional<DAddr> dst = gpu_memory->GpuToCpuAddress(dest_address + offset);
        if (size == 0 || !src || !dst) {
            return false;
        }
        if (((*src | *dst | size) & 3) != 0 || size > std::numeric_limits<u32>::max() ||
            !IsRegionPageTableAddressable(*src, size) ||
            !IsRegionPageTableAddressable(*dst, size)) {
            return false;
        }
        if (!IsRegionRegistered(*src, size) && !IsRegionRegistered(*dst, size)) {
            // Not cached on either side: let the caller do the whole copy through guest memory,
            // which translates every page
            return false;
        }
        pieces.push_back({src_address + offset, dest_address + offset, size});
        offset += size;
    }
    for (const Piece& src_piece : pieces) {
        const DAddr src = *gpu_memory->GpuToCpuAddress(src_piece.src);
        for (const Piece& dst_piece : pieces) {
            const DAddr dst = *gpu_memory->GpuToCpuAddress(dst_piece.dst);
            if (src < dst + dst_piece.size && dst < src + src_piece.size) {
                return false; // Guest-memory fallback snapshots the entire source before writes.
            }
        }
    }
    // A direct-path rejection must not turn into a partial copy when a later
    // piece uses shared backing. Check all ownership before executing fallback.
    bool shared = false;
    for (const Piece& piece : pieces) {
        const DAddr src = *gpu_memory->GpuToCpuAddress(piece.src);
        const DAddr dst = *gpu_memory->GpuToCpuAddress(piece.dst);
        shared |= IsDirectGuest(slot_buffers[FindBuffer(src, static_cast<u32>(piece.size))]);
        shared |= IsDirectGuest(slot_buffers[FindBuffer(dst, static_cast<u32>(piece.size))]);
    }
    if (shared) {
        for (const Piece& piece : pieces) {
            DownloadMemory(*gpu_memory->GpuToCpuAddress(piece.src), piece.size);
            DownloadMemory(*gpu_memory->GpuToCpuAddress(piece.dst), piece.size);
        }
        GuestMemoryBarrier(true);
        runtime.Finish();
        return false;
    }
    for (const Piece& piece : pieces) {
        if (!DMACopyImpl(piece.src, piece.dst, piece.size)) {
            return false;
        }
    }
    return true;
}

template <class P>
bool BufferCache<P>::DMAClearSplit(GPUVAddr dst_address, u64 amount, u32 value) {
    const u64 bytes = amount * sizeof(u32);
    boost::container::small_vector<std::pair<GPUVAddr, u64>, 8> pieces;
    for (u64 offset = 0; offset < bytes;) {
        const u64 size = std::min<u64>(
            gpu_memory->MaxContinuousRange(dst_address + offset, bytes - offset), bytes - offset);
        const std::optional<DAddr> dst = gpu_memory->GpuToCpuAddress(dst_address + offset);
        if (size == 0 || size % sizeof(u32) != 0 || size > std::numeric_limits<u32>::max() ||
            !dst || !IsRegionPageTableAddressable(*dst, size) || !IsRegionRegistered(*dst, size)) {
            return false;
        }
        pieces.emplace_back(dst_address + offset, size);
        offset += size;
    }
    for (const auto& [addr, size] : pieces) {
        if (!DMAClear(addr, size / sizeof(u32), value)) {
            return false;
        }
    }
    return true;
}

template <class P>
bool BufferCache<P>::DMAClear(GPUVAddr dst_address, u64 amount, u32 value) {
    if (gpu_memory->MaxContinuousRange(dst_address, amount * sizeof(u32)) < amount * sizeof(u32)) {
        return DMAClearSplit(dst_address, amount, value);
    }
    const std::optional<DAddr> cpu_dst_address = gpu_memory->GpuToCpuAddress(dst_address);
    if (!cpu_dst_address) {
        return false;
    }
    const bool dest_dirty = IsRegionRegistered(*cpu_dst_address, amount * sizeof(u32));
    if (!dest_dirty) {
        return false;
    }

    const size_t size = amount * sizeof(u32);
    ClearDownload(*cpu_dst_address, size);
    gpu_modified_ranges.Subtract(*cpu_dst_address, size);

    const BufferId buffer = FindBuffer(*cpu_dst_address, static_cast<u32>(size));
    Buffer& dest_buffer = slot_buffers[buffer];
    const u32 offset = dest_buffer.Offset(*cpu_dst_address);
    MarkGpuUse(dest_buffer, offset, size);
    dest_buffer.gpu_write_revision.Mark();
    runtime.ClearBuffer(dest_buffer, offset, size, value);
    NotifyGatherWrite(*cpu_dst_address, size);
    return true;
}

template <class P>
bool BufferCache<P>::HasUniqueAliasSource(DAddr source, const Tegra::PhysicalAccessPlan& plan,
                                        std::span<const AliasCopyRange> copies) {
    if (plan.Segments().empty() || copies.empty()) return false;
    bool unique = true;
    const bool current = device_memory.WithCurrentControlledAliases(
        [&](const auto& backing, const auto& aliases_for) {
            if (backing != plan.Segments().front().storage.Epoch()) { unique = false; return; }
            std::array<DAddr, 64> aliases{};
            for (const auto& copy : copies) {
                size_t count{};
                if (!copy.size || copy.size > DEVICE_PAGESIZE - copy.physical_offset % DEVICE_PAGESIZE ||
                    !aliases_for(copy.physical_offset, aliases, count)) { unique = false; return; }
                bool found_source{};
                for (size_t i = 0; i < count; ++i) {
                    if (aliases[i] == source + copy.source_offset) { found_source = true; continue; }
                    gpu_modified_ranges.ForEachInRange(aliases[i], copy.size,
                        [&](DAddr, DAddr) { unique = false; });
                }
                if (!found_source) unique = false;
            }
        });
    return current && unique; // Unknown alias cardinality/mapping is never disjointness.
}

template <class P>
std::optional<AliasWriteSnapshot> BufferCache<P>::SnapshotAliasWrites(
    DAddr address, u64 size) {
    if (!size || !IsRegionPageTableAddressable(address, size) || scatter_busy ||
        !async_downloads.Empty() || !pending_downloads.empty() ||
        std::ranges::any_of(committed_gpu_modified_ranges,
                           [](const auto& ranges) { return !ranges.Empty(); }) ||
        std::ranges::any_of(gather_buffers, [](const auto& item) {
            return item.second.scatter_pending;
        }) || memory_tracker.IsRegionCpuModified(address, size)) return std::nullopt;
    CheckControlledSynchronization(address, size);
    const auto id = GetPageBufferId(address >> CACHING_PAGEBITS);
    if (!id || id == NULL_BUFFER_ID) return std::nullopt;
    const auto& buffer = slot_buffers[id];
    if (address < buffer.CpuAddr() || address - buffer.CpuAddr() >= buffer.SizeBytes() ||
        size > buffer.SizeBytes() - (address - buffer.CpuAddr())) return std::nullopt;
    const auto revision = buffer.gpu_write_revision.Snapshot();
    if (!revision) return std::nullopt;
    std::vector<AliasWrittenRange> ranges;
    gpu_modified_ranges.ForEachInRange(address, size, [&](DAddr begin, DAddr end) {
        if (ranges.size() <= Common::PhysicalRangeAdmission::MaxAccesses)
            ranges.push_back({begin - address, end - begin});
    });
    if (ranges.size() > Common::PhysicalRangeAdmission::MaxAccesses) return std::nullopt;
    return AliasWriteSnapshot{std::move(ranges), *revision};
}

template <class P>
template <typename B>
auto BufferCache<P>::PinExistingDmaRepresentation(
    DAddr device_addr, const Tegra::PhysicalAccessPlan& physical)
    -> decltype(std::declval<const B&>().PinStagedRepresentation(u64{}, u64{})) {
    static_assert(std::is_same_v<B, Buffer>);
    const u64 size = physical.Size();
    if (!size || !IsRegionPageTableAddressable(device_addr, size) || scatter_busy ||
        std::ranges::any_of(gather_buffers, [](const auto& item) {
            return item.second.scatter_pending;
        })) {
        return std::nullopt;
    }
    const auto id = GetPageBufferId(device_addr >> CACHING_PAGEBITS);
    if (!id || id == NULL_BUFFER_ID) {
        return std::nullopt;
    }
    const auto& buffer = slot_buffers[id];
    if (device_addr < buffer.CpuAddr() || device_addr - buffer.CpuAddr() >= buffer.SizeBytes() ||
        size > buffer.SizeBytes() - (device_addr - buffer.CpuAddr())) {
        return std::nullopt;
    }
    const auto last_page = (device_addr + size - 1) >> CACHING_PAGEBITS;
    for (auto page = device_addr >> CACHING_PAGEBITS; page <= last_page; ++page) {
        if (GetPageBufferId(page) != id) {
            return std::nullopt;
        }
    }
    std::shared_ptr<const Common::MappingEpoch> device_epoch;
    u64 device_generation{};
    const bool matches = device_memory.ReadPhysicalMappingSnapshot(
        [&](auto epoch, u64 generation, auto backing, u64 backing_size, const auto& translate) {
            if (!epoch || !epoch->alive.load(std::memory_order_acquire) || (generation & 1) ||
                !Tegra::Detail::MatchesDevicePhysicalPlanLocked(
                    physical, device_addr, Core::DEVICE_PAGESIZE, backing, backing_size, translate)) {
                return false;
            }
            device_epoch = std::move(epoch);
            device_generation = generation;
            return true;
        });
    if (!matches) {
        return std::nullopt;
    }
    // Acquire the real resource owner outside the device mapping lock. Never FindBuffer,
    // synchronize contents or resolve pending scatter just to discover a representation.
    auto pin = buffer.PinStagedRepresentation(device_addr - buffer.CpuAddr(), size);
    if (!pin) {
        return std::nullopt;
    }
    const bool current = device_memory.ReadPhysicalMappingSnapshot(
        [&](auto epoch, u64 generation, auto, u64, const auto&) {
            return epoch == device_epoch && generation == device_generation &&
                   epoch->alive.load(std::memory_order_acquire);
        });
    if (!current) {
        return std::nullopt;
    }
    return pin;
}

template <class P>
std::pair<typename P::Buffer*, u32> BufferCache<P>::ObtainBuffer(GPUVAddr gpu_addr, u32 size,
                                                                 ObtainBufferSynchronize sync_info,
                                                                 ObtainBufferOperation post_op) {
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr);
    if (!device_addr) {
        return {&slot_buffers[NULL_BUFFER_ID], 0};
    }
    return ObtainCPUBuffer(*device_addr, size, sync_info, post_op);
}

template <class P>
std::pair<typename P::Buffer*, u32> BufferCache<P>::ObtainCPUBuffer(
    DAddr device_addr, u32 size, ObtainBufferSynchronize sync_info, ObtainBufferOperation post_op) {
    const BufferId buffer_id = FindBuffer(device_addr, size);
    Buffer& buffer = slot_buffers[buffer_id];

    // synchronize op
    switch (sync_info) {
    case ObtainBufferSynchronize::FullSynchronize:
        SynchronizeBuffer(buffer, device_addr, size);
        break;
    default:
        break;
    }

    switch (post_op) {
    case ObtainBufferOperation::MarkAsWritten:
        MarkWrittenBuffer(buffer_id, device_addr, size);
        break;
    case ObtainBufferOperation::DiscardWrite: {
        const DAddr device_addr_start = Common::AlignDown(device_addr, 64);
        const DAddr device_addr_end = Common::AlignUp(device_addr + size, 64);
        const size_t new_size = device_addr_end - device_addr_start;
        ClearDownload(device_addr_start, new_size);
        gpu_modified_ranges.Subtract(device_addr_start, new_size);
        break;
    }
    default:
        break;
    }

    // The caller hands this range to GPU work (queries, indirect dispatch, DMA image copies).
    Uma::StampPages(buffer.page_gpu_tick, buffer.Offset(device_addr), size, runtime.CurrentTick());
    return {&buffer, buffer.Offset(device_addr)};
}

template <class P>
void BufferCache<P>::BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr,
                                               u32 size) {
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr);
    const Binding binding{
        .device_addr = *device_addr,
        .size = size,
        .buffer_id = BufferId{},
        .gpu_addr = gpu_addr,
    };
    channel_state->uniform_buffers[stage][index] = binding;
}

template <class P>
void BufferCache<P>::DisableGraphicsUniformBuffer(size_t stage, u32 index) {
    channel_state->uniform_buffers[stage][index] = NULL_BINDING;
}

template <class P>
void BufferCache<P>::UpdateGraphicsBuffers(bool is_indexed) {
    for (auto& [addr, entry] : gather_buffers) {
        entry.prepared_size = 0;
    }
    do {
        channel_state->has_deleted_buffers = false;
        DoUpdateGraphicsBuffers(is_indexed);
    } while (channel_state->has_deleted_buffers);
}

template <class P>
void BufferCache<P>::UpdateComputeBuffers() {
    for (auto& [addr, entry] : gather_buffers) {
        entry.prepared_size = 0;
    }
    do {
        channel_state->has_deleted_buffers = false;
        DoUpdateComputeBuffers();
    } while (channel_state->has_deleted_buffers);
}

template <class P>
void BufferCache<P>::BindHostGeometryBuffers(bool is_indexed) {
    if (is_indexed) {
        BindHostIndexBuffer();
    } else if constexpr (!HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT) {
        const auto& draw_state = maxwell3d->draw_manager->GetDrawState();
        if (draw_state.topology == Maxwell::PrimitiveTopology::Quads ||
            draw_state.topology == Maxwell::PrimitiveTopology::QuadStrip) {
            runtime.BindQuadIndexBuffer(draw_state.topology, draw_state.vertex_buffer.first,
                                        draw_state.vertex_buffer.count);
        }
    }
    BindHostVertexBuffers();
    BindHostTransformFeedbackBuffers();
    if (current_draw_indirect) {
        BindHostDrawIndirectBuffers();
    }
}

template <class P>
void BufferCache<P>::BindHostStageBuffers(size_t stage) {
    BindHostGraphicsUniformBuffers(stage);
    BindHostGraphicsStorageBuffers(stage);
    BindHostGraphicsTextureBuffers(stage);
}

template <class P>
void BufferCache<P>::BindHostComputeBuffers() {
    BindHostComputeUniformBuffers();
    BindHostComputeStorageBuffers();
    BindHostComputeTextureBuffers();
}

template <class P>
void BufferCache<P>::SetUniformBuffersState(const std::array<u32, NUM_STAGES>& mask,
                                            const UniformBufferSizes* sizes) {
    if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
        if (channel_state->enabled_uniform_buffer_masks != mask) {
            if constexpr (IS_OPENGL) {
                channel_state->fast_bound_uniform_buffers.fill(0);
            }
            channel_state->dirty_uniform_buffers.fill(~u32{0});
            channel_state->uniform_buffer_binding_sizes.fill({});
        }
    }
    channel_state->enabled_uniform_buffer_masks = mask;
    channel_state->uniform_buffer_sizes = sizes;
}

template <class P>
void BufferCache<P>::SetComputeUniformBufferState(u32 mask,
                                                  const ComputeUniformBufferSizes* sizes) {
    channel_state->enabled_compute_uniform_buffer_mask = mask;
    channel_state->compute_uniform_buffer_sizes = sizes;
}

template <class P>
void BufferCache<P>::UnbindGraphicsStorageBuffers(size_t stage) {
    channel_state->enabled_storage_buffers[stage] = 0;
    channel_state->written_storage_buffers[stage] = 0;
}

template <class P>
void BufferCache<P>::BindGraphicsStorageBuffer(size_t stage, size_t ssbo_index, u32 cbuf_index,
                                               u32 cbuf_offset, bool is_written) {
    channel_state->enabled_storage_buffers[stage] |= 1U << ssbo_index;
    channel_state->written_storage_buffers[stage] |= (is_written ? 1U : 0U) << ssbo_index;

    const auto& cbufs = maxwell3d->state.shader_stages[stage];
    const GPUVAddr ssbo_addr = cbufs.const_buffers[cbuf_index].address + cbuf_offset;
    channel_state->storage_buffers[stage][ssbo_index] =
        StorageBufferBinding(ssbo_addr, cbuf_index, is_written);
}

template <class P>
void BufferCache<P>::UnbindGraphicsTextureBuffers(size_t stage) {
    channel_state->enabled_texture_buffers[stage] = 0;
    channel_state->written_texture_buffers[stage] = 0;
    channel_state->image_texture_buffers[stage] = 0;
}

template <class P>
void BufferCache<P>::BindGraphicsTextureBuffer(size_t stage, size_t tbo_index, GPUVAddr gpu_addr,
                                               u32 size, PixelFormat format, bool is_written,
                                               bool is_image) {
    channel_state->enabled_texture_buffers[stage] |= 1U << tbo_index;
    channel_state->written_texture_buffers[stage] |= (is_written ? 1U : 0U) << tbo_index;
    if constexpr (SEPARATE_IMAGE_BUFFERS_BINDINGS) {
        channel_state->image_texture_buffers[stage] |= (is_image ? 1U : 0U) << tbo_index;
    }
    channel_state->texture_buffers[stage][tbo_index] =
        GetTextureBufferBinding(gpu_addr, size, format);
}

template <class P>
void BufferCache<P>::UnbindComputeStorageBuffers() {
    channel_state->enabled_compute_storage_buffers = 0;
    channel_state->written_compute_storage_buffers = 0;
    channel_state->image_compute_texture_buffers = 0;
}

template <class P>
void BufferCache<P>::BindComputeStorageBuffer(size_t ssbo_index, u32 cbuf_index, u32 cbuf_offset,
                                              bool is_written) {
    if (ssbo_index >= channel_state->compute_storage_buffers.size()) [[unlikely]] {
        LOG_ERROR(HW_GPU, "Storage buffer index {} exceeds maximum storage buffer count",
                  ssbo_index);
        return;
    }
    channel_state->enabled_compute_storage_buffers |= 1U << ssbo_index;
    channel_state->written_compute_storage_buffers |= (is_written ? 1U : 0U) << ssbo_index;

    const auto& launch_desc = kepler_compute->launch_description;
    assert(((launch_desc.const_buffer_enable_mask >> cbuf_index) & 1) != 0);

    const auto& cbufs = launch_desc.const_buffer_config;
    const GPUVAddr ssbo_addr = cbufs[cbuf_index].Address() + cbuf_offset;
    channel_state->compute_storage_buffers[ssbo_index] =
        StorageBufferBinding(ssbo_addr, cbuf_index, is_written);
}

template <class P>
void BufferCache<P>::UnbindComputeTextureBuffers() {
    channel_state->enabled_compute_texture_buffers = 0;
    channel_state->written_compute_texture_buffers = 0;
    channel_state->image_compute_texture_buffers = 0;
}

template <class P>
void BufferCache<P>::BindComputeTextureBuffer(size_t tbo_index, GPUVAddr gpu_addr, u32 size,
                                              PixelFormat format, bool is_written, bool is_image) {
    if (tbo_index >= channel_state->compute_texture_buffers.size()) [[unlikely]] {
        LOG_ERROR(HW_GPU, "Texture buffer index {} exceeds maximum texture buffer count",
                  tbo_index);
        return;
    }
    channel_state->enabled_compute_texture_buffers |= 1U << tbo_index;
    channel_state->written_compute_texture_buffers |= (is_written ? 1U : 0U) << tbo_index;
    if constexpr (SEPARATE_IMAGE_BUFFERS_BINDINGS) {
        channel_state->image_compute_texture_buffers |= (is_image ? 1U : 0U) << tbo_index;
    }
    channel_state->compute_texture_buffers[tbo_index] =
        GetTextureBufferBinding(gpu_addr, size, format);
}

template <class P>
void BufferCache<P>::FlushCachedWrites() {
    memory_tracker.FlushCachedWrites();
}

template <class P>
bool BufferCache<P>::HasUncommittedFlushes() const noexcept {
    return !uncommitted_gpu_modified_ranges.Empty() || !committed_gpu_modified_ranges.empty();
}

template <class P>
void BufferCache<P>::AccumulateFlushes() {
    if (uncommitted_gpu_modified_ranges.Empty()) {
        return;
    }
    committed_gpu_modified_ranges.emplace_back(std::move(uncommitted_gpu_modified_ranges));
}

template <class P>
bool BufferCache<P>::ShouldWaitAsyncFlushes() const noexcept {
    return (!async_buffers.empty() && async_buffers.front().has_value());
}

template <class P>
void BufferCache<P>::CommitAsyncFlushesHigh() {
    // Reject the complete candidate batch before ResolveScatter/AccumulateFlushes or dirty
    // bookkeeping changes. There is no silent skip or waiting with a cache lock held.
    if (!controlled_registrations.empty()) {
        uncommitted_gpu_modified_ranges.ForEach([&](DAddr start, DAddr end) {
            CheckControlledSynchronization(start, end - start);
        });
        for (const auto& ranges : committed_gpu_modified_ranges) {
            ranges.ForEach([&](DAddr start, DAddr end) {
                CheckControlledSynchronization(start, end - start);
            });
        }
    }
    // Without reactive flushing, CPU reads of GPU-written memory are not trapped, so everything
    // has to be downloaded ahead of time.
    const bool download_all = !Settings::values.use_reactive_flushing.GetValue();
    ResolveScatter(0, 0);
    AccumulateFlushes();

    if (committed_gpu_modified_ranges.empty()) {
        async_buffers.emplace_back(std::optional<Async_Buffer>{});
        return;
    }

    auto it = committed_gpu_modified_ranges.begin();
    while (it != committed_gpu_modified_ranges.end()) {
        auto& current_intervals = *it;
        auto next_it = std::next(it);
        while (next_it != committed_gpu_modified_ranges.end()) {
            next_it->ForEach([&current_intervals](DAddr start, DAddr end) {
                current_intervals.Subtract(start, end - start);
            });
            next_it++;
        }
        it++;
    }

    boost::container::small_vector<std::pair<BufferCopy, BufferId>, 16> downloads;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    Common::RangeSet<DAddr> shared_downloads;
    for (const Common::RangeSet<DAddr>& range_set : committed_gpu_modified_ranges) {
        range_set.ForEach([&](DAddr interval_lower, DAddr interval_upper) {
            const std::size_t size = interval_upper - interval_lower;
            const DAddr device_addr = interval_lower;
            ForEachBufferInRange(device_addr, size, [&](BufferId buffer_id, Buffer& buffer) {
                const DAddr buffer_start = buffer.CpuAddr();
                const DAddr buffer_end = buffer_start + buffer.SizeBytes();
                const DAddr new_start = std::max(buffer_start, device_addr);
                const DAddr new_end = std::min(buffer_end, device_addr + size);
                memory_tracker.ForEachDownloadRange(
                    new_start, new_end - new_start, false,
                    [&](u64 device_addr_out, u64 range_size) {
                        const DAddr buffer_addr = buffer.CpuAddr();
                        const auto add_download = [&](DAddr start, DAddr end) {
                            if (IsDirectGuest(buffer)) {
                                shared_downloads.Add(start, end - start);
                                return;
                            }
                            const u64 new_offset = start - buffer_addr;
                            const u64 new_size = end - start;
                            downloads.push_back({
                                BufferCopy{
                                    .src_offset = new_offset,
                                    .dst_offset = total_size_bytes,
                                    .size = new_size,
                                },
                                buffer_id,
                            });
                            // Align up to avoid cache conflicts
                            constexpr u64 align = 64ULL;
                            constexpr u64 mask = ~(align - 1ULL);
                            total_size_bytes += (new_size + align - 1) & mask;
                            largest_copy = std::max(largest_copy, new_size);
                        };

                        if (download_all) {
                            gpu_modified_ranges.ForEachInRange(device_addr_out, range_size,
                                                               add_download);
                            return;
                        }
                        // Only memory the CPU has read before (preflushable); the rest is
                        // flushed synchronously on its first CPU read (GetFlushArea).
                        const auto add_preflushable = [&](DAddr start, DAddr end) {
                            // Small GPU-written ranges (e.g. exposure / luminance results) are
                            // cheap: keep downloading them every time, so CPU reads that bypass
                            // the first-read flush still see current data. Only large ranges are
                            // limited to what the CPU has read before.
                            constexpr u64 SMALL_DOWNLOAD_LIMIT = 256_KiB;
                            if (end - start <= SMALL_DOWNLOAD_LIMIT) {
                                add_download(start, end);
                                return;
                            }
                            DAddr run_start = 0;
                            for (DAddr page = Common::AlignDown(start, DEVICE_PAGESIZE); page < end;
                                 page += DEVICE_PAGESIZE) {
                                const DAddr lo = std::max(page, start);
                                const DAddr hi = std::min<DAddr>(page + DEVICE_PAGESIZE, end);
                                if (memory_tracker.IsRegionPreflushable(lo, hi - lo)) {
                                    if (run_start == 0) {
                                        run_start = lo;
                                    }
                                    continue;
                                }
                                if (run_start != 0) {
                                    add_download(run_start, lo);
                                    run_start = 0;
                                }
                            }
                            if (run_start != 0) {
                                add_download(run_start, end);
                            }
                        };
                        gpu_modified_ranges.ForEachInRange(device_addr_out, range_size,
                                                           add_preflushable);
                    });
            });
        });
    }
    committed_gpu_modified_ranges.clear();
    if (!shared_downloads.Empty()) {
        GuestMemoryBarrier(true);
        runtime.Finish();
        shared_downloads.ForEach([&](DAddr start, DAddr end) {
            ForEachBufferInRange(start, end - start, [&](BufferId, Buffer& buffer) {
                if (IsDirectGuest(buffer)) { BeginGuestCpuAccess(buffer); }
            });
            memory_tracker.ForEachDownloadRangeAndClear(start, end - start, [&](DAddr addr, u64 size) {
                ClearDownload(addr, size);
                gpu_modified_ranges.Subtract(addr, size);
            });
        });
    }
    if (downloads.empty()) {
        async_buffers.emplace_back(std::optional<Async_Buffer>{});
        return;
    }
    auto download_staging = runtime.DownloadStagingBuffer(total_size_bytes, true);
    boost::container::small_vector<BufferCopy, 4> normalized_copies;
    runtime.PreCopyBarrier();
    for (auto& [copy, buffer_id] : downloads) {
        copy.dst_offset += download_staging.offset;
        const std::array copies{copy};
        BufferCopy second_copy{copy};
        Buffer& buffer = slot_buffers[buffer_id];
        second_copy.src_offset = static_cast<size_t>(buffer.CpuAddr()) + copy.src_offset;
        const DAddr orig_device_addr = static_cast<DAddr>(second_copy.src_offset);
        async_downloads.Add(orig_device_addr, copy.size);
        MarkGpuUse(buffer, copy.src_offset, copy.size);
        runtime.CopyBuffer(download_staging.buffer, buffer, copies, false);
        normalized_copies.push_back(second_copy);
    }
    runtime.PostCopyBarrier();
    pending_downloads.emplace_back(std::move(normalized_copies));
    async_buffers.emplace_back(download_staging);
}

template <class P>
void BufferCache<P>::CommitAsyncFlushes() {
    CommitAsyncFlushesHigh();
}

template <class P>
void BufferCache<P>::PopAsyncFlushes() {
    PopAsyncBuffers();
}

template <class P>
void BufferCache<P>::PopAsyncBuffers() {
    if (async_buffers.empty()) {
        return;
    }
    if (!async_buffers.front().has_value()) {
        async_buffers.pop_front();
        return;
    }
    auto& downloads = pending_downloads.front();
    // Validate every destination before any copyback or queue/range retirement. Otherwise
    // a later conflicting copy could reject after an earlier one has already written.
    for (const auto& copy : downloads)
        CheckControlledSynchronization(static_cast<DAddr>(copy.src_offset), copy.size);
    auto& async_buffer = async_buffers.front();
    u8* base = async_buffer->mapped_span.data();
    const size_t base_offset = async_buffer->offset;
    for (const auto& copy : downloads) {
        const DAddr device_addr = static_cast<DAddr>(copy.src_offset);
        const u64 dst_offset = copy.dst_offset - base_offset;
        const u8* read_mapped_memory = base + dst_offset;
        async_downloads.ForEachInRange(device_addr, copy.size, [&](DAddr start, DAddr end, s32) {
            WriteBackDownload(start, &read_mapped_memory[start - device_addr], end - start);
        });
        async_downloads.Subtract(device_addr, copy.size, [&](DAddr start, DAddr end) {
            gpu_modified_ranges.Subtract(start, end - start);
        });
    }
    async_buffers_death_ring.emplace_back(*async_buffer);
    async_buffers.pop_front();
    pending_downloads.pop_front();
}

template <class P>
bool BufferCache<P>::IsRegionGpuModified(DAddr addr, size_t size) {
    bool is_dirty = false;
    gpu_modified_ranges.ForEachInRange(addr, size, [&](DAddr, DAddr) { is_dirty = true; });
    return is_dirty;
}

template <class P>
bool BufferCache<P>::IsRegionRegistered(DAddr addr, size_t size) {
    if (!IsRegionPageTableAddressable(addr, size)) {
        return false;
    }
    const DAddr end_addr = addr + size;
    const u64 page_end = Common::DivCeil(end_addr, CACHING_PAGESIZE);
    for (u64 page = addr >> CACHING_PAGEBITS; page < page_end;) {
        const BufferId buffer_id = GetPageBufferId(page);
        if (!buffer_id) {
            ++page;
            continue;
        }
        Buffer& buffer = slot_buffers[buffer_id];
        const DAddr buf_start_addr = buffer.CpuAddr();
        const DAddr buf_end_addr = buf_start_addr + buffer.SizeBytes();
        if (buf_start_addr < end_addr && addr < buf_end_addr) {
            return true;
        }
        page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
    }
    return false;
}

template <class P>
bool BufferCache<P>::IsRegionCpuModified(DAddr addr, size_t size) {
    return memory_tracker.IsRegionCpuModified(addr, size);
}

template <class P>
void BufferCache<P>::ResolveCachedBinding(Binding& binding) {
    ResolveScatter(binding.device_addr, binding.size);
    // A scatter may grow a buffer and clear this cached id through DeleteBuffer.
    if (binding.buffer_id == NULL_BUFFER_ID && binding.device_addr && binding.size) {
        binding.buffer_id = FindBuffer(binding.device_addr, binding.size);
    }
}

template <class P>
void BufferCache<P>::BindHostIndexBuffer() {
    ResolveCachedBinding(channel_state->index_buffer);
    Buffer& buffer = slot_buffers[channel_state->index_buffer.buffer_id];
    TouchBuffer(buffer, channel_state->index_buffer.buffer_id);
    const u32 offset = buffer.Offset(channel_state->index_buffer.device_addr);
    const u32 size = channel_state->index_buffer.size;
    const auto& draw_state = maxwell3d->draw_manager->GetDrawState();
    if (!draw_state.inline_index_draw_indexes.empty()) [[unlikely]] {
        if constexpr (USE_MEMORY_MAPS_FOR_UPLOADS) {
            auto upload_staging = runtime.UploadStagingBuffer(size);
            std::array<BufferCopy, 1> copies{
                {BufferCopy{.src_offset = upload_staging.offset, .dst_offset = 0, .size = size}}};
            std::memcpy(upload_staging.mapped_span.data(),
                        draw_state.inline_index_draw_indexes.data(), size);
            runtime.CopyBuffer(buffer, upload_staging.buffer, copies, true);
            Uma::StampPages(buffer.page_gpu_tick, 0, size, runtime.CurrentTick());
        } else {
            buffer.ImmediateUpload(0, draw_state.inline_index_draw_indexes);
        }
    } else {
        SynchronizeBuffer(buffer, channel_state->index_buffer.device_addr, size);
    }
    if constexpr (HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT) {
        const u32 new_offset =
            offset + draw_state.index_buffer.first * draw_state.index_buffer.FormatSizeInBytes();
        runtime.BindIndexBuffer(buffer, new_offset, size);
    } else {
        MarkGpuUse(buffer, offset, size);
        runtime.BindIndexBuffer(draw_state.topology, draw_state.index_buffer.format,
                                draw_state.index_buffer.first, draw_state.index_buffer.count,
                                buffer, offset, size);
    }
}

template <class P>
void BufferCache<P>::BindHostVertexBuffers() {
    HostBindings<typename P::Buffer> host_bindings;
    bool any_valid{false};
    auto& flags = maxwell3d->dirty.flags;
    // Most of the vertex streams are off in any one draw, and those have nothing to upload
    ForEachEnabledBit(channel_state->enabled_vertex_buffers, [&](u32 index) {
        Binding& binding = channel_state->vertex_buffers[index];
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        TouchBuffer(buffer, binding.buffer_id);
        SynchronizeBuffer(buffer, binding.device_addr, binding.size);
    });
    for (u32 index = 0; index < NUM_VERTEX_BUFFERS; ++index) {
        if (!flags[Dirty::VertexBuffer0 + index]) {
            continue;
        }
        flags[Dirty::VertexBuffer0 + index] = false;

        host_bindings.min_index = std::min(host_bindings.min_index, index);
        host_bindings.max_index = std::max(host_bindings.max_index, index);
        any_valid = true;
    }

    if (any_valid) {
        host_bindings.max_index++;
        for (u32 index = host_bindings.min_index; index < host_bindings.max_index; index++) {
            flags[Dirty::VertexBuffer0 + index] = false;

            Binding& binding = channel_state->vertex_buffers[index];
            ResolveCachedBinding(binding);
            Buffer& buffer = slot_buffers[binding.buffer_id];

            const u32 stride = maxwell3d->regs.vertex_streams[index].stride;
            const u32 offset = buffer.Offset(binding.device_addr);
            MarkGpuUse(buffer, offset, binding.size);

            host_bindings.buffers.push_back(&buffer);
            host_bindings.offsets.push_back(offset);
            host_bindings.sizes.push_back(binding.size);
            host_bindings.strides.push_back(stride);
        }
        runtime.BindVertexBuffers(host_bindings);
    }

    if (!runtime.NeedsPaddedVertexBuffers()) {
        return;
    }
    const auto& draw_state = maxwell3d->draw_manager->GetDrawState();
    if (draw_state.draw_indexed) {
        return;
    }
    const u64 num_vertices = u64{draw_state.vertex_buffer.first} + draw_state.vertex_buffer.count;
    const auto& regs = maxwell3d->regs;
    for (u32 index = 0; index < NUM_VERTEX_BUFFERS; ++index) {
        Binding& binding = channel_state->vertex_buffers[index];
        const u32 stride = regs.vertex_streams[index].stride;
        if (binding.buffer_id == NULL_BUFFER_ID || stride == 0 ||
            !regs.vertex_streams[index].enable ||
            regs.vertex_stream_instances.IsInstancingEnabled(index)) {
            continue;
        }
        const u64 needed = num_vertices * stride;
        if (needed <= binding.size || needed > MAX_PADDED_VERTEX_BUFFER_SIZE ||
            memory_tracker.IsRegionGpuModified(binding.device_addr, binding.size)) {
            continue;
        }
        const u32 padded_size = static_cast<u32>(needed);
        const std::span<u8> span = runtime.BindMappedVertexBuffer(index, padded_size, stride);
        device_memory.ReadBlockUnsafe(binding.device_addr, span.data(), binding.size,
                                      "BufferCache.BindPaddedVertexBuffer", false);
        std::memset(span.data() + binding.size, 0, padded_size - binding.size);
        flags[Dirty::VertexBuffer0 + index] = true;
    }
}

template <class P>
void BufferCache<P>::BindHostDrawIndirectBuffers() {
    const auto bind_buffer = [this](Binding& binding) {
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        TouchBuffer(buffer, binding.buffer_id);
        SynchronizeBuffer(buffer, binding.device_addr, binding.size);
    };
    if (current_draw_indirect->include_count) {
        bind_buffer(channel_state->count_buffer_binding);
    }
    bind_buffer(channel_state->indirect_buffer_binding);
}

template <class P>
void BufferCache<P>::BindHostGraphicsUniformBuffers(size_t stage) {
    u32 dirty = ~0U;
    if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
        dirty = std::exchange(channel_state->dirty_uniform_buffers[stage], 0);
    }
    u32 binding_index = 0;
    ForEachEnabledBit(channel_state->enabled_uniform_buffer_masks[stage], [&](u32 index) {
        const bool needs_bind = ((dirty >> index) & 1) != 0;
        BindHostGraphicsUniformBuffer(stage, index, binding_index, needs_bind);
        if constexpr (NEEDS_BIND_UNIFORM_INDEX) {
            ++binding_index;
        }
    });
}

template <class P>
void BufferCache<P>::BindHostGraphicsUniformBuffer(size_t stage, u32 index, u32 binding_index,
                                                   bool needs_bind) {
    Binding& binding = channel_state->uniform_buffers[stage][index];
    const DAddr device_addr = binding.device_addr;
    const u32 shader_size = (*channel_state->uniform_buffer_sizes)[stage][index];
    const u32 size = std::min(binding.size, shader_size);
    const GPUVAddr ubo_gpu_addr = binding.gather_gpu_addr;
    if (binding.gather_size != 0 && size != 0) {
        Buffer& scratch = GatherBuffer(ubo_gpu_addr, binding.gather_size, false);
        const auto bind_uniform = [&](Buffer& source, u32 offset, u32 bound_size) {
            if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
                channel_state->uniform_buffer_binding_sizes[stage][binding_index] = bound_size;
            }
            if constexpr (NEEDS_BIND_UNIFORM_INDEX) {
                runtime.BindUniformBuffer(stage, binding_index, source, offset, bound_size);
            } else {
                runtime.BindUniformBuffer(source, offset, bound_size);
            }
        };
        if (shader_size > size) {
            // Keep the zero-padding contract even when later chunks contain GPU writes.
            Buffer padded{runtime, 0, Common::AlignUp<u64>(shader_size, 4)};
            runtime.ClearBuffer(padded, 0, padded.SizeBytes(), 0);
            const std::array copies{BufferCopy{
                .src_offset = binding.gather_offset,
                .dst_offset = 0,
                .size = size,
            }};
            runtime.CopyBuffer(padded, scratch, copies, true);
            bind_uniform(padded, 0, shader_size);
            delayed_destruction_ring.Push(std::move(padded));
        } else {
            bind_uniform(scratch, binding.gather_offset, size);
        }
        return;
    }
    ResolveCachedBinding(binding);
    Buffer& buffer = slot_buffers[binding.buffer_id];
    TouchBuffer(buffer, binding.buffer_id);
    if (binding.buffer_id != NULL_BUFFER_ID && shader_size > binding.size &&
        runtime.NeedsPaddedUniformBuffer(binding.size) &&
        !memory_tracker.IsRegionGpuModified(device_addr, size)) {
        const std::span<u8> span =
            runtime.BindMappedUniformBuffer(stage, binding_index, shader_size);
        device_memory.ReadBlockUnsafe(device_addr, span.data(), size,
                                      "BufferCache.BindPaddedUniformBuffer", false);
        std::memset(span.data() + size, 0, shader_size - size);
        return;
    }
    // The stream path also takes a uniform buffer the guest rewrote while draws earlier in this
    // frame still use the cached copy. Uploading it would take a copy in submission order, and a
    // copy cannot run inside a render pass: the pass would end before the draw and start again
    // after it, which on a tiled GPU stores and reloads every attachment. Galaxy's gameplay did
    // this on every draw. The cached copy stays marked as modified for its other users.
    const bool use_fast_buffer =
        binding.buffer_id != NULL_BUFFER_ID &&
        !memory_tracker.IsRegionGpuModified(device_addr, size) &&
        (size <= channel_state->uniform_buffer_skip_cache_size ||
         UploadNeedsOrderedCopy(buffer, device_addr, size));
    if (use_fast_buffer) {
        if constexpr (IS_OPENGL) {
            if (runtime.HasFastBufferSubData()) {
                // Fast path for Nvidia
                const bool should_fast_bind =
                    !HasFastUniformBufferBound(stage, binding_index) ||
                    channel_state->uniform_buffer_binding_sizes[stage][binding_index] != size;
                if (should_fast_bind) {
                    // We only have to bind when the currently bound buffer is not the fast version
                    channel_state->fast_bound_uniform_buffers[stage] |= 1U << binding_index;
                    channel_state->uniform_buffer_binding_sizes[stage][binding_index] = size;
                    runtime.BindFastUniformBuffer(stage, binding_index, size);
                }
                const auto span = ImmediateBufferWithData(device_addr, size);
                runtime.PushFastUniformBuffer(stage, binding_index, span);
                return;
            }
        }
        if constexpr (IS_OPENGL) {
            channel_state->fast_bound_uniform_buffers[stage] |= 1U << binding_index;
            channel_state->uniform_buffer_binding_sizes[stage][binding_index] = size;
        }
        // Stream buffer path to avoid stalling on non-Nvidia drivers or Vulkan
        const std::span<u8> span = runtime.BindMappedUniformBuffer(stage, binding_index, size);
        const auto read_result = device_memory.ReadBlockUnsafe(
            device_addr, span.data(), size, "BufferCache.BindMappedUniformBuffer", false);
        if (!read_result.fully_mapped) {
            static std::atomic<u64> mapping_hole_count{0};
            const u64 count = mapping_hole_count.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= 8 || (count % 256) == 0) {
                LOG_WARNING(HW_Memory,
                            "BufferCache mapped uniform contains unmapped pages: stage={} "
                            "index={} binding_index={} buffer_id={} d_address=0x{:016X} size={} "
                            "first_unmapped=0x{:016X} unmapped_bytes={} sample_count={}",
                            stage, index, binding_index, binding.buffer_id.index, device_addr, size,
                            read_result.first_unmapped_address, read_result.unmapped_bytes, count);
            }
        }
        return;
    }
    // Classic cached path
    const bool sync_cached = SynchronizeBuffer(buffer, device_addr, size);
    if (sync_cached) {
        ++channel_state->uniform_cache_hits[0];
    }
    ++channel_state->uniform_cache_shots[0];

    // Skip binding if it's not needed and if the bound buffer is not the fast version
    // This exists to avoid instances where the fast buffer is bound and a GPU write happens
    needs_bind |= HasFastUniformBufferBound(stage, binding_index);
    if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
        needs_bind |= channel_state->uniform_buffer_binding_sizes[stage][binding_index] != size;
    }
    if (!needs_bind) {
        return;
    }
    const u32 offset = buffer.Offset(device_addr);
    if constexpr (IS_OPENGL) {
        // Fast buffer will be unbound
        channel_state->fast_bound_uniform_buffers[stage] &= ~(1U << binding_index);

        // Mark the index as dirty if offset doesn't match
        const bool is_copy_bind = offset != 0 && !runtime.SupportsNonZeroUniformOffset();
        channel_state->dirty_uniform_buffers[stage] |= (is_copy_bind ? 1U : 0U) << index;
    }
    if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
        channel_state->uniform_buffer_binding_sizes[stage][binding_index] = size;
    }
    MarkGpuUse(buffer, offset, size);
    if constexpr (NEEDS_BIND_UNIFORM_INDEX) {
        runtime.BindUniformBuffer(stage, binding_index, buffer, offset, size);
    } else {
        runtime.BindUniformBuffer(buffer, offset, size);
    }
}

template <class P>
void BufferCache<P>::BindHostGraphicsStorageBuffers(size_t stage) {
    u32 binding_index = 0;
    ForEachEnabledBit(channel_state->enabled_storage_buffers[stage], [&](u32 index) {
        Binding& binding = channel_state->storage_buffers[stage][index];
        if (binding.gather_gpu_addr != 0) {
            const bool written =
                ((channel_state->written_storage_buffers[stage] >> index) & 1) != 0;
            Buffer& scratch = GatherBuffer(binding.gather_gpu_addr, binding.gather_size, written);
            if (written && std::ranges::find(pending_gather_writes, binding.gather_gpu_addr) ==
                               pending_gather_writes.end()) {
                pending_gather_writes.push_back(binding.gather_gpu_addr);
            }
            if constexpr (NEEDS_BIND_STORAGE_INDEX) {
                runtime.BindStorageBuffer(stage, binding_index, scratch, binding.gather_offset,
                                          binding.size, written);
                ++binding_index;
            } else {
                runtime.BindStorageBuffer(scratch, binding.gather_offset, binding.size, written);
            }
            return;
        }
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        TouchBuffer(buffer, binding.buffer_id);
        const u32 size = binding.size;
        SynchronizeBuffer(buffer, binding.device_addr, size);

        const u32 offset = buffer.Offset(binding.device_addr);
        MarkGpuUse(buffer, offset, size);
        const bool is_written = ((channel_state->written_storage_buffers[stage] >> index) & 1) != 0;

        if (is_written) {
            MarkWrittenBuffer(binding.buffer_id, binding.device_addr, size);
        }

        if constexpr (NEEDS_BIND_STORAGE_INDEX) {
            runtime.BindStorageBuffer(stage, binding_index, buffer, offset, size, is_written);
            ++binding_index;
        } else {
            runtime.BindStorageBuffer(buffer, offset, size, is_written);
        }
    });
}

template <class P>
void BufferCache<P>::BindHostGraphicsTextureBuffers(size_t stage) {
    ForEachEnabledBit(channel_state->enabled_texture_buffers[stage], [&](u32 index) {
        TextureBufferBinding& binding = channel_state->texture_buffers[stage][index];
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        const u32 size = binding.size;
        SynchronizeBuffer(buffer, binding.device_addr, size);

        const bool is_written = ((channel_state->written_texture_buffers[stage] >> index) & 1) != 0;
        if (is_written) {
            MarkWrittenBuffer(binding.buffer_id, binding.device_addr, size);
        }

        const u32 offset = buffer.Offset(binding.device_addr);
        const PixelFormat format = binding.format;
        MarkGpuUse(buffer, offset, size);
        if constexpr (SEPARATE_IMAGE_BUFFERS_BINDINGS) {
            if (((channel_state->image_texture_buffers[stage] >> index) & 1) != 0) {
                runtime.BindImageBuffer(buffer, offset, size, format);
            } else {
                runtime.BindTextureBuffer(buffer, offset, size, format);
            }
        } else {
            runtime.BindTextureBuffer(buffer, offset, size, format);
        }
    });
}

template <class P>
void BufferCache<P>::BindHostTransformFeedbackBuffers() {
    if (maxwell3d->regs.transform_feedback_enabled == 0) {
        return;
    }
    HostBindings<typename P::Buffer> host_bindings;
    for (u32 index = 0; index < NUM_TRANSFORM_FEEDBACK_BUFFERS; ++index) {
        Binding& binding = channel_state->transform_feedback_buffers[index];
        if (maxwell3d->regs.transform_feedback.controls[index].varying_count == 0 &&
            maxwell3d->regs.transform_feedback.controls[index].stride == 0) {
            break;
        }
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        TouchBuffer(buffer, binding.buffer_id);
        const u32 size = binding.size;
        SynchronizeBuffer(buffer, binding.device_addr, size);

        MarkWrittenBuffer(binding.buffer_id, binding.device_addr, size);

        const u32 offset = buffer.Offset(binding.device_addr);
        MarkGpuUse(buffer, offset, size);
        host_bindings.buffers.push_back(&buffer);
        host_bindings.offsets.push_back(offset);
        host_bindings.sizes.push_back(size);
    }
    if (host_bindings.buffers.size() > 0) {
        runtime.BindTransformFeedbackBuffers(host_bindings);
    }
}

template <class P>
void BufferCache<P>::BindHostComputeUniformBuffers() {
    if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
        // Mark all uniform buffers as dirty
        channel_state->dirty_uniform_buffers.fill(~u32{0});
        channel_state->fast_bound_uniform_buffers.fill(0);
    }
    u32 binding_index = 0;
    ForEachEnabledBit(channel_state->enabled_compute_uniform_buffer_mask, [&](u32 index) {
        Binding& binding = channel_state->compute_uniform_buffers[index];
        const u32 size =
            std::min(binding.size, (*channel_state->compute_uniform_buffer_sizes)[index]);
        if (binding.gather_size != 0 && size != 0) {
            Buffer& scratch = GatherBuffer(binding.gather_gpu_addr, binding.gather_size, false);
            if constexpr (NEEDS_BIND_UNIFORM_INDEX) {
                runtime.BindComputeUniformBuffer(binding_index, scratch, binding.gather_offset,
                                                 size);
                ++binding_index;
            } else {
                runtime.BindUniformBuffer(scratch, binding.gather_offset, size);
            }
            return;
        }
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        TouchBuffer(buffer, binding.buffer_id);
        SynchronizeBuffer(buffer, binding.device_addr, size);

        const u32 offset = buffer.Offset(binding.device_addr);
        MarkGpuUse(buffer, offset, size);
        if constexpr (NEEDS_BIND_UNIFORM_INDEX) {
            runtime.BindComputeUniformBuffer(binding_index, buffer, offset, size);
            ++binding_index;
        } else {
            runtime.BindUniformBuffer(buffer, offset, size);
        }
    });
}

template <class P>
void BufferCache<P>::BindHostComputeStorageBuffers() {
    u32 binding_index = 0;
    ForEachEnabledBit(channel_state->enabled_compute_storage_buffers, [&](u32 index) {
        Binding& binding = channel_state->compute_storage_buffers[index];
        if (binding.gather_gpu_addr != 0) {
            const bool written =
                ((channel_state->written_compute_storage_buffers >> index) & 1) != 0;
            Buffer& scratch = GatherBuffer(binding.gather_gpu_addr, binding.gather_size, written);
            if (written && std::ranges::find(pending_gather_writes, binding.gather_gpu_addr) ==
                               pending_gather_writes.end()) {
                pending_gather_writes.push_back(binding.gather_gpu_addr);
            }
            if constexpr (NEEDS_BIND_STORAGE_INDEX) {
                runtime.BindComputeStorageBuffer(binding_index, scratch, binding.gather_offset,
                                                 binding.size, written);
                ++binding_index;
            } else {
                runtime.BindStorageBuffer(scratch, binding.gather_offset, binding.size, written);
            }
            return;
        }
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        TouchBuffer(buffer, binding.buffer_id);
        const u32 size = binding.size;
        SynchronizeBuffer(buffer, binding.device_addr, size);

        const u32 offset = buffer.Offset(binding.device_addr);
        MarkGpuUse(buffer, offset, size);
        const bool is_written =
            ((channel_state->written_compute_storage_buffers >> index) & 1) != 0;

        if (is_written) {
            MarkWrittenBuffer(binding.buffer_id, binding.device_addr, size);
        }

        if constexpr (NEEDS_BIND_STORAGE_INDEX) {
            runtime.BindComputeStorageBuffer(binding_index, buffer, offset, size, is_written);
            ++binding_index;
        } else {
            runtime.BindStorageBuffer(buffer, offset, size, is_written);
        }
    });
}

template <class P>
void BufferCache<P>::BindHostComputeTextureBuffers() {
    ForEachEnabledBit(channel_state->enabled_compute_texture_buffers, [&](u32 index) {
        TextureBufferBinding& binding = channel_state->compute_texture_buffers[index];
        ResolveCachedBinding(binding);
        Buffer& buffer = slot_buffers[binding.buffer_id];
        const u32 size = binding.size;
        SynchronizeBuffer(buffer, binding.device_addr, size);

        const bool is_written =
            ((channel_state->written_compute_texture_buffers >> index) & 1) != 0;
        if (is_written) {
            MarkWrittenBuffer(binding.buffer_id, binding.device_addr, size);
        }

        const u32 offset = buffer.Offset(binding.device_addr);
        const PixelFormat format = binding.format;
        MarkGpuUse(buffer, offset, size);
        if constexpr (SEPARATE_IMAGE_BUFFERS_BINDINGS) {
            if (((channel_state->image_compute_texture_buffers >> index) & 1) != 0) {
                runtime.BindImageBuffer(buffer, offset, size, format);
            } else {
                runtime.BindTextureBuffer(buffer, offset, size, format);
            }
        } else {
            runtime.BindTextureBuffer(buffer, offset, size, format);
        }
    });
}

template <class P>
void BufferCache<P>::DoUpdateGraphicsBuffers(bool is_indexed) {
    BufferOperations([&]() {
        ResetGatherBindings(false);
        if (is_indexed) {
            UpdateIndexBuffer();
        }
        UpdateVertexBuffers();
        UpdateTransformFeedbackBuffers();
        for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
            UpdateUniformBuffers(stage);
            UpdateStorageBuffers(stage);
            UpdateTextureBuffers(stage);
        }
        if (current_draw_indirect) {
            UpdateDrawIndirect();
        }
        PrepareGatherBindings(false);
    });
}

template <class P>
void BufferCache<P>::DoUpdateComputeBuffers() {
    BufferOperations([&]() {
        ResetGatherBindings(true);
        UpdateComputeUniformBuffers();
        UpdateComputeStorageBuffers();
        UpdateComputeTextureBuffers();
        PrepareGatherBindings(true);
    });
}

template <class P>
void BufferCache<P>::UpdateIndexBuffer() {
    // We have to check for the dirty flags and index count
    // The index count is currently changed without updating the dirty flags
    const auto& draw_state = maxwell3d->draw_manager->GetDrawState();
    const auto& index_buffer_ref = draw_state.index_buffer;
    auto& flags = maxwell3d->dirty.flags;
    if (!flags[Dirty::IndexBuffer]) {
        return;
    }
    flags[Dirty::IndexBuffer] = false;
    if (!draw_state.inline_index_draw_indexes.empty()) [[unlikely]] {
        auto inline_index_size = static_cast<u32>(draw_state.inline_index_draw_indexes.size());
        u32 buffer_size = Common::AlignUp(inline_index_size, CACHING_PAGESIZE);
        if (inline_buffer_id == NULL_BUFFER_ID) [[unlikely]] {
            inline_buffer_id = CreateBuffer(0, buffer_size);
        }
        if (slot_buffers[inline_buffer_id].SizeBytes() < buffer_size) [[unlikely]] {
            slot_buffers.erase(inline_buffer_id);
            inline_buffer_id = CreateBuffer(0, buffer_size);
        }
        channel_state->index_buffer = Binding{
            .device_addr = 0,
            .size = inline_index_size,
            .buffer_id = inline_buffer_id,
        };
        return;
    }

    const GPUVAddr gpu_addr_begin = index_buffer_ref.StartAddress();
    const GPUVAddr gpu_addr_end = index_buffer_ref.EndAddress();
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr_begin);
    const u32 address_size = static_cast<u32>(gpu_addr_end - gpu_addr_begin);
    const u32 draw_size =
        (index_buffer_ref.count + index_buffer_ref.first) * index_buffer_ref.FormatSizeInBytes();
    const u32 size = std::min(address_size, draw_size);
    if (size == 0 || !device_addr) {
        channel_state->index_buffer = NULL_BINDING;
        return;
    }
    channel_state->index_buffer = Binding{
        .device_addr = *device_addr,
        .size = size,
        .buffer_id = FindBuffer(*device_addr, size),
    };
}

template <class P>
void BufferCache<P>::UpdateVertexBuffers() {
    auto& flags = maxwell3d->dirty.flags;
    if (!maxwell3d->dirty.flags[Dirty::VertexBuffers]) {
        return;
    }
    flags[Dirty::VertexBuffers] = false;

    for (u32 index = 0; index < NUM_VERTEX_BUFFERS; ++index) {
        UpdateVertexBuffer(index);
    }
}

template <class P>
void BufferCache<P>::UpdateVertexBuffer(u32 index) {
    if (!maxwell3d->dirty.flags[Dirty::VertexBuffer0 + index]) {
        return;
    }
    const auto& array = maxwell3d->regs.vertex_streams[index];
    const auto& limit = maxwell3d->regs.vertex_stream_limits[index];
    const GPUVAddr gpu_addr_begin = array.Address();
    const GPUVAddr gpu_addr_end = limit.Address() + 1;
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr_begin);
    const u32 address_size = static_cast<u32>(gpu_addr_end - gpu_addr_begin);
    u32 size = address_size; // TODO: Analyze stride and number of vertices
    if (array.enable == 0 || size == 0 || !device_addr) {
        channel_state->vertex_buffers[index] = NULL_BINDING;
        channel_state->enabled_vertex_buffers &= ~(1U << index);
        return;
    }
    if (!gpu_memory->IsWithinGPUAddressRange(gpu_addr_end) || size >= 64_MiB) {
        size = static_cast<u32>(gpu_memory->MaxContinuousRange(gpu_addr_begin, size));
    }
    const BufferId buffer_id = FindBuffer(*device_addr, size);
    channel_state->vertex_buffers[index] = Binding{
        .device_addr = *device_addr,
        .size = size,
        .buffer_id = buffer_id,
    };
    if (buffer_id != NULL_BUFFER_ID) {
        channel_state->enabled_vertex_buffers |= 1U << index;
    } else {
        channel_state->enabled_vertex_buffers &= ~(1U << index);
    }
}

template <class P>
void BufferCache<P>::UpdateDrawIndirect() {
    const auto update = [this](GPUVAddr gpu_addr, size_t size, Binding& binding) {
        const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr);
        if (!device_addr) {
            binding = NULL_BINDING;
            return;
        }
        binding = Binding{
            .device_addr = *device_addr,
            .size = static_cast<u32>(size),
            .buffer_id = FindBuffer(*device_addr, static_cast<u32>(size)),
        };
    };
    if (current_draw_indirect->include_count) {
        update(current_draw_indirect->count_start_address, sizeof(u32),
               channel_state->count_buffer_binding);
    }
    update(current_draw_indirect->indirect_start_address, current_draw_indirect->buffer_size,
           channel_state->indirect_buffer_binding);
}

template <class P>
void BufferCache<P>::UpdateUniformBuffers(size_t stage) {
    ForEachEnabledBit(channel_state->enabled_uniform_buffer_masks[stage], [&](u32 index) {
        Binding& binding = channel_state->uniform_buffers[stage][index];
        if (binding.buffer_id) {
            // Already updated
            return;
        }
        // Mark as dirty
        if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
            channel_state->dirty_uniform_buffers[stage] |= 1U << index;
        }
        // Resolve buffer
        if (binding.gather_gpu_addr != 0 && binding.size != 0 &&
            !IsGpuRangeContiguous(binding.gather_gpu_addr, binding.size)) {
            ResolveGatherChunks(binding.gather_gpu_addr, binding.size);
            binding.buffer_id = NULL_BUFFER_ID;
        } else {
            binding.buffer_id = FindBuffer(binding.device_addr, binding.size);
        }
    });
}

template <class P>
void BufferCache<P>::UpdateStorageBuffers(size_t stage) {
    ForEachEnabledBit(channel_state->enabled_storage_buffers[stage], [&](u32 index) {
        // Resolve buffer
        Binding& binding = channel_state->storage_buffers[stage][index];
        if (binding.gather_gpu_addr != 0) {
            ResolveGatherChunks(binding.gather_gpu_addr, binding.size);
            binding.buffer_id = NULL_BUFFER_ID;
            return;
        }
        const BufferId buffer_id = FindBuffer(binding.device_addr, binding.size);
        binding.buffer_id = buffer_id;
    });
}

template <class P>
void BufferCache<P>::UpdateTextureBuffers(size_t stage) {
    ForEachEnabledBit(channel_state->enabled_texture_buffers[stage], [&](u32 index) {
        Binding& binding = channel_state->texture_buffers[stage][index];
        binding.buffer_id = FindBuffer(binding.device_addr, binding.size);
    });
}

template <class P>
void BufferCache<P>::UpdateTransformFeedbackBuffers() {
    if (maxwell3d->regs.transform_feedback_enabled == 0) {
        return;
    }
    for (u32 index = 0; index < NUM_TRANSFORM_FEEDBACK_BUFFERS; ++index) {
        UpdateTransformFeedbackBuffer(index);
    }
}

template <class P>
void BufferCache<P>::UpdateTransformFeedbackBuffer(u32 index) {
    const auto& binding = maxwell3d->regs.transform_feedback.buffers[index];
    const GPUVAddr gpu_addr = binding.Address() + binding.start_offset;
    const u32 size = binding.size;
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr);
    if (binding.enable == 0 || size == 0 || !device_addr) {
        channel_state->transform_feedback_buffers[index] = NULL_BINDING;
        return;
    }
    const BufferId buffer_id = FindBuffer(*device_addr, size);
    channel_state->transform_feedback_buffers[index] = Binding{
        .device_addr = *device_addr,
        .size = size,
        .buffer_id = buffer_id,
    };
}

template <class P>
void BufferCache<P>::UpdateComputeUniformBuffers() {
    ForEachEnabledBit(channel_state->enabled_compute_uniform_buffer_mask, [&](u32 index) {
        Binding& binding = channel_state->compute_uniform_buffers[index];
        binding = NULL_BINDING;
        const auto& launch_desc = kepler_compute->launch_description;
        if (((launch_desc.const_buffer_enable_mask >> index) & 1) != 0) {
            const auto& cbuf = launch_desc.const_buffer_config[index];
            const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(cbuf.Address());
            if (device_addr) {
                binding.device_addr = *device_addr;
                binding.size = cbuf.size;
                binding.gpu_addr = cbuf.Address();
                binding.gather_gpu_addr =
                    IsGpuRangeContiguous(binding.gpu_addr, binding.size) ? 0 : binding.gpu_addr;
            }
        }
        if (binding.gather_gpu_addr != 0 && binding.size != 0 &&
            !IsGpuRangeContiguous(binding.gather_gpu_addr, binding.size)) {
            ResolveGatherChunks(binding.gather_gpu_addr, binding.size);
            binding.buffer_id = NULL_BUFFER_ID;
        } else {
            binding.buffer_id = FindBuffer(binding.device_addr, binding.size);
        }
    });
}

template <class P>
void BufferCache<P>::UpdateComputeStorageBuffers() {
    ForEachEnabledBit(channel_state->enabled_compute_storage_buffers, [&](u32 index) {
        // Resolve buffer
        Binding& binding = channel_state->compute_storage_buffers[index];
        if (binding.gather_gpu_addr != 0) {
            ResolveGatherChunks(binding.gather_gpu_addr, binding.size);
            binding.buffer_id = NULL_BUFFER_ID;
            return;
        }
        binding.buffer_id = FindBuffer(binding.device_addr, binding.size);
    });
}

template <class P>
void BufferCache<P>::UpdateComputeTextureBuffers() {
    ForEachEnabledBit(channel_state->enabled_compute_texture_buffers, [&](u32 index) {
        Binding& binding = channel_state->compute_texture_buffers[index];
        binding.buffer_id = FindBuffer(binding.device_addr, binding.size);
    });
}

template <class P>
void BufferCache<P>::MarkWrittenBuffer(BufferId buffer_id, DAddr device_addr, u32 size) {
    slot_buffers[buffer_id].gpu_write_revision.Mark();
    NotifyGatherWrite(device_addr, size, 2); // the write happens after this bind
    memory_tracker.MarkRegionAsGpuModified(device_addr, size);
    gpu_modified_ranges.Add(device_addr, size);
    uncommitted_gpu_modified_ranges.Add(device_addr, size);
}

template <class P>
BufferId BufferCache<P>::FindBuffer(DAddr device_addr, u32 size) {
    if (device_addr == 0 || !IsRegionPageTableAddressable(device_addr, size)) {
        return NULL_BUFFER_ID;
    }
    ResolveScatter(device_addr, size);
    const u64 page = device_addr >> CACHING_PAGEBITS;
    const BufferId buffer_id = GetPageBufferId(page);
    if (!buffer_id) {
        return CreateBuffer(device_addr, size);
    }
    const Buffer& buffer = slot_buffers[buffer_id];
    if (buffer.IsInBounds(device_addr, size)) {
        return buffer_id;
    }
    return CreateBuffer(device_addr, size);
}

template <class P>
typename BufferCache<P>::OverlapResult BufferCache<P>::ResolveOverlaps(DAddr device_addr,
                                                                       u32 wanted_size) {
    static constexpr int STREAM_LEAP_THRESHOLD = 16;
    boost::container::small_vector<BufferId, 16> overlap_ids;
    DAddr begin = device_addr;
    DAddr end = device_addr + wanted_size;
    int stream_score = 0;
    bool has_stream_leap = false;
    auto expand_begin = [&](DAddr add_value) {
        static constexpr DAddr min_page = CACHING_PAGESIZE + Core::DEVICE_PAGESIZE;
        if (add_value > begin - min_page) {
            begin = min_page;
            device_addr = Core::DEVICE_PAGESIZE;
            return;
        }
        begin -= add_value;
        device_addr = begin - CACHING_PAGESIZE;
    };
    auto expand_end = [&](DAddr add_value) {
        static constexpr DAddr max_page = 1ULL << Tegra::MaxwellDeviceMemoryManager::AS_BITS;
        if (add_value > max_page - end) {
            end = max_page;
            return;
        }
        end += add_value;
    };
    if (begin == 0) {
        return OverlapResult{
            .ids = std::move(overlap_ids),
            .begin = begin,
            .end = end,
            .has_stream_leap = has_stream_leap,
        };
    }
    for (; device_addr >> CACHING_PAGEBITS < Common::DivCeil(end, CACHING_PAGESIZE);
         device_addr += CACHING_PAGESIZE) {
        const u64 page = device_addr >> CACHING_PAGEBITS;
        const BufferId overlap_id = GetPageBufferId(page);
        if (!overlap_id) {
            continue;
        }
        Buffer& overlap = slot_buffers[overlap_id];
        if (overlap.IsPicked()) {
            continue;
        }
        overlap_ids.push_back(overlap_id);
        overlap.Pick();
        const DAddr overlap_device_addr = overlap.CpuAddr();
        const bool expands_left = overlap_device_addr < begin;
        if (expands_left) {
            begin = overlap_device_addr;
        }
        const DAddr overlap_end = overlap_device_addr + overlap.SizeBytes();
        const bool expands_right = overlap_end > end;
        if (overlap_end > end) {
            end = overlap_end;
        }
        stream_score += overlap.StreamScore();
        if (stream_score > STREAM_LEAP_THRESHOLD && !has_stream_leap) {
            // When this memory region has been joined a bunch of times, we assume it's being used
            // as a stream buffer. Increase the size to skip constantly recreating buffers.
            has_stream_leap = true;
            if (expands_right) {
                expand_begin(CACHING_PAGESIZE * 128);
            }
            if (expands_left) {
                expand_end(CACHING_PAGESIZE * 128);
            }
        }
    }
    return OverlapResult{
        .ids = std::move(overlap_ids),
        .begin = begin,
        .end = end,
        .has_stream_leap = has_stream_leap,
    };
}

template <class P>
bool BufferCache<P>::CanJoinDirectGuest(BufferId overlap_id) {
    Buffer& overlap = slot_buffers[overlap_id];
    if (IsDirectGuest(overlap)) { return true; }
    bool aligned = true;
    gpu_modified_ranges.ForEachInRange(overlap.CpuAddr(), overlap.SizeBytes(),
        [&](DAddr start, DAddr end) { aligned &= ((start | end) & 3) == 0; });
    // vkCmdCopyBuffer cannot preserve sub-word clean CPU bytes by expanding a
    // dirty range. Keep the staged cache/upload path in this case instead.
    return aligned;
}

template <class P>
void BufferCache<P>::JoinOverlap(BufferId new_buffer_id, BufferId overlap_id,
                                 bool accumulate_stream_score) {
    Buffer& new_buffer = slot_buffers[new_buffer_id];
    Buffer& overlap = slot_buffers[overlap_id];
    if constexpr (requires { runtime.InvalidatePhysicalOrigin(new_buffer); }) {
        // This bounded observer does not yet prove representation inheritance.
        runtime.InvalidatePhysicalOrigin(new_buffer);
    }
    if (accumulate_stream_score) {
        new_buffer.IncreaseStreamScore(overlap.StreamScore() + 1);
    }
    boost::container::small_vector<BufferCopy, 10> copies;
    const size_t dst_base_offset = overlap.CpuAddr() - new_buffer.CpuAddr();
    if (IsDirectGuest(new_buffer) && !IsDirectGuest(overlap)) {
        gpu_modified_ranges.ForEachInRange(overlap.CpuAddr(), overlap.SizeBytes(),
            [&](DAddr start, DAddr end) {
                copies.push_back(BufferCopy{
                    .src_offset = start - overlap.CpuAddr(),
                    .dst_offset = dst_base_offset + start - overlap.CpuAddr(),
                    .size = end - start,
                });
            });
    } else {
        copies.push_back(BufferCopy{
            .src_offset = 0,
            .dst_offset = dst_base_offset,
            .size = overlap.SizeBytes(),
        });
    }
    for (const auto& copy : copies) {
        MarkGpuUse(new_buffer, copy.dst_offset, copy.size);
        MarkGpuUse(overlap, copy.src_offset, copy.size);
    }
    // Two imports of the same payload alias even when their VkBuffer handles differ.
    // Copying an overlapping range would violate vkCmdCopyBuffer's aliasing rule.
    if (!copies.empty() && !(IsDirectGuest(new_buffer) && IsDirectGuest(overlap))) {
        runtime.CopyBuffer(new_buffer, overlap, copies, true);
    }
    DeleteBuffer(overlap_id, true);
}

template <class P>
BufferId BufferCache<P>::CreateBuffer(DAddr device_addr, u32 wanted_size) {
    if (!IsRegionPageTableAddressable(device_addr, wanted_size)) {
        return NULL_BUFFER_ID;
    }
    DAddr device_addr_end = Common::AlignUp(device_addr + wanted_size, CACHING_PAGESIZE);
    device_addr = Common::AlignDown(device_addr, CACHING_PAGESIZE);
    wanted_size = static_cast<u32>(device_addr_end - device_addr);
    const OverlapResult overlap = ResolveOverlaps(device_addr, wanted_size);
    const u32 size = static_cast<u32>(overlap.end - overlap.begin);
    const BufferId new_buffer_id = slot_buffers.insert(runtime, overlap.begin, size);
    auto& new_buffer = slot_buffers[new_buffer_id];
    const size_t size_bytes = new_buffer.SizeBytes();
    const bool direct_join = std::ranges::all_of(overlap.ids, [&](BufferId id) {
        return CanJoinDirectGuest(id);
    });
    if (!direct_join || !TryDirectGuest(new_buffer, overlap.begin, size_bytes)) {
        runtime.ClearBuffer(new_buffer, 0, size_bytes, 0);
    }
    MarkGpuUse(new_buffer, 0, size_bytes);
    for (const BufferId overlap_id : overlap.ids) {
        JoinOverlap(new_buffer_id, overlap_id, !overlap.has_stream_leap);
    }
    Register(new_buffer_id);
    TouchBuffer(new_buffer, new_buffer_id);
    return new_buffer_id;
}

template <class P>
void BufferCache<P>::Register(BufferId buffer_id) {
    ChangeRegister<true>(buffer_id);
}

template <class P>
void BufferCache<P>::Unregister(BufferId buffer_id) {
    if constexpr (requires(Buffer& buffer) { buffer.RevokeContentEligibility(); }) {
        slot_buffers[buffer_id].RevokeContentEligibility();
    }
    ChangeRegister<false>(buffer_id);
}

template <class P>
template <bool insert>
void BufferCache<P>::ChangeRegister(BufferId buffer_id) {
    Buffer& buffer = slot_buffers[buffer_id];
    const auto size = buffer.SizeBytes();
    const u64 aligned_size = Common::AlignUp(size, 1024);
    const bool is_large = aligned_size >= LARGE_BUFFER_THRESHOLD;

    if (insert) {
        total_used_memory += aligned_size;
        buffer.setLRUID(lru_cache.Insert(buffer_id, frame_tick));

        // FIXED: VRAM leak prevention - Track buffer statistics
        ++buffer_count;
        if (is_large) {
            large_buffer_memory += aligned_size;
            ++large_buffer_count;
        }
    } else {
        assert(total_used_memory >= aligned_size);
        total_used_memory -= std::min(total_used_memory, aligned_size);
        lru_cache.Free(buffer.getLRUID());

        // FIXED: VRAM leak prevention - Update buffer statistics on removal
        if (buffer_count > 0) {
            --buffer_count;
        }
        if (is_large && large_buffer_count > 0) {
            assert(large_buffer_memory >= aligned_size);
            large_buffer_memory -= std::min(large_buffer_memory, aligned_size);
            --large_buffer_count;
        }
    }
    const DAddr device_addr_begin = buffer.CpuAddr();
    if (!IsRegionPageTableAddressable(device_addr_begin, size)) {
        return;
    }
    const DAddr device_addr_end = device_addr_begin + size;
    const u64 page_begin = device_addr_begin / CACHING_PAGESIZE;
    const u64 page_end = Common::DivCeil(device_addr_end, CACHING_PAGESIZE);
    for (u64 page = page_begin; page != page_end; ++page) {
        if constexpr (insert) {
            SetPageBufferId(page, buffer_id);
        } else {
            SetPageBufferId(page, BufferId{});
        }
    }
}

template <class P>
void BufferCache<P>::TouchBuffer(Buffer& buffer, BufferId buffer_id) noexcept {
    if (buffer_id != NULL_BUFFER_ID) {
        lru_cache.Touch(buffer.getLRUID(), frame_tick);
    }
}

template <class P>
void BufferCache<P>::MarkGpuUse(Buffer& buffer, u64 offset, u64 size) {
    EndGuestCpuAccess(buffer);
    const u64 tick = runtime.CurrentTick();
    buffer.MarkUsage(offset, size, tick);
    Uma::StampPages(buffer.page_gpu_tick, offset, size, tick);
}

template <class P>
bool BufferCache<P>::ControlledReadbacksIdle(const ControlledRegistrationPtr& operation) {
    if (!operation) return false;
    bool idle = true;
    const bool qualified = device_memory.WithControlledMapping(
        operation->plan.DeviceEpoch(), operation->plan.DeviceGeneration(),
        [&](const auto& aliases_for) {
            std::array<DAddr, 64> aliases{};
            for (const auto& segment : operation->plan.Segments()) {
                size_t count{};
                if (!segment.storage.OwnerAlive() ||
                    !aliases_for(segment.storage.PhysicalOffset(), aliases, count)) {
                    idle = false;
                    return;
                }
                for (size_t i = 0; i < count; ++i) {
                    const auto address = aliases[i];
                    const auto conflict = [&](DAddr start, DAddr end) {
                        if (start <= address ? address - start < end - start
                                             : start - address < DEVICE_PAGESIZE)
                            idle = false;
                    };
                    uncommitted_gpu_modified_ranges.ForEachInRange(address, DEVICE_PAGESIZE, conflict);
                    for (const auto& ranges : committed_gpu_modified_ranges)
                        ranges.ForEachInRange(address, DEVICE_PAGESIZE, conflict);
                    // pending_downloads also retains copies invalidated in async_downloads;
                    // conservatively require their queued native/callback identity to retire.
                    for (const auto& batch : pending_downloads) {
                        for (const auto& copy : batch) {
                            const DAddr start = static_cast<DAddr>(copy.src_offset);
                            if (start <= address ? address - start < copy.size
                                                 : start - address < DEVICE_PAGESIZE)
                                idle = false;
                        }
                    }
                }
            }
        });
    return qualified && idle;
}

template <class P>
void BufferCache<P>::CheckControlledSynchronization(DAddr device_addr, u64 size) {
    if (!size) return;
    // This owner-thread continuation must run before consuming the real tracker. A failure
    // cannot be represented by the legacy bool (whose false also means successful upload).
    for (const auto& operation : controlled_registrations) {
        if (operation->phase != ControlledRegistration::Phase::Writing &&
            operation->phase != ControlledRegistration::Phase::Unknown)
            continue;
        bool overlaps = false;
        const bool qualified = device_memory.WithCurrentControlledAliases(
            [&](const auto& backing, const auto& aliases_for) {
                std::array<DAddr, 64> aliases{};
                for (const auto& segment : operation->plan.Segments()) {
                    if (segment.storage.Epoch() != backing) continue;
                    size_t count{};
                    if (!aliases_for(segment.storage.PhysicalOffset(), aliases, count)) {
                        overlaps = true; // Unknown aliases cannot qualify an upload.
                        return;
                    }
                    for (size_t i = 0; i < count; ++i)
                        if (device_addr < aliases[i] + DEVICE_PAGESIZE &&
                            aliases[i] < device_addr + size)
                            overlaps = true;
                }
            });
        if (!qualified || overlaps)
            throw ControlledProducerConflict("controlled CPU write excludes legacy buffer access");
    }
}

template <class P>
bool BufferCache<P>::SynchronizeBuffer(Buffer& buffer, DAddr device_addr, u32 size) {
    CheckControlledSynchronization(device_addr, size);
    boost::container::small_vector<BufferCopy, 4> copies;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    DAddr buffer_start = buffer.CpuAddr();
    const bool shared = IsDirectGuest(buffer);
    if (shared) { EndGuestCpuAccess(buffer); }
    memory_tracker.ForEachUploadRange(device_addr, size, [&](u64 device_addr_out, u64 range_size) {
        copies.push_back(BufferCopy{
            .src_offset = total_size_bytes,
            .dst_offset = device_addr_out - buffer_start,
            .size = range_size,
        });
        total_size_bytes += range_size;
        largest_copy = std::max(largest_copy, range_size);
    });
    if (total_size_bytes == 0) {
        return true;
    }
    if (shared) {
        for (const auto& copy : copies) {
            NotifyGatherWrite(buffer_start + copy.dst_offset, copy.size);
        }
        GuestMemoryBarrier(false);
        return false;
    }
    const std::span<BufferCopy> copies_span(copies.data(), copies.size());
    UploadMemory(buffer, total_size_bytes, largest_copy, copies_span);
    return false;
}

template <class P>
bool BufferCache<P>::UploadNeedsOrderedCopy([[maybe_unused]] Buffer& buffer,
                                            [[maybe_unused]] DAddr device_addr,
                                            [[maybe_unused]] u32 size) {
    if constexpr (USE_MEMORY_MAPS_FOR_UPLOADS) {
        // Outside a render pass an ordered copy costs nothing extra, and it brings the cached
        // copy up to date.
        if (size == 0 || !runtime.IsRenderPassActive() ||
            !memory_tracker.IsRegionCpuModified(device_addr, size)) {
            return false;
        }
        // The same tests MappedUploadMemory makes, over the whole range: the uploaded ranges are
        // inside it, so this can only answer yes more often than the upload would need.
        const u64 offset = buffer.Offset(device_addr);
        const std::span<u8> mapped = buffer.Mapped();
        if (offset <= mapped.size() && size <= mapped.size() - offset &&
            Uma::CanWriteDirectly(!mapped.empty(), buffer.page_gpu_tick, offset, size,
                                  runtime.KnownGpuTick())) {
            return false;
        }
        const std::array copies{BufferCopy{.src_offset = 0, .dst_offset = offset, .size = size}};
        return !runtime.CanReorderUpload(buffer, copies);
    } else {
        return false;
    }
}

template <class P>
void BufferCache<P>::UploadMemory(Buffer& buffer, u64 total_size_bytes, u64 largest_copy,
                                  std::span<BufferCopy> copies) {
    for (const BufferCopy& copy : copies) {
        NotifyGatherWrite(buffer.CpuAddr() + copy.dst_offset, copy.size);
    }
    if constexpr (USE_MEMORY_MAPS_FOR_UPLOADS) {
        MappedUploadMemory(buffer, total_size_bytes, copies);
    } else {
        ImmediateUploadMemory(buffer, largest_copy, copies);
    }
}

template <class P>
void BufferCache<P>::ImmediateUploadMemory([[maybe_unused]] Buffer& buffer,
                                           [[maybe_unused]] u64 largest_copy,
                                           [[maybe_unused]] std::span<const BufferCopy> copies) {
    if constexpr (!USE_MEMORY_MAPS_FOR_UPLOADS) {
        std::span<u8> immediate_buffer;
        for (const BufferCopy& copy : copies) {
            std::span<const u8> upload_span;
            const DAddr device_addr = buffer.CpuAddr() + copy.dst_offset;
            if (IsRangeGranular(device_addr, copy.size)) {
                auto* const ptr = device_memory.GetPointer<u8>(device_addr);
                if (ptr != nullptr) {
                    upload_span = std::span(ptr, copy.size);
                }
            } else {
                if (immediate_buffer.empty()) {
                    immediate_buffer = ImmediateBuffer(largest_copy);
                }
                device_memory.ReadBlockUnsafe(device_addr, immediate_buffer.data(), copy.size,
                                              "BufferCache.UploadMemory.immediate");
                upload_span = immediate_buffer.subspan(0, copy.size);
            }
            buffer.ImmediateUpload(copy.dst_offset, upload_span);
        }
    }
}

template <class P>
void BufferCache<P>::MappedUploadMemory([[maybe_unused]] Buffer& buffer,
                                        [[maybe_unused]] u64 total_size_bytes,
                                        [[maybe_unused]] std::span<BufferCopy> copies) {
    if constexpr (USE_MEMORY_MAPS) {
        // Unified memory: when no GPU work can still use any page the copies touch, guest memory
        // is read straight into the buffer. Otherwise it is staged and copied on the GPU, as it
        // was before unified memory.
        const std::span<u8> mapped = buffer.Mapped();
        const u64 known_gpu_tick = runtime.KnownGpuTick();
        const bool direct = std::ranges::all_of(copies, [&](const BufferCopy& copy) {
            return copy.dst_offset <= mapped.size() &&
                   copy.size <= mapped.size() - copy.dst_offset &&
                   Uma::CanWriteDirectly(!mapped.empty(), buffer.page_gpu_tick, copy.dst_offset,
                                         copy.size, known_gpu_tick);
        });
        Async_Buffer upload_staging{};
        std::span<u8> destination = mapped;
        if (!direct) {
            upload_staging = runtime.UploadStagingBuffer(total_size_bytes);
            destination = upload_staging.mapped_span;

            // Never form staging or buffer pointers until every copy has been bounds checked. A
            // short staging allocation can occur when a request exceeds a driver's allocation
            // limits.
            const bool staging_too_small = destination.size() < total_size_bytes;
            const auto invalid_copy =
                std::find_if(copies.begin(), copies.end(), [&](const auto& copy) {
                    const bool staging_out_of_bounds =
                        copy.src_offset > destination.size() ||
                        copy.size > destination.size() - copy.src_offset;
                    const bool buffer_out_of_bounds =
                        copy.dst_offset > buffer.SizeBytes() ||
                        copy.size > buffer.SizeBytes() - copy.dst_offset;
                    return staging_out_of_bounds || buffer_out_of_bounds;
                });
            if (staging_too_small || invalid_copy != copies.end()) {
                static std::atomic<u64> invalid_upload_count{0};
                const u64 count = invalid_upload_count.fetch_add(1, std::memory_order_relaxed) + 1;
                if (count <= 8 || (count % 256) == 0) {
                    if (invalid_copy != copies.end()) {
                        LOG_ERROR(HW_Memory,
                                  "BufferCache rejected out-of-bounds mapped upload: buffer_base="
                                  "0x{:016X} buffer_size={} staging_size={} total_size={} "
                                  "src_offset={} dst_offset={} copy_size={} sample_count={}",
                                  buffer.CpuAddr(), buffer.SizeBytes(), destination.size(),
                                  total_size_bytes, invalid_copy->src_offset,
                                  invalid_copy->dst_offset, invalid_copy->size, count);
                    } else {
                        LOG_ERROR(HW_Memory,
                                  "BufferCache rejected short mapped upload staging allocation: "
                                  "buffer_base=0x{:016X} buffer_size={} staging_size={} "
                                  "total_size={} sample_count={}",
                                  buffer.CpuAddr(), buffer.SizeBytes(), destination.size(),
                                  total_size_bytes, count);
                    }
                }
                return;
            }
        }

        for (BufferCopy& copy : copies) {
            u8* const dst_pointer =
                destination.data() + (direct ? copy.dst_offset : copy.src_offset);
            const DAddr device_addr = buffer.CpuAddr() + copy.dst_offset;
            const auto read_result = device_memory.ReadBlockUnsafe(
                device_addr, dst_pointer, copy.size, "BufferCache.UploadMemory", false);
            if (!read_result.fully_mapped) {
                static std::atomic<u64> mapping_hole_count{0};
                const u64 count = mapping_hole_count.fetch_add(1, std::memory_order_relaxed) + 1;
                if (count <= 8 || (count % 256) == 0) {
                    LOG_WARNING(
                        HW_Memory,
                        "BufferCache mapped upload contains unmapped pages (zero-filled): "
                        "buffer_base=0x{:016X} buffer_size={} d_address=0x{:016X} direct={} "
                        "src_offset={} dst_offset={} copy_size={} total_size={} "
                        "first_unmapped=0x{:016X} unmapped_bytes={} sample_count={}",
                        buffer.CpuAddr(), buffer.SizeBytes(), device_addr, direct, copy.src_offset,
                        copy.dst_offset, copy.size, total_size_bytes,
                        read_result.first_unmapped_address, read_result.unmapped_bytes, count);
                }
            }
            if (!direct) {
                copy.src_offset += upload_staging.offset; // Apply the staging offset
            }
        }
        if (direct) {
            return;
        }
        const bool can_reorder = runtime.CanReorderUpload(buffer, copies);
        runtime.CopyBuffer(buffer, upload_staging.buffer, copies, true, can_reorder);
        // The GPU copy writes these ranges: a later direct write must wait for it.
        const u64 tick = runtime.CurrentTick();
        for (const BufferCopy& copy : copies) {
            Uma::StampPages(buffer.page_gpu_tick, copy.dst_offset, copy.size, tick);
        }
    }
}

template <class P>
bool BufferCache<P>::InlineMemory(DAddr dest_address, size_t copy_size,
                                  std::span<const u8> inlined_buffer) {
    const bool is_dirty = IsRegionRegistered(dest_address, copy_size);
    if (!is_dirty) {
        return false;
    }
    DAddr aligned_start = Common::AlignDown(dest_address, DEVICE_PAGESIZE);
    DAddr aligned_end = Common::AlignUp(dest_address + copy_size, DEVICE_PAGESIZE);
    if (!IsRegionGpuModified(aligned_start, aligned_end - aligned_start)) {
        return false;
    }

    InlineMemoryImplementation(dest_address, copy_size, inlined_buffer);

    return true;
}

template <class P>
void BufferCache<P>::InlineMemoryImplementation(DAddr dest_address, size_t copy_size,
                                                std::span<const u8> inlined_buffer) {
    NotifyGatherWrite(dest_address, copy_size);
    ClearDownload(dest_address, copy_size);
    gpu_modified_ranges.Subtract(dest_address, copy_size);

    BufferId buffer_id = FindBuffer(dest_address, static_cast<u32>(copy_size));
    auto& buffer = slot_buffers[buffer_id];
    SynchronizeBuffer(buffer, dest_address, static_cast<u32>(copy_size));

    if constexpr (USE_MEMORY_MAPS_FOR_UPLOADS) {
        auto upload_staging = runtime.UploadStagingBuffer(copy_size);
        std::array copies{BufferCopy{
            .src_offset = upload_staging.offset,
            .dst_offset = buffer.Offset(dest_address),
            .size = copy_size,
        }};
        u8* const src_pointer = upload_staging.mapped_span.data();
        std::memcpy(src_pointer, inlined_buffer.data(), copy_size);
        const bool can_reorder = runtime.CanReorderUpload(buffer, copies);
        runtime.CopyBuffer(buffer, upload_staging.buffer, copies, true, can_reorder);
        Uma::StampPages(buffer.page_gpu_tick, buffer.Offset(dest_address), copy_size,
                        runtime.CurrentTick());
    } else {
        buffer.ImmediateUpload(buffer.Offset(dest_address), inlined_buffer.first(copy_size));
    }
}

template <class P>
void BufferCache<P>::DownloadBufferMemory(Buffer& buffer) {
    DownloadBufferMemory(buffer, buffer.CpuAddr(), buffer.SizeBytes());
}

template <class P>
void BufferCache<P>::DownloadBufferMemory(Buffer& buffer, DAddr device_addr, u64 size,
                                         bool preserve_on_unmap) {
    const auto started = preserve_on_unmap ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
    const DAddr unmap_begin = std::max(buffer.CpuAddr(), Common::AlignDown(device_addr, DEVICE_PAGESIZE));
    const DAddr unmap_end = std::min(buffer.CpuAddr() + buffer.SizeBytes(),
                                    Common::AlignUp(device_addr + size, DEVICE_PAGESIZE));
    if (preserve_on_unmap) {
        CheckControlledSynchronization(unmap_begin, unmap_end - unmap_begin);
    } else {
        CheckControlledSynchronization(device_addr, size);
    }
    const auto complete_unmap = [&] {
        if (preserve_on_unmap) {
            ClearDownload(unmap_begin, unmap_end - unmap_begin);
            gpu_modified_ranges.Subtract(unmap_begin, unmap_end - unmap_begin);
            memory_tracker.UnmarkRegionAsGpuModified(unmap_begin, unmap_end - unmap_begin);
        }
    };
    boost::container::small_vector<BufferCopy, 1> copies;
    u64 total_size_bytes = 0;
    u64 largest_copy = 0;
    const auto collect_range = [&](DAddr start, DAddr end) {
        copies.push_back(BufferCopy{.src_offset = start - buffer.CpuAddr(),
                                    .dst_offset = total_size_bytes, .size = end - start});
        total_size_bytes += Common::AlignUp<u64>(end - start, 64);
        largest_copy = std::max(largest_copy, end - start);
    };
    if (preserve_on_unmap) {
        // CPU dirty flags cover whole pages, but completed CPU writes have already
        // subtracted only their actual bytes from gpu_modified_ranges. Copy the
        // remaining GPU bytes even on mixed CPU/GPU pages. Complete each page
        // before clearing its page-granular tracker bit; clip to this allocation.
        gpu_modified_ranges.ForEachInRange(unmap_begin, unmap_end - unmap_begin, collect_range);
    } else {
        memory_tracker.ForEachDownloadRangeAndClear(
        device_addr, size, [&](u64 device_addr_out, u64 range_size) {
            const DAddr buffer_addr = buffer.CpuAddr();
            const auto add_download = [&](DAddr start, DAddr end) {
                const u64 new_offset = start - buffer_addr;
                const u64 new_size = end - start;
                copies.push_back(BufferCopy{
                    .src_offset = new_offset,
                    .dst_offset = total_size_bytes,
                    .size = new_size,
                });
                // Align up to avoid cache conflicts
                constexpr u64 align = 64ULL;
                constexpr u64 mask = ~(align - 1ULL);
                total_size_bytes += (new_size + align - 1) & mask;
                largest_copy = std::max(largest_copy, new_size);
            };

            gpu_modified_ranges.ForEachInRange(device_addr_out, range_size, add_download);
            ClearDownload(device_addr_out, range_size);
            gpu_modified_ranges.Subtract(device_addr_out, range_size);
        });
    }
    if (IsDirectGuest(buffer)) {
        GuestMemoryBarrier(true);
        runtime.Finish();
        BeginGuestCpuAccess(buffer);
        return; // GPU writes already inhabit guest backing; no CPU copy-back.
    }
    if (total_size_bytes == 0) {
        complete_unmap();
        return;
    }
    if constexpr (USE_MEMORY_MAPS) {
        auto download_staging = runtime.DownloadStagingBuffer(total_size_bytes);
        const u8* const mapped_memory = download_staging.mapped_span.data();
        const std::span<BufferCopy> copies_span(copies.data(), copies.data() + copies.size());
        for (BufferCopy& copy : copies) {
            // Modify copies to have the staging offset in mind
            copy.dst_offset += download_staging.offset;
            MarkGpuUse(buffer, copy.src_offset, copy.size);
        }
        runtime.CopyBuffer(download_staging.buffer, buffer, copies_span, true);
        runtime.Finish();
        for (const BufferCopy& copy : copies) {
            const DAddr copy_device_addr = buffer.CpuAddr() + copy.src_offset;
            // Undo the modified offset
            const u64 dst_offset = copy.dst_offset - download_staging.offset;
            const u8* copy_mapped_memory = mapped_memory + dst_offset;
            WriteBackDownload(copy_device_addr, copy_mapped_memory, copy.size);
        }
    } else {
        const std::span<u8> immediate_buffer = ImmediateBuffer(largest_copy);
        for (const BufferCopy& copy : copies) {
            buffer.ImmediateDownload(copy.src_offset, immediate_buffer.subspan(0, copy.size));
            const DAddr copy_device_addr = buffer.CpuAddr() + copy.src_offset;
            WriteBackDownload(copy_device_addr, immediate_buffer.data(), copy.size);
        }
    }
    complete_unmap(); // only after GPU completion and all CPU copies succeed
    if (preserve_on_unmap) {
        const u64 elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        ++unmap_readback_count;
        for (const auto& copy : copies) {
            unmap_readback_bytes += copy.size;
        }
        unmap_readback_time_ns += elapsed;
        if (unmap_readback_count <= 8 || (unmap_readback_count % 64) == 0) {
            LOG_INFO(HW_Memory, "Unmap staged readback: count={} bytes={} time_ns={} last_time_ns={}",
                     unmap_readback_count, unmap_readback_bytes, unmap_readback_time_ns, elapsed);
        }
    }
}

template <class P>
void BufferCache<P>::WriteBackDownload(DAddr device_addr, const u8* data, u64 size) {
    CheckControlledSynchronization(device_addr, size);
    // A buffer can cover device pages no guest memory backs (a storage buffer bound without a
    // size spans its whole GPU mapping, see StorageBufferBinding). The GPU's writes there have
    // nowhere to go; skip them, as the upload zero-fills them, and say so now and then instead
    // of once per page per flush.
    const auto result = device_memory.WriteBlockUnsafe(device_addr, data, size, false);
    if (result.fully_mapped) {
        return;
    }
    const u64 count = ++unbacked_write_back_count;
    if (count <= 8 || (count % 256) == 0) {
        LOG_WARNING(HW_Memory,
                    "BufferCache download skips unmapped pages: d_address=0x{:016X} size={} "
                    "first_unmapped=0x{:016X} unmapped_bytes={} sample_count={}",
                    device_addr, size, result.first_unmapped_address, result.unmapped_bytes,
                    count);
    }
}

template <class P>
void BufferCache<P>::DeleteBuffer(BufferId buffer_id, bool do_not_mark) {
    bool dirty_index{false};
    boost::container::small_vector<u64, NUM_VERTEX_BUFFERS> dirty_vertex_buffers;
    const auto scalar_replace = [buffer_id](Binding& binding) {
        if (binding.buffer_id == buffer_id) {
            binding.buffer_id = BufferId{};
        }
    };
    const auto replace = [scalar_replace](std::span<Binding> bindings) {
        std::ranges::for_each(bindings, scalar_replace);
    };

    if (channel_state->index_buffer.buffer_id == buffer_id) {
        channel_state->index_buffer.buffer_id = BufferId{};
        dirty_index = true;
    }

    for (u32 index = 0; index < channel_state->vertex_buffers.size(); index++) {
        auto& binding = channel_state->vertex_buffers[index];
        if (binding.buffer_id == buffer_id) {
            binding.buffer_id = BufferId{};
            channel_state->enabled_vertex_buffers &= ~(1U << index);
            dirty_vertex_buffers.push_back(index);
        }
    }
    std::ranges::for_each(channel_state->uniform_buffers, replace);
    std::ranges::for_each(channel_state->storage_buffers, replace);
    replace(channel_state->transform_feedback_buffers);
    replace(channel_state->compute_uniform_buffers);
    replace(channel_state->compute_storage_buffers);

    // Mark the whole buffer as CPU written to stop tracking CPU writes
    if (!do_not_mark) {
        Buffer& buffer = slot_buffers[buffer_id];
        memory_tracker.MarkRegionAsCpuModified(buffer.CpuAddr(), buffer.SizeBytes());
    }

    Unregister(buffer_id);
    delayed_destruction_ring.Push(std::move(slot_buffers[buffer_id]));
    slot_buffers.erase(buffer_id);

    if constexpr (HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS) {
        channel_state->dirty_uniform_buffers.fill(~u32{0});
        channel_state->uniform_buffer_binding_sizes.fill({});
    }

    auto& flags = maxwell3d->dirty.flags;
    if (dirty_index) {
        flags[Dirty::IndexBuffer] = true;
    }

    if (dirty_vertex_buffers.size() > 0) {
        flags[Dirty::VertexBuffers] = true;
        for (auto index : dirty_vertex_buffers) {
            flags[Dirty::VertexBuffer0 + index] = true;
        }
    }
    channel_state->has_deleted_buffers = true;
}

template <class P>
boost::container::small_vector<Binding*, 32> BufferCache<P>::GatherBindings(bool compute) {
    boost::container::small_vector<Binding*, 32> bindings;
    if (compute) {
        ForEachEnabledBit(channel_state->enabled_compute_uniform_buffer_mask, [&](u32 index) {
            bindings.push_back(&channel_state->compute_uniform_buffers[index]);
        });
        ForEachEnabledBit(channel_state->enabled_compute_storage_buffers, [&](u32 index) {
            bindings.push_back(&channel_state->compute_storage_buffers[index]);
        });
    } else {
        for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
            ForEachEnabledBit(channel_state->enabled_uniform_buffer_masks[stage], [&](u32 index) {
                bindings.push_back(&channel_state->uniform_buffers[stage][index]);
            });
            ForEachEnabledBit(channel_state->enabled_storage_buffers[stage], [&](u32 index) {
                bindings.push_back(&channel_state->storage_buffers[stage][index]);
            });
        }
    }
    return bindings;
}

template <class P>
void BufferCache<P>::ObservePhysicalBindings(PhysicalShadow& shadow, bool compute) {
    const auto observe = [&](const Binding& binding, ShadowKind kind, bool written) {
        if (!binding.size) {
            return;
        }
        if (binding.gather_gpu_addr) {
            kind = ShadowKind::Scratch;
            const auto it = gather_buffers.find(binding.gather_gpu_addr);
            if (it != gather_buffers.end() && it->second.buffer && IsDirectGuest(*it->second.buffer)) {
                kind = ShadowKind::DirectBuffer;
            }
        } else if (kind == ShadowKind::Buffer && binding.buffer_id &&
                   binding.buffer_id != NULL_BUFFER_ID && IsDirectGuest(slot_buffers[binding.buffer_id])) {
            kind = ShadowKind::DirectBuffer;
        }
        // Cached texel views require their real contiguous DAddr to match. Split
        // uniform/storage views instead describe logical GPU ranges in scratch.
        const std::optional<DAddr> expected = binding.gather_gpu_addr ? std::nullopt
                                                                    : std::optional{binding.device_addr};
        // A writable gathered view scatters its whole prepared allocation, including padding.
        shadow.Observe(*gpu_memory, binding.gather_gpu_addr ? binding.gather_gpu_addr : binding.gpu_addr,
                       binding.gather_gpu_addr ? binding.gather_size : binding.size, kind, written, expected);
    };
    if (compute) {
        ForEachEnabledBit(channel_state->enabled_compute_uniform_buffer_mask, [&](u32 i) {
            observe(channel_state->compute_uniform_buffers[i], ShadowKind::Buffer, false);
        });
        ForEachEnabledBit(channel_state->enabled_compute_storage_buffers, [&](u32 i) {
            observe(channel_state->compute_storage_buffers[i], ShadowKind::Buffer,
                    ((channel_state->written_compute_storage_buffers >> i) & 1) != 0);
        });
        ForEachEnabledBit(channel_state->enabled_compute_texture_buffers, [&](u32 i) {
            observe(channel_state->compute_texture_buffers[i], ShadowKind::Texel,
                    ((channel_state->written_compute_texture_buffers >> i) & 1) != 0);
        });
    } else {
        for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
            ForEachEnabledBit(channel_state->enabled_uniform_buffer_masks[stage], [&](u32 i) {
                observe(channel_state->uniform_buffers[stage][i], ShadowKind::Buffer, false);
            });
            ForEachEnabledBit(channel_state->enabled_storage_buffers[stage], [&](u32 i) {
                observe(channel_state->storage_buffers[stage][i], ShadowKind::Buffer,
                        ((channel_state->written_storage_buffers[stage] >> i) & 1) != 0);
            });
            ForEachEnabledBit(channel_state->enabled_texture_buffers[stage], [&](u32 i) {
                observe(channel_state->texture_buffers[stage][i], ShadowKind::Texel,
                        ((channel_state->written_texture_buffers[stage] >> i) & 1) != 0);
            });
        }
        if (maxwell3d->regs.transform_feedback_enabled) {
            for (const auto& binding : channel_state->transform_feedback_buffers) {
                // Current TF bindings don't retain a GPU VA. Explicit unresolved
                // write intent, rather than inventing a physical resource identity.
                observe(binding, ShadowKind::Buffer, true);
            }
        }
    }
}

template <class P>
void BufferCache<P>::ResetGatherBindings(bool compute) {
    for (Binding* binding : GatherBindings(compute)) {
        binding->gather_gpu_addr = binding->gpu_addr != 0 && binding->size != 0 &&
                                           !IsGpuRangeContiguous(binding->gpu_addr, binding->size)
                                       ? binding->gpu_addr
                                       : 0;
        binding->gather_size = 0;
        binding->gather_offset = 0;
    }
}

template <class P>
void BufferCache<P>::ValidateGatherAliases(std::span<const GatherAliasRange> ranges) {
    const auto mapping_read = gpu_memory->AcquireMappingRead();
    device_memory.ReadPhysicalAliases([&](u64 generation, const auto& physical_address) {
        const GuestMappingIdentity identity{gpu_memory, gpu_memory->GetID(), gpu_memory->MapGeneration()};
        if (!identity.Stable() || (generation & 1)) {
            throw UnsupportedGatherMapping("Page table is changing during split alias validation");
        }
        if (checked_alias_identity == identity && checked_alias_device_generation == generation &&
            std::ranges::equal(checked_alias_ranges, ranges)) {
            return;
        }
        GatherAliasValidator validator{DEVICE_PAGESIZE};
        for (const auto& range : ranges) {
            if (range.gpu_addr > std::numeric_limits<u64>::max() - range.size) {
                throw UnsupportedGatherMapping("Split alias validation range overflows GPU address space");
            }
            for (u64 offset = 0; offset < range.size;) {
                const u64 gpu = range.gpu_addr + offset;
                const auto dev = gpu_memory->GpuToCpuAddress(gpu);
                u64 length = std::min<u64>(range.size - offset,
                    DEVICE_PAGESIZE - gpu % DEVICE_PAGESIZE);
                if (dev) {
                    length = std::min<u64>(length, DEVICE_PAGESIZE - *dev % DEVICE_PAGESIZE);
                    const auto physical = physical_address(*dev);
                    if (physical) {
                        validator.Add(*physical, gpu, length, range.scratch, range.writable);
                    }
                }
                offset += length;
            }
        }
        const GuestMappingIdentity after{gpu_memory, gpu_memory->GetID(), gpu_memory->MapGeneration()};
        if (after != identity || device_memory.MapGeneration() != generation) {
            throw UnsupportedGatherMapping("Page table changed during split alias validation");
        }
        checked_alias_ranges.assign(ranges.begin(), ranges.end());
        checked_alias_identity = identity;
        checked_alias_device_generation = generation;
    });
}

template <class P>
void BufferCache<P>::PrepareGatherBindings(bool compute) {
    struct Range {
        GPUVAddr begin;
        GPUVAddr end;
    };
    boost::container::small_vector<Range, 8> ranges;
    const auto bindings = GatherBindings(compute);
    for (const Binding* binding : bindings) {
        if (binding->gather_gpu_addr != 0 && binding->size != 0) {
            ranges.push_back({Common::AlignDown(binding->gpu_addr, Core::DEVICE_PAGESIZE),
                              Common::AlignUp(binding->gpu_addr + binding->size, 4)});
        }
    }
    // Include overlapping contiguous GPU ranges too: shaders must see one allocation when
    // a storage write overlaps another storage/uniform view in the same command.
    bool changed;
    do {
        changed = false;
        for (Range& range : ranges) {
            for (const Binding* binding : bindings) {
                if (binding->gpu_addr != 0 && binding->size != 0 && binding->gpu_addr < range.end &&
                    range.begin < binding->gpu_addr + binding->size) {
                    const GPUVAddr begin = std::min(
                        range.begin, Common::AlignDown(binding->gpu_addr, Core::DEVICE_PAGESIZE));
                    const GPUVAddr end =
                        std::max(range.end, Common::AlignUp(binding->gpu_addr + binding->size, 4));
                    changed |= begin != range.begin || end != range.end;
                    range = {begin, end};
                }
            }
        }
        std::ranges::sort(ranges, {}, &Range::begin);
        for (size_t i = 1; i < ranges.size();) {
            if (ranges[i].begin < ranges[i - 1].end) {
                ranges[i - 1].end = std::max(ranges[i - 1].end, ranges[i].end);
                ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
            } else {
                ++i;
            }
        }
    } while (changed);
    if (!ranges.empty()) {
        std::vector<const Binding*> written_bindings;
        if (compute) {
            ForEachEnabledBit(channel_state->written_compute_storage_buffers &
                              channel_state->enabled_compute_storage_buffers, [&](u32 index) {
                written_bindings.push_back(&channel_state->compute_storage_buffers[index]);
            });
        } else {
            for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
                ForEachEnabledBit(channel_state->written_storage_buffers[stage] &
                                  channel_state->enabled_storage_buffers[stage], [&](u32 index) {
                    written_bindings.push_back(&channel_state->storage_buffers[stage][index]);
                });
            }
        }
        std::vector<GatherAliasRange> aliases;
        for (const Range& range : ranges) {
            if (range.end - range.begin > std::numeric_limits<u32>::max()) {
                throw UnsupportedGatherMapping("Gathered buffer exceeds the supported buffer size");
            }
            // Whole-chunk scatter writes the prepared range, including padding.
            // Keep that range conservative when any binding can write it.
            if (std::ranges::any_of(written_bindings, [&](const Binding* binding) {
                    return binding->gpu_addr < range.end &&
                           range.begin < binding->gpu_addr + binding->size;
                })) {
                aliases.push_back({range.begin, static_cast<u32>(range.end - range.begin), true, true});
            }
        }
        for (const Binding* binding : bindings) {
            if (binding->gpu_addr && binding->size) {
                const bool scratch = std::ranges::any_of(ranges, [&](const Range& range) {
                    return binding->gpu_addr >= range.begin &&
                           binding->gpu_addr + binding->size <= range.end;
                });
                const bool covered = std::ranges::any_of(aliases, [&](const GatherAliasRange& range) {
                    return range.writable && binding->gpu_addr >= range.gpu_addr &&
                           binding->gpu_addr + binding->size <= range.gpu_addr + range.size;
                });
                if (!covered) {
                    const bool written =
                        std::ranges::find(written_bindings, binding) != written_bindings.end();
                    // Read-only padding is not shader-accessible and is never scattered.
                    aliases.push_back({binding->gpu_addr, binding->size, scratch, written});
                }
            }
        }
        const auto add_texture = [&](const TextureBufferBinding& binding, bool written) {
            if (binding.gpu_addr == 0 || binding.size == 0) {
                return;
            }
            // Texel/image views bind the cached DAddr range, never the shared
            // scratch used by storage/uniform views (even at the same GPU VA).
            // A split texel range cannot be represented by that cached buffer.
            if (!IsGpuRangeContiguous(binding.gpu_addr, binding.size)) {
                throw UnsupportedGatherMapping(
                    "Split texel/image buffer cannot share gathered bindings");
            }
            aliases.push_back({binding.gpu_addr, binding.size, false, written});
        };
        if (compute) {
            ForEachEnabledBit(channel_state->enabled_compute_texture_buffers, [&](u32 index) {
                add_texture(channel_state->compute_texture_buffers[index],
                            ((channel_state->written_compute_texture_buffers >> index) & 1) != 0);
            });
        } else {
            for (size_t stage = 0; stage < NUM_STAGES; ++stage) {
                ForEachEnabledBit(channel_state->enabled_texture_buffers[stage], [&](u32 index) {
                    add_texture(channel_state->texture_buffers[stage][index],
                                ((channel_state->written_texture_buffers[stage] >> index) & 1) != 0);
                });
            }
        }
        ValidateGatherAliases(aliases); // before allocation, binding mutation, or dispatch
    }
    for (const Range& range : ranges) {
        if (range.end - range.begin > std::numeric_limits<u32>::max()) {
            throw std::length_error("Gathered buffer views exceed the supported buffer size");
        }
        const u32 size = static_cast<u32>(range.end - range.begin);
        ResolveGatherChunks(range.begin, size);
        for (Binding* binding : bindings) {
            if (binding->gpu_addr != 0 && binding->size != 0 && binding->gpu_addr >= range.begin &&
                binding->gpu_addr + binding->size <= range.end) {
                binding->gather_gpu_addr = range.begin;
                binding->gather_size = size;
                binding->gather_offset = static_cast<u32>(binding->gpu_addr - range.begin);
                binding->buffer_id = NULL_BUFFER_ID;
            }
        }
    }
}

template <class P>
void BufferCache<P>::InvalidateGatherMemory(DAddr device_addr, u64 size) {
    ResolveScatter(device_addr, size);
    // Removing a GPU mapping does not overwrite its guest bytes. Preserve staged
    // writes before the caller marks this range CPU-modified: another GPU VA may
    // still refer to it, or the device mapping may be about to change.
    ForEachBufferInRange(device_addr, size, [&](BufferId, Buffer& buffer) {
        if (!IsDirectGuest(buffer)) {
            const DAddr begin = std::max(device_addr, buffer.CpuAddr());
            const DAddr end = std::min(device_addr + size, buffer.CpuAddr() + buffer.SizeBytes());
            DownloadBufferMemory(buffer, begin, end - begin, true);
        }
    });
    WaitGuestBacking(device_addr, size);
    std::vector<BufferId> direct_buffers;
    ForEachBufferInRange(device_addr, size, [&](BufferId id, Buffer& buffer) {
        if (IsDirectGuest(buffer)) { direct_buffers.push_back(id); }
    });
    for (const auto id : direct_buffers) { DeleteBuffer(id); }
    for (auto it = gather_buffers.begin(); it != gather_buffers.end();) {
        const bool overlaps = std::ranges::any_of(it->second.chunks, [&](const GatherChunk& chunk) {
            return device_addr < chunk.device_addr + chunk.size &&
                   chunk.device_addr < device_addr + size;
        });
        if (!overlaps) {
            ++it;
            continue;
        }
        if (it->second.buffer) {
            gather_used_memory -= it->second.buffer->SizeBytes();
            delayed_destruction_ring.Push(std::move(*it->second.buffer));
        }
        it = gather_buffers.erase(it);
    }
}

template <class P>
void BufferCache<P>::TrimGatherBuffers(bool aggressive) {
    constexpr u64 budget = 64_MiB;
    constexpr size_t max_entries = 128;
    while (!gather_buffers.empty()) {
        const auto oldest = std::ranges::min_element(
            gather_buffers, {}, [](const auto& item) { return item.second.last_used; });
        if (!aggressive && gather_used_memory <= budget && gather_buffers.size() <= max_entries &&
            frame_tick - oldest->second.last_used < 120) {
            break;
        }
        GatherEntry& entry = oldest->second;
        if (entry.scatter_pending) {
            ScatterGatherEntry(oldest->first, entry);
        }
        if (entry.buffer) {
            gather_used_memory -= entry.buffer->SizeBytes();
            delayed_destruction_ring.Push(std::move(*entry.buffer));
        }
        gather_buffers.erase(oldest);
    }
}

template <class P>
void BufferCache<P>::ResolveGatherChunks(GPUVAddr gpu_addr, u32 size) {
    // Reserve the largest view before any descriptor is bound. Smaller views of the same
    // address must not replace its allocation/layout partway through a draw or dispatch.
    gather_buffers[gpu_addr].prepared_size = std::max(gather_buffers[gpu_addr].prepared_size, size);
    scatter_skip = gpu_addr;
    SCOPE_EXIT {
        scatter_skip = 0;
    };
    u64 offset = 0;
    while (offset < size) {
        const size_t chunk = gpu_memory->MaxContinuousRange(gpu_addr + offset, size - offset);
        const std::optional<DAddr> dev = gpu_memory->GpuToCpuAddress(gpu_addr + offset);
        if (chunk == 0 || !dev || !IsRegionPageTableAddressable(*dev, chunk)) {
            offset = Common::AlignUp(gpu_addr + offset + 1, Core::DEVICE_PAGESIZE) - gpu_addr;
            continue;
        }
        static_cast<void>(FindBuffer(*dev, static_cast<u32>(chunk)));
        offset += chunk;
    }
}

template <class P>
void BufferCache<P>::NotifyGatherWrite(DAddr device_addr, u64 size, u8 level, GPUVAddr skip) {
    if (gather_buffers.empty()) {
        return;
    }
    const DAddr end = device_addr + size;
    for (auto& [gpu_addr, entry] : gather_buffers) {
        if (gpu_addr == skip) {
            continue;
        }
        for (GatherChunk& chunk : entry.chunks) {
            if (device_addr < chunk.device_addr + chunk.size && chunk.device_addr < end) {
                chunk.dirty = std::max(chunk.dirty, level);
            }
        }
    }
}

template <class P>
typename BufferCache<P>::Buffer& BufferCache<P>::GatherBuffer(GPUVAddr gpu_addr, u32 size,
                                                            bool is_written) {
    GatherEntry& entry = gather_buffers[gpu_addr];
    const GuestMappingIdentity identity{gpu_memory, gpu_memory->GetID(), gpu_memory->MapGeneration()};
    const u64 device_generation = device_memory.MapGeneration();
    size = std::max(size, entry.prepared_size);
    entry.last_used = frame_tick;
    // Current device layout of the GPU range
    boost::container::small_vector<GatherChunk, 8> layout;
    u64 offset = 0;
    while (offset < size) {
        const size_t chunk = gpu_memory->MaxContinuousRange(gpu_addr + offset, size - offset);
        const std::optional<DAddr> dev = gpu_memory->GpuToCpuAddress(gpu_addr + offset);
        if (chunk == 0 || !dev || !IsRegionPageTableAddressable(*dev, chunk)) {
            offset = Common::AlignUp(gpu_addr + offset + 1, Core::DEVICE_PAGESIZE) - gpu_addr;
            continue;
        }
        layout.push_back(GatherChunk{*dev, static_cast<u32>(chunk), static_cast<u32>(offset), 1});
        offset += chunk;
    }

    bool shared_layout = true;
    if (entry.buffer && IsDirectGuest(*entry.buffer)) {
        // A later cache expansion can replace shared chunks with a staged mirror.
        // Resolve all allocations first, then inspect the final ownership state.
        do {
            channel_state->has_deleted_buffers = false;
            for (const auto& chunk : layout) {
                static_cast<void>(FindBuffer(chunk.device_addr, chunk.size));
            }
        } while (channel_state->has_deleted_buffers);
        for (const auto& chunk : layout) {
            shared_layout &= IsDirectGuest(slot_buffers[FindBuffer(chunk.device_addr, chunk.size)]);
        }
    }
    const bool same_layout = shared_layout && entry.identity == identity &&
                             entry.device_generation == device_generation && entry.chunks.size() == layout.size() &&
                             std::equal(layout.begin(), layout.end(), entry.chunks.begin(),
                                        [](const auto& x, const auto& y) {
                                            return x.device_addr == y.device_addr &&
                                                   x.size == y.size && x.offset == y.offset;
                                        });
    if (!same_layout || (is_written && !entry.aliases_checked)) {
        const std::array ranges{GatherAliasRange{gpu_addr, size, true, is_written}};
        ValidateGatherAliases(ranges);
    }
    const bool needs_allocation = !entry.buffer || entry.buffer->SizeBytes() < size ||
                                  (IsDirectGuest(*entry.buffer) && !same_layout);
    // Preserve the old contents before changing layout or replacing the allocation. Extending
    // a view into unmapped pages can grow the allocation without changing its mapped chunks.
    if (entry.scatter_pending && (!same_layout || needs_allocation)) {
        ScatterGatherEntry(gpu_addr, entry);
    }
    if (needs_allocation) {
        auto replacement = std::make_unique<Buffer>(runtime, 0, Common::AlignUp<u64>(size, 4));
        // First prove every segment is already shared by the ordinary cache; otherwise
        // a stale staged mirror could hide writes made through this direct view.
        bool direct = false;
        if constexpr (requires { runtime.TryDirectGuestBuffer(*replacement, Common::GuestBackingRange{}); }) {
            std::vector<DirectGuestSegment> segments;
            std::optional<Common::GuestBackingRange> first;
            bool eligible = identity.Stable() && (device_generation & 1) == 0;
            do {
                channel_state->has_deleted_buffers = false;
                for (const auto& chunk : layout) {
                    static_cast<void>(FindBuffer(chunk.device_addr, chunk.size));
                }
            } while (channel_state->has_deleted_buffers);
            for (const auto& chunk : layout) {
                const BufferId id = FindBuffer(chunk.device_addr, chunk.size);
                eligible &= IsDirectGuest(slot_buffers[id]);
                const auto backing = device_memory.GetDriverBackingRange(chunk.device_addr, chunk.size);
                if (!backing) { eligible = false; break; }
                if (!first) { first = backing; }
                eligible &= first->fd == backing->fd;
                segments.push_back({backing->offset, chunk.offset, chunk.size});
            }
            const GuestMappingIdentity after{gpu_memory, gpu_memory->GetID(), gpu_memory->MapGeneration()};
            if (eligible && first && device_memory.MapGeneration() == device_generation) {
                const ZeroCopyEligibility eligibility{true, true, true, true, true, true, true, true,
                                                      1, first->allocation_size};
                if (eligibility.Select(identity, after, segments, size) == GuestMappingMode::Direct) {
                    direct = runtime.TryDirectGuestBuffer(*replacement, *first);
                }
            }
        }
        if (!direct) {
            runtime.ClearBuffer(*replacement, 0, replacement->SizeBytes(), 0);
        }
        if (entry.buffer) {
            gather_used_memory -= entry.buffer->SizeBytes();
            delayed_destruction_ring.Push(std::move(*entry.buffer));
        }
        gather_used_memory += replacement->SizeBytes();
        entry.buffer = std::move(replacement);
    }
    if (!same_layout || needs_allocation) {
        // Clear holes that were previously mapped, then repopulate only the mapped chunks.
        if (!IsDirectGuest(*entry.buffer)) {
            runtime.ClearBuffer(*entry.buffer, 0, entry.buffer->SizeBytes(), 0);
        }
        entry.chunks.assign(layout.begin(), layout.end());
        entry.identity = identity;
        entry.device_generation = device_generation;
    }
    entry.aliases_checked = is_written || (same_layout && entry.aliases_checked);
    scatter_skip = gpu_addr;
    SCOPE_EXIT {
        scatter_skip = 0;
    };
    Buffer& scratch = *entry.buffer;
    for (size_t i = 0; i < entry.chunks.size(); ++i) {
        const DAddr dev = entry.chunks[i].device_addr;
        const u32 chunk_size = entry.chunks[i].size;
        const BufferId src_id = FindBuffer(dev, chunk_size);
        Buffer& src = slot_buffers[src_id];
        TouchBuffer(src, src_id);
        SynchronizeBuffer(src, dev, chunk_size); // CPU uploads mark the chunk dirty
        GatherChunk& chunk = entry.chunks[i];
        if (IsDirectGuest(scratch)) {
            // SynchronizeBuffer above still visits every segment, including uniform buffers.
            chunk.dirty = 0;
            continue;
        }
        if (chunk.dirty == 0 || entry.scatter_pending) {
            // Pending: the scratch is newer than the chunk, keep it
            chunk.dirty = 0;
            continue;
        }
        const std::array copies{BufferCopy{
            .src_offset = src.Offset(dev),
            .dst_offset = chunk.offset,
            .size = chunk_size,
        }};
        MarkGpuUse(src, copies[0].src_offset, chunk_size);
        runtime.CopyBuffer(scratch, src, copies, true);
        --chunk.dirty;
    }
    EndGuestCpuAccess(scratch);
    return scratch;
}

template <class P>
void BufferCache<P>::FlushGatherWrites() {
    if (pending_gather_writes.empty()) {
        return;
    }
    for (const GPUVAddr gpu_addr : pending_gather_writes) {
        const auto it = gather_buffers.find(gpu_addr);
        if (it == gather_buffers.end() || !it->second.buffer) {
            continue;
        }
        GatherEntry& entry = it->second;
        if (entry.scatter_pending) {
            continue;
        }
        entry.scatter_pending = true;
        scatter_pending.push_back(gpu_addr);
        // Bookkeeping now, so downloads and DMA see the range as GPU-modified (they write back
        // first through ResolveScatter)
        for (const GatherChunk& chunk : entry.chunks) {
            NotifyGatherWrite(chunk.device_addr, chunk.size, 1, gpu_addr);
            memory_tracker.MarkRegionAsGpuModified(chunk.device_addr, chunk.size);
            gpu_modified_ranges.Add(chunk.device_addr, chunk.size);
            uncommitted_gpu_modified_ranges.Add(chunk.device_addr, chunk.size);
        }
    }
    pending_gather_writes.clear();
}

template <class P>
void BufferCache<P>::ScatterGatherEntry(GPUVAddr gpu_addr, GatherEntry& entry) {
    if (!entry.buffer) {
        entry.scatter_pending = false;
        std::erase(scatter_pending, gpu_addr);
        return;
    }
    scatter_busy = true;
    SCOPE_EXIT {
        scatter_busy = false;
    };
    Buffer& scratch = *entry.buffer;
    // Defend pre-existing entries too: reject before the first copy, keeping the
    // pending writes and entry contents intact. Never silently skip write-back.
    if (!entry.aliases_checked || entry.device_generation != device_memory.MapGeneration()) {
        device_memory.ReadPhysicalAliases([&](u64 generation, const auto& physical_address) {
            if (generation & 1) {
                throw UnsupportedGatherMapping("Page table is changing during pending scatter validation");
            }
            GatherAliasValidator validator{DEVICE_PAGESIZE};
            for (const GatherChunk& chunk : entry.chunks) {
                for (u64 offset = 0; offset < chunk.size;) {
                    const u64 device_addr = chunk.device_addr + offset;
                    const u64 length = std::min<u64>(chunk.size - offset,
                        DEVICE_PAGESIZE - device_addr % DEVICE_PAGESIZE);
                    const auto physical = physical_address(device_addr);
                    if (physical) {
                        validator.Add(*physical, gpu_addr + chunk.offset + offset, length, true);
                    }
                    offset += length;
                }
            }
            if (device_memory.MapGeneration() != generation) {
                throw UnsupportedGatherMapping("Page table changed during pending scatter validation");
            }
        });
    }
    if (IsDirectGuest(scratch)) { GuestMemoryBarrier(false); }
    for (const GatherChunk& chunk : entry.chunks) {
        const BufferId dst_id = FindBuffer(chunk.device_addr, chunk.size);
        Buffer& dst = slot_buffers[dst_id];
        const std::array copies{BufferCopy{
            .src_offset = chunk.offset,
            .dst_offset = dst.Offset(chunk.device_addr),
            .size = chunk.size,
        }};
        MarkGpuUse(dst, copies[0].dst_offset, chunk.size);
        dst.gpu_write_revision.Mark();
        if (!IsDirectGuest(scratch) || !IsDirectGuest(dst)) {
            // A staged mirror introduced after binding still needs refreshed bytes;
            // directly shared destinations already see the same payload.
            runtime.CopyBuffer(dst, scratch, copies, true);
        }
        // Same bookkeeping as MarkWrittenBuffer, without dirtying this gather's own chunks
        NotifyGatherWrite(chunk.device_addr, chunk.size, 1, gpu_addr);
        memory_tracker.MarkRegionAsGpuModified(chunk.device_addr, chunk.size);
        gpu_modified_ranges.Add(chunk.device_addr, chunk.size);
        uncommitted_gpu_modified_ranges.Add(chunk.device_addr, chunk.size);
    }
    entry.scatter_pending = false;
    std::erase(scatter_pending, gpu_addr);
}

template <class P>
void BufferCache<P>::ResolveScatter(DAddr device_addr, u64 size) {
    if (scatter_pending.empty() || scatter_busy) {
        return;
    }
    const DAddr end = device_addr + size;
    // Copy: ScatterGatherEntry erases from scatter_pending
    const boost::container::small_vector<GPUVAddr, 8> pending(scatter_pending.begin(),
                                                              scatter_pending.end());
    for (const GPUVAddr gpu_addr : pending) {
        if (gpu_addr == scatter_skip) {
            continue;
        }
        GatherEntry& entry = gather_buffers[gpu_addr];
        const bool overlaps =
            size == 0 || std::ranges::any_of(entry.chunks, [&](const GatherChunk& chunk) {
                return device_addr < chunk.device_addr + chunk.size && chunk.device_addr < end;
            });
        if (overlaps) {
            ScatterGatherEntry(gpu_addr, entry);
        }
    }
}

template <class P>
bool BufferCache<P>::IsGpuRangeContiguous(GPUVAddr gpu_addr, u32 size) const {
    // Same answer as MaxContinuousRange(gpu_addr, size) == size, cached until the GPU page table
    // changes: the full page walk ran for every storage buffer bind.
    const u64 generation = gpu_memory->MapGeneration();
    const size_t address_space = gpu_memory->GetID();
    if ((generation & 1) != 0) {
        return gpu_memory->MaxContinuousRange(gpu_addr, size) >= size;
    }
    if (address_space != contiguity_cache_address_space ||
        generation != contiguity_cache_generation || contiguity_cache.size() > 4096) {
        contiguity_cache.clear();
        contiguity_cache_generation = generation;
        contiguity_cache_address_space = address_space;
    }
    const auto [it, is_new] = contiguity_cache.try_emplace(ContiguityKey{gpu_addr, size}, false);
    if (is_new) {
        it->second = gpu_memory->MaxContinuousRange(gpu_addr, size) >= size;
        if (gpu_memory->MapGeneration() != generation) {
            // The page table changed during the walk; don't keep the answer
            const bool result = it->second;
            contiguity_cache.erase(it);
            return result;
        }
    }
    return it->second;
}

template <class P>
Binding BufferCache<P>::StorageBufferBinding(GPUVAddr ssbo_addr, u32 cbuf_index,
                                             bool is_written) const {
    const GPUVAddr gpu_addr = gpu_memory->Read<u64>(ssbo_addr);
    const auto size = [&]() {
        const bool is_nvn_cbuf = cbuf_index == 0;
        // The NVN driver buffer (index 0) is known to pack the SSBO address followed by its size.
        if (is_nvn_cbuf) {
            const u32 ssbo_size = gpu_memory->Read<u32>(ssbo_addr + 8);
            if (ssbo_size != 0) {
                return ssbo_size;
            }
        }
        // Other titles (notably Doom Eternal) may use STG/LDG on buffer addresses in custom defined
        // cbufs, which do not store the sizes adjacent to the addresses, so use the fully
        // mapped buffer size for now.
        const u32 memory_layout_size = static_cast<u32>(gpu_memory->GetMemoryLayoutSize(gpu_addr));
        return std::min(memory_layout_size, static_cast<u32>(8_MiB));
    }();
    if (size == 0) {
        return NULL_BINDING;
    }
    // Alignment only applies to the offset of the buffer
    const u32 alignment = runtime.GetStorageBufferAlignment();
    const GPUVAddr aligned_gpu_addr = Common::AlignDown(gpu_addr, alignment);
    const u32 aligned_size = static_cast<u32>(gpu_addr - aligned_gpu_addr) + size;

    const std::optional<DAddr> aligned_device_addr = gpu_memory->GpuToCpuAddress(aligned_gpu_addr);
    if (!aligned_device_addr) {
        if (!std::exchange(channel_state->has_logged_unmapped_storage_buffer, true)) {
            LOG_DEBUG(HW_GPU,
                      "Failed to map storage buffer for cbuf index {} at GPU address 0x{:016X}; "
                      "suppressing further messages for this channel",
                      cbuf_index, aligned_gpu_addr);
        }
        return NULL_BINDING;
    }
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr);
    assert(device_addr && "Unaligned storage buffer address not found for cbuf index {}" &&
           (void(cbuf_index), true));
    // The end address used for size calculation does not need to be aligned
    const DAddr cpu_end = Common::AlignUp(*device_addr + size, Core::DEVICE_PAGESIZE);

    // The binding below assumes the range is contiguous in device memory. When it is not (e.g. a
    // heap mapped in chunks whose device addresses are not in GPU address order), gather it into
    // a scratch buffer at bind time instead (and scatter shader writes back).
    if (!IsGpuRangeContiguous(aligned_gpu_addr, aligned_size)) {
        return Binding{
            .device_addr = *aligned_device_addr,
            .size = aligned_size,
            .buffer_id = NULL_BUFFER_ID,
            .gather_gpu_addr = aligned_gpu_addr,
            .gpu_addr = aligned_gpu_addr,
        };
    }
    const Binding binding{
        .device_addr = *aligned_device_addr,
        .size = is_written ? aligned_size : static_cast<u32>(cpu_end - *aligned_device_addr),
        .buffer_id = BufferId{},
        .gpu_addr = aligned_gpu_addr,
    };
    return binding;
}

template <class P>
TextureBufferBinding BufferCache<P>::GetTextureBufferBinding(GPUVAddr gpu_addr, u32 size,
                                                             PixelFormat format) {
    const std::optional<DAddr> device_addr = gpu_memory->GpuToCpuAddress(gpu_addr);
    TextureBufferBinding binding;
    if (!device_addr || size == 0) {
        binding.device_addr = 0;
        binding.size = 0;
        binding.buffer_id = NULL_BUFFER_ID;
        binding.format = PixelFormat::Invalid;
    } else {
        binding.device_addr = *device_addr;
        binding.size = size;
        binding.buffer_id = BufferId{};
        binding.format = format;
        binding.gpu_addr = gpu_addr;
    }
    return binding;
}

template <class P>
std::span<const u8> BufferCache<P>::ImmediateBufferWithData(DAddr device_addr, size_t size) {
    u8* const base_pointer = device_memory.GetPointer<u8>(device_addr);
    if (IsRangeGranular(device_addr, size) ||
        base_pointer + size == device_memory.GetPointer<u8>(device_addr + size)) {
        return std::span(base_pointer, size);
    } else {
        const std::span<u8> span = ImmediateBuffer(size);
        device_memory.ReadBlockUnsafe(device_addr, span.data(), size,
                                      "BufferCache.ImmediateBufferWithData");
        return span;
    }
}

template <class P>
std::span<u8> BufferCache<P>::ImmediateBuffer(size_t wanted_capacity) {
    immediate_buffer_alloc.resize_destructive(wanted_capacity);
    return std::span<u8>(immediate_buffer_alloc.data(), wanted_capacity);
}

template <class P>
bool BufferCache<P>::HasFastUniformBufferBound(size_t stage, u32 binding_index) const noexcept {
    if constexpr (IS_OPENGL) {
        return ((channel_state->fast_bound_uniform_buffers[stage] >> binding_index) & 1) != 0;
    } else {
        // Only OpenGL has fast uniform buffers
        return false;
    }
}

template <class P>
std::pair<typename BufferCache<P>::Buffer*, u32> BufferCache<P>::GetDrawIndirectCount() {
    const Binding& binding = channel_state->count_buffer_binding;
    auto& buffer = slot_buffers[binding.buffer_id];
    Uma::StampPages(buffer.page_gpu_tick, buffer.Offset(binding.device_addr), binding.size,
                    runtime.CurrentTick());
    return std::make_pair(&buffer, buffer.Offset(binding.device_addr));
}

template <class P>
std::pair<typename BufferCache<P>::Buffer*, u32> BufferCache<P>::GetDrawIndirectBuffer() {
    const Binding& binding = channel_state->indirect_buffer_binding;
    auto& buffer = slot_buffers[binding.buffer_id];
    Uma::StampPages(buffer.page_gpu_tick, buffer.Offset(binding.device_addr), binding.size,
                    runtime.CurrentTick());
    return std::make_pair(&buffer, buffer.Offset(binding.device_addr));
}

} // namespace VideoCommon
