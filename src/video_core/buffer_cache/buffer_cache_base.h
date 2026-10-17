// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/common_types.h"
#include "common/physical_registration_bundle.h"
#include "core/controlled_cpu_page.h"
#include "common/div_ceil.h"
#include "common/literals.h"
#include "common/lru_cache.h"
#include "common/virtual_buffer.h"
#include "common/range_sets.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/slot_vector.h"
#include "video_core/buffer_cache/buffer_base.h"
#include "video_core/buffer_cache/controlled_transfer.h"
#include "video_core/buffer_cache/physical_alias_copy.h"
#include "video_core/buffer_cache/uma.h"
#include "video_core/buffer_cache/direct_guest_mapping.h"
#include "video_core/buffer_cache/gather_alias.h"
#include "video_core/cache_memory_pressure.h"
#include "video_core/control/channel_state_cache.h"
#include "video_core/delayed_destruction_ring.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/draw_manager.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/memory_manager.h"
#include "video_core/physical_shadow.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/surface.h"
#include "video_core/texture_cache/types.h"

namespace VideoCommon {

using BufferId = Common::SlotId;

using VideoCore::Surface::PixelFormat;
using namespace Common::Literals;

#ifdef __APPLE__
constexpr u32 NUM_VERTEX_BUFFERS = 16;
#else
constexpr u32 NUM_VERTEX_BUFFERS = 32;
#endif
constexpr u64 MAX_PADDED_VERTEX_BUFFER_SIZE = 1_MiB;
constexpr u32 NUM_TRANSFORM_FEEDBACK_BUFFERS = 4;
constexpr u32 NUM_GRAPHICS_UNIFORM_BUFFERS = 18;
constexpr u32 NUM_COMPUTE_UNIFORM_BUFFERS = 8;
// Must hold every storage buffer a shader can declare (Shader::Info::MAX_SSBOS, checked where
// pipelines bind them). The enabled and written masks are u32, so 32 is also their limit.
constexpr u32 NUM_STORAGE_BUFFERS = 32;
constexpr u32 NUM_TEXTURE_BUFFERS = 32;
constexpr u32 NUM_STAGES = 5;

using UniformBufferSizes = std::array<std::array<u32, NUM_GRAPHICS_UNIFORM_BUFFERS>, NUM_STAGES>;
using ComputeUniformBufferSizes = std::array<u32, NUM_COMPUTE_UNIFORM_BUFFERS>;

enum class ObtainBufferSynchronize : u32 {
    NoSynchronize = 0,
    FullSynchronize = 1,
    SynchronizeNoDirty = 2,
};

enum class ObtainBufferOperation : u32 {
    DoNothing = 0,
    MarkAsWritten = 1,
    DiscardWrite = 2,
    MarkQuery = 3,
};

static constexpr BufferId NULL_BUFFER_ID{0};
static constexpr u32 DEFAULT_SKIP_CACHE_SIZE = static_cast<u32>(4_KiB);

struct Binding {
    DAddr device_addr{};
    u32 size{};
    BufferId buffer_id;
    // Base, extent and binding offset of a shared scratch allocation for split/overlapping views.
    GPUVAddr gather_gpu_addr{};
    GPUVAddr gpu_addr{}; ///< original GPU address, preserved when overlapping views share scratch
    u32 gather_size{};
    u32 gather_offset{};
};

struct TextureBufferBinding : Binding {
    PixelFormat format;
};

static constexpr Binding NULL_BINDING{
    .device_addr = 0,
    .size = 0,
    .buffer_id = NULL_BUFFER_ID,
};

template <typename Buffer>
struct HostBindings {
    boost::container::small_vector<Buffer*, NUM_VERTEX_BUFFERS> buffers;
    boost::container::small_vector<u64, NUM_VERTEX_BUFFERS> offsets;
    boost::container::small_vector<u64, NUM_VERTEX_BUFFERS> sizes;
    boost::container::small_vector<u64, NUM_VERTEX_BUFFERS> strides;
    u32 min_index{NUM_VERTEX_BUFFERS};
    u32 max_index{0};
};

class BufferCacheChannelInfo : public ChannelInfo {
public:
    BufferCacheChannelInfo() = delete;
    BufferCacheChannelInfo(Tegra::Control::ChannelState& state) noexcept : ChannelInfo(state) {}
    BufferCacheChannelInfo(const BufferCacheChannelInfo& state) = delete;
    BufferCacheChannelInfo& operator=(const BufferCacheChannelInfo&) = delete;

    Binding index_buffer;
    std::array<Binding, NUM_VERTEX_BUFFERS> vertex_buffers;
    std::array<std::array<Binding, NUM_GRAPHICS_UNIFORM_BUFFERS>, NUM_STAGES> uniform_buffers;
    std::array<std::array<Binding, NUM_STORAGE_BUFFERS>, NUM_STAGES> storage_buffers;
    std::array<std::array<TextureBufferBinding, NUM_TEXTURE_BUFFERS>, NUM_STAGES> texture_buffers;
    std::array<Binding, NUM_TRANSFORM_FEEDBACK_BUFFERS> transform_feedback_buffers;
    Binding count_buffer_binding;
    Binding indirect_buffer_binding;

