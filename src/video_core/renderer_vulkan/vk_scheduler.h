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
#include "video_core/buffer_cache/physical_write_identity.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_buffer_payload.h"
#include "video_core/renderer_vulkan/vk_sync_provenance.h"
#include "video_core/renderer_vulkan/vk_transfer_content.h"
#include "video_core/renderer_vulkan/vk_transfer_eligibility.h"
#include "video_core/renderer_vulkan/vk_replica_copy_provenance.h"
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
        explicit CheckedTransfer(std::shared_ptr<std::atomic_bool> owner_) : owner{std::move(owner_)} {}
        enum class Emission { Pending, Recorded, Unknown };
        const std::shared_ptr<std::atomic_bool> owner;
        std::vector<TransferBuffer> resources;
        std::vector<std::shared_ptr<const VideoCommon::PhysicalWriteIdentity>> origins;
        std::shared_ptr<const TransferContent> content;
        std::shared_ptr<const ReplicaCopyProvenance> replica_copy;
        std::shared_ptr<CheckedTransfer> replica_parent;
        std::shared_ptr<CheckedTransfer> replica_anchor;
        Submission submission;
        std::array<SyncProvenance::Position, TransferContent::MaxCommands> positions{};
        std::atomic<Emission> emission{Emission::Pending};
        std::atomic_bool worker_pending{};
        bool current_eligible{}; // GPU/cache-thread cancellation only, never an execution version.
        std::array<TransferEligibility, TransferContent::MaxBuffers> current_ranges;
        bool retired{};
    public:
        // Planned metadata is inspectable before execution, never completed evidence.
        const auto& PlannedContent() const noexcept { return content; }
        const auto& HistoricalOrigins() const noexcept { return origins; }
        const auto& PlannedReplicaCopy() const noexcept { return replica_copy; }
        bool ResourcesRetained() const noexcept { return !resources.empty(); }
    };
    using Transfer = std::shared_ptr<CheckedTransfer>;
    struct ReplicaCopyRequest {
        Transfer parent;
        TransferContent::Index source{}, destination{};
        u64 source_offset{}, destination_offset{}, size{};
        TransferContent::Index source_fill{}, destination_fill{};
        u64 source_root_offset{}, destination_root_offset{};
        Transfer source_receipt{}; // Optional prior copy; root IDs remain in parent, the original anchor.
    };
    template <typename Memory>
    Transfer RecordControlledReplicaCopy(const Memory& memory, const ReplicaCopyRequest& request) {
        const auto& parent = request.parent;
        if (!parent || parent->replica_copy || request.source >= parent->resources.size() ||
            request.destination >= parent->resources.size()) return {};
        const auto& source = parent->resources[request.source];
        const auto& destination = parent->resources[request.destination];
        const auto& receipt = request.source_receipt;
        if (receipt && (!receipt->replica_copy || receipt->replica_anchor != parent ||
            receipt->replica_copy->DestinationBuffer() != request.source || receipt->resources.size() != 2 ||
            receipt->resources[1].pin.Identity() != source.pin.Identity() ||
            receipt->resources[1].physical != source.physical)) return {};
        const auto current_source = [&] {
            return receipt ? CurrentReplicaCopyHasRoot(receipt, memory, parent, request.source_offset, request.size,
                                                       request.source_fill, request.source_root_offset)
                           : CurrentTransferHasRoot(parent, memory, request.source, request.source_offset, request.size,
                                                    request.source_fill, request.source_root_offset);
        };
        if (!source.pin.CopyProvenanceEligible() || !destination.pin.CopyProvenanceEligible() ||
            source.pin.Identity() == destination.pin.Identity() ||
            !current_source() ||
            !CurrentTransferHasRoot(parent, memory, request.destination, 0, destination.pin.Size(),
                                    request.destination_fill, request.destination_root_offset)) return {};
        const auto source_physical = source.physical->Slice(request.source_offset, request.size);
        const auto destination_physical = destination.physical->Slice(request.destination_offset, request.size);
        if (!source_physical || !destination_physical ||
            source_physical->Backing() != destination_physical->Backing() ||
            source_physical->DeviceEpoch() != destination_physical->DeviceEpoch() ||
            source_physical->DeviceAddress() == destination_physical->DeviceAddress() ||
            !std::ranges::equal(source_physical->Segments(), destination_physical->Segments())) return {};
        auto plan = ReplicaCopyProvenance::Build(parent->content, request.source, request.destination,
            request.source_offset, request.destination_offset, request.size, destination.pin.Size(),
            request.source_fill, request.source_root_offset, request.destination_fill, request.destination_root_offset,
            receipt ? receipt->replica_copy : nullptr);
        if (!plan) return {};
        RequestOutsideRenderPassOperationContext();
        if (!current_source() ||
            !CurrentTransferHasRoot(parent, memory, request.destination, 0, destination.pin.Size(),
                                    request.destination_fill, request.destination_root_offset)) return {};
        const std::array<TransferBuffer, 2> buffers{source, destination};
        const std::array<TransferContent::Command, 1> commands{{{TransferContent::Kind::Copy,
            1, 0, request.destination_offset, request.source_offset, request.size}}};
        return RecordTransferBatchImpl(buffers, commands,
            std::make_shared<const ReplicaCopyProvenance>(std::move(*plan)), parent, receipt ? receipt : parent);
    }
    template <typename Memory>
    bool CurrentReplicaCopyHasRoot(const Transfer& result, const Memory& memory, const Transfer& parent,
        u64 offset, u64 size, TransferContent::Index fill, u64 root_offset) const noexcept {
        if (!result || !CurrentTransferContentRange(result, memory, 1, offset, size) ||
            !CompletedReplicaCopyHasRoot(result, parent, offset, size, fill, root_offset)) return false;
        // Storage/mapping cancellation spans ancestors, but later byte writes must not rebase copied history.
        size_t hops{};
        try {
            for (auto link = result; link != parent; link = link->replica_parent) {
                if (!link || ++hops > ReplicaCopyProvenance::MaxHops || link->resources.size() != 2) return false;
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

    enum class WriteState { Pending, Submitted, Skipped, Complete, Unknown };
    // Explicit, bounded transfer-only callback, with its own actual checked submission.
    // No previously completed replica or external submission is imported as known content.
    [[nodiscard]] Transfer RecordTransferBatch(std::span<const TransferBuffer> buffers,
                                              std::span<const TransferContent::Command> commands);
    [[nodiscard]] WriteState PollCheckedTransfer(const Transfer& transfer) const noexcept;
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
    // Caller has joined producers and successfully waited for real device idle.
    void RetireCheckedWritesAfterDeviceIdle();

    explicit Scheduler(const Device& device, StateTracker& state_tracker);
    ~Scheduler();

    /// Sends the current execution context to the GPU.
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
        RevokeTransferContentEligibility();
        EnqueueCommand(std::forward<T>(command));
    }

    // GPU/cache-thread only. Unclassified reads/disjoint operations also conservatively cancel.
private:
    void RevokeBufferContent(VkBuffer destination, u64 offset, u64 size) noexcept;
    Transfer RecordTransferBatchImpl(std::span<const TransferBuffer> buffers,
        std::span<const TransferContent::Command> commands,
        std::shared_ptr<const ReplicaCopyProvenance> replica_copy, Transfer parent, Transfer predecessor = {});
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
