// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <limits>
#include <stdexcept>
#include <mutex>
#include <thread>
#include <utility>

#include "video_core/renderer_vulkan/vk_query_cache.h"

#include "common/thread.h"
#include "common/profiling.h"
#include "video_core/renderer_vulkan/vk_command_pool.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

void Scheduler::SetQueryCache(VideoCommon::QueryCacheBase<QueryCacheParams>& query_cache_) {
    query_cache = &query_cache_;
}

void Scheduler::CommandChunk::ExecuteAll(vk::CommandBuffer cmdbuf,
                                         vk::CommandBuffer upload_cmdbuf) {
    auto command = first;
    while (command != nullptr) {
        auto next = command->GetNext();
        command->Execute(cmdbuf, upload_cmdbuf);
        command->~Command();
        command = next;
    }
    submit = false;
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Device& device_, StateTracker& state_tracker_)
    : device{device_}, state_tracker{state_tracker_},
      master_semaphore{std::make_unique<MasterSemaphore>(device)},
      command_pool{std::make_unique<CommandPool>(*master_semaphore, device)} {
    AcquireNewChunk();
    AllocateWorkerCommandBuffer();
    worker_thread = std::jthread([this](std::stop_token token) { WorkerThread(token); });
}

Scheduler::~Scheduler() {
    submission_owner->store(false, std::memory_order_release);
    // Normal renderer shutdown explicitly drains while callback owners still live.
    // On construction failure/unwinding, never dispatch new callbacks from here:
    // later-declared renderer members may already have been destroyed.
    worker_thread.request_stop();
    event_cv.notify_all();
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void Scheduler::Shutdown() {
    submission_owner->store(false, std::memory_order_release);
    if (!worker_thread.joinable()) {
        return;
    }
    WaitWorker();
    worker_thread.request_stop();
    event_cv.notify_all();
    worker_thread.join();
}

u64 Scheduler::Flush(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When flushing, we only send data to the worker thread; no waiting is necessary.
    const u64 signal_value = SubmitExecution(signal_semaphore, wait_semaphore);
    AllocateNewContext();
    return signal_value;
}

Scheduler::Submission Scheduler::FlushChecked() {
    if (!submission_owner->load(std::memory_order_acquire) || !device.HasTimelineSemaphore())
        return {};
    auto submission = observed_submission ? observed_submission :
        Submission{new CheckedSubmission{submission_owner}};
    SubmitExecution(nullptr, nullptr, submission);
    AllocateNewContext();
    return submission;
}

VkResult Scheduler::PollChecked(const Submission& submission) const noexcept {
    if (!submission || submission->owner != submission_owner ||
        !submission_owner->load(std::memory_order_acquire))
        return VK_ERROR_UNKNOWN;
    const auto result = submission->result.load(std::memory_order_acquire);
    if (result != VK_SUCCESS) return result;
    return master_semaphore->PollTimeline(submission->tick);
}

Scheduler::Write Scheduler::RecordCheckedWrite(std::shared_ptr<const void> resources,
    const void* identity, u64 offset, u64 size, u64 intent_revision,
    std::function<bool(vk::CommandBuffer)> command) {
    if (!resources || !identity || !size || !intent_revision ||
        size > std::numeric_limits<u64>::max() - offset ||
        !command || !submission_owner->load(std::memory_order_acquire) ||
        !device.HasTimelineSemaphore() || checked_writes.size() >= 256) return {};
    auto write = Write{new CheckedWrite{std::move(resources), identity, offset, size,
                                      intent_revision, submission_owner}};
    checked_writes.push_back(write); // Retain before recording, including uncertain failures.
    try {
        Record([write, bound_command = std::move(command)](vk::CommandBuffer cmdbuf) {
            try {
                const bool emitted = bound_command(cmdbuf);
                auto expected = CheckedWrite::Emission::Pending;
                (void)write->emission.compare_exchange_strong(
                    expected, emitted ? CheckedWrite::Emission::Recorded : CheckedWrite::Emission::Skipped,
                    std::memory_order_release, std::memory_order_relaxed);
            } catch (...) {
                // A command may have emitted partial effects. Never replay it.
                write->emission.store(CheckedWrite::Emission::Unknown, std::memory_order_release);
            }
        });
        write->submission = FlushChecked();
    } catch (...) {
        write->emission.store(CheckedWrite::Emission::Unknown, std::memory_order_release);
    }
    return write;
}

Scheduler::WriteState Scheduler::PollCheckedWrite(const Write& write) const noexcept {
    using Emission = CheckedWrite::Emission;
    if (!write || write->owner != submission_owner || write->retired ||
        !submission_owner->load(std::memory_order_acquire)) return WriteState::Unknown;
    const auto emission = write->emission.load(std::memory_order_acquire);
    if (emission == Emission::Unknown) return WriteState::Unknown;
    const auto status = PollChecked(write->submission);
    if (status != VK_SUCCESS && status != VK_NOT_READY) {
        write->emission.store(Emission::Unknown, std::memory_order_release);
        return WriteState::Unknown;
    }
    if (emission == Emission::Pending) return WriteState::Pending;
    if (status != VK_SUCCESS) {
        return write->submission && write->submission->result.load(std::memory_order_acquire) == VK_SUCCESS
            ? WriteState::Submitted : WriteState::Pending;
    }
    return emission == Emission::Skipped ? WriteState::Skipped : WriteState::Complete;
}

void Scheduler::RevokeBufferContent(VkBuffer destination, u64 offset, u64 size) noexcept {
    if (!destination || !size || size > std::numeric_limits<u64>::max() - offset) {
        RevokeTransferContentEligibility();
        return;
    }
    for (const auto& transfer : checked_transfers) {
        if (!transfer->current_eligible) continue;
        for (size_t i = 0; i < transfer->resources.size(); ++i) {
            const auto& pin = transfer->resources[i].pin;
            if (pin.Handle() != destination) continue; // Same live staged allocation, not physical alias ranking.
            const auto begin = std::max(offset, pin.Offset());
            const auto end = std::min(offset + size, pin.Offset() + pin.Size());
            if (begin < end) transfer->current_ranges[i].Revoke(begin - pin.Offset(), end - begin);
        }
    }
}

void Scheduler::RecordBufferFill(VkBuffer destination, u64 offset, u64 size, u32 value) {
    if (!destination) return;
    RequestOutsideRenderPassSyncedOperationContext();
    RevokeBufferContent(destination, offset, size);
    EnqueueCommand([destination, offset, size, value](vk::CommandBuffer cmdbuf, vk::CommandBuffer) {
        const VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
        const VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, before);
        cmdbuf.FillBuffer(destination, offset, size, value);
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, after);
    });
}