    std::array<Binding, NUM_COMPUTE_UNIFORM_BUFFERS> compute_uniform_buffers;
    std::array<Binding, NUM_STORAGE_BUFFERS> compute_storage_buffers;
    std::array<TextureBufferBinding, NUM_TEXTURE_BUFFERS> compute_texture_buffers;

    std::array<u32, NUM_STAGES> enabled_uniform_buffer_masks{};
    u32 enabled_compute_uniform_buffer_mask = 0;

    /// One bit per vertex stream that is bound to a real buffer
    u32 enabled_vertex_buffers = 0;
    static_assert(NUM_VERTEX_BUFFERS <= 32);

    const UniformBufferSizes* uniform_buffer_sizes{};
    const ComputeUniformBufferSizes* compute_uniform_buffer_sizes{};

    std::array<u32, NUM_STAGES> enabled_storage_buffers{};
    std::array<u32, NUM_STAGES> written_storage_buffers{};
    u32 enabled_compute_storage_buffers = 0;
    u32 written_compute_storage_buffers = 0;

    std::array<u32, NUM_STAGES> enabled_texture_buffers{};
    std::array<u32, NUM_STAGES> written_texture_buffers{};
    std::array<u32, NUM_STAGES> image_texture_buffers{};
    u32 enabled_compute_texture_buffers = 0;
    u32 written_compute_texture_buffers = 0;
    u32 image_compute_texture_buffers = 0;

    std::array<u32, 16> uniform_cache_hits{};
    std::array<u32, 16> uniform_cache_shots{};

    u32 uniform_buffer_skip_cache_size = DEFAULT_SKIP_CACHE_SIZE;

    bool has_deleted_buffers = false;
    bool has_logged_unmapped_storage_buffer = false;

    std::array<u32, NUM_STAGES> dirty_uniform_buffers{};
    std::array<u32, NUM_STAGES> fast_bound_uniform_buffers{};
    std::array<std::array<u32, NUM_GRAPHICS_UNIFORM_BUFFERS>, NUM_STAGES>
        uniform_buffer_binding_sizes{};
};

template <class P>
class BufferCache : public VideoCommon::ChannelSetupCaches<BufferCacheChannelInfo> {
    friend struct Core::SchedulerTestAccess;
    // Page size for caching purposes.
    // This is unrelated to the CPU page size and it can be changed as it seems optimal.
    static constexpr u32 CACHING_PAGEBITS = 16;
    static constexpr u64 CACHING_PAGESIZE = u64{1} << CACHING_PAGEBITS;
    // Device addresses are 39-bit. Keep the cache page table aligned with the address space
    // exposed by MaxwellDeviceMemoryManager instead of silently truncating it to 34 bits.
    static constexpr u64 CACHING_PAGE_COUNT =
        1ULL << (Tegra::MaxwellDeviceMemoryManager::AS_BITS - CACHING_PAGEBITS);
    static constexpr u64 CACHING_ADDRESS_SPACE_SIZE = CACHING_PAGE_COUNT * CACHING_PAGESIZE;
    static constexpr bool IS_OPENGL = P::IS_OPENGL;
    static constexpr bool HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS =
        P::HAS_PERSISTENT_UNIFORM_BUFFER_BINDINGS;
    static constexpr bool HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT =
        P::HAS_FULL_INDEX_AND_PRIMITIVE_SUPPORT;
    static constexpr bool NEEDS_BIND_UNIFORM_INDEX = P::NEEDS_BIND_UNIFORM_INDEX;
    static constexpr bool NEEDS_BIND_STORAGE_INDEX = P::NEEDS_BIND_STORAGE_INDEX;
    static constexpr bool USE_MEMORY_MAPS = P::USE_MEMORY_MAPS;
    static constexpr bool SEPARATE_IMAGE_BUFFERS_BINDINGS = P::SEPARATE_IMAGE_BUFFER_BINDINGS;
    static constexpr bool USE_MEMORY_MAPS_FOR_UPLOADS = P::USE_MEMORY_MAPS_FOR_UPLOADS;

    // FIXED: VRAM leak prevention - Enhanced buffer eviction constants
    static constexpr u64 DEFAULT_BUFFER_EVICTION_FRAMES = 5;
    static constexpr size_t LARGE_BUFFER_THRESHOLD = 8_MiB;
    static constexpr f32 BUFFER_VRAM_WARNING_THRESHOLD = 0.70f;
    static constexpr f32 BUFFER_VRAM_CRITICAL_THRESHOLD = 0.85f;

    // Debug Flags.

    static constexpr bool DISABLE_DOWNLOADS = true;

