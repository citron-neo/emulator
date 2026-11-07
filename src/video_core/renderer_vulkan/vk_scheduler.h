// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <queue>
#include <vector>
#include <span>

#include "common/alignment.h"
#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "common/profiling.h"
#include "common/slow_path_metrics.h"
#include "video_core/buffer_cache/physical_write_identity.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_native_source_retainer.h"
#include "video_core/renderer_vulkan/vk_buffer_payload.h"
#include "video_core/renderer_vulkan/vk_sync_provenance.h"
#include "video_core/renderer_vulkan/vk_transfer_content.h"
#include "video_core/renderer_vulkan/vk_transfer_eligibility.h"
#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"
#include "video_core/renderer_vulkan/vk_reconciliation_planner.h"
#include "video_core/renderer_vulkan/vk_private_producer_guard.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace VideoCommon {
template <typename Trait>
class QueryCacheBase;
}

namespace Vulkan {

class CommandPool;
class Device;
class Framebuffer;
class GraphicsPipeline;
class StateTracker;

struct QueryCacheParams;

/// The scheduler abstracts command buffer and fence management with an interface that's able to do
/// OpenGL-like operations on Vulkan command buffers.
class Scheduler {
public:
    class CheckedSubmission {
        friend class Scheduler;
        explicit CheckedSubmission(std::shared_ptr<std::atomic_bool> owner_)
            : owner{std::move(owner_)} {}
        const std::shared_ptr<std::atomic_bool> owner;
        std::atomic<VkResult> result{VK_NOT_READY};
        std::atomic_bool queue_attempted{};
        VkQueue queue{};
        std::weak_ptr<CheckedSubmission> queue_predecessor;
        u64 tick{};
        SyncProvenance synchronization;
    };
    using Submission = std::shared_ptr<CheckedSubmission>;

