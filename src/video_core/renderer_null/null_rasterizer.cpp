// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/alignment.h"
#include "video_core/control/channel_state.h"
#include "video_core/host1x/host1x.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace Null {

AccelerateDMA::AccelerateDMA() = default;

bool AccelerateDMA::BufferCopy(GPUVAddr start_address, GPUVAddr end_address, u64 amount) {
    return true;
}
bool AccelerateDMA::BufferClear(GPUVAddr src_address, u64 amount, u32 value) {
    return true;
}

RasterizerNull::RasterizerNull(Tegra::GPU& gpu) : m_gpu{gpu} {}
RasterizerNull::~RasterizerNull() = default;

void RasterizerNull::Draw(bool is_indexed, u32 instance_count) {}
void RasterizerNull::DrawTexture() {}
void RasterizerNull::Clear(u32 layer_count) {}
void RasterizerNull::DispatchCompute() {}
void RasterizerNull::ResetCounter(VideoCommon::QueryType type) {}
void RasterizerNull::Query(GPUVAddr gpu_addr, VideoCommon::QueryType type,
                           VideoCommon::QueryPropertiesFlags flags, u32 payload, u32 subreport) {
    if (!gpu_memory) {
        return;
    }
    if (True(flags & VideoCommon::QueryPropertiesFlags::HasTimeout)) {
        u64 ticks = m_gpu.GetTicks();
        gpu_memory->Write<u64>(gpu_addr + 8, ticks);
        gpu_memory->Write<u64>(gpu_addr, static_cast<u64>(payload));
    } else {
        gpu_memory->Write<u32>(gpu_addr, payload);
    }
}
void RasterizerNull::BindGraphicsUniformBuffer(size_t stage, u32 index, GPUVAddr gpu_addr,
                                               u32 size) {}
void RasterizerNull::DisableGraphicsUniformBuffer(size_t stage, u32 index) {}
void RasterizerNull::FlushAll() {}
void RasterizerNull::FlushRegion(DAddr addr, u64 size, VideoCommon::CacheType) {}
bool RasterizerNull::MustFlushRegion(DAddr addr, u64 size, VideoCommon::CacheType) {
    return false;
}
void RasterizerNull::InvalidateRegion(DAddr addr, u64 size, VideoCommon::CacheType) {}
bool RasterizerNull::OnCPUWrite(PAddr addr, u64 size) {
    return false;
}
void RasterizerNull::OnCacheInvalidation(PAddr addr, u64 size) {}
VideoCore::RasterizerDownloadArea RasterizerNull::GetFlushArea(PAddr addr, u64 size) {
    VideoCore::RasterizerDownloadArea new_area{
        .start_address = Common::AlignDown(addr, Core::DEVICE_PAGESIZE),
        .end_address = Common::AlignUp(addr + size, Core::DEVICE_PAGESIZE),
        .preemtive = true,
    };
    return new_area;
}
void RasterizerNull::InvalidateGPUCache() {}
void RasterizerNull::UnmapMemory(DAddr addr, u64 size) {}
void RasterizerNull::ModifyGPUMemory(size_t as_id, GPUVAddr addr, u64 size) {}
void RasterizerNull::SignalFence(std::function<void()>&& func) {
    func();
}
void RasterizerNull::SyncOperation(std::function<void()>&& func) {
    func();
}
void RasterizerNull::SignalSyncPoint(u32 value) {
    auto& syncpoint_manager = m_gpu.Host1x().GetSyncpointManager();
    syncpoint_manager.IncrementGuest(value);
    syncpoint_manager.IncrementHost(value);
}
void RasterizerNull::SignalReference() {}
void RasterizerNull::ReleaseFences(bool) {}
void RasterizerNull::FlushAndInvalidateRegion(DAddr addr, u64 size, VideoCommon::CacheType) {}
void RasterizerNull::WaitForIdle() {}
void RasterizerNull::FragmentBarrier() {}
void RasterizerNull::TiledCacheBarrier() {}
void RasterizerNull::FlushCommands() {}
void RasterizerNull::TickFrame() {}
Tegra::Engines::AccelerateDMAInterface& RasterizerNull::AccessAccelerateDMA() {
    return m_accelerate_dma;
}
bool RasterizerNull::AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface& src,
                                           const Tegra::Engines::Fermi2D::Surface& dst,
                                           const Tegra::Engines::Fermi2D::Config& copy_config) {
    return true;
}
void RasterizerNull::AccelerateInlineToMemory(GPUVAddr address, size_t copy_size,
                                              std::span<const u8> memory) {}
void RasterizerNull::LoadDiskResources(u64 title_id, std::stop_token stop_loading,
                                       const VideoCore::DiskResourceLoadCallback& callback) {}
void RasterizerNull::InitializeChannel(Tegra::Control::ChannelState& channel) {
    CreateChannel(channel);
}
void RasterizerNull::BindChannel(Tegra::Control::ChannelState& channel) {
    BindToChannel(channel.bind_id);
}
void RasterizerNull::ReleaseChannel(s32 channel_id) {
    EraseChannel(channel_id);
}

} // namespace Null