    using Maxwell = Tegra::Engines::Maxwell3D::Regs;

    using Runtime = typename P::Runtime;
    using Buffer = typename P::Buffer;
    using Async_Buffer = typename P::Async_Buffer;
    using MemoryTracker = typename P::MemoryTracker;

    struct OverlapResult {
        boost::container::small_vector<BufferId, 16> ids;
        DAddr begin;
        DAddr end;
        bool has_stream_leap = false;
    };

public:
    explicit BufferCache(Tegra::MaxwellDeviceMemoryManager& device_memory_, Runtime& runtime_);

    class ControlledRegistration {
        friend class BufferCache;

    public:
        enum class Phase { Waiting, Admitted, Submitted, Ready, Writing, Unknown, Cancelled };
        [[nodiscard]] Phase State() const { return phase; }
        [[nodiscard]] std::optional<Common::PhysicalRegistrationBundle::CachedWrite>
        BeginCachedWrite() const {
            return phase == Phase::Ready ? admission->BeginCachedWrite() : std::nullopt;
        }

    private:
        ControlledRegistration(Tegra::PhysicalAccessPlan plan_,
                               std::span<const Core::Memory::PreparedCpuPage> views_)
            : plan{std::move(plan_)}, views{views_.begin(), views_.end()} {}
        Tegra::PhysicalAccessPlan plan;
        std::vector<Core::Memory::PreparedCpuPage> views;
        std::optional<Common::PhysicalRegistrationBundle> admission;
        std::optional<Common::PhysicalRegistrationBundle::CachedWrite> refresh;
        std::shared_ptr<ControlledBufferTransfer> transfer;
        BufferId id{}, previous{};
        const void* previous_identity{};
        DAddr address{};
        Phase phase{Phase::Waiting};
    };
    using ControlledRegistrationPtr = std::shared_ptr<ControlledRegistration>;

    /// GPU-owner continuation, with no cache/PTE lock across polling. Admission and physical/
    /// native owners survive caller drop or uncertain effects. Not an ordinary CPU dispatcher.
    class ControlledCpuWrite {
        friend class BufferCache;
    public:
        enum class Phase { Waiting, Submitted, Preserved, Committed, Cancelled, Unknown };
        [[nodiscard]] Phase State() const { return phase; }
        [[nodiscard]] const Core::Memory::ControlledCpuWriteRequestPtr& Request() const {
            return request;
        }
        ~ControlledCpuWrite() {
            if (phase != Phase::Committed && phase != Phase::Cancelled) {
                (void)write.MarkUnknown();
                operation->phase = ControlledRegistration::Phase::Unknown;
                if (request) request->Publish(Core::Memory::ControlledCpuWriteRequest::Phase::Unknown);
            }
        }
    private:
        ControlledCpuWrite(ControlledRegistrationPtr operation_,
                           const Core::Memory::PreparedCpuPage& view_, u64 address_, u64 size_,
                           std::shared_ptr<ControlledBufferTransfer> preservation_,
                           Common::PhysicalRegistrationBundle::CachedWrite write_)
            : operation{std::move(operation_)}, view{view_}, address{address_}, size{size_},
              preservation{std::move(preservation_)}, write{std::move(write_)} {}
        ControlledRegistrationPtr operation;
        Core::Memory::PreparedCpuPage view;
        u64 address, size;
        std::shared_ptr<ControlledBufferTransfer> preservation;
        Common::PhysicalRegistrationBundle::CachedWrite write;
        Phase phase{Phase::Waiting};
        Core::Memory::ControlledCpuWriteRequestPtr request;
        bool executing_request{};
    };
    using ControlledCpuWritePtr = std::shared_ptr<ControlledCpuWrite>;

    [[nodiscard]] ControlledCpuWritePtr BeginControlledCpuWrite(
        const ControlledRegistrationPtr&, const Core::Memory::PreparedCpuPage&, u64 address,
        u64 size, std::shared_ptr<const void> extra_owner = {});
    [[nodiscard]] ControlledCpuWritePtr BeginControlledCpuWriteRequest(
        const ControlledRegistrationPtr&, const Core::Memory::ControlledCpuWriteRequestPtr&);
    template <typename Execute>
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCpuWriteRequest(
        const ControlledCpuWritePtr&, Execute&& execute);
    void AbandonControlledCpuWrite(const ControlledCpuWritePtr& transaction);
    [[nodiscard]] typename ControlledCpuWrite::Phase PollControlledCpuWrite(
        const ControlledCpuWritePtr&);
    template <typename Store>
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCpuTransaction(
        const ControlledCpuWritePtr&, u64 size, Store&& store);