    struct TransferBuffer {
        StagedBufferPin pin;
        std::shared_ptr<const VideoCommon::PhysicalWriteIdentity> physical;
    };
    class CheckedTransfer {
        friend class Scheduler;
        friend class PrivateProducerTestAccess;
        explicit CheckedTransfer(std::shared_ptr<std::atomic_bool> owner_) : owner{std::move(owner_)} {}
        enum class Emission { Pending, Recorded, Unknown };
        const std::shared_ptr<std::atomic_bool> owner;
        std::vector<TransferBuffer> resources;
        std::vector<std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>> origins;
        std::shared_ptr<const TransferContent> content;
        std::shared_ptr<const ReplicaCopyProvenance> replica_copy;
        std::shared_ptr<CheckedTransfer> replica_parent;
        std::shared_ptr<CheckedTransfer> replica_source;
        std::shared_ptr<CheckedTransfer> replica_destination;
        std::shared_ptr<CheckedTransfer> replica_anchor;
        std::vector<std::shared_ptr<CheckedTransfer>> replica_dependencies;
        size_t TargetIndex() const noexcept {
            return replica_copy && replica_copy->IsBatch() ? replica_copy->DestinationBuffer() : 1;
        }
        bool IncludesAncestor(const std::shared_ptr<CheckedTransfer>& dependency) const noexcept {
            const CheckedTransfer* link = this;
            for (size_t count = 0; link && count <= ReplicaCopyProvenance::MaxHops; ++count) {
                if (link == dependency.get()) return true;
                link = link->replica_parent.get();
            }
            return false;
        }
        Submission submission;
        std::array<SyncProvenance::Position, TransferContent::MaxCommands> positions{};
        std::atomic<Emission> emission{Emission::Pending};
        std::atomic_bool worker_pending{};
        bool current_eligible{}; // GPU/cache-thread cancellation only, never an execution version.
        std::array<TransferEligibility, TransferContent::MaxBuffers> current_ranges;
        bool retired{};
        std::shared_ptr<PrivateProducerGuard> exclusion;
    public:
        // Planned metadata is inspectable before execution, never completed evidence.
        const auto& PlannedContent() const noexcept { return content; }
        const auto& HistoricalOrigins() const noexcept { return origins; }
        const auto& PlannedReplicaCopy() const noexcept { return replica_copy; }
        bool ResourcesRetained() const noexcept { return !resources.empty(); }
    };
    using Transfer = std::shared_ptr<CheckedTransfer>;
    enum class WriteState;
    class PrivateProducerDomain {
        friend class BufferCacheRuntime;
        friend class PrivateProducerTestAccess;
        Scheduler* scheduler;
        std::thread::id owner_thread=std::this_thread::get_id();
        std::weak_ptr<std::atomic_bool> owner_lifetime;
        std::shared_ptr<PrivateProducerGuard::State> backing;
        std::vector<TransferBuffer> replicas;
        std::weak_ptr<CheckedTransfer> operation;
        PrivateProducerDomain(Scheduler& owner,std::shared_ptr<PrivateProducerGuard::State> state)
            :scheduler{&owner},owner_lifetime{owner.submission_owner},backing{std::move(state)}{}
        bool Available() const;
        bool Initialize();
    public:
        ~PrivateProducerDomain(){backing->Close();}
        bool Fill(u32 replica,u64 offset,u64 size,u32 value);
        bool CpuWrite(u64 offset,std::span<const u8> bytes);
        // Same owner thread as creation. Returns actual checked completion,
        // never a caller-provided success boolean. No native capability export.
        WriteState Poll();
        void Retire(){backing->Close();}
        PrivateProducerDomain(const PrivateProducerDomain&)=delete;
        PrivateProducerDomain& operator=(const PrivateProducerDomain&)=delete;
    };
    struct ReplicaCopyRequest {
        Transfer parent;
        TransferContent::Index source{}, destination{};
        u64 source_offset{}, destination_offset{}, size{};
        TransferContent::Index source_fill{}, destination_fill{};
        u64 source_root_offset{}, destination_root_offset{};
        Transfer source_receipt{}; // Optional prior copy; root IDs remain in parent, the original anchor.
        std::span<const ReplicaCopyProvenance::RootSpan> source_roots{}; // Optional complete mixed-source partition.
        Transfer destination_receipt{};
        std::span<const ReplicaCopyProvenance::RootSpan> destination_roots{}; // Exact untouched complement only.
    };
    struct ReplicaCopyRegion {
        TransferContent::Index source{};
        u64 source_offset{}, destination_offset{}, size{};
        Transfer source_receipt{};
        std::span<const ReplicaCopyProvenance::RootSpan> roots{};
    };
    struct ReplicaCopiesRequest {
        Transfer parent;
        TransferContent::Index destination{};
        u64 destination_offset{}, size{};
        std::span<const ReplicaCopyRegion> regions;
        Transfer destination_receipt{};
        std::span<const ReplicaCopyProvenance::RootSpan> complements;
    };
    struct ReconciliationReplica {
        TransferContent::Index slot{};
        Transfer receipt;
        std::span<const ReplicaCopyProvenance::RootSpan> roots;
    };
    struct ReconciliationRequest {
        Transfer anchor;
        BoundedReconciliationPlanner::Scope scope;
        TransferContent::Index destination{};
        std::span<const ReconciliationReplica> replicas;
    };
    // GPU/cache-thread query only. No recording, pin lease, publication or future queue admission.
    template <typename Memory>
    BoundedReconciliationPlanner::Result PlanControlledReconciliation(const Memory& memory,
                                                                    const ReconciliationRequest& request) {
        using Planner=BoundedReconciliationPlanner;using Reason=Planner::Reason;using DecisionState=Planner::State;
        const auto reject=[](DecisionState decision,Reason reason) {return Planner::Result{decision,reason,{}};};
        try {
            const auto& parent=request.anchor;
            if (!parent || parent->replica_copy || request.replicas.size()<2) return reject(DecisionState::Refused,Reason::InvalidRequest);
            if (request.replicas.size()>3) return reject(DecisionState::Refused,Reason::Budget);
            if (parent->retired || parent->resources.empty()) return reject(DecisionState::Unknown,Reason::Retired);
            std::vector<Planner::Replica> replicas;
            Transfer predecessor=parent;
            for (const auto& replica:request.replicas) {
                if (replica.slot>=parent->resources.size()) return reject(DecisionState::Refused,Reason::InvalidRequest);
                const auto& resource=parent->resources[replica.slot];const auto& receipt=replica.receipt;
                if (!resource.pin.IsUsable() || !resource.pin.ContentEligible()) return reject(DecisionState::Unknown,Reason::Retired);
                if (!resource.pin.CopyProvenanceEligible()) return reject(DecisionState::Unknown,Reason::CurrentUnavailable);
                if (!resource.physical || resource.physical->Representation()!=resource.pin.Identity() ||
                    resource.physical->NativeOffset()!=resource.pin.Offset() || resource.physical->Size()!=resource.pin.Size())
                    return reject(DecisionState::Refused,Reason::PhysicalGeometry);
                if (receipt) {
                    if (!receipt->replica_copy || receipt->replica_anchor!=parent ||
                        receipt->replica_copy->DestinationBuffer()!=replica.slot || receipt->TargetIndex()>=receipt->resources.size())
                        return reject(DecisionState::Refused,Reason::Ancestry);
                    const auto& original=receipt->resources[receipt->TargetIndex()];
                    if (original.pin.Identity()!=resource.pin.Identity() || original.pin.Offset()!=resource.pin.Offset() ||
                        original.pin.Size()!=resource.pin.Size() || original.physical!=resource.physical)
                        return reject(DecisionState::Refused,Reason::Ancestry);
                    if (!predecessor->replica_copy || receipt->replica_copy->Depth()>predecessor->replica_copy->Depth()) predecessor=receipt;
                }
                replicas.push_back({replica.slot,resource.physical,receipt ? receipt->replica_copy : nullptr,replica.roots});
            }
            for (const auto& replica:request.replicas)
                if (!predecessor->IncludesAncestor(replica.receipt ? replica.receipt : parent))
                    return reject(DecisionState::Unknown,Reason::SynchronizationUnknown);
            const auto evidence=[&](const Planner::Replica& replica,const Planner::RootSpan& part) {
                const auto& resource=parent->resources[replica.slot];
                const Transfer* receipt=&parent;
                for (const auto& offered:request.replicas) if (offered.slot==replica.slot && offered.receipt) receipt=&offered.receipt;
                if (!resource.pin.IsUsable() || !resource.pin.ContentEligible()) return Reason::Retired;
                if (!resource.pin.CopyProvenanceEligible()) return Reason::CurrentUnavailable;
                if (!resource.physical->MatchesCurrent(memory)) return Reason::Remapped;
                if (PollCheckedTransfer(*receipt)!=WriteState::Complete) return Reason::SubmissionUnknown;
                if ((*receipt)->submission->queue!=device.GetGraphicsQueue().Handle()) return Reason::CrossQueueUnknown;
                if (replica.receipt ? !CompletedReplicaCopy(*receipt,parent) : !CompletedTransferContent(parent))
                    return Reason::SynchronizationUnknown;
                const bool current=replica.receipt
                    ? CurrentReplicaCopyHasRoot(*receipt,memory,parent,part.offset,part.size,part.fill,part.root_offset)
                    : CurrentTransferHasRoot(parent,memory,replica.slot,part.offset,part.size,part.fill,part.root_offset);
                return current ? Reason::None : Reason::CurrentUnavailable;
            };
            return Planner::Build({parent->content,request.scope,request.destination,replicas},evidence);
        } catch (...) {return reject(DecisionState::Unknown,Reason::EvidenceUnavailable);}
    }
    template <typename Memory>
    Transfer RecordControlledReplicaCopies(const Memory& memory, const ReplicaCopiesRequest& request) {
        try {
            const auto& parent=request.parent;
            if (!parent || parent->replica_copy || request.destination>=parent->resources.size() ||
                request.regions.empty() || request.regions.size()>TransferContent::MaxCommands) return {};
            std::vector<ReplicaCopyProvenance::Region> regions;
            std::vector<Transfer> dependencies;
            std::vector<TransferContent::Command> commands;
            Transfer predecessor=parent;
            size_t budget{};
            const auto binding=[&](const Transfer& receipt, size_t slot) {
                if (!receipt) return true;
                if (!receipt->replica_copy || receipt->replica_anchor!=parent ||
                    receipt->replica_copy->DestinationBuffer()!=slot || receipt->TargetIndex()>=receipt->resources.size()) return false;
                const auto& saved=receipt->resources[receipt->TargetIndex()];
                const auto& original=parent->resources[slot];
                return saved.pin.Identity()==original.pin.Identity() && saved.pin.Offset()==original.pin.Offset() &&
                    saved.pin.Size()==original.pin.Size() && saved.physical==original.physical;
            };
            const auto include=[&](const Transfer& dependency) {
                if (dependency && dependency->replica_copy &&
                    (!predecessor->replica_copy || dependency->replica_copy->Depth()>predecessor->replica_copy->Depth())) predecessor=dependency;
            };
            if (!binding(request.destination_receipt,request.destination)) return {};
            include(request.destination_receipt);
            for (const auto& region : request.regions) {
                if (region.source>=parent->resources.size() || region.roots.size()>ReplicaCopyProvenance::MaxRootSpans-budget ||
                    !binding(region.source_receipt,region.source)) return {};
                budget+=region.roots.size();
                regions.push_back({region.source,region.source_offset,region.destination_offset,region.size,
                    region.source_receipt ? region.source_receipt->replica_copy : nullptr,
                    {region.roots.begin(),region.roots.end()}});
                dependencies.push_back(region.source_receipt ? region.source_receipt : parent);
                commands.push_back({TransferContent::Kind::Copy,request.destination,region.source,
                    region.destination_offset,region.source_offset,region.size});
                include(region.source_receipt);
            }
            auto plan=ReplicaCopyProvenance::BuildBatch(parent->content,request.destination,request.destination_offset,
                request.size,parent->resources[request.destination].pin.Size(),regions,
                request.destination_receipt ? request.destination_receipt->replica_copy : nullptr,request.complements);
            if (!plan) return {};
            if (!predecessor->IncludesAncestor(request.destination_receipt ? request.destination_receipt : parent)) return {};
            for (const auto& dependency : dependencies) if (!predecessor->IncludesAncestor(dependency)) return {};
            const auto qualify=[&] {
                const auto& destination=parent->resources[request.destination];
                if (!destination.pin.CopyProvenanceEligible() || !destination.physical->MatchesCurrent(memory)) return false;
                if (request.destination_receipt && !CompletedReplicaCopy(request.destination_receipt,parent)) return false;
                for (const auto& part : plan->DestinationRoots())
                    if (!(request.destination_receipt ? CurrentReplicaCopyHasRoot(request.destination_receipt,memory,parent,
                        part.offset,part.size,part.fill,part.root_offset) : CurrentTransferHasRoot(parent,memory,request.destination,
                        part.offset,part.size,part.fill,part.root_offset))) return false;
                for (size_t i=0;i<plan->Regions().size();++i) {
                    const auto& region=plan->Regions()[i];const auto& source=parent->resources[region.source];
                    if (!source.pin.CopyProvenanceEligible() || source.pin.Identity()==destination.pin.Identity()) return false;
                    for (const auto& part : region.roots)
                        if (!(dependencies[i]!=parent ? CurrentReplicaCopyHasRoot(dependencies[i],memory,parent,
                            part.offset,part.size,part.fill,part.root_offset) : CurrentTransferHasRoot(parent,memory,region.source,
                            part.offset,part.size,part.fill,part.root_offset))) return false;
                    const auto a=source.physical->Slice(region.source_offset,region.size);
                    const auto b=destination.physical->Slice(region.destination_offset,region.size);
                    if (!a || !b || a->Backing()!=b->Backing() || a->DeviceEpoch()!=b->DeviceEpoch() ||
                        a->DeviceAddress()==b->DeviceAddress() || !std::ranges::equal(a->Segments(),b->Segments())) return false;
                }
                return true;
            };
            if (!qualify()) return {};
            RequestOutsideRenderPassOperationContext();
            if (!qualify()) return {};
            return RecordTransferBatchImpl(parent->resources,commands,std::make_shared<const ReplicaCopyProvenance>(std::move(*plan)),
                parent,predecessor,request.destination_receipt,dependencies);
        } catch (...) { return {}; } // Only pre-record admission/allocation is handled here.
    }
    template <typename Memory>
    Transfer RecordControlledReplicaCopy(const Memory& memory, const ReplicaCopyRequest& request) {
        const auto& parent = request.parent;
        if (!parent || parent->replica_copy || request.source >= parent->resources.size() ||
            request.destination >= parent->resources.size()) return {};
        const auto& source = parent->resources[request.source];
        const auto& destination = parent->resources[request.destination];
        const auto& receipt = request.source_receipt;
        const auto& destination_receipt = request.destination_receipt;
        if (receipt && (!receipt->replica_copy || receipt->replica_anchor != parent ||
            receipt->replica_copy->DestinationBuffer() != request.source || receipt->TargetIndex() >= receipt->resources.size() ||
            receipt->resources[receipt->TargetIndex()].pin.Identity() != source.pin.Identity() ||
            receipt->resources[receipt->TargetIndex()].physical != source.physical)) return {};
        if (destination_receipt && (!destination_receipt->replica_copy || destination_receipt->replica_anchor != parent ||
            destination_receipt->replica_copy->DestinationBuffer() != request.destination || destination_receipt->TargetIndex() >= destination_receipt->resources.size() ||
            destination_receipt->resources[destination_receipt->TargetIndex()].pin.Identity() != destination.pin.Identity() ||
            destination_receipt->resources[destination_receipt->TargetIndex()].pin.Offset() != destination.pin.Offset() ||
            destination_receipt->resources[destination_receipt->TargetIndex()].pin.Size() != destination.pin.Size() ||
            destination_receipt->resources[destination_receipt->TargetIndex()].physical != destination.physical)) return {};
        auto plan = ReplicaCopyProvenance::Build(parent->content, request.source, request.destination,
            request.source_offset, request.destination_offset, request.size, destination.pin.Size(),
            request.source_fill, request.source_root_offset, request.destination_fill, request.destination_root_offset,
            receipt ? receipt->replica_copy : nullptr, request.source_roots,
            destination_receipt ? destination_receipt->replica_copy : nullptr, request.destination_roots);
        if (!plan) return {};
        const auto current_source = [&] {
            for (const auto& part : plan->SourceRoots()) {
                if (!(receipt ? CurrentReplicaCopyHasRoot(receipt, memory, parent, part.offset, part.size,
                                                          part.fill, part.root_offset)
                              : CurrentTransferHasRoot(parent, memory, request.source, part.offset, part.size,
                                                       part.fill, part.root_offset))) return false;
            }
            return true;
        };
        const auto current_destination = [&] {
            try {
                if (!destination.physical->MatchesCurrent(memory)) return false;
                if (destination_receipt || !request.destination_roots.empty()) {
                    if (destination_receipt && !CompletedReplicaCopy(destination_receipt, parent)) return false;
                    for (const auto& part : plan->DestinationRoots()) {
                        if (!(destination_receipt ? CurrentReplicaCopyHasRoot(destination_receipt, memory, parent,
                                                  part.offset, part.size, part.fill, part.root_offset)
                                                 : CurrentTransferHasRoot(parent, memory, request.destination,
                                                  part.offset, part.size, part.fill, part.root_offset))) return false;
                    }
                    return true;
                }
                return CurrentTransferHasRoot(parent, memory, request.destination, 0, destination.pin.Size(),
                                    request.destination_fill, request.destination_root_offset);
            } catch (...) { return false; }
        };
        if (!source.pin.CopyProvenanceEligible() || !destination.pin.CopyProvenanceEligible() ||
            source.pin.Identity() == destination.pin.Identity() ||
            !current_source() ||
            !current_destination()) return {};
        const auto source_physical = source.physical->Slice(request.source_offset, request.size);
        const auto destination_physical = destination.physical->Slice(request.destination_offset, request.size);
        if (!source_physical || !destination_physical ||
            source_physical->Backing() != destination_physical->Backing() ||
            source_physical->DeviceEpoch() != destination_physical->DeviceEpoch() ||
            source_physical->DeviceAddress() == destination_physical->DeviceAddress() ||
            !std::ranges::equal(source_physical->Segments(), destination_physical->Segments())) return {};
        RequestOutsideRenderPassOperationContext();
        if (!current_source() ||
            !current_destination()) return {};
        const std::array<TransferBuffer, 2> buffers{source, destination};
        const std::array<TransferContent::Command, 1> commands{{{TransferContent::Kind::Copy,
            1, 0, request.destination_offset, request.source_offset, request.size}}};
        return RecordTransferBatchImpl(buffers, commands,
            std::make_shared<const ReplicaCopyProvenance>(std::move(*plan)), parent, receipt ? receipt : parent, destination_receipt);
    }
    template <typename Memory>
    bool CurrentReplicaCopyHasRoot(const Transfer& result, const Memory& memory, const Transfer& parent,
        u64 offset, u64 size, TransferContent::Index fill, u64 root_offset) const noexcept {
        if (!result || !CurrentTransferContentRange(result, memory, result->TargetIndex(), offset, size) ||
            !CompletedReplicaCopyHasRoot(result, parent, offset, size, fill, root_offset)) return false;
        // Storage/mapping cancellation spans ancestors, but later byte writes must not rebase copied history.
        size_t hops{};
        try {
            for (auto link = result; link != parent; link = link->replica_parent) {
                if (!link || ++hops > ReplicaCopyProvenance::MaxHops || link->TargetIndex()>=link->resources.size()) return false;
                for (const auto& resource : link->resources)
                    if (!resource.pin.CopyProvenanceEligible() || !resource.physical->MatchesCurrent(memory)) return false;
            }
        } catch (...) {
            return false;
        }
        return true;
    }
    bool CompletedReplicaCopyHasRoot(const Transfer& result, const Transfer& parent,
        u64 offset, u64 size, TransferContent::Index fill, u64 root_offset) const noexcept;
    bool CompletedReplicaCopy(const Transfer& result, const Transfer& parent) const noexcept;

