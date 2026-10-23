// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <deque>
#include <memory>
#include <span>
#include <vector>

#include "video_core/texture_cache/texture_cache_base.h"

#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/vk_compute_pass.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/texture_cache/image_view_base.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Settings {
struct ResolutionScalingInfo;
}

namespace Vulkan {

using Common::SlotVector;
using VideoCommon::ImageId;
using VideoCommon::NUM_RT;
using VideoCommon::Region2D;
using VideoCommon::RenderTargets;
using VideoCore::Surface::PixelFormat;

class BlitImageHelper;
class DescriptorPool;
class Device;
class Image;
class ImageView;
class Framebuffer;
class RenderPassCache;
class StagingBufferPool;
class Scheduler;

class TextureCacheRuntime {
public:
    explicit TextureCacheRuntime(const Device& device_, Scheduler& scheduler_,
                                 MemoryAllocator& memory_allocator_,
                                 StagingBufferPool& staging_buffer_pool_,
                                 BlitImageHelper& blit_image_helper_,
                                 RenderPassCache& render_pass_cache_,
                                 DescriptorPool& descriptor_pool,
                                 ComputePassDescriptorQueue& compute_pass_descriptor_queue);
    ~TextureCacheRuntime();

    void Finish();

    StagingBufferRef UploadStagingBuffer(size_t size);

    StagingBufferRef DownloadStagingBuffer(size_t size, bool deferred = false);

    void FreeDeferredStagingBuffer(StagingBufferRef& ref);

    void TickFrame();

    u64 GetDeviceLocalMemory() const;

    u64 GetDeviceMemoryUsage() const;

    GCDownloadRef TryGCDownloadStagingBuffer(size_t size);

    bool CanReportMemoryUsage() const;

    void BlitImage(Framebuffer* dst_framebuffer, ImageView& dst, ImageView& src,
                   const Region2D& dst_region, const Region2D& src_region,
                   Tegra::Engines::Fermi2D::Filter filter,
                   Tegra::Engines::Fermi2D::Operation operation);

    void CopyImage(Image& dst, Image& src, std::span<const VideoCommon::ImageCopy> copies);

    void CopyImageMSAA(Image& dst, Image& src, std::span<const VideoCommon::ImageCopy> copies);

    bool ShouldReinterpret(Image& dst, Image& src);

    void ReinterpretImage(Image& dst, Image& src, std::span<const VideoCommon::ImageCopy> copies);

    void ConvertImage(Framebuffer* dst, ImageView& dst_view, ImageView& src_view);

    bool CanAccelerateImageUpload(Image&) const noexcept {
        return false;
    }

    bool CanUploadMSAA() const noexcept {
        // TODO: Implement buffer to MSAA uploads
        return false;
    }

    void AccelerateImageUpload(Image&, const StagingBufferRef&,
                               std::span<const VideoCommon::SwizzleParameters>);

    void InsertUploadMemoryBarrier() {}

    void TransitionImageLayout(Image& image);

    bool HasBrokenTextureViewFormats() const noexcept {
        // No known Vulkan driver has broken image views
        return false;
    }

    /// Whether a BGR view of an RGB image (and the reverse) is made as a view of the same image.
    /// Without it the texture cache keeps BGR and RGB in separate images and copies between them.
    bool HasNativeBgr() const noexcept {
        return native_bgr;
    }

    [[nodiscard]] VkBuffer GetTemporaryBuffer(size_t needed_size);
    void CleanupUnusedBuffers();

    /// Formats an image of this format may be viewed as. The narrow list holds only the formats
    /// with the same components (A8B8G8R8 UNORM, SNORM, SINT, UINT and SRGB for example), so a
    /// driver can keep framebuffer compression on the image; the wide list holds every format a
    /// view of it may need.
    std::span<const VkFormat> ViewFormats(PixelFormat format, bool wide) const {
        const auto index = static_cast<std::size_t>(format);
        return wide ? view_formats[index] : narrow_view_formats[index];
    }

    /// Keeps the replaced images of an image that was made again with the wide format list (and
    /// what was made from them) until the GPU and the delayed view destruction are done with them.
    void RetireImage(vk::Image original, vk::Image scaled,
                     std::vector<vk::ImageView> storage_views,
                     std::unique_ptr<Framebuffer> scale_framebuffer,
                     std::unique_ptr<ImageView> scale_view,
                     std::unique_ptr<Framebuffer> normal_framebuffer,
                     std::unique_ptr<ImageView> normal_view);