    /// GPU/cache owner only, without a cache/PTE lock across retries. Explicit controlled
    /// endpoint, not ordinary game routing. Overlaps, unsupported backends and unqualified
    /// CPU views refuse before registration/marking/upload; no legacy fallback is permitted.
    [[nodiscard]] ControlledRegistrationPtr BeginControlledFirstRegistration(
        DAddr address, u32 size, std::span<const Core::Memory::PreparedCpuPage> views);
    [[nodiscard]] typename ControlledRegistration::Phase PollControlledRegistration(
        const ControlledRegistrationPtr& operation);
    [[nodiscard]] bool BeginControlledRefresh(const ControlledRegistrationPtr& operation);
    [[nodiscard]] BufferId ControlledReadyBuffer(const ControlledRegistrationPtr& operation);

    template <typename Store>
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult CommitControlledCachedWrite(
        const ControlledRegistrationPtr& operation, const Core::Memory::PreparedCpuPage& view,
        Common::PhysicalRegistrationBundle::CachedWrite& write, u64 address, u64 size,
        Store&& store);

    ~BufferCache();

    void TickFrame(u64 cache_usage, u64 cache_target, std::span<const HeapMemoryBudget> heaps);

    bool HasDirectGuestBuffers() const { return has_direct_guest_buffers; }
    bool CpuWriteNeedsGpuThread(DAddr address, u64 size) const;
    void WriteMemory(DAddr device_addr, u64 size);

    void CachedWriteMemory(DAddr device_addr, u64 size);

    bool OnCPUWrite(DAddr device_addr, u64 size);

    void DownloadMemory(DAddr device_addr, u64 size);

    std::optional<VideoCore::RasterizerDownloadArea> GetFlushArea(DAddr device_addr, u64 size);

    bool InlineMemory(DAddr dest_address, size_t copy_size, std::span<const u8> inlined_buffer);

    void BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr, u32 size);

    void DisableGraphicsUniformBuffer(size_t stage, u32 index);

    void UpdateGraphicsBuffers(bool is_indexed);

    void UpdateComputeBuffers();

    // Read-only visitor of already prepared bindings; never resolves/scatters buffers.
    void ObservePhysicalBindings(PhysicalShadow& shadow, bool compute);

    void BindHostGeometryBuffers(bool is_indexed);

    void BindHostStageBuffers(size_t stage);

    void BindHostComputeBuffers();

    /// Records that written gathered storage buffers hold new data. The copy back to their
    /// (non-contiguous) device chunks is deferred until something else touches those chunks.
    /// Must be called after the draw/dispatch that used them has been recorded.
    void FlushGatherWrites();

    void SetUniformBuffersState(const std::array<u32, NUM_STAGES>& mask,
                                const UniformBufferSizes* sizes);

    void SetComputeUniformBufferState(u32 mask, const ComputeUniformBufferSizes* sizes);

    void UnbindGraphicsStorageBuffers(size_t stage);

    void BindGraphicsStorageBuffer(size_t stage, size_t ssbo_index, u32 cbuf_index, u32 cbuf_offset,
                                   bool is_written);

    void UnbindGraphicsTextureBuffers(size_t stage);

    void BindGraphicsTextureBuffer(size_t stage, size_t tbo_index, GPUVAddr gpu_addr, u32 size,
                                   PixelFormat format, bool is_written, bool is_image);

    void UnbindComputeStorageBuffers();

    void BindComputeStorageBuffer(size_t ssbo_index, u32 cbuf_index, u32 cbuf_offset,
                                  bool is_written);

    void UnbindComputeTextureBuffers();

    void BindComputeTextureBuffer(size_t tbo_index, GPUVAddr gpu_addr, u32 size, PixelFormat format,
                                  bool is_written, bool is_image);

    /// Pure existing-cache lookup plus current physical correspondence. Caller holds mutex on
    /// the GPU/cache thread and retains device/backing/renderer owners. No creation, sync, dirty
    /// publication or execution permission. Unsupported Buffer backends expose no pin method.
    template <typename B = Buffer>
    [[nodiscard]] auto PinExistingDmaRepresentation(
        DAddr device_addr, const Tegra::PhysicalAccessPlan& physical)
        -> decltype(std::declval<const B&>().PinStagedRepresentation(u64{}, u64{}));

    /// Explicit isolated alias adapter only; caller holds mutex on the GPU owner.
    /// Snapshots dirty subranges and resource write intent, without consuming uploads.
    [[nodiscard]] std::optional<AliasWriteSnapshot> SnapshotAliasWrites(
        DAddr address, u64 size);
    [[nodiscard]] bool HasUniqueAliasSource(DAddr source, const Tegra::PhysicalAccessPlan& plan,
                                          std::span<const AliasCopyRange> copies);

    [[nodiscard]] std::pair<Buffer*, u32> ObtainBuffer(GPUVAddr gpu_addr, u32 size,
                                                       ObtainBufferSynchronize sync_info,
                                                       ObtainBufferOperation post_op);

    [[nodiscard]] std::pair<Buffer*, u32> ObtainCPUBuffer(DAddr gpu_addr, u32 size,
                                                          ObtainBufferSynchronize sync_info,
                                                          ObtainBufferOperation post_op);
    void FlushCachedWrites();