    enum class WriteState { Pending, Submitted, Skipped, Complete, Unknown };
    // Explicit, bounded transfer-only callback, with its own actual checked submission.
    // No previously completed replica or external submission is imported as known content.
    [[nodiscard]] Transfer RecordTransferBatch(std::span<const TransferBuffer> buffers,
                                              std::span<const TransferContent::Command> commands);
    [[nodiscard]] WriteState PollCheckedTransfer(const Transfer& transfer) const noexcept;
    // Explicit diagnostic overloads only. Existing polls and frame routes pay
    // no clock/counter cost. Not a wait, completion certificate or device timer.
    [[nodiscard]] WriteState PollCheckedTransferMeasured(
        const Transfer& transfer, Common::SlowPathMetrics& metrics) const noexcept {
        Common::SlowPathMetrics::Measurement timing{metrics, Common::SlowPathMetrics::Stage::GpuCompletion};
        const auto result = PollCheckedTransfer(transfer);
        if (result == WriteState::Pending || result == WriteState::Submitted) timing.Contended();
        if (result == WriteState::Unknown) timing.Unknown();
        return result;
    }
    [[nodiscard]] const TransferContent* CompletedTransferContent(const Transfer& transfer) const noexcept;
    template <typename Memory>
    const TransferContent* TransferContentForMapping(const Transfer& transfer, const Memory& memory) const noexcept {
        const auto* content = CompletedTransferContent(transfer);
        if (!content) return nullptr;
        try {
            for (const auto& origin : transfer->origins)
                if (!origin->MatchesCurrent(memory)) return nullptr;
        } catch (...) {
            return nullptr;
        }
        return content; // Historical callback-end content, never current/latest authority.
    }
    // Current qualification is separate from immutable historical ancestry.
    template <typename Memory>
    const TransferContent* CurrentTransferContent(const Transfer& transfer, const Memory& memory) const noexcept {
        if (!transfer || transfer->resources.empty()) return nullptr;
        for (size_t i = 0; i < transfer->resources.size(); ++i)
            if (!CurrentTransferContentRange(transfer, memory, i, 0, transfer->resources[i].pin.Size())) return nullptr;
        return transfer->content.get();
    }
    template <typename Memory>
    const TransferContent* CurrentTransferContentRange(const Transfer& transfer, const Memory& memory,
        size_t buffer, u64 offset, u64 size) const noexcept {
        if (!transfer || !transfer->current_eligible) return nullptr;
        if (current_content_uncertain.load(std::memory_order_acquire)) return nullptr;
        if (buffer >= transfer->resources.size() || !transfer->current_ranges[buffer].Contains(offset, size)) return nullptr;
        // Pending is not current evidence, but must not cancel eventual completion.
        const auto transfer_state = PollCheckedTransfer(transfer);
        if (transfer_state != WriteState::Complete) return nullptr;
        const auto* content = TransferContentForMapping(transfer, memory);
        if (!content || std::ranges::any_of(transfer->resources, [](const auto& resource) {
                return !resource.pin.ContentEligible();
            })) {
            transfer->current_eligible = false;
            return nullptr;
        }
        return content; // Scheduler-local snapshot qualification only, never physical authority.
    }
    // Both gates apply to exactly the same bytes; no historical-pointer promotion to a wider interval.
    template <typename Memory>
    bool CurrentTransferHasRoot(const Transfer& transfer, const Memory& memory, size_t buffer,
        u64 offset, u64 size, TransferContent::Index fill, u64 root_offset) const noexcept {
        const auto* content = CurrentTransferContentRange(transfer, memory, buffer, offset, size);
        return content && content->HasRoot(static_cast<TransferContent::Index>(buffer), offset, size, fill, root_offset);
    }