void Scheduler::RecordBufferCopy(VkBuffer destination, VkBuffer source, std::span<const VkBufferCopy> copies,
                                 bool barrier, bool upload) {
    if (!destination || !source) return;
    if (!upload) {
        if (barrier) RequestOutsideRenderPassSyncedOperationContext();
        else RequestOutsideRenderPassOperationContext();
    }
    // Keep the ordinary <=8-region path allocation-free, as in the runtime's small vector.
    const auto record_regions = [&](auto regions, size_t region_count) {
        for (const auto& region : std::span{regions}.first(region_count))
            RevokeBufferContent(destination, region.dstOffset, region.size);
        EnqueueCommand([destination, source, regions = std::move(regions), region_count, barrier, upload](
                          vk::CommandBuffer main_cmdbuf, vk::CommandBuffer upload_cmdbuf) {
            auto cmdbuf = upload ? upload_cmdbuf : main_cmdbuf;
            const VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                VK_ACCESS_MEMORY_WRITE_BIT, upload ? VkAccessFlags{VK_ACCESS_TRANSFER_WRITE_BIT} :
                    VkAccessFlags{VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT}};
            const VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
            // Upload work must still wait for prior submissions on this queue. No ancestry is imported.
            if (upload || barrier)
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, before);
            const auto recorded_regions = std::span{regions}.first(region_count);
            cmdbuf.CopyBuffer(source, destination, recorded_regions);
            if (!upload && barrier)
                // HOST scope is required for downloads read by the CPU after the fence.
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, after);
        });
    };
    if (copies.size() <= 8) {
        std::array<VkBufferCopy, 8> regions{};
        std::copy(copies.begin(), copies.end(), regions.begin());
        record_regions(regions, copies.size());
    } else {
        record_regions(std::vector<VkBufferCopy>{copies.begin(), copies.end()}, copies.size());
    }
}

Scheduler::Transfer Scheduler::RecordTransferBatch(std::span<const TransferBuffer> buffers,
    std::span<const TransferContent::Command> commands) {
    return RecordTransferBatchImpl(buffers, commands, {}, {});
}