    /// Return true when there are uncommitted buffers to be downloaded
    [[nodiscard]] bool HasUncommittedFlushes() const noexcept;

    void AccumulateFlushes();

    /// Return true when the caller should wait for async downloads
    [[nodiscard]] bool ShouldWaitAsyncFlushes() const noexcept;

    /// Commit asynchronous downloads
    void CommitAsyncFlushes();
    void CommitAsyncFlushesHigh();

    /// Pop asynchronous downloads
    void PopAsyncFlushes();
    void PopAsyncBuffers();

    bool DMACopy(GPUVAddr src_address, GPUVAddr dest_address, u64 amount);

    bool DMAClear(GPUVAddr src_address, u64 amount, u32 value);

    /// DMACopy/DMAClear for ranges that are not contiguous in device memory.
    bool DMACopySplit(GPUVAddr src_address, GPUVAddr dest_address, u64 amount);
    bool DMAClearSplit(GPUVAddr dst_address, u64 amount, u32 value);

    /// Return true when a CPU region is modified from the GPU
    [[nodiscard]] bool IsRegionGpuModified(DAddr addr, size_t size);

    /// Return true when a region is registered on the cache
    [[nodiscard]] bool IsRegionRegistered(DAddr addr, size_t size);

    /// Return true when a CPU region is modified from the CPU
    [[nodiscard]] bool IsRegionCpuModified(DAddr addr, size_t size);

    void SetDrawIndirect(
        const Tegra::Engines::DrawManager::IndirectParams* current_draw_indirect_) {
        current_draw_indirect = current_draw_indirect_;
    }

    [[nodiscard]] std::pair<Buffer*, u32> GetDrawIndirectCount();

    [[nodiscard]] std::pair<Buffer*, u32> GetDrawIndirectBuffer();

    template <typename Func>
    void BufferOperations(Func&& func) {
        do {
            channel_state->has_deleted_buffers = false;
            func();
        } while (channel_state->has_deleted_buffers);
    }

    mutable std::recursive_mutex mutex;
    Runtime& runtime;

    /// Controlled cache-owner entry only. Caller cold-qualifies the COMPLETE immutable device
    /// alias list, keeps mappings/representations stable for this bounded execution, and binds
    /// store to the real prepared Memory cached Write with this SAME admission. No waits,
    /// preservation callbacks, normal-game activation or automatic legacy fallback are allowed.
    /// GPU-dirty/pending scatter remains ineligible until its preservation adapter is proved.
    template <typename Store>
    [[nodiscard]] Core::Memory::PreparedCpuWriteResult
    CommitControlledCpuWrite(Common::PhysicalRegistrationBundle::CachedWrite& write,
                             std::span<const DAddr> device_aliases, u64 size, Store&& store);

private:
    BufferId ControlledBufferId(const ControlledRegistrationPtr& operation, bool require_clean,
                                bool nonblocking_mapping = false);
    void CheckControlledSynchronization(DAddr address, u64 size);
    // One producer-family exclusion, not a certificate for ordinary-game F2 activation.
    bool ControlledReadbacksIdle(const ControlledRegistrationPtr& operation);
    template <typename Func>
    static void ForEachEnabledBit(u32 enabled_mask, Func&& func) {
        for (u32 index = 0; enabled_mask != 0; ++index, enabled_mask >>= 1) {
            const int disabled_bits = std::countr_zero(enabled_mask);
            index += disabled_bits;
            enabled_mask >>= disabled_bits;
            func(index);
        }
    }

    template <typename Func>
    void ForEachBufferInRange(DAddr device_addr, u64 size, Func&& func) {
        if (!IsRegionPageTableAddressable(device_addr, size)) {
            return;
        }
        const u64 page_end = Common::DivCeil(device_addr + size, CACHING_PAGESIZE);
        for (u64 page = device_addr >> CACHING_PAGEBITS; page < page_end;) {
            const BufferId buffer_id = GetPageBufferId(page);
            if (!buffer_id) {
                ++page;
                continue;
            }
            Buffer& buffer = slot_buffers[buffer_id];
            func(buffer_id, buffer);

            const DAddr end_addr = buffer.CpuAddr() + buffer.SizeBytes();
            page = Common::DivCeil(end_addr, CACHING_PAGESIZE);
        }
    }

    static bool IsRangeGranular(DAddr device_addr, size_t size) {
        return (device_addr & ~Core::DEVICE_PAGEMASK) ==
               ((device_addr + size) & ~Core::DEVICE_PAGEMASK);
    }

    [[nodiscard]] static constexpr bool IsRegionPageTableAddressable(DAddr device_addr,
                                                                      u64 size) noexcept {
        return device_addr < CACHING_ADDRESS_SPACE_SIZE &&
               size <= CACHING_ADDRESS_SPACE_SIZE - device_addr;
    }