    void BarrierFeedbackLoop();

    const Device& device;
    Scheduler& scheduler;
    MemoryAllocator& memory_allocator;
    StagingBufferPool& staging_buffer_pool;
    BlitImageHelper& blit_image_helper;
    RenderPassCache& render_pass_cache;
    std::optional<ASTCDecoderPass> astc_decoder_pass;
    std::unique_ptr<MSAACopyPass> msaa_copy_pass;
    const Settings::ResolutionScalingInfo& resolution;
    std::array<std::vector<VkFormat>, VideoCore::Surface::MaxPixelFormat> view_formats;
    std::array<std::vector<VkFormat>, VideoCore::Surface::MaxPixelFormat> narrow_view_formats;
    bool native_bgr = true;

    struct RetiredImage;
    std::deque<std::unique_ptr<RetiredImage>> retired_images;
    u64 frame_count{};

    static constexpr size_t indexing_slots = 8 * sizeof(size_t);
    std::array<vk::Buffer, indexing_slots> buffers{};

    /// Custom border colour slots handed to samplers so far (never given back, see Sampler).
    u32 custom_border_samplers{};
    /// Whether running out of those slots has been logged.
    bool custom_border_budget_reported{};
};

class Image : public VideoCommon::ImageBase {
public:
    explicit Image(TextureCacheRuntime&, const VideoCommon::ImageInfo& info, GPUVAddr gpu_addr,
                   VAddr cpu_addr);
    explicit Image(const VideoCommon::NullImageParams&);

    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    Image(Image&&) = default;
    Image& operator=(Image&&) = default;

    void UploadMemory(VkBuffer buffer, VkDeviceSize offset,
                      std::span<const VideoCommon::BufferImageCopy> copies);

    void UploadMemory(const StagingBufferRef& map,
                      std::span<const VideoCommon::BufferImageCopy> copies);

    void DownloadMemory(VkBuffer buffer, size_t offset,
                        std::span<const VideoCommon::BufferImageCopy> copies);

    void DownloadMemory(std::span<VkBuffer> buffers, std::span<size_t> offsets,
                        std::span<const VideoCommon::BufferImageCopy> copies);

    void DownloadMemory(const StagingBufferRef& map,
                        std::span<const VideoCommon::BufferImageCopy> copies);

    [[nodiscard]] VkImage Handle() const noexcept {
        return current_image;
    }

    [[nodiscard]] VkImageAspectFlags AspectMask() const noexcept {
        return aspect_mask;
    }

    /// Returns true when the image is already initialized and mark it as initialized
    [[nodiscard]] bool ExchangeInitialization() noexcept {
        return std::exchange(initialized, true);
    }

    VkImageView StorageImageView(s32 level) noexcept;

    bool IsRescaled() const noexcept;

    bool ScaleUp(bool ignore = false);

    bool ScaleDown(bool ignore = false);

    // Allows cache GC to defer readbacks that may allocate scaling helper resources.
    bool NeedsScaleHelper() const;

    /// Whether a view in this format needs the image made again with the wide format list.
    [[nodiscard]] bool NeedsWideViews(PixelFormat view_format) const;

    /// Same for a typed storage view (Shader::ImageFormat other than Typeless).
    [[nodiscard]] bool NeedsWideStorageViews(Shader::ImageFormat storage_format) const;

    /// Makes the image again with the wide format list and copies its contents. Every view of
    /// the old image has to be dropped by the caller. Returns false when it already had it.
    bool WidenViewFormats();

private:
    bool BlitScaleHelper(bool scale_up);

    [[nodiscard]] bool NeedsWideVkFormat(VkFormat view_format) const;

    [[nodiscard]] VideoCommon::ImageInfo ScaledInfo() const;

    void MakeStorageImageViews();

    Scheduler* scheduler{};
    TextureCacheRuntime* runtime{};

    vk::Image original_image;
    std::vector<vk::ImageView> storage_image_views;
    VkImageAspectFlags aspect_mask = 0;
    bool initialized = false;
    /// Whether the image was made with the wide view format list
    bool wide_view_formats = false;
    vk::Image scaled_image{};
    VkImage current_image{};

    std::unique_ptr<Framebuffer> scale_framebuffer;
    std::unique_ptr<ImageView> scale_view;