Scheduler::Transfer Scheduler::RecordTransferBatchImpl(std::span<const TransferBuffer> buffers,
    std::span<const TransferContent::Command> commands,
    std::shared_ptr<const ReplicaCopyProvenance> replica_copy, Transfer parent, Transfer predecessor) {
    if (!predecessor) predecessor = parent;
    if (replica_copy) {
        if (!parent || parent->replica_copy || replica_copy->ParentContent() != parent->content ||
            buffers.size() != 2 || commands.size() != 1 || commands[0].kind != TransferContent::Kind::Copy ||
            commands[0].source != 0 || commands[0].destination != 1 ||
            !replica_copy->MatchesCopy(commands[0].source_offset, commands[0].destination_offset, commands[0].size)) return {};
        const auto matches = [&](size_t slot, size_t original) {
            if (original >= parent->resources.size()) return false;
            const auto& saved = parent->resources[original];
            return buffers[slot].pin.Identity() == saved.pin.Identity() && buffers[slot].pin.Offset() == saved.pin.Offset() &&
                buffers[slot].pin.Size() == saved.pin.Size() && buffers[slot].physical == saved.physical;
        };
        if (!matches(0, replica_copy->SourceBuffer()) || !matches(1, replica_copy->DestinationBuffer())) return {};
        if (predecessor != parent) {
            if (!predecessor || predecessor->replica_anchor != parent ||
                predecessor->replica_copy != replica_copy->SourceCopy() || predecessor->resources.size() != 2 ||
                buffers[0].pin.Identity() != predecessor->resources[1].pin.Identity() ||
                buffers[0].pin.Offset() != predecessor->resources[1].pin.Offset() ||
                buffers[0].pin.Size() != predecessor->resources[1].pin.Size() ||
                buffers[0].physical != predecessor->resources[1].physical) return {};
        } else if (replica_copy->SourceCopy()) return {};
    }
    if (observed_submission || !device.HasTimelineSemaphore() ||
        !submission_owner->load(std::memory_order_acquire) || checked_transfers.size() >= 16 ||
        buffers.empty() || buffers.size() > TransferContent::MaxBuffers) return {};
    Transfer transfer;
    try {
        std::array<TransferContent::Offset, TransferContent::MaxBuffers> sizes{};
        for (size_t i = 0; i < buffers.size(); ++i) {
            const auto& buffer = buffers[i];
            const auto& physical = buffer.physical;
            if (!buffer.pin.IsUsable() || buffer.pin.Offset() % 4 || !physical || !physical->OwnersAlive() ||
                physical->Representation() != buffer.pin.Identity() || physical->Size() != buffer.pin.Size() ||
                physical->NativeOffset() != buffer.pin.Offset()) return {};
            for (size_t j = 0; j < i; ++j)
                if (buffers[j].pin.Identity() == buffer.pin.Identity()) return {}; // No hidden native aliases.
            sizes[i] = buffer.pin.Size();
        }
        auto content = TransferContent::Build({sizes.data(), buffers.size()}, commands);
        if (!content) return {};
        transfer = Transfer{new CheckedTransfer{submission_owner}};
        transfer->resources.assign(buffers.begin(), buffers.end());
        for (const auto& buffer : buffers) transfer->origins.push_back(buffer.physical);
        transfer->content = std::make_shared<const TransferContent>(std::move(*content));
        transfer->replica_copy = std::move(replica_copy);
        transfer->replica_parent = std::move(predecessor);
        transfer->replica_anchor = std::move(parent);
        transfer->submission = Submission{new CheckedSubmission{submission_owner}};
        checked_transfers.push_back(transfer); // Retain uncertain effects until trusted device idle.
    } catch (...) {
        return {}; // No command has been recorded.
    }
    try {
        RequestOutsideRenderPassOperationContext();
        observed_submission = transfer->submission;
        transfer->worker_pending.store(true, std::memory_order_release);
        for (const auto& operation : transfer->content->Commands()) {
            const auto& destination = transfer->resources[operation.destination].pin;
            RevokeBufferContent(destination.Handle(), destination.Offset() + operation.destination_offset, operation.size);
        }
        EnqueueCommand([this, transfer](vk::CommandBuffer cmdbuf, vk::CommandBuffer) {
            if (transfer->emission.load(std::memory_order_acquire) == CheckedTransfer::Emission::Unknown) {
                transfer->worker_pending.store(false, std::memory_order_release);
                return;
            }
            try {
                if (transfer->replica_copy) {
                    const auto& producer = transfer->replica_parent->submission;
                    if (last_accepted_submission.lock() != producer ||
                        producer->queue != device.GetGraphicsQueue().Handle())
                        throw std::runtime_error("unproved replica queue predecessor");
                    for (const auto& resource : transfer->resources)
                        if (!resource.pin.CopyProvenanceEligible() || !resource.physical->CapturedEpochCurrent())
                            throw std::runtime_error("replica source/destination retired or remapped");
                }
                auto& journal = transfer->submission->synchronization;
                size_t index{};
                for (const auto& operation : transfer->content->Commands()) {
                    const VkMemoryBarrier dependency{
                        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                        .pNext = nullptr,
                        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                    };
                    const auto source_stage = index ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                    cmdbuf.PipelineBarrier(source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, dependency);
                    journal.RecordBarrier(source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, dependency);
                    const auto& destination = transfer->resources[operation.destination].pin;
                    if (!destination.IsUsable()) throw std::runtime_error("transfer destination sealed");
                    if (operation.kind == TransferContent::Kind::Fill) {
                        cmdbuf.FillBuffer(destination.Handle(), destination.Offset() + operation.destination_offset,
                                          operation.size, operation.value);
                    } else {
                        const auto& source = transfer->resources[operation.source].pin;
                        if (!source.IsUsable()) throw std::runtime_error("transfer source sealed");
                        const std::array<VkBufferCopy, 1> region{{{
                            .srcOffset = source.Offset() + operation.source_offset,
                            .dstOffset = destination.Offset() + operation.destination_offset,
                            .size = operation.size,
                        }}};
                        cmdbuf.CopyBuffer(source.Handle(), destination.Handle(), region);
                    }
                    transfer->positions[index++] = journal.RecordAccess();
                }
                if (transfer->replica_copy) {
                    const VkMemoryBarrier release{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
                    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, release);
                    journal.RecordBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, release);
                }
                auto expected = CheckedTransfer::Emission::Pending;
                (void)transfer->emission.compare_exchange_strong(expected, CheckedTransfer::Emission::Recorded,
                    std::memory_order_release, std::memory_order_relaxed);
            } catch (...) {
                transfer->emission.store(CheckedTransfer::Emission::Unknown, std::memory_order_release);
            }
            transfer->worker_pending.store(false, std::memory_order_release);
        });
        transfer->current_eligible = true; // Arm once after recording; the worker never rearms.
        for (size_t i = 0; i < transfer->resources.size(); ++i)
            transfer->current_ranges[i].Arm(transfer->resources[i].pin.Size());
        (void)Flush(); // Adopts the checked token while preserving ordinary queue-error propagation.
    } catch (...) {
        transfer->emission.store(CheckedTransfer::Emission::Unknown, std::memory_order_release);
    }
    return transfer;
}