    [[nodiscard]] BufferId GetPageBufferId(u64 page) const noexcept {
        if (page >= CACHING_PAGE_COUNT) {
            return BufferId{};
        }
        const u32 entry = page_table[page];
        if (entry == 0) {
            return BufferId{};
        }
        const BufferId buffer_id{entry - 1};
        // A damaged page-table entry must not become an unchecked SlotVector index.
        return slot_buffers.contains(buffer_id) ? buffer_id : BufferId{};
    }

    void SetPageBufferId(u64 page, BufferId buffer_id) {
        if (page >= CACHING_PAGE_COUNT) {
            return;
        }
        // An entry that already holds the value is not written, so table pages nothing was ever
        // registered in stay untouched.
        const u32 entry = buffer_id ? buffer_id.index + 1 : 0;
        if (page_table[page] != entry) {
            page_table[page] = entry;
        }
    }

    bool RunGarbageCollector(bool aggressive_gc);

public:
    // FIXED: VRAM leak prevention - Enhanced public interface for buffer VRAM management

    /// Get buffer VRAM usage statistics
    struct BufferVRAMStats {
        u64 total_used_bytes;
        u64 large_buffer_bytes;
        u64 evicted_total;
        u32 buffer_count;
        u32 large_buffer_count;
    };
    [[nodiscard]] BufferVRAMStats GetBufferVRAMStats() const noexcept {
        return BufferVRAMStats{
            .total_used_bytes = total_used_memory,
            .large_buffer_bytes = large_buffer_memory,
            .evicted_total = evicted_buffer_bytes,
            .buffer_count = buffer_count,
            .large_buffer_count = large_buffer_count,
        };
    }

    void BindHostIndexBuffer();

    void BindHostVertexBuffers();

    void BindHostDrawIndirectBuffers();

    void BindHostGraphicsUniformBuffers(size_t stage);

    void BindHostGraphicsUniformBuffer(size_t stage, u32 index, u32 binding_index, bool needs_bind);

    void BindHostGraphicsStorageBuffers(size_t stage);

    void BindHostGraphicsTextureBuffers(size_t stage);

    void BindHostTransformFeedbackBuffers();

    void BindHostComputeUniformBuffers();

    void BindHostComputeStorageBuffers();

    void BindHostComputeTextureBuffers();

    void DoUpdateGraphicsBuffers(bool is_indexed);

    void DoUpdateComputeBuffers();

    void UpdateIndexBuffer();

    void UpdateVertexBuffers();

    void UpdateVertexBuffer(u32 index);

    void UpdateDrawIndirect();

    void UpdateUniformBuffers(size_t stage);

    void UpdateStorageBuffers(size_t stage);

    void UpdateTextureBuffers(size_t stage);

    void UpdateTransformFeedbackBuffers();

    void UpdateTransformFeedbackBuffer(u32 index);

    void UpdateComputeUniformBuffers();

    void UpdateComputeStorageBuffers();

    void UpdateComputeTextureBuffers();

    void MarkWrittenBuffer(BufferId buffer_id, DAddr device_addr, u32 size);

    /// Grows a guest write over the pages the guest is likely to write next
    [[nodiscard]] u64 WidenHotWrite(DAddr device_addr, u64 size);

    [[nodiscard]] BufferId FindBuffer(DAddr device_addr, u32 size);

    [[nodiscard]] OverlapResult ResolveOverlaps(DAddr device_addr, u32 wanted_size);

    bool CanJoinDirectGuest(BufferId overlap_id);
    bool DMACopyImpl(GPUVAddr src_address, GPUVAddr dest_address, u64 amount);
    void JoinOverlap(BufferId new_buffer_id, BufferId overlap_id, bool accumulate_stream_score);

    [[nodiscard]] BufferId CreateBuffer(DAddr device_addr, u32 wanted_size);

    void Register(BufferId buffer_id);

    void Unregister(BufferId buffer_id);

    template <bool insert>
    void ChangeRegister(BufferId buffer_id);

    void TouchBuffer(Buffer& buffer, BufferId buffer_id) noexcept;

    /// Records that GPU work is about to use [offset, offset + size) of the buffer: marks the
    /// range for upload reordering and stamps its pages with the current scheduler tick for the
    /// unified memory rule. Bind, clear, DMA, overlap and download sites use this; staged copies,
    /// ObtainCPUBuffer and the indirect getters stamp their range with Uma::StampPages, as
    /// they are not tracked for upload reordering.
    void MarkGpuUse(Buffer& buffer, u64 offset, u64 size);

    bool SynchronizeBuffer(Buffer& buffer, DAddr device_addr, u32 size);

    /// True when SynchronizeBuffer would have to upload into this range with a copy that keeps
    /// its place in the command stream, which ends the current render pass.
    [[nodiscard]] bool UploadNeedsOrderedCopy(Buffer& buffer, DAddr device_addr, u32 size);

    void UploadMemory(Buffer& buffer, u64 total_size_bytes, u64 largest_copy,
                      std::span<BufferCopy> copies);

