// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/terminal_fail_stop.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "core/restricted_process_renderer.h"
#include "video_core/renderer_base.h"

namespace Core::Detail {
namespace {
[[noreturn]] void ForbiddenSubmission() {
    Common::TerminalFailStop("restricted_owner.renderer", "submission_forbidden");
}

// No rendering/channel/fence request can be accepted, so these cold cache
// maintenance operations truthfully have no cached content or queued fences.
// Never invoke a callback or manufacture a GPU completion for this owner gate.
class RestrictedRasterizer final : public VideoCore::RasterizerInterface {
public:
    void Draw(bool, u32) override { ForbiddenSubmission(); }
    void DrawIndirect() override { ForbiddenSubmission(); }
    void DrawTexture() override { ForbiddenSubmission(); }
    void Clear(u32) override { ForbiddenSubmission(); }
    void DispatchCompute() override { ForbiddenSubmission(); }
    void ResetCounter(VideoCommon::QueryType) override { ForbiddenSubmission(); }
    void Query(GPUVAddr, VideoCommon::QueryType, VideoCommon::QueryPropertiesFlags, u32, u32) override {
        ForbiddenSubmission();
    }
    void BindGraphicsUniformBuffer(size_t, u32, GPUVAddr, u32) override { ForbiddenSubmission(); }
    void DisableGraphicsUniformBuffer(size_t, u32) override { ForbiddenSubmission(); }
    void SignalFence(std::function<void()>&&) override { ForbiddenSubmission(); }
    void SyncOperation(std::function<void()>&&) override { ForbiddenSubmission(); }
    void SignalSyncPoint(u32) override { ForbiddenSubmission(); }
    void SignalReference() override { ForbiddenSubmission(); }
    void ReleaseFences(bool) override {}
    void FlushAll() override {}
    void FlushRegion(DAddr, u64, VideoCommon::CacheType) override {}
    bool MustFlushRegion(DAddr, u64, VideoCommon::CacheType) override { return false; }
    VideoCore::RasterizerDownloadArea GetFlushArea(DAddr addr, u64 size) override {
        return {addr, addr + size, false};
    }
    void InvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void OnCacheInvalidation(PAddr, u64) override {}
    bool OnCPUWrite(PAddr, u64) override { return false; }
    void InvalidateGPUCache() override {}
    void UnmapMemory(DAddr, u64) override {}
    void ModifyGPUMemory(size_t, GPUVAddr, u64) override {}
    void FlushAndInvalidateRegion(DAddr, u64, VideoCommon::CacheType) override {}
    void WaitForIdle() override {}
    void FragmentBarrier() override { ForbiddenSubmission(); }
    void TiledCacheBarrier() override { ForbiddenSubmission(); }
    void FlushCommands() override {}
    void TickFrame() override {}
    bool AccelerateSurfaceCopy(const Tegra::Engines::Fermi2D::Surface&,
                              const Tegra::Engines::Fermi2D::Surface&,
                              const Tegra::Engines::Fermi2D::Config&) override {
        ForbiddenSubmission();
    }
    Tegra::Engines::AccelerateDMAInterface& AccessAccelerateDMA() override { ForbiddenSubmission(); }
    void AccelerateInlineToMemory(GPUVAddr, size_t, std::span<const u8>) override { ForbiddenSubmission(); }
    void LoadDiskResources(u64, std::stop_token, const VideoCore::DiskResourceLoadCallback&) override {
        ForbiddenSubmission();
    }
    void InitializeChannel(Tegra::Control::ChannelState&) override { ForbiddenSubmission(); }
    void BindChannel(Tegra::Control::ChannelState&) override { ForbiddenSubmission(); }
    void ReleaseChannel(s32) override { ForbiddenSubmission(); }
    void RegisterTransformFeedback(GPUVAddr) override { ForbiddenSubmission(); }
};

class RestrictedRenderer final : public VideoCore::RendererBase {
public:
    explicit RestrictedRenderer(Frontend::EmuWindow& window)
        : RendererBase{window, window.CreateSharedContext()} {}
    void Composite(std::span<const Tegra::FramebufferConfig>) override { ForbiddenSubmission(); }
    std::vector<u8> GetAppletCaptureBuffer() override { ForbiddenSubmission(); }
    VideoCore::RasterizerInterface* ReadRasterizer() override { return &rasterizer; }
    std::string GetDeviceVendor() const override { return "RESTRICTED_COLD_OWNER"; }
private:
    RestrictedRasterizer rasterizer;
};
}

std::unique_ptr<VideoCore::RendererBase> CreateRestrictedProcessRenderer(Frontend::EmuWindow& window) {
    return std::make_unique<RestrictedRenderer>(window);
}
}