Scheduler::WriteState Scheduler::PollCheckedTransfer(const Transfer& transfer) const noexcept {
    if (!transfer || transfer->owner != submission_owner || transfer->retired ||
        !submission_owner->load(std::memory_order_acquire)) return WriteState::Unknown;
    if (transfer->emission.load(std::memory_order_acquire) == CheckedTransfer::Emission::Unknown)
        return WriteState::Unknown;
    for (const auto& resource : transfer->resources) {
        if (!resource.pin.IsUsable() || !resource.physical->OwnersAlive()) {
            transfer->emission.store(CheckedTransfer::Emission::Unknown, std::memory_order_release);
            return WriteState::Unknown;
        }
    }
    const auto result = PollChecked(transfer->submission);
    if (result != VK_SUCCESS && result != VK_NOT_READY) {
        transfer->emission.store(CheckedTransfer::Emission::Unknown, std::memory_order_release);
        return WriteState::Unknown;
    }
    if (transfer->emission.load(std::memory_order_acquire) == CheckedTransfer::Emission::Pending)
        return WriteState::Pending;
    if (result != VK_SUCCESS)
        return transfer->submission->result.load(std::memory_order_acquire) == VK_SUCCESS
            ? WriteState::Submitted : WriteState::Pending;
    return WriteState::Complete;
}

const TransferContent* Scheduler::CompletedTransferContent(const Transfer& transfer) const noexcept {
    if (PollCheckedTransfer(transfer) != WriteState::Complete) return nullptr;
    const auto commands = transfer->content->Commands();
    for (size_t i = 1; i < commands.size(); ++i) {
        const SyncProvenance::Access previous{VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT};
        VkAccessFlags destination_access = VK_ACCESS_TRANSFER_WRITE_BIT;
        if (commands[i].kind == TransferContent::Kind::Copy) destination_access |= VK_ACCESS_TRANSFER_READ_BIT;
        const SyncProvenance::Access next{VK_PIPELINE_STAGE_TRANSFER_BIT, destination_access};
        if (!transfer->submission->synchronization.Proves(transfer->positions[i - 1], previous,
                                                          transfer->positions[i], next)) return nullptr;
    }
    return transfer->content.get();
}