    std::unique_ptr<Framebuffer> normal_framebuffer;
    std::unique_ptr<ImageView> normal_view;
};

class ImageView : public VideoCommon::ImageViewBase {
public:
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::ImageViewInfo&, ImageId, Image&);
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::ImageViewInfo&, ImageId, Image&,
                       const SlotVector<Image>&);
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::ImageInfo&,
                       const VideoCommon::ImageViewInfo&, GPUVAddr);
    explicit ImageView(TextureCacheRuntime&, const VideoCommon::NullImageViewParams&);

    ~ImageView();

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;

    ImageView(ImageView&&) = default;
    ImageView& operator=(ImageView&&) = default;

    [[nodiscard]] VkImageView DepthView();

    [[nodiscard]] VkImageView StencilView();

    [[nodiscard]] VkImageView ColorView();

    [[nodiscard]] VkImageView StorageView(Shader::TextureType texture_type,
                                          Shader::ImageFormat image_format);

    [[nodiscard]] bool IsRescaled() const noexcept;

    [[nodiscard]] VkImageView Handle(Shader::TextureType texture_type) const noexcept {
        return *image_views[static_cast<size_t>(texture_type)];
    }

    [[nodiscard]] VkImageView HandleNonSrgb(Shader::TextureType texture_type);

    [[nodiscard]] VkImage ImageHandle() const noexcept {
        return image_handle;
    }

    [[nodiscard]] VkImageView RenderTarget() const noexcept {
        return render_target;
    }

    [[nodiscard]] VkSampleCountFlagBits Samples() const noexcept {
        return samples;
    }

    [[nodiscard]] bool SupportsDepthComparison() const noexcept {
        return supports_depth_comparison;
    }

    /// False when the view's format cannot be sampled with a linear filter (Turnip: the 32-bit
    /// float formats), in which case a linear sampler must not be paired with it
    [[nodiscard]] bool SupportsLinearFilter() const noexcept {
        return supports_linear_filter;
    }

    [[nodiscard]] GPUVAddr GpuAddr() const noexcept {
        return gpu_addr;
    }

    [[nodiscard]] u32 BufferSize() const noexcept {
        return buffer_size;
    }

    /// VkImage type backing this view (e3D images use VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT).
    [[nodiscard]] VideoCommon::ImageType PhysicalImageType() const noexcept {
        return physical_image_type;
    }

private:
    struct StorageViews {
        std::array<vk::ImageView, Shader::NUM_TEXTURE_TYPES> signeds;
        std::array<vk::ImageView, Shader::NUM_TEXTURE_TYPES> unsigneds;
        /// Typeless accesses, in the view's own format with the identity swizzle
        std::array<vk::ImageView, Shader::NUM_TEXTURE_TYPES> typeless;
    };

    [[nodiscard]] vk::ImageView MakeView(VkFormat view_format, VkImageAspectFlags aspect);

    /// A storage view in the sampled format, shaped like Handle(texture_type)
    [[nodiscard]] vk::ImageView MakeTypelessStorageView(Shader::TextureType texture_type);

    const Device* device = nullptr;
    const SlotVector<Image>* slot_images = nullptr;

    std::array<vk::ImageView, Shader::NUM_TEXTURE_TYPES> image_views;
    std::array<vk::ImageView, Shader::NUM_TEXTURE_TYPES> non_srgb_image_views;
    std::unique_ptr<StorageViews> storage_views;
    vk::ImageView depth_view;
    vk::ImageView stencil_view;
    vk::ImageView color_view;
    vk::Image null_image;
    VkImage image_handle = VK_NULL_HANDLE;
    VkImageView render_target = VK_NULL_HANDLE;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkFormat vk_format = VK_FORMAT_UNDEFINED;
    VkComponentMapping stored_components{};
    VkImageAspectFlags view_aspect_mask = 0;
    u32 buffer_size = 0;
    VideoCommon::ImageType physical_image_type = VideoCommon::ImageType::e2D;
    bool supports_depth_comparison = false;
    bool supports_linear_filter = true;
};

class ImageAlloc : public VideoCommon::ImageAllocBase {};

class Sampler {
public:
    explicit Sampler(TextureCacheRuntime&, const Tegra::Texture::TSCEntry&);

    [[nodiscard]] VkSampler Handle() const noexcept {
        return *sampler;
    }

    [[nodiscard]] VkSampler HandleWithDefaultAnisotropy() const noexcept {
        return *sampler_default_anisotropy;
    }

    [[nodiscard]] bool HasAddedAnisotropy() const noexcept {
        return static_cast<bool>(sampler_default_anisotropy);
    }

