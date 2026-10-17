// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>

#include <boost/container/static_vector.hpp>

#include "common/common_types.h"
#include "video_core/control/channel_state_cache.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/blit_image.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_fence_manager.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_query_cache.h"
#include "video_core/renderer_vulkan/vk_render_pass_cache.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Core {
class System;
}

namespace Core::Frontend {
class EmuWindow;
}

namespace Tegra {

namespace Engines {
class Maxwell3D;
}

} // namespace Tegra

namespace Vulkan {
struct ControlledAliasCopy;
enum class ControlledAliasPhase { Prepared, Parked, Submitted, Complete, Refused, Unknown };

struct FramebufferTextureInfo;

class StateTracker;

class AccelerateDMA : public Tegra::Engines::AccelerateDMAInterface {
public:
    explicit AccelerateDMA(BufferCache& buffer_cache, TextureCache& texture_cache,
                           Scheduler& scheduler);

    bool BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) override;

    bool BufferClear(GPUVAddr src_address, u64 amount, u32 value) override;

    bool ImageToBuffer(const Tegra::DMA::ImageCopy& copy_info, const Tegra::DMA::ImageOperand& src,
                       const Tegra::DMA::BufferOperand& dst) override;

    bool BufferToImage(const Tegra::DMA::ImageCopy& copy_info, const Tegra::DMA::BufferOperand& src,
                       const Tegra::DMA::ImageOperand& dst) override;

private:
    template <bool IS_IMAGE_UPLOAD>
    bool DmaBufferImageCopy(const Tegra::DMA::ImageCopy& copy_info,
                            const Tegra::DMA::BufferOperand& src,
                            const Tegra::DMA::ImageOperand& dst);

    BufferCache& buffer_cache;
    TextureCache& texture_cache;
    Scheduler& scheduler;
};