bool Scheduler::CompletedReplicaCopyHasRoot(const Transfer& result, const Transfer& parent,
    u64 offset, u64 size, TransferContent::Index fill, u64 root_offset) const noexcept {
    if (!result || !parent || !result->replica_copy || result->replica_anchor != parent ||
        result->replica_copy->ParentContent() != parent->content ||
        PollCheckedTransfer(result) != WriteState::Complete || PollCheckedTransfer(parent) != WriteState::Complete) return false;
    auto remaining = result->replica_copy->Depth();
    for (auto link = result; link != parent; link = link->replica_parent) {
        if (!link || !remaining || !link->replica_copy || link->replica_anchor != parent ||
            link->replica_copy->Depth() != remaining-- || !link->replica_parent ||
            (link != result && PollCheckedTransfer(link) != WriteState::Complete) ||
            link->replica_copy->SourceCopy() != link->replica_parent->replica_copy) return false;
        const auto& consumer = link->submission;
        const auto& producer = link->replica_parent->submission;
        if (producer == consumer || producer->result.load(std::memory_order_acquire) != VK_SUCCESS ||
            consumer->queue_predecessor.lock() != producer || !consumer->queue ||
            consumer->queue != producer->queue || consumer->queue != device.GetGraphicsQueue().Handle()) return false;
        const SyncProvenance::Access prior{VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
        const SyncProvenance::Access copy{VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
        const SyncProvenance::Access later{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        if (!consumer->synchronization.ProvesEarlierSubmission(prior, link->positions[0], copy) ||
            !consumer->synchronization.ProvesRelease(link->positions[0], copy, later)) return false;
    }
    if (remaining) return false;
    return result->replica_copy->HasRoot(offset, size, fill, root_offset);
}

Scheduler::ObservedWrites Scheduler::RecordObservedWrites(std::span<const WriteFootprint> footprints,
    std::function<bool(vk::CommandBuffer)> command,
    std::shared_ptr<VideoCommon::PhysicalWriteCoverage> coverage) {
    if (!command) return {};
    std::shared_ptr<std::vector<Write>> writes;
    Submission submission;
    try {
        if (footprints.size() > 64 || checked_writes.size() + footprints.size() > 256 ||
            !submission_owner->load(std::memory_order_acquire) || !device.HasTimelineSemaphore() ||
            (coverage && !coverage->Accepting()))
            throw std::runtime_error("unsupported observed write batch");
        for (const auto& range : footprints) {
            if (!range.resources || !range.identity || !range.size || !range.intent_revision ||
                range.size > std::numeric_limits<u64>::max() - range.offset)
                throw std::runtime_error("invalid observed write footprint");
        }
        writes = std::make_shared<std::vector<Write>>();
        writes->reserve(footprints.size());
        checked_writes.reserve(checked_writes.size() + footprints.size());
        if (!footprints.empty()) {
            submission = observed_submission ? observed_submission :
                Submission{new CheckedSubmission{submission_owner}};
            for (const auto& range : footprints) {
                auto write = Write{new CheckedWrite{range.resources, range.identity, range.offset,
                    range.size, range.intent_revision, submission_owner}};
                write->submission = submission;
                write->coverage = coverage;
                write->access = range.access;
                const bool associated = range.physical &&
                    range.physical->Representation() == range.identity &&
                    range.physical->NativeOffset() == range.offset &&
                    range.physical->Size() == range.size && range.physical->OwnersAlive();
                if (associated) write->physical = range.physical;
                else if (coverage) coverage->Invalidate();
                writes->push_back(std::move(write));
            }
        }
    } catch (...) {
        if (coverage) coverage->Invalidate();
        // No observed command has been queued. Diagnostics never drop/replay rendering.
        Record(std::move(command));
        return {};
    }
    if (submission) observed_submission = submission;
    checked_writes.insert(checked_writes.end(), writes->begin(), writes->end());
    try {
        Record([writes, bound_command = std::move(command)](vk::CommandBuffer cmdbuf) {
            try {
                const auto emission = bound_command(cmdbuf) ? CheckedWrite::Emission::Recorded :
                                                            CheckedWrite::Emission::Skipped;
                const auto position = !writes->empty() && emission == CheckedWrite::Emission::Recorded
                    ? writes->front()->submission->synchronization.RecordAccess() : 0;
                for (const auto& write : *writes) {
                    write->position = position;
                    auto expected = CheckedWrite::Emission::Pending;
                    (void)write->emission.compare_exchange_strong(expected, emission,
                        std::memory_order_release, std::memory_order_relaxed);
                }
            } catch (...) {
                for (const auto& write : *writes)
                    write->emission.store(CheckedWrite::Emission::Unknown, std::memory_order_release);
                throw; // Preserve the ordinary worker's exception behavior.
            }
        });
    } catch (...) {
        for (const auto& write : *writes)
            write->emission.store(CheckedWrite::Emission::Unknown, std::memory_order_release);
        throw;
    }
    return writes;
}

const VideoCommon::PhysicalWriteIdentity* Scheduler::CompletedPhysicalIdentity(
    const Write& write) const noexcept {
    if (PollCheckedWrite(write) != WriteState::Complete || !write->PhysicalCoverageComplete() ||
        !write->physical || !write->physical->OwnersAlive()) return nullptr;
    return write->physical.get();
}

bool Scheduler::HasConfirmedMemoryDependency(const Write& before, const Write& after,
                                            u64 physical_offset, u64 size) const noexcept {
    const auto* a = CompletedPhysicalIdentity(before);
    const auto* b = CompletedPhysicalIdentity(after);
    if (!a || !b || !size || size > std::numeric_limits<u64>::max() - physical_offset ||
        before->submission != after->submission || a->Backing() != b->Backing() ||
        a->Representation() != b->Representation()) return false;
    const auto unique_projection = [](const VideoCommon::PhysicalWriteIdentity& plan) {
        const auto segments = plan.Segments();
        for (size_t i = 0; i < segments.size(); ++i)
            for (size_t j = i + 1; j < segments.size(); ++j)
                if (segments[i].physical_offset < segments[j].physical_offset + segments[j].length &&
                    segments[j].physical_offset < segments[i].physical_offset + segments[i].length)
                    return false;
        return true;
    };
    if (!unique_projection(*a) || !unique_projection(*b)) return false;
    // Each requested physical byte must project to the SAME native allocation byte.
    // Physical aliases backed by independent staged buffers do not inherit contents.
    for (u64 cursor = physical_offset; cursor < physical_offset + size;) {
        u64 advance{};
        for (const auto& left : a->Segments()) {
            if (cursor < left.physical_offset || cursor - left.physical_offset >= left.length) continue;
            for (const auto& right : b->Segments()) {
                if (cursor < right.physical_offset || cursor - right.physical_offset >= right.length)
                    continue;
                const auto native_a = a->NativeOffset() + left.logical_offset + cursor - left.physical_offset;
                const auto native_b = b->NativeOffset() + right.logical_offset + cursor - right.physical_offset;
                if (native_a == native_b)
                    advance = std::min({physical_offset + size - cursor,
                        left.length - (cursor - left.physical_offset),
                        right.length - (cursor - right.physical_offset)});
            }
        }
        if (!advance) return false;
        cursor += advance;
    }
    return before->submission->synchronization.Proves(
        before->position, before->access, after->position, after->access);
}

void Scheduler::RecordProvenanceBarrier(VkPipelineStageFlags source,
                                       VkPipelineStageFlags destination,
                                       const VkMemoryBarrier& memory) {
    // A typed memory barrier records no buffer writes and cannot revoke byte content.
    EnqueueCommand([submission = observed_submission, source, destination, memory](vk::CommandBuffer cmdbuf, vk::CommandBuffer) {
        cmdbuf.PipelineBarrier(source, destination, 0, memory);
        if (submission) submission->synchronization.RecordBarrier(source, destination, memory);
    });
}

void Scheduler::RetireCheckedWritesAfterDeviceIdle() {
    if (observed_submission) return; // Device idle does not submit recorded host work.
    for (const auto& transfer : checked_transfers) {
        if (transfer->worker_pending.load(std::memory_order_acquire) ||
            !transfer->submission->queue_attempted.load(std::memory_order_acquire) ||
            transfer->submission->result.load(std::memory_order_acquire) == VK_NOT_READY) return;
    }
    for (const auto& write : checked_writes) {
        write->retired = true;
        write->resources.reset();
    }
    checked_writes.clear();
    for (const auto& transfer : checked_transfers) {
        transfer->retired = true;
        transfer->resources.clear();
    }
    checked_transfers.clear();
}

void Scheduler::Finish(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitExecution(signal_semaphore, wait_semaphore);
    Wait(presubmit_tick);
    AllocateNewContext();
}

void Scheduler::WaitWorker() {
    DispatchWork();

    // Ensure the queue is drained.
    {
        std::unique_lock ql{queue_mutex};
        event_cv.wait(ql, [this] { return work_queue.empty(); });
    }

    // Now wait for execution to finish.
    std::scoped_lock el{execution_mutex};
}

void Scheduler::DispatchWork() {
    if (chunk->Empty()) {
        return;
    }
    {
        std::scoped_lock ql{queue_mutex};
        work_queue.push(std::move(chunk));
    }
    event_cv.notify_all();
    AcquireNewChunk();
}

void Scheduler::RequestRenderpass(const Framebuffer* framebuffer) {
    const VkRenderPass renderpass = framebuffer->RenderPass();
    const VkFramebuffer framebuffer_handle = framebuffer->Handle();
    const VkExtent2D render_area = framebuffer->RenderArea();
    if (renderpass == state.renderpass && framebuffer_handle == state.framebuffer &&
        render_area.width == state.render_area.width &&
        render_area.height == state.render_area.height) {
        return;
    }
    EndRenderPass();
    state.renderpass = renderpass;
    state.framebuffer = framebuffer_handle;
    state.render_area = render_area;

    Record([renderpass, framebuffer_handle, render_area](vk::CommandBuffer cmdbuf) {
        const VkRenderPassBeginInfo renderpass_bi{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = nullptr,
            .renderPass = renderpass,
            .framebuffer = framebuffer_handle,
            .renderArea =
                {
                    .offset = {.x = 0, .y = 0},
                    .extent = render_area,
                },
            .clearValueCount = 0,
            .pClearValues = nullptr,
        };
        cmdbuf.BeginRenderPass(renderpass_bi, VK_SUBPASS_CONTENTS_INLINE);
    });
    num_renderpass_images = framebuffer->NumImages();
    renderpass_images = framebuffer->Images();
    renderpass_image_ranges = framebuffer->ImageRanges();
}

void Scheduler::RequestOutsideRenderPassOperationContext() {
    EndRenderPass();
    RecordDeferredWaitForIdle();
    // Copies, clears, dispatches and query work all come through here
    unsynced_writes = true;
}

void Scheduler::RequestOutsideRenderPassSyncedOperationContext() {
    EndRenderPass();
    RecordDeferredWaitForIdle();
}

void Scheduler::WaitForIdle(VkPipelineStageFlags src_stages) {
    deferred_wfi_stages |= src_stages;
    if (state.renderpass && !unsynced_writes) {
        // Draws since the last barrier wrote attachments only, which later draws of this render
        // pass see in order. The barrier waits until something reads or writes memory in a way
        // the pass does not order.
        return;
    }
    EndRenderPass();
    RecordDeferredWaitForIdle();
    unsynced_writes = false;
}

void Scheduler::RecordDeferredWaitForIdle() {
    if (deferred_wfi_stages == 0) {
        return;
    }
    // A pipeline barrier with a memory barrier: later work starts after earlier work has
    // finished and sees its writes. The event pair this replaced had TOP_OF_PIPE as its second
    // scope, which carries no memory dependency, and the event was never reset, so every wait
    // after the first did nothing.
    static constexpr VkMemoryBarrier WRITES_VISIBLE{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
    };
    RecordProvenanceBarrier(deferred_wfi_stages, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, WRITES_VISIBLE);
    deferred_wfi_stages = 0;
}

bool Scheduler::UpdateGraphicsPipeline(GraphicsPipeline* pipeline) {
    if (state.graphics_pipeline == pipeline) {
        return false;
    }
    state.graphics_pipeline = pipeline;
    return true;
}

bool Scheduler::UpdateRescaling(bool is_rescaling) {
    if (state.rescaling_defined && is_rescaling == state.is_rescaling) {
        return false;
    }
    state.rescaling_defined = true;
    state.is_rescaling = is_rescaling;
    return true;
}

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("VulkanWorker");

    const auto TryPopQueue{[this](auto& work) -> bool {
        if (work_queue.empty()) {
            return false;
        }

        work = std::move(work_queue.front());
        work_queue.pop();
        event_cv.notify_all();
        return true;
    }};

    while (!stop_token.stop_requested()) {
        std::unique_ptr<CommandChunk> work;

        {
            std::unique_lock lk{queue_mutex};

            // Wait for work.
            Common::CondvarWait(event_cv, lk, stop_token, [&] { return TryPopQueue(work); });

            // If we've been asked to stop, we're done.
            if (stop_token.stop_requested()) {
                return;
            }

            // Exchange lock ownership so that we take the execution lock before
            // the queue lock goes out of scope. This allows us to force execution
            // to complete in the next step.
            std::exchange(lk, std::unique_lock{execution_mutex});

            // Perform the work, tracking whether the chunk was a submission
            // before executing.
            const bool has_submit = work->HasSubmit();
            {
                CITRON_PROFILE_SCOPE("Vulkan::WorkerExecute");
                work->ExecuteAll(current_cmdbuf, current_upload_cmdbuf);
            }

            // If the chunk was a submission, reallocate the command buffer.
            if (has_submit) {
                AllocateWorkerCommandBuffer();
            }
        }

        {
            std::scoped_lock rl{reserve_mutex};

            // Recycle the chunk back to the reserve.
            chunk_reserve.emplace_back(std::move(work));
        }
    }
}

void Scheduler::AllocateWorkerCommandBuffer() {
    current_cmdbuf = vk::CommandBuffer(command_pool->Commit(), device.GetDispatchLoader());
    current_cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
    current_upload_cmdbuf = vk::CommandBuffer(command_pool->Commit(), device.GetDispatchLoader());
    current_upload_cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });
}

