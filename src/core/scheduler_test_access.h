// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Linked only by CITRON_TESTS builds. These peers select real owners/locks; they do not
// replace scheduler, store, tracker, GPU queue, completion or process finalization bodies.
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/core.h"
#include "core/scheduler_system_test_access.h"
#include "core/memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/physical_core.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Core {
struct SchedulerTestAccess : SystemTestAccess {
    static auto EnterControlledMapping(Memory::Memory& memory) {
        return memory.mapping_publications.Begin();
    }
    static auto EnterGuestExecution(Kernel::PhysicalCore& core) {return core.guest_execution.TryEnter();}
    static bool BackendRunning(Kernel::PhysicalCore& core) {
        std::scoped_lock lock{core.m_guard};
        return core.m_arm_interface != nullptr;
    }
    static bool VisitManagedMemory(Memory::Memory& memory, const std::function<void(Memory::Memory&)>& visit) {
        return memory.controlled_owner->TryVisit(visit);
    }
    template <typename Prepare>
    static auto PrepareManagedMemory(Memory::Memory& memory, Prepare&& prepare) {
        return memory.controlled_owner->TryPrepare(std::forward<Prepare>(prepare));
    }
    static auto& Cache(Vulkan::RasterizerVulkan& rasterizer) { return rasterizer.buffer_cache; }
    static auto& CounterGuard(Tegra::MaxwellDeviceMemoryManager& memory) {
        return memory.counter_guard;
    }
    static void StartProcess(Kernel::KProcess& process) {
        // Same running reference/state transition as Run(), with a service fiber entrypoint
        // instead of executing an ARM application image. All termination/finalization is real.
        process.Open();
        process.ChangeState(Kernel::KProcess::State::Running);
    }
    static void FillGpu(Vulkan::RasterizerVulkan& rasterizer,
                        const Vulkan::RasterizerVulkan::ControlledRegistrationPtr& op,
                        DAddr address, u64 size) {
        auto& cache = Cache(rasterizer);
        const auto id = cache.ControlledReadyBuffer(op);
        auto& buffer = cache.slot_buffers[id];
        rasterizer.buffer_cache_runtime.ClearBuffer(buffer.Handle(), 0, size, 0x63636363);
        cache.memory_tracker.MarkRegionAsGpuModified(address, size);
        cache.gpu_modified_ranges.Add(address, size);
    }
    static void LegacyPublish(Vulkan::RasterizerVulkan& rasterizer, DAddr address) {
        Cache(rasterizer).memory_tracker.MarkRegionAsCpuModified(address, 1);
    }
    static bool QueueLegacyReadback(Vulkan::RasterizerVulkan& rasterizer,
                                   const Vulkan::RasterizerVulkan::ControlledRegistrationPtr& op,
                                   DAddr address, u32 size) {
        auto& cache = Cache(rasterizer);
        const auto id = cache.ControlledReadyBuffer(op);
        cache.MarkWrittenBuffer(id, address, size);
        cache.CommitAsyncFlushes();
        return cache.ShouldWaitAsyncFlushes();
    }
    static void RetireLegacyReadback(Vulkan::RasterizerVulkan& rasterizer) {
        rasterizer.scheduler.Finish();
        Cache(rasterizer).PopAsyncFlushes();
    }
    static unsigned ProbeLegacyReadbackGuards(
        Vulkan::RasterizerVulkan& rasterizer,
        const Vulkan::RasterizerVulkan::ControlledRegistrationPtr& op,
        DAddr address, u32 size) {
        auto& cache = Cache(rasterizer);
        const auto id = cache.ControlledBufferId(op, false, true);
        unsigned blocked{};
        const auto probe = [&](auto&& operation) {
            try { operation(); } catch (const std::runtime_error&) { ++blocked; }
        };
        probe([&] { cache.DownloadMemory(address, size); });
        probe([&] { cache.DownloadBufferMemory(cache.slot_buffers[id], address, size); });
        const u8 stale = 10;
        probe([&] { cache.WriteBackDownload(address, &stale, 1); });
        // Deliberately attempt a legacy scheduling violation. Rejection must precede
        // AccumulateFlushes, so the test can remove its untouched candidate afterwards.
        cache.uncommitted_gpu_modified_ranges.Add(address, 1);
        probe([&] { cache.CommitAsyncFlushes(); });
        if (cache.uncommitted_gpu_modified_ranges.Empty()) return 0;
        cache.uncommitted_gpu_modified_ranges.Subtract(address, 1);
        return blocked;
    }
    // M4 observation helpers: real ordinary cache methods and Vulkan transfers. These
    // select/observe resources; they provide no production coherency or eligibility policy.
    static Vulkan::StagedBufferPin OrdinaryPin(Vulkan::RasterizerVulkan& rasterizer,
                                              DAddr address, u32 size) {
        auto& cache = Cache(rasterizer);
        std::scoped_lock lock{cache.mutex};
        auto [buffer, offset] = cache.ObtainCPUBuffer(address, size,
            VideoCommon::ObtainBufferSynchronize::FullSynchronize,
            VideoCommon::ObtainBufferOperation::DoNothing);
        auto pin = buffer->PinStagedRepresentation(offset, size);
        if (!pin) throw std::runtime_error("ordinary staged observation pin unavailable");
        return *pin;
    }
    static std::vector<u8> ReadPin(Vulkan::RasterizerVulkan& rasterizer,
                                   const Vulkan::StagedBufferPin& pin) {
        auto& runtime = rasterizer.buffer_cache_runtime;
        auto staging = runtime.DownloadStagingBuffer(pin.Size());
        const std::array<VideoCommon::BufferCopy, 1> copies{{
            {.src_offset = pin.Offset(), .dst_offset = staging.offset, .size = pin.Size()}}};
        runtime.CopyBuffer(staging.buffer, pin.Handle(), copies, true);
        rasterizer.scheduler.Finish(); // No cache, mapping, PTE or execution lease held.
        return {staging.mapped_span.begin(), staging.mapped_span.begin() + pin.Size()};
    }
    static void FillPinRange(Vulkan::RasterizerVulkan& rasterizer, const Vulkan::StagedBufferPin& pin,
                             u32 offset, u32 size, u32 value) {
        if (pin.Offset() > std::numeric_limits<u32>::max() - offset)
            throw std::runtime_error("ordinary fill native offset overflow");
        rasterizer.buffer_cache_runtime.ClearBuffer(pin.Handle(), static_cast<u32>(pin.Offset()) + offset, size, value);
    }
    static void CopyPinRange(Vulkan::RasterizerVulkan& rasterizer, const Vulkan::StagedBufferPin& destination,
                             const Vulkan::StagedBufferPin& source, u64 offset, u64 size) {
        auto& runtime = rasterizer.buffer_cache_runtime;
        const std::array<VideoCommon::BufferCopy, 1> copies{{{source.Offset() + offset, destination.Offset() + offset, size}}};
        runtime.PreCopyBarrier();
        runtime.CopyBuffer(destination.Handle(), source.Handle(), copies, false, false);
        runtime.PostCopyBarrier();
    }
    static std::shared_ptr<Vulkan::Buffer> IsolatedContentBuffer(Vulkan::RasterizerVulkan& rasterizer,
                                                                DAddr address, u64 size) {
        // No cache registration, uploads, host mappings or ordinary-game selection can find this allocation.
        return std::make_shared<Vulkan::Buffer>(rasterizer.buffer_cache_runtime, address, size);
    }
    static Vulkan::Scheduler::TransferBuffer IsolatedContentTarget(Vulkan::RasterizerVulkan& rasterizer,
        const std::shared_ptr<Vulkan::Buffer>& buffer, u64 size) {
        const auto pin = buffer->PinStagedRepresentation(0, size);
        const auto physical = rasterizer.buffer_cache_runtime.PhysicalIdentityForBinding(*buffer, 0, size);
        if (!pin || !physical || !pin->CopyProvenanceEligible()) throw std::runtime_error("unqualified isolated copy target");
        return {*pin, physical};
    }
    static void WriteOrdinaryGpu(Vulkan::RasterizerVulkan& rasterizer,
                                 DAddr address, u32 size, u32 value) {
        auto& cache = Cache(rasterizer);
        std::optional<Vulkan::StagedBufferPin> pin;
        {
            std::scoped_lock lock{cache.mutex};
            auto [buffer, offset] = cache.ObtainCPUBuffer(address, size,
                VideoCommon::ObtainBufferSynchronize::FullSynchronize,
                VideoCommon::ObtainBufferOperation::MarkAsWritten);
            pin = buffer->PinStagedRepresentation(offset, size);
            if (!pin) throw std::runtime_error("ordinary GPU writer pin unavailable");
            rasterizer.buffer_cache_runtime.ClearBuffer(pin->Handle(), offset, size, value);
        }
        rasterizer.scheduler.Finish();
    }
    static Vulkan::Scheduler::Write CheckedOrdinaryFill(Vulkan::RasterizerVulkan& rasterizer,
        DAddr address, u32 size, u32 value, std::function<bool()> emit = {}) {
        auto& cache = Cache(rasterizer);
        std::optional<Vulkan::StagedBufferPin> pin;
        std::optional<u64> revision;
        {
            std::scoped_lock lock{cache.mutex};
            auto [buffer, offset] = cache.ObtainCPUBuffer(address, size,
                VideoCommon::ObtainBufferSynchronize::FullSynchronize,
                VideoCommon::ObtainBufferOperation::MarkAsWritten);
            pin = buffer->PinStagedRepresentation(offset, size);
            revision = buffer->gpu_write_revision.Snapshot();
        }
        if (!pin || !revision) throw std::runtime_error("checked writer pin or intent unavailable");
        return rasterizer.buffer_cache_runtime.ClearBufferChecked(std::move(*pin), *revision, value, std::move(emit));
    }
    static void RetireOrdinary(Vulkan::RasterizerVulkan& rasterizer, DAddr address) {
        auto& cache = Cache(rasterizer);
        std::scoped_lock lock{cache.mutex};
        cache.DeleteBuffer(cache.FindBuffer(address, 1));
    }
    static Vulkan::Scheduler::TransferBuffer ContentTransferTarget(
        Vulkan::RasterizerVulkan& rasterizer, DAddr address, u32 size) {
        auto& cache = Cache(rasterizer);
        std::scoped_lock lock{cache.mutex};
        auto [buffer, offset] = cache.ObtainCPUBuffer(address, size,
            VideoCommon::ObtainBufferSynchronize::FullSynchronize,
            VideoCommon::ObtainBufferOperation::MarkAsWritten);
        auto pin = buffer->PinStagedRepresentation(offset, size);
        auto physical = rasterizer.buffer_cache_runtime.PhysicalIdentityForBinding(*buffer, offset, size);
        if (!pin || !physical) throw std::runtime_error("content transfer origin unavailable");
        return {*pin, physical};
    }
    // Exercise the ordinary batch observer with real transfer commands. This is not
    // an actual shader binding test; extracted Draw/Dispatch fixtures cover that wiring.
    static Vulkan::Scheduler::Write ObserveOrdinaryFill(Vulkan::RasterizerVulkan& rasterizer,
        DAddr address, u32 size, u32 value,
        std::shared_ptr<VideoCommon::PhysicalWriteCoverage> coverage = {}) {
        auto& cache = Cache(rasterizer);
        std::optional<Vulkan::StagedBufferPin> pin;
        std::optional<u64> revision;
        std::shared_ptr<const VideoCommon::PhysicalWriteIdentity> physical;
        {
            std::scoped_lock lock{cache.mutex};
            auto [buffer, offset] = cache.ObtainCPUBuffer(address, size,
                VideoCommon::ObtainBufferSynchronize::FullSynchronize,
                VideoCommon::ObtainBufferOperation::MarkAsWritten);
            pin = buffer->PinStagedRepresentation(offset, size);
            revision = buffer->gpu_write_revision.Snapshot();
            physical = rasterizer.buffer_cache_runtime.PhysicalIdentityForBinding(*buffer, offset, size);
        }
        if (!pin || !revision) throw std::runtime_error("observed fill pin or intent unavailable");
        const std::array<Vulkan::Scheduler::WriteFootprint, 1> ranges{{
            {pin->RetainedOwner(), pin->Identity(), pin->Offset(), pin->Size(), *revision, physical,
             {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT}}}};
        auto& scheduler = rasterizer.scheduler;
        scheduler.RequestOutsideRenderPassOperationContext();
        const auto writes = scheduler.RecordObservedWrites(ranges,
            [bound_pin = *pin, value](Vulkan::vk::CommandBuffer cmdbuf) {
                // Serialize the test's consecutive fills; submission identity alone
                // does not supply Vulkan memory/execution dependencies.
                const VkMemoryBarrier barrier{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                };
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, barrier);
                cmdbuf.FillBuffer(bound_pin.Handle(), bound_pin.Offset(), bound_pin.Size(), value);
                return true;
            }, std::move(coverage));
        if (!writes || writes->size() != 1) throw std::runtime_error("observed fill refused");
        return writes->front();
    }
};
} // namespace Core