class RasterizerVulkan final : public VideoCore::RasterizerInterface,
                               protected VideoCommon::ChannelSetupCaches<VideoCommon::ChannelInfo> {
public:
    explicit RasterizerVulkan(Core::Frontend::EmuWindow& emu_window_, Tegra::GPU& gpu_,
                              Tegra::MaxwellDeviceMemoryManager& device_memory_,
                              const Device& device_, MemoryAllocator& memory_allocator_,
                              StateTracker& state_tracker_, Scheduler& scheduler_);
    friend struct Core::SchedulerTestAccess;
    ~RasterizerVulkan() override;

    /// GPU-owner isolated-footprint adapter only. External producer/mapping/allocator
    /// exclusion is a caller precondition, not an ordinary-game eligibility certificate.
    /// Captures existing representations; never creates/uploads or changes dirty trackers.
    [[nodiscard]] std::shared_ptr<ControlledAliasCopy> BeginControlledAliasCopy(
        DAddr source, DAddr destination, u32 size);
    [[nodiscard]] ControlledAliasPhase PollControlledAliasCopy(
        const std::shared_ptr<ControlledAliasCopy>& operation);
    struct StorageWriteObservation {
        bool capture_valid;
        std::vector<Scheduler::Write> writes;
        bool physical_coverage_complete;
    };
    // Explicit GPU-owner diagnostics. No physical authority or eligibility certificate.
    bool SetControlledStorageWriteObservation(bool enabled);
    [[nodiscard]] std::optional<StorageWriteObservation> GetControlledStorageWriteObservation();

    void Shutdown() override;

    void Draw(bool is_indexed, u32 instance_count) override;
    void DrawIndirect() override;
    void DrawTexture() override;
    void Clear(u32 layer_count) override;
    void DispatchCompute() override;
    void ResetCounter(VideoCommon::QueryType type) override;
    void Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
               VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) override;
    void BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr, u32 size) override;
    void DisableGraphicsUniformBuffer(size_t stage, u32 index) override;
    void FlushAll() override;
    void FlushRegion(DAddr addr, u64 size,
                     VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    bool MustFlushRegion(DAddr addr, u64 size,
                         VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override;
    void InvalidateRegion(DAddr addr, u64 size,
                          VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void InnerInvalidation(std::span<const std::pair<DAddr, std::size_t>> sequences) override;
    void OnCacheInvalidation(DAddr addr, u64 size) override;
    bool OnCPUWrite(DAddr addr, u64 size) override;
    void InvalidateGPUCache() override;
    void UnmapMemory(DAddr addr, u64 size) override;
    void ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) override;
    void SignalFence(std::function<void()>&& func) override;
    void SyncOperation(std::function<void()>&& func) override;
    void SignalSyncPoint(u32 value) override;
    void SignalReference() override;
    void ReleaseFences(bool force = true) override;
    void FlushAndInvalidateRegion(
        DAddr addr, u64 size, VideoCommon::CacheType which = VideoCommon::CacheType::All) override;
    void WaitForIdle() override;
    void FragmentBarrier() override;
    void TiledCacheBarrier() override;
    void FlushCommands() override;
    void TickFrame() override;

    // VRAM monitoring functions
    u64 GetTotalVram() const;
    u64 GetUsedVram() const;
    u64 GetBufferMemoryUsage() const;
    u64 GetTextureMemoryUsage() const;
    u64 GetStagingMemoryUsage() const;

    /// Explicit GPU-owner controlled endpoint. Ordinary game routing remains unenrolled.
    using ControlledRegistrationPtr = BufferCache::ControlledRegistrationPtr;
    using ControlledCpuWritePtr = BufferCache::ControlledCpuWritePtr;
    /// Explicit isolated-footprint handoff only. A successful enqueue is not acceptance or
    /// completion. Never wait for it while retaining CPU/PTE/cache locks or producer duties.
    [[nodiscard]] bool QueueControlledCpuWriteRequest(
        const ControlledRegistrationPtr&, const Core::Memory::ControlledCpuWriteRequestPtr&);
    [[nodiscard]] ControlledCpuWritePtr BeginControlledCpuWriteRequest(
        const ControlledRegistrationPtr& operation,
        const Core::Memory::ControlledCpuWriteRequestPtr& request) {
        return buffer_cache.BeginControlledCpuWriteRequest(operation, request);
    }
    [[nodiscard]] Core::Memory::ControlledCpuWriteRequest::Phase AdvanceControlledCpuWriteRequest(
        const ControlledCpuWritePtr&);
    [[nodiscard]] ControlledCpuWritePtr BeginControlledCpuWrite(
        const ControlledRegistrationPtr& operation, const Core::Memory::PreparedCpuPage& view,
        u64 address, u64 size) {
        return buffer_cache.BeginControlledCpuWrite(operation, view, address, size);
    }
    [[nodiscard]] BufferCache::ControlledCpuWrite::Phase PollControlledCpuWrite(
        const ControlledCpuWritePtr& transaction) {
        return buffer_cache.PollControlledCpuWrite(transaction);
    }
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCpuWrite8(
        const ControlledCpuWritePtr&, Core::Memory::Memory&, u8);
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCpuWrite16(
        const ControlledCpuWritePtr&, Core::Memory::Memory&, u16);
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCpuWrite32(
        const ControlledCpuWritePtr&, Core::Memory::Memory&, u32);
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCpuWrite64(
        const ControlledCpuWritePtr&, Core::Memory::Memory&, u64);
    [[nodiscard]] ControlledRegistrationPtr BeginControlledBufferRegistration(
        DAddr address, u32 size, std::span<const Core::Memory::PreparedCpuPage> views) {
        return buffer_cache.BeginControlledFirstRegistration(address, size, views);
    }
    [[nodiscard]] BufferCache::ControlledRegistration::Phase PollControlledBufferRegistration(
        const ControlledRegistrationPtr& operation) {
        return buffer_cache.PollControlledRegistration(operation);
    }
    [[nodiscard]] bool RefreshControlledBufferRegistration(
        const ControlledRegistrationPtr& operation) {
        return buffer_cache.BeginControlledRefresh(operation);
    }
    void RetireControlledTransfersAfterDeviceIdle() {
        buffer_cache_runtime.RetireControlledTransfersAfterDeviceIdle();
        scheduler.RetireCheckedWritesAfterDeviceIdle();
    }
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult WriteControlledCached8(
        const ControlledRegistrationPtr&, Core::Memory::Memory&,
        const Core::Memory::PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u8);
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult WriteControlledCached16(
        const ControlledRegistrationPtr&, Core::Memory::Memory&,
        const Core::Memory::PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u16);
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult WriteControlledCached32(
        const ControlledRegistrationPtr&, Core::Memory::Memory&,
        const Core::Memory::PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u32);
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult WriteControlledCached64(
        const ControlledRegistrationPtr&, Core::Memory::Memory&,
        const Core::Memory::PreparedCpuPage&, Common::PhysicalRegistrationBundle::CachedWrite&,
        Common::ProcessAddress, u64);

    bool AccelerateConditionalRendering() override;
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface& src,
                               const Tegra::Engines::Fermi2D::Surface& dst,
                               const Tegra::Engines::Fermi2D::Config& copy_config) override;
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override;
    void AccelerateInlineToMemory(GPUVAddr address, size_t copy_size,
                                  std::span<const u8> memory) override;
    void LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) override;

    void InitializeChannel(Tegra::Control::ChannelState& channel) override;

    void BindChannel(Tegra::Control::ChannelState& channel) override;

    void ReleaseChannel(s32 channel_id) override;

    std::optional<FramebufferTextureInfo> AccelerateDisplay(const Tegra::FramebufferConfig& config,
                                                            VAddr framebuffer_addr,
                                                            u32 pixel_stride);