    void ImmediateUploadMemory(Buffer& buffer, u64 largest_copy,
                               std::span<const BufferCopy> copies);

    void MappedUploadMemory(Buffer& buffer, u64 total_size_bytes, std::span<BufferCopy> copies);

    void DownloadBufferMemory(Buffer& buffer_id);

    void DownloadBufferMemory(Buffer& buffer_id, DAddr device_addr, u64 size,
                              bool preserve_on_unmap = false);

    /// Writes downloaded buffer data back to guest memory, skipping device pages nothing backs
    void WriteBackDownload(DAddr device_addr, const u8* data, u64 size);

    void DeleteBuffer(BufferId buffer_id, bool do_not_mark = false);

    [[nodiscard]] Binding StorageBufferBinding(GPUVAddr ssbo_addr, u32 cbuf_index,
                                               bool is_written) const;

    /// Resolves (creates/joins) the cache buffers backing each contiguous chunk of a gathered
    /// range.
    void ResolveGatherChunks(GPUVAddr gpu_addr, u32 size);
    void ResetGatherBindings(bool compute);
    void PrepareGatherBindings(bool compute);
    boost::container::small_vector<Binding*, 32> GatherBindings(bool compute);

    /// Whether [gpu_addr, +size) maps to one contiguous device range (cached per page table
    /// generation).
    bool IsGpuRangeContiguous(GPUVAddr gpu_addr, u32 size) const;
    struct ContiguityKey {
        GPUVAddr gpu_addr;
        u32 size;
        bool operator==(const ContiguityKey&) const = default;
    };
    struct ContiguityKeyHash {
        size_t operator()(const ContiguityKey& key) const noexcept {
            return std::hash<u64>{}(key.gpu_addr ^ (static_cast<u64>(key.size) << 40));
        }
    };
    mutable std::unordered_map<ContiguityKey, bool, ContiguityKeyHash> contiguity_cache;
    mutable u64 contiguity_cache_generation = ~0ULL;
    mutable size_t contiguity_cache_address_space = ~size_t{};

    /// Copies every contiguous chunk of a non-contiguous GPU range into a scratch buffer.
    Buffer& GatherBuffer(GPUVAddr gpu_addr, u32 size, bool is_written = true);

    [[nodiscard]] TextureBufferBinding GetTextureBufferBinding(GPUVAddr gpu_addr, u32 size,
                                                               PixelFormat format);

    [[nodiscard]] std::span<const u8> ImmediateBufferWithData(DAddr device_addr, size_t size);

    [[nodiscard]] std::span<u8> ImmediateBuffer(size_t wanted_capacity);

    [[nodiscard]] bool HasFastUniformBufferBound(size_t stage, u32 binding_index) const noexcept;

    void ClearDownload(DAddr base_addr, u64 size);

    void InlineMemoryImplementation(DAddr dest_address, size_t copy_size,
                                    std::span<const u8> inlined_buffer);

    Tegra::MaxwellDeviceMemoryManager& device_memory;

    Common::SlotVector<Buffer> slot_buffers;
    std::vector<ControlledRegistrationPtr> controlled_registrations;
    struct GatherChunk {
        DAddr device_addr;
        u32 size;
        u32 offset; ///< offset inside the gathered range
        u8 dirty;   ///< number of upcoming gathers that must re-copy this chunk
    };
    static bool IsDirectGuest(const Buffer& buffer) {
        if constexpr (requires { buffer.IsDirectGuest(); }) {
            return buffer.IsDirectGuest();
        }
        return false;
    }
    std::optional<bool> TryDirectDMACopy(GPUVAddr src, GPUVAddr dst, u64 size);
    bool TryDirectGuest(Buffer& buffer, DAddr address, u64 size) {
        if constexpr (requires { runtime.TryDirectGuestBuffer(buffer, Common::GuestBackingRange{}); }) {
            const auto backing = device_memory.GetDriverBackingRange(address, size);
            const bool direct = backing && runtime.TryDirectGuestBuffer(buffer, *backing);
            has_direct_guest_buffers |= direct;
            return direct;
        }
        return false;
    }
    bool NeedsGuestCpuAccess(const Buffer& buffer) const {
        if constexpr (requires { runtime.NeedsGuestCpuAccess(buffer); }) {
            return runtime.NeedsGuestCpuAccess(buffer);
        }
        return IsDirectGuest(buffer);
    }
    void BeginGuestCpuAccess(Buffer& buffer) {
        if constexpr (requires { runtime.BeginGuestCpuAccess(buffer); }) {
            runtime.BeginGuestCpuAccess(buffer);
        }
    }
    void EndGuestCpuAccess(Buffer& buffer) {
        if constexpr (requires { runtime.EndGuestCpuAccess(buffer); }) {
            runtime.EndGuestCpuAccess(buffer);
        }
    }
    void ResolveCachedBinding(Binding& binding);
    void WaitGuestBacking(DAddr address, u64 size);
    void GuestMemoryBarrier(bool to_cpu) {
        if constexpr (requires { runtime.GuestMemoryBarrier(to_cpu); }) {
            runtime.GuestMemoryBarrier(to_cpu);
        }
    }
    struct GatherEntry {
        std::unique_ptr<Buffer> buffer;
        std::vector<GatherChunk> chunks;
        u64 last_used{};
        u32 prepared_size{};
        GuestMappingIdentity identity;
        u64 device_generation{};
        bool scatter_pending{}; ///< scratch holds GPU writes not yet copied back to the chunks
        bool aliases_checked{}; ///< complete scratch range was validated for GPU writes
    };
    void ValidateGatherAliases(std::span<const GatherAliasRange> ranges);
    std::vector<GatherAliasRange> checked_alias_ranges;
    GuestMappingIdentity checked_alias_identity;
    u64 checked_alias_device_generation{~u64{0}};
    std::unordered_map<GPUVAddr, GatherEntry> gather_buffers;
    u64 gather_used_memory{};
    bool has_direct_guest_buffers{};
    void TrimGatherBuffers(bool aggressive);
    void InvalidateGatherMemory(DAddr device_addr, u64 size);