    // Typed commands bind exact destination cancellation to the actual Vulkan operation.
    // Other recording APIs remain unclassified and revoke all current eligibility.
    void RecordBufferFill(VkBuffer destination, u64 offset, u64 size, u32 value);
    void RecordBufferCopy(VkBuffer destination, VkBuffer source, std::span<const VkBufferCopy> copies,
                          bool barrier, bool upload = false);
    class CheckedWrite {
        friend class Scheduler;
    public:
        const void* ResourceIdentity() const noexcept { return resources ? identity : nullptr; }
        const void* SubmissionIdentity() const noexcept { return submission.get(); }
        u64 Offset() const noexcept { return offset; }
        u64 Size() const noexcept { return size; }
        u64 IntentRevision() const noexcept { return intent_revision; }
        const auto& HistoricalPhysicalIdentity() const noexcept { return physical; }
        bool PhysicalCoverageComplete() const noexcept { return coverage && coverage->Complete(); }
    private:
        enum class Emission { Pending, Recorded, Skipped, Unknown };
        CheckedWrite(std::shared_ptr<const void> resources_, const void* identity_, u64 offset_,
                     u64 size_, u64 intent_revision_, std::shared_ptr<std::atomic_bool> owner_)
            : resources{std::move(resources_)}, identity{identity_}, offset{offset_}, size{size_},
              intent_revision{intent_revision_}, owner{std::move(owner_)} {}
        std::shared_ptr<const void> resources;
        const void* const identity;
        const u64 offset, size, intent_revision;
        const std::shared_ptr<std::atomic_bool> owner;
        Submission submission;
        std::shared_ptr<const VideoCommon::PhysicalWriteIdentity> physical;
        std::shared_ptr<VideoCommon::PhysicalWriteCoverage> coverage;
        std::atomic<Emission> emission{Emission::Pending};
        bool retired{};
        SyncProvenance::Access access;
        SyncProvenance::Position position{};
    };
    using Write = std::shared_ptr<CheckedWrite>;
    struct WriteFootprint {
        std::shared_ptr<const void> resources;
        const void* identity;
        u64 offset, size, intent_revision;
        std::shared_ptr<const VideoCommon::PhysicalWriteIdentity> physical;
        SyncProvenance::Access access{};
    };
    using ObservedWrites = std::shared_ptr<const std::vector<Write>>;