    [[nodiscard]] VkSampler HandleWithNearestFilter() const noexcept {
        return *sampler_nearest;
    }

    [[nodiscard]] bool HasLinearFiltering() const noexcept {
        return static_cast<bool>(sampler_nearest);
    }

    [[nodiscard]] VkSampler HandleWithoutDepthComparison() const noexcept {
        return *sampler_noncompare;
    }

    [[nodiscard]] bool HasDepthComparison() const noexcept {
        return static_cast<bool>(sampler_noncompare);
    }

    [[nodiscard]] bool IsSrgbConversion() const noexcept {
        return srgb_conversion;
    }

private:
    vk::Sampler sampler;
    vk::Sampler sampler_default_anisotropy;
    vk::Sampler sampler_nearest;
    vk::Sampler sampler_noncompare;
    bool srgb_conversion = true;
};

class Framebuffer {
public:
    explicit Framebuffer(TextureCacheRuntime& runtime, std::span<ImageView*, NUM_RT> color_buffers,
                         ImageView* depth_buffer, const VideoCommon::RenderTargets& key);

    explicit Framebuffer(TextureCacheRuntime& runtime, ImageView* color_buffer,
                         ImageView* depth_buffer, VkExtent2D extent, bool is_rescaled);

    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&&) = default;
    Framebuffer& operator=(Framebuffer&&) = default;

    void CreateFramebuffer(TextureCacheRuntime& runtime,
                           std::span<ImageView*, NUM_RT> color_buffers, ImageView* depth_buffer,
                           bool is_rescaled = false);

    [[nodiscard]] VkFramebuffer Handle() const noexcept {
        return *framebuffer;
    }

    [[nodiscard]] VkRenderPass RenderPass() const noexcept {
        return renderpass;
    }

    [[nodiscard]] VkExtent2D RenderArea() const noexcept {
        return render_area;
    }

    [[nodiscard]] VkSampleCountFlagBits Samples() const noexcept {
        return samples;
    }

    /// Colour attachment slots of the render pass's subpass: the highest bound render target plus
    /// one, since slot i is render target i (vk_render_pass_cache.cpp).
    [[nodiscard]] u32 NumColorAttachments() const noexcept {
        return num_color_attachments;
    }

    [[nodiscard]] u32 NumImages() const noexcept {
        return num_images;
    }

    [[nodiscard]] const std::array<VkImage, 9>& Images() const noexcept {
        return images;
    }

    [[nodiscard]] const std::array<VkImageSubresourceRange, 9>& ImageRanges() const noexcept {
        return image_ranges;
    }

    [[nodiscard]] bool HasAspectColorBit(size_t index) const noexcept {
        return (image_ranges.at(rt_map[index]).aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0;
    }

    [[nodiscard]] bool HasAspectDepthBit() const noexcept {
        return has_depth;
    }

    [[nodiscard]] bool HasAspectStencilBit() const noexcept {
        return has_stencil;
    }

    [[nodiscard]] bool IsRescaled() const noexcept {
        return is_rescaled;
    }

private:
    vk::Framebuffer framebuffer;
    VkRenderPass renderpass{};
    VkExtent2D render_area{};
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    u32 num_color_attachments = 0;
    u32 num_images = 0;
    std::array<VkImage, 9> images{};
    std::array<VkImageSubresourceRange, 9> image_ranges{};
    std::array<size_t, NUM_RT> rt_map{};
    bool has_depth{};
    bool has_stencil{};
    bool is_rescaled{};
};

struct TextureCacheParams {
    static constexpr bool ENABLE_VALIDATION = true;
    static constexpr bool FRAMEBUFFER_BLITS = false;
    static constexpr bool HAS_EMULATED_COPIES = false;
    static constexpr bool HAS_DEVICE_MEMORY_INFO = true;
    static constexpr bool IMPLEMENTS_ASYNC_DOWNLOADS = true;

    using Runtime = Vulkan::TextureCacheRuntime;
    using Image = Vulkan::Image;
    using ImageAlloc = Vulkan::ImageAlloc;
    using ImageView = Vulkan::ImageView;
    using Sampler = Vulkan::Sampler;
    using Framebuffer = Vulkan::Framebuffer;
    using AsyncBuffer = Vulkan::StagingBufferRef;
    using BufferType = VkBuffer;
};

using TextureCache = VideoCommon::TextureCache<TextureCacheParams>;

} // namespace Vulkan