    /// Marks gathered chunks overlapping [device_addr, +size) as needing a re-copy.
    /// Chunks of the gather entry at `skip` are left alone.
    void NotifyGatherWrite(DAddr device_addr, u64 size, u8 level = 1, GPUVAddr skip = 0);

    /// Gathered storage buffers bound as written since the last FlushGatherWrites.
    std::vector<GPUVAddr> pending_gather_writes;

    /// Gathered storage buffers whose write-back to the device chunks is deferred.
    std::vector<GPUVAddr> scatter_pending;
    bool scatter_busy = false; ///< inside a write-back (its FindBuffer calls must not recurse)
    u64 unmap_readback_count{}, unmap_readback_bytes{}, unmap_readback_time_ns{};
    u64 cpu_write_preserve_count{}, cpu_write_preserve_time_ns{};
    GPUVAddr scatter_skip = 0; ///< entry being gathered right now; its own chunks don't flush it

    /// Copies a deferred gather entry back to its device chunks.
    void ScatterGatherEntry(GPUVAddr gpu_addr, GatherEntry& entry);
    /// Writes back deferred entries with a chunk overlapping [device_addr, +size); size 0 = all.
    void ResolveScatter(DAddr device_addr, u64 size);
    static constexpr u64 TICKS_TO_DESTROY = 8;
    DelayedDestructionRing<Buffer, TICKS_TO_DESTROY> delayed_destruction_ring;

    const Tegra::Engines::DrawManager::IndirectParams* current_draw_indirect{};

    u32 last_index_count = 0;

    MemoryTracker memory_tracker;
    Common::RangeSet<DAddr> uncommitted_gpu_modified_ranges;
    Common::RangeSet<DAddr> gpu_modified_ranges;
    std::deque<Common::RangeSet<DAddr>> committed_gpu_modified_ranges;

    // Async Buffers
    Common::OverlapRangeSet<DAddr> async_downloads;
    std::deque<std::optional<Async_Buffer>> async_buffers;
    std::deque<boost::container::small_vector<BufferCopy, 4>> pending_downloads;
    std::optional<Async_Buffer> current_buffer;

    std::deque<Async_Buffer> async_buffers_death_ring;

    size_t immediate_buffer_capacity = 0;
    Common::ScratchBuffer<u8> immediate_buffer_alloc;

    struct LRUItemParams {
        using ObjectType = BufferId;
        using TickType = u64;
    };
    Common::LeastRecentlyUsedCache<LRUItemParams> lru_cache;
    u64 frame_tick = 0;

    /// How often the guest recently wrote into pages this cache was watching, per caching page:
    /// frame of the last write in the upper 16 bits, count in the lower 16. One flat table like
    /// page_table: the host commits its pages when they are first written, so only the parts of
    /// the address space the guest writes to take memory.
    static constexpr u16 HOT_WRITE_COUNT = 4;
    static constexpr u16 WRITE_HEAT_FRAMES = 30;
    static constexpr u64 MAX_WIDENED_WRITE = 16 * CACHING_PAGESIZE;
    Common::VirtualBuffer<u32> write_heat;

    u64 total_used_memory = 0; // Bytes owned by this cache.
    CacheMemoryPressure memory_pressure;
    BufferId inline_buffer_id;

    u64 large_buffer_memory = 0;
    u64 evicted_buffer_bytes = 0;
    u32 buffer_count = 0;
    u32 large_buffer_count = 0;
    u64 unbacked_write_back_count = 0; ///< WriteBackDownload calls that met unmapped pages

    Common::VirtualBuffer<u32> page_table;
    Common::ScratchBuffer<u8> tmp_buffer;
};

} // namespace VideoCommon