    // Diagnostic storage projection. Uses the existing batch, without flushing.
    // Null means observation refused; the original command is still recorded once.
    [[nodiscard]] ObservedWrites RecordObservedWrites(std::span<const WriteFootprint> footprints,
        std::function<bool(vk::CommandBuffer)> command,
        std::shared_ptr<VideoCommon::PhysicalWriteCoverage> coverage = {});
    // Closed complete scope + checked historical execution only, never current authority.
    [[nodiscard]] const VideoCommon::PhysicalWriteIdentity* CompletedPhysicalIdentity(
        const Write& write) const noexcept;

    // Direct same-submission memory dependency for this historical physical interval.
    // False means Unknown, never proof of an unordered execution or of latest content.
    [[nodiscard]] bool HasConfirmedMemoryDependency(const Write& before, const Write& after,
                                                    u64 physical_offset, u64 size) const noexcept;
    // Outside a render pass. Metadata is recorded alongside the actual global barrier.
    void RecordProvenanceBarrier(VkPipelineStageFlags source, VkPipelineStageFlags destination,
                                 const VkMemoryBarrier& memory);

    // Explicit isolated producer only. Record and flush are inseparable: the returned
    // identity is the submission containing this command, not a later timeline tick.
    // The worker callback returns false only when it emitted no write commands.
    [[nodiscard]] Write RecordCheckedWrite(std::shared_ptr<const void> resources,
        const void* identity, u64 offset, u64 size, u64 intent_revision,
        std::function<bool(vk::CommandBuffer)> command);
    [[nodiscard]] WriteState PollCheckedWrite(const Write& write) const noexcept;
    [[nodiscard]] WriteState PollCheckedWriteMeasured(
        const Write& write, Common::SlowPathMetrics& metrics) const noexcept {
        Common::SlowPathMetrics::Measurement timing{metrics, Common::SlowPathMetrics::Stage::GpuCompletion};
        const auto result = PollCheckedWrite(write);
        if (result == WriteState::Pending || result == WriteState::Submitted) timing.Contended();
        if (result == WriteState::Unknown) timing.Unknown();
        return result;
    }
    // Caller has joined producers and successfully waited for real device idle.
    // False leaves every holder intact; callers must not retire dependent cache holders.
    bool RetireCheckedWritesAfterDeviceIdle();