private:
    bool RunBufferCacheCpuWrite(DAddr addr, u64 size, bool check_gpu_writes = false,
                                bool unmap = false);
    // nullopt means preservation was not confirmed. The contained bool retains
    // the legacy deferred-dirty decision; false is a successful clean result.
    // Normal host context only; do not call from an NCE signal handler.
    std::optional<bool> TryRunBufferCacheCpuWrite(DAddr addr, u64 size, bool check_gpu_writes,
                                                bool unmap);
    static constexpr size_t MAX_TEXTURES = 192;
    static constexpr size_t MAX_IMAGES = 48;
    static constexpr size_t MAX_IMAGE_VIEWS = MAX_TEXTURES + MAX_IMAGES;

    static constexpr VkDeviceSize DEFAULT_BUFFER_SIZE = 4 * sizeof(float);

    /// Returns the pipeline the draw was recorded with, or nullptr when it was skipped
    template <typename Func>
    GraphicsPipeline* PrepareDraw(bool is_indexed, Func&&);
    template <typename Command>
    void RecordShaderStorageWrite(Command&& command) {
        if (!observe_storage_writes) {
            scheduler.Record(std::forward<Command>(command));
            return;
        }
        std::function<bool(vk::CommandBuffer)> observed_command;
        try {
            observed_command = command;
        } catch (...) {
            MarkStorageObservationIncomplete();
            scheduler.Record(std::forward<Command>(command));
            return;
        }
        // Once recording starts, exceptions must propagate without replaying.
        RecordObservedShaderStorageWrite(std::move(observed_command));
    }
    void RecordObservedShaderStorageWrite(std::function<bool(vk::CommandBuffer)> command);
    void MarkStorageObservationIncomplete();

    /// Performs the pipeline's vertex pointer stores, instance_count is empty for indirect draws
    void PerformVertexPointerStores(const GraphicsPipeline& pipeline, bool is_indexed,
                                    std::optional<u32> instance_count);

    void FlushWork();

    /// Once a frame: decides whether finished passes are submitted to the host GPU early.
    void UpdatePassEndSubmit();

    void UpdateDynamicStates();

    void HandleTransformFeedback();

    void UpdateViewportsState(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateScissorsState(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBias(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateBlendConstants(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBounds(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateStencilFaces(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLineWidth(Tegra::Engines::Maxwell3D::Regs& regs);

    void UpdateCullMode(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBoundsTestEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthTestEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthWriteEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthCompareOp(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdatePrimitiveRestartEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateRasterizerDiscardEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthBiasEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLogicOpEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateDepthClampEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateFrontFace(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateStencilOp(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateStencilTestEnable(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateLogicOp(Tegra::Engines::Maxwell3D::Regs& regs);
    void UpdateBlending(Tegra::Engines::Maxwell3D::Regs& regs);

    void UpdateVertexInput(Tegra::Engines::Maxwell3D::Regs& regs);

    Tegra::GPU& gpu;
    Tegra::MaxwellDeviceMemoryManager& device_memory;

    const Device& device;
    MemoryAllocator& memory_allocator;
    StateTracker& state_tracker;
    Scheduler& scheduler;

    StagingBufferPool staging_pool;
    DescriptorPool descriptor_pool;
    GuestDescriptorQueue guest_descriptor_queue;
    ComputePassDescriptorQueue compute_pass_descriptor_queue;
    BlitImageHelper blit_image;
    RenderPassCache render_pass_cache;

    // Declared before caches so their borrowed diagnostic pointer dies first.
    std::unique_ptr<VideoCommon::PhysicalShadow> physical_shadow;

    void ReportPhysicalShadow() noexcept;

    TextureCacheRuntime texture_cache_runtime;
    TextureCache texture_cache;
    BufferCacheRuntime buffer_cache_runtime;
    BufferCache buffer_cache;
    std::vector<std::shared_ptr<ControlledAliasCopy>> controlled_alias_copies;
    bool observe_storage_writes{};
    bool storage_observation_incomplete{};
    std::vector<Scheduler::Write> observed_storage_writes;
    std::shared_ptr<VideoCommon::PhysicalWriteCoverage> storage_write_coverage;
    const std::shared_ptr<const u8> controlled_alias_owner{std::make_shared<const u8>(0)};
    QueryCacheRuntime query_cache_runtime;
    QueryCache query_cache;
    PipelineCache pipeline_cache;
    AccelerateDMA accelerate_dma;
    FenceManager fence_manager;


    /// Past-stride vertex layouts already logged by UpdateVertexInput, by guest type and size
    std::bitset<512> reported_vertex_layouts;

    boost::container::static_vector<u32, MAX_IMAGE_VIEWS> image_view_indices;
    std::array<VideoCommon::ImageViewId, MAX_IMAGE_VIEWS> image_view_ids;
    boost::container::static_vector<VkSampler, MAX_TEXTURES> sampler_handles;

    u32 draw_counter = 0;
    /// Draws recorded since the last submission to the host GPU, and the tick of that submission.
    u32 draws_since_submit = 0;
    u64 last_submit_tick = 0;
    /// Whether finished passes are submitted early, and what decides it: the share of the last
    /// frames this thread spent running (in 1/1024, starts saturated so the answer is "no"
    /// until it has been measured) and the share below which it is switched on.
    bool submit_at_pass_end = false;
    [[maybe_unused]] s64 gpu_thread_busy = 1024;
    [[maybe_unused]] s64 busy_to_turn_on = 819; // 80% of 1024
    [[maybe_unused]] u32 frames_off = 0;
    [[maybe_unused]] s64 last_frame_wall_ns = 0;
    [[maybe_unused]] s64 last_frame_cpu_ns = 0;

    std::shared_mutex shutdown_mutex;
    std::atomic_bool is_shutting_down{false};
};

} // namespace Vulkan