u64 Scheduler::SubmitExecution(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore,
                               Submission checked) {
    CITRON_PROFILE_SCOPE("Vulkan::SubmitExecution");
    const bool ordinary_errors = !checked;
    if (observed_submission) {
        checked = std::exchange(observed_submission, {});
    }
    try {
        EndPendingOperations();
        InvalidateState();

        const u64 signal_value = master_semaphore->NextTick();
        if (checked) checked->tick = signal_value;
        EnqueueCommand([signal_semaphore, wait_semaphore, signal_value, checked, ordinary_errors,
                                this](vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) {
            try {
                static constexpr VkMemoryBarrier WRITE_BARRIER{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                };
                upload_cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, WRITE_BARRIER);
                upload_cmdbuf.End();
                cmdbuf.End();

                if (on_submit) {
                    on_submit();
                }

                std::scoped_lock lock{submit_mutex};
                if (checked) checked->queue_attempted.store(true, std::memory_order_release);
                const VkResult result = master_semaphore->SubmitQueue(
                    cmdbuf, upload_cmdbuf, signal_semaphore, wait_semaphore, signal_value);
                if (result == VK_SUCCESS) {
                    if (checked) {
                        checked->queue = device.GetGraphicsQueue().Handle();
                        checked->queue_predecessor = last_accepted_submission;
                    }
                    last_accepted_submission = checked; // Unknown ordinary submissions break the explicit adjacency proof.
                } else {
                    last_accepted_submission.reset();
                }
                if (result != VK_SUCCESS) current_content_uncertain.store(true, std::memory_order_release);
                if (checked) {
                    checked->result.store(result, std::memory_order_release);
                    if (!ordinary_errors) return; // Explicit checked callers observe Unknown.
                }
                switch (result) {
                case VK_SUCCESS:
                    break;
                case VK_ERROR_DEVICE_LOST:
                    device.ReportLoss();
                    [[fallthrough]];
                default:
                    vk::Check(result);
                    break;
                }
            } catch (const vk::Exception& error) {
                last_accepted_submission.reset();
                current_content_uncertain.store(true, std::memory_order_release);
                if (checked) checked->result.store(error.GetResult(), std::memory_order_release);
                if (ordinary_errors || !checked) throw;
            } catch (...) {
                last_accepted_submission.reset();
                current_content_uncertain.store(true, std::memory_order_release);
                if (checked) checked->result.store(VK_ERROR_UNKNOWN, std::memory_order_release);
                if (ordinary_errors || !checked) throw;
            }
        });
        chunk->MarkSubmit();
        DispatchWork();
        return signal_value;
    } catch (...) {
        if (checked) checked->result.store(VK_ERROR_UNKNOWN, std::memory_order_release);
        current_content_uncertain.store(true, std::memory_order_release);
        throw; // Host-side failure before queue acceptance is not indefinitely Pending.
    }
}