    explicit Scheduler(const Device& device, StateTracker& state_tracker);
    ~Scheduler();

    /// Sends the current execution context to the GPU.
    NativeSourceRetainer<Submission>::Scope BeginPresentationSources();
    void RetainPresentationSource(std::shared_ptr<const void> owner);
    void TrackPresentationSources();
    void CollectPresentationSources();
    bool RetirePresentationSourcesAfterDeviceIdle();
    u64 Flush(VkSemaphore signal_semaphore = nullptr, VkSemaphore wait_semaphore = nullptr);

    [[nodiscard]] Submission FlushChecked();
    [[nodiscard]] VkResult PollChecked(const Submission& submission) const noexcept;

    /// Sends the current execution context to the GPU and waits for it to complete.
    void Finish(VkSemaphore signal_semaphore = nullptr, VkSemaphore wait_semaphore = nullptr);

    /// Waits for the worker thread to finish executing everything. After this function returns it's
    /// safe to touch worker resources.
    void WaitWorker();

    /// Owner-thread teardown after all recording producers stop. Drain host work and join.
    void Shutdown();

    /// Sends currently recorded work to the worker thread.
    void DispatchWork();

    /// Requests to begin a renderpass.
    void RequestRenderpass(const Framebuffer* framebuffer);

    /// Requests the current execution context to be able to execute operations only allowed outside
    /// of a renderpass.
    void RequestOutsideRenderPassOperationContext();

    /// Same, for an operation that records a barrier before and after its own writes (reads and
    /// writes of earlier work, then everything later), so it leaves no writes for a later
    /// WaitForIdle to make visible.
    void RequestOutsideRenderPassSyncedOperationContext();

    /// WAIT_FOR_IDLE: later work runs after earlier work and sees its writes. Inside a render pass
    /// with no writes besides attachments since the last such barrier, the barrier is deferred to
    /// the next operation outside the render pass or the next draw that writes memory, and the
    /// render pass goes on.
    void WaitForIdle(VkPipelineStageFlags src_stages);

    /// True while a WaitForIdle barrier is deferred. A draw that writes memory must end the render
    /// pass first (RequestOutsideRenderPassOperationContext records the barrier).
    [[nodiscard]] bool HasDeferredWaitForIdle() const noexcept {
        return deferred_wfi_stages != 0;
    }

    /// True while recorded commands are inside a render pass.
    [[nodiscard]] bool IsRenderPassActive() const noexcept {
        return state.renderpass != nullptr;
    }

    /// Records that commands may have written memory other than render pass attachments.
    void NoteMemoryWrites() noexcept {
        unsynced_writes = true;
    }

    /// Update the pipeline to the current execution context.
    bool UpdateGraphicsPipeline(GraphicsPipeline* pipeline);

    /// Update the rescaling state. Returns true if the state has to be updated.
    bool UpdateRescaling(bool is_rescaling);

    /// Invalidates current command buffer state except for render passes
    void InvalidateState();

    /// Assigns the query cache.
    void SetQueryCache(VideoCommon::QueryCacheBase<QueryCacheParams>& query_cache_);

    // Registers a callback to perform on queue submission.
    void RegisterOnSubmit(std::function<void()>&& func) {
        on_submit = std::move(func);
    }

    /// Send work to a separate thread.
    template <typename T>
        requires std::is_invocable_v<T, vk::CommandBuffer, vk::CommandBuffer>
    void RecordWithUploadBuffer(T&& command) {
        TrackPresentationSources();
        RevokeTransferContentEligibility();
        EnqueueCommand(std::forward<T>(command));
    }

    // GPU/cache-thread only, before possible effects. Invalidates all retained
    // current gates intersecting the saved physical write. No authority/lease.
    [[nodiscard]] bool InvalidatePhysicalWrite(
        const VideoCommon::PhysicalWriteIdentity& writer, u64 offset, u64 size) noexcept;

    // Explicit isolated harness endpoint. Retained resource ownership is required;
    // the caller must independently exclude other producers through completion.
    // Returned transfer owns this new effect through existing retirement rules;
    // it is NOT completed or admitted for ordinary production.
    template <typename Memory>
    Transfer RecordControlledPhysicalFill(const Transfer& transfer, const Memory& memory,
                                         u32 buffer, u64 offset, u64 size, u32 value) {
        if (!transfer || transfer->owner != submission_owner || transfer->retired ||
            buffer >= transfer->resources.size()) return {};
        if (PollCheckedTransfer(transfer) != WriteState::Complete ||
            current_content_uncertain.load(std::memory_order_acquire)) {
            RevokeTransferContentEligibility();
            return {};
        }
        const auto& resource = transfer->resources[buffer];
        const auto& pin = resource.pin;
        const auto& physical = resource.physical;
        if (!pin.IsUsable() || !pin.CopyProvenanceEligible() || !physical ||
            physical->Representation() != pin.Identity() || physical->NativeOffset() != pin.Offset() ||
            physical->Size() != pin.Size() || !physical->MatchesCurrent(memory) ||
            !size || offset >= pin.Size() || size > pin.Size() - offset ||
            ((pin.Offset() + offset) | size) % 4) {
            RevokeTransferContentEligibility();
            return {};
        }
        if (!InvalidatePhysicalWrite(*physical, offset, size)) return {};
        const std::array<TransferBuffer, 1> buffers{{resource}};
        const std::array<TransferContent::Command, 1> commands{{
            {TransferContent::Kind::Fill, 0, 0, offset, 0, size, value}}};
        return RecordTransferBatch(buffers, commands);
    }

    // GPU/cache-thread only. Unclassified reads/disjoint operations also conservatively cancel.
private:
    void RevokeBufferContent(VkBuffer destination, u64 offset, u64 size) noexcept;
    Transfer RecordTransferBatchImpl(std::span<const TransferBuffer> buffers,
        std::span<const TransferContent::Command> commands,
        std::shared_ptr<const ReplicaCopyProvenance> replica_copy, Transfer parent, Transfer predecessor = {},
        Transfer destination_history = {}, std::span<const Transfer> dependencies = {},
        std::shared_ptr<PrivateProducerGuard> exclusion = {});
    void RevokeTransferContentEligibility() noexcept {
        for (const auto& transfer : checked_transfers) transfer->current_eligible = false;
    }

    // Submission closure only: no new data producer. Other callbacks use ordinary Record.
    template <typename T>
    void EnqueueCommand(T&& command) {
        if (chunk->Record(command)) {
            return;
        }
        DispatchWork();
        (void)chunk->Record(command);
    }

public:
    template <typename T>
        requires std::is_invocable_v<T, vk::CommandBuffer>
    void Record(T&& c) {
        this->RecordWithUploadBuffer(
            [command = std::move(c)](vk::CommandBuffer cmdbuf, vk::CommandBuffer) {
                command(cmdbuf);
            });
    }

    /// Returns the current command buffer tick.
    [[nodiscard]] u64 CurrentTick() const noexcept {
        return master_semaphore->CurrentTick();
    }

    /// Returns true when a tick has been triggered by the GPU.
    [[nodiscard]] bool IsFree(u64 tick) const noexcept {
        return master_semaphore->IsFree(tick);
    }

    // GPU owner thread only; these checks do not acquire an independent producer lease.
    // Layer destruction may skip its cached tick only after terminal checked disposal.
    void WaitForResourceRelease(u64 tick);
    void RequireNativeBorrowAdmission() const;
    // One existing Finish submission, host recording drain, then checked native lifetime.
    // Call inside a handler which retains all stack-borrowed native resources on error.
    void FinishNativeBorrow();