void Scheduler::AllocateNewContext() {
    // Enable counters once again. These are disabled when a command buffer is finished.
    if (query_cache) {
#if ANDROID
        if (Settings::IsGPULevelHigh()) {
            // This is problematic on Android, disable on GPU Normal and Low.
            query_cache->NotifySegment(true);
        }
#else
        if (Settings::IsGPULevelNormal()) {
            // Skip query cache operations for Low accuracy
            query_cache->NotifySegment(true);
        }
#endif
    }
}

void Scheduler::InvalidateState() {
    state.graphics_pipeline = nullptr;
    state.rescaling_defined = false;
    state_tracker.InvalidateCommandBufferState();
}

void Scheduler::EndPendingOperations() {
#if ANDROID
    if (Settings::IsGPULevelHigh()) {
        // This is problematic on Android, disable on GPU Normal and Low.
        // query_cache->DisableStreams();
    }
#else
    // query_cache->DisableStreams();
#endif
    if (Settings::IsGPULevelNormal()) {
        // Skip query cache operations for Low accuracy
        query_cache->NotifySegment(false);
    }
    EndRenderPass();
}

void Scheduler::EndRenderPass() {
    if (!state.renderpass) {
        return;
    }
    Record([num_images = num_renderpass_images, images = renderpass_images,
            ranges = renderpass_image_ranges](vk::CommandBuffer cmdbuf) {
        std::array<VkImageMemoryBarrier, 9> barriers;
        for (size_t i = 0; i < num_images; ++i) {
            barriers[i] = VkImageMemoryBarrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i],
                .subresourceRange = ranges[i],
            };
        }
        cmdbuf.EndRenderPass();
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, nullptr, nullptr,
                               vk::Span(barriers.data(), num_images));
    });
    state.renderpass = nullptr;
    num_renderpass_images = 0;
}

void Scheduler::AcquireNewChunk() {
    std::scoped_lock rl{reserve_mutex};

    if (chunk_reserve.empty()) {
        // If we don't have anything reserved, we need to make a new chunk.
        chunk = std::make_unique<CommandChunk>();
    } else {
        // Otherwise, we can just take from the reserve.
        chunk = std::move(chunk_reserve.back());
        chunk_reserve.pop_back();
    }
}

} // namespace Vulkan