    /// Waits for the given tick to trigger on the GPU.
    void Wait(u64 tick) {
        if (tick >= master_semaphore->CurrentTick()) {
            // Make sure we are not waiting for the current tick without signalling
            Flush();
        }
        master_semaphore->Wait(tick);
    }

    /// Returns the master timeline semaphore.
    [[nodiscard]] MasterSemaphore& GetMasterSemaphore() const noexcept {
        return *master_semaphore;
    }

    CITRON_PROFILE_LOCKABLE(std::mutex, submit_mutex);

private:
    class Command {
    public:
        virtual ~Command() = default;

        virtual void Execute(vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) const = 0;

        Command* GetNext() const {
            return next;
        }

        void SetNext(Command* next_) {
            next = next_;
        }

    private:
        Command* next = nullptr;
    };

    template <typename T>
    class TypedCommand final : public Command {
    public:
        explicit TypedCommand(T&& command_) : command{std::move(command_)} {}
        ~TypedCommand() override = default;

        TypedCommand(TypedCommand&&) = delete;
        TypedCommand& operator=(TypedCommand&&) = delete;

        void Execute(vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf) const override {
            command(cmdbuf, upload_cmdbuf);
        }

    private:
        T command;
    };

    class CommandChunk final {
    public:
        void ExecuteAll(vk::CommandBuffer cmdbuf, vk::CommandBuffer upload_cmdbuf);

        template <typename T>
        bool Record(T& command) {
            using FuncType = TypedCommand<T>;
            static_assert(sizeof(FuncType) < sizeof(data), "Lambda is too large");

            command_offset = Common::AlignUp(command_offset, alignof(FuncType));
            if (command_offset > sizeof(data) - sizeof(FuncType)) {
                return false;
            }
            Command* const current_last = last;
            last = new (data.data() + command_offset) FuncType(std::move(command));

            if (current_last) {
                current_last->SetNext(last);
            } else {
                first = last;
            }
            command_offset += sizeof(FuncType);
            return true;
        }

        void MarkSubmit() {
            submit = true;
        }

        bool Empty() const {
            return command_offset == 0;
        }

        bool HasSubmit() const {
            return submit;
        }

    private:
        Command* first = nullptr;
        Command* last = nullptr;

        // A full chunk is handed to the worker thread, and every hand over wakes the worker.
        // A busy scene fills a small chunk every twenty draws or so, which is thousands of
        // wake-ups a second, so Android gets larger chunks.
#ifdef ANDROID
        static constexpr size_t CHUNK_SIZE = 0x20000;
#else
        static constexpr size_t CHUNK_SIZE = 0x8000;
#endif

        size_t command_offset = 0;
        bool submit = false;
        alignas(std::max_align_t) std::array<u8, CHUNK_SIZE> data{};
    };

    struct State {
        VkRenderPass renderpass = nullptr;
        VkFramebuffer framebuffer = nullptr;
        VkExtent2D render_area = {0, 0};
        GraphicsPipeline* graphics_pipeline = nullptr;
        bool is_rescaling = false;
        bool rescaling_defined = false;
    };

    void WorkerThread(std::stop_token stop_token);

    void AllocateWorkerCommandBuffer();

    u64 SubmitExecution(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore,
                        Submission checked = {});

    void AllocateNewContext();

    void EndPendingOperations();

    void EndRenderPass();

    /// Records the deferred WaitForIdle barrier, if any. Outside a render pass only.
    void RecordDeferredWaitForIdle();

    void AcquireNewChunk();

    const Device& device;
    const std::shared_ptr<std::atomic_bool> submission_owner{
        std::make_shared<std::atomic_bool>(true)};
    NativeSourceRetainer<Submission> presentation_sources{[this] {
        return Submission{new CheckedSubmission{submission_owner}};
    }};
    bool native_resources_retired{}; // terminal owner only, never an admission/content token
    StateTracker& state_tracker;

    std::unique_ptr<MasterSemaphore> master_semaphore;
    std::unique_ptr<CommandPool> command_pool;

    VideoCommon::QueryCacheBase<QueryCacheParams>* query_cache = nullptr;

    vk::CommandBuffer current_cmdbuf;
    vk::CommandBuffer current_upload_cmdbuf;

    std::unique_ptr<CommandChunk> chunk;
    std::function<void()> on_submit;
    std::vector<Write> checked_writes;
    std::vector<Transfer> checked_transfers;
    std::atomic_bool current_content_uncertain{}; // Sticky submission failure; no worker mutation of range sets.
    Submission observed_submission;
    std::weak_ptr<CheckedSubmission> last_accepted_submission; // Worker-only, actual owned graphics queue, no ranking.

    State state;

    /// Memory other than attachments may have been written since the last full memory barrier.
    /// True to start with: the scheduler does not know what came before it.
    bool unsynced_writes = true;

    /// Source stages of WaitForIdle barriers not yet recorded, 0 when none is deferred.
    VkPipelineStageFlags deferred_wfi_stages = 0;

    u32 num_renderpass_images = 0;
    std::array<VkImage, 9> renderpass_images{};
    std::array<VkImageSubresourceRange, 9> renderpass_image_ranges{};

    std::queue<std::unique_ptr<CommandChunk>> work_queue;
    std::vector<std::unique_ptr<CommandChunk>> chunk_reserve;
    std::mutex execution_mutex;
    std::mutex reserve_mutex;
    std::mutex queue_mutex;
    std::condition_variable_any event_cv;
    std::jthread worker_thread;
};

} // namespace Vulkan
