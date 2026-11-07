// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <type_traits>
#include <utility>
#include <array>

#include "common/settings.h"
#include "common/terminal_fail_stop.h"
#include "common/thread.h"
#include "core/frontend/emu_window.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_surface.h"

namespace Vulkan {

namespace {

bool CanBlitToSwapchain(const vk::PhysicalDevice& physical_device, VkFormat format) {
    const VkFormatProperties props{physical_device.GetFormatProperties(format)};
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
}

[[nodiscard]] VkImageSubresourceLayers MakeImageSubresourceLayers() {
    return VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] VkImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                        s32 swapchain_height) {
    return VkImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            {
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                {
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            {
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                {
                    .x = swapchain_width,
                    .y = swapchain_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] VkImageCopy MakeImageCopy(u32 frame_width, u32 frame_height, u32 swapchain_width,
                                        u32 swapchain_height) {
    return VkImageCopy{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .extent =
            {
                .width = std::min(frame_width, swapchain_width),
                .height = std::min(frame_height, swapchain_height),
                .depth = 1,
            },
    };
}

} // Anonymous namespace

PresentManager::PresentManager(const vk::Instance& instance_,
                               Core::Frontend::EmuWindow& render_window_, const Device& device_,
                               MemoryAllocator& memory_allocator_, Scheduler& scheduler_,
                               Swapchain& swapchain_, vk::SurfaceKHR& surface_)
    : instance{instance_}, render_window{render_window_}, device{device_},
      memory_allocator{memory_allocator_}, scheduler{scheduler_}, swapchain{swapchain_},
      surface{surface_}, blit_supported{CanBlitToSwapchain(device.GetPhysical(),
                                                           swapchain.GetImageViewFormat())},
      use_present_thread{Settings::values.async_presentation.GetValue()} {
    SetImageCount();
    UpdateFramePipelineLimit();

    auto& dld = device.GetLogical();
    cmdpool = dld.CreateCommandPool({
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    });
    auto cmdbuffers = cmdpool.Allocate(image_count);

    frames.resize(image_count);
    for (u32 i = 0; i < frames.size(); i++) {
        Frame& frame = frames[i];
        frame.cmdbuf = vk::CommandBuffer{cmdbuffers[i], device.GetDispatchLoader()};
        frame.render_ready = dld.CreateSemaphore({
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        });
        frame.present_done = dld.CreateFence({
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        });
        free_queue.push(&frame);
    }

    if (use_present_thread) {
        present_thread = std::jthread([this](std::stop_token token) { PresentThread(token); });
    }
}

PresentManager::~PresentManager() {
    Shutdown();
}

void PresentManager::Shutdown() {
    if (!present_thread.joinable()) {
        return;
    }
    // The recording worker can enqueue Present closures, so its host work must
    // be drained by the renderer first. WaitPresent includes the last active frame.
    WaitPresent();
    present_thread.request_stop();
    frame_cv.notify_all();
    present_thread.join();
}

Frame* PresentManager::GetRenderFrame() {
    // Bound the whole Mailbox async pipeline before another frame can enter present_queue. Waiting
    // here preserves the fences and semaphores of frames that are already rendered or in flight.
    std::unique_lock lock{free_mutex};
    free_cv.wait(lock, [this] {
        return !free_queue.empty() &&
               (!limit_mailbox_frames || frames_in_flight < MAX_MAILBOX_FRAMES_IN_FLIGHT);
    });

    // Take the frame from the queue
    Frame* frame = free_queue.front();
    free_queue.pop();
    ++frames_in_flight;

    // Wait for the presentation to be finished so all frame resources are free
    const char* stage = "present.frame_completion";
    try {
        const auto completed = frame->present_done.Wait();
        if (completed != VK_SUCCESS) {
            Common::TerminalFailStop(stage, "vulkan_result", static_cast<int>(completed));
        }
        stage = "present.frame_reset";
        frame->present_done.Reset();
    } catch (const vk::Exception& error) {
        // Reuse is uncertain. Do not unwind frame/renderer owners or reset/retry
        // an unconfirmed fence; host presentation drainage is not native completion.
        Common::TerminalFailStop(stage, "vulkan_exception", static_cast<int>(error.GetResult()));
    } catch (...) {
        Common::TerminalFailStop(stage, "host_exception");
    }

    return frame;
}

void PresentManager::Present(Frame* frame) {
    if (!use_present_thread) {
        scheduler.WaitWorker();
        CopyToSwapchain(frame);
        ReleaseRenderFrame(frame);
        return;
    }

    scheduler.Record([this, frame](vk::CommandBuffer) {
        std::unique_lock lock{queue_mutex};
        present_queue.push(frame);
        frame_cv.notify_one();
    });
}

void PresentManager::RecreateFrame(Frame* frame, u32 width, u32 height, VkFormat image_view_format,
                                   VkRenderPass rd) {
    scheduler.RequireNativeBorrowAdmission();
    auto& dld = device.GetLogical();
    Frame candidate{};

    candidate.width = width;
    candidate.height = height;
    candidate.image_view_format = image_view_format;

    // The frame is only ever viewed as image_view_format. Mutable without a format list lets the
    // driver assume any format; Turnip then stores the image linear (tu_image.cc).
    const VkFormat image_format = swapchain.GetImageFormat();
    const std::array view_formats{image_format, image_view_format};
    const VkImageFormatListCreateInfo format_list{
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .pNext = nullptr,
        .viewFormatCount = static_cast<u32>(view_formats.size()),
        .pViewFormats = view_formats.data(),
    };
    const bool is_mutable = image_view_format != image_format;
    candidate.image = memory_allocator.CreateImage({
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = is_mutable && device.IsKhrImageFormatListSupported() ? &format_list : nullptr,
        .flags = is_mutable ? VkImageCreateFlags{VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT} : 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image_format,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });

    candidate.image_view = dld.CreateImageView({
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = *candidate.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_view_format,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    });

    const VkImageView image_view{*candidate.image_view};
    candidate.framebuffer = dld.CreateFramebuffer({
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = rd,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = width,
        .height = height,
        .layers = 1,
    });
    // No work records this candidate. Swap only the resource tuple after every
    // child exists; leave command buffer, semaphores and fence identities intact.
    static_assert(std::is_nothrow_swappable_v<vk::Image>);
    static_assert(std::is_nothrow_swappable_v<vk::ImageView>);
    static_assert(std::is_nothrow_swappable_v<vk::Framebuffer>);
    std::swap(frame->image, candidate.image);
    std::swap(frame->image_view, candidate.image_view);
    std::swap(frame->framebuffer, candidate.framebuffer);
    frame->width = candidate.width;
    frame->height = candidate.height;
    frame->image_view_format = candidate.image_view_format;
    // candidate now retires old framebuffer, view, image in that order. The
    // owning caller must already have completed old host/presentation/native use.
}

void PresentManager::WaitPresent() {
    if (!use_present_thread) {
        return;
    }

    // Wait for the present queue to be empty
    {
        std::unique_lock queue_lock{queue_mutex};
        frame_cv.wait(queue_lock, [this] { return present_queue.empty(); });
    }

    // The above condition will be satisfied when the last frame is taken from the queue.
    // To ensure that frame has been presented as well take hold of the swapchain
    // mutex.
    std::scoped_lock swapchain_lock{swapchain_mutex};
}

void PresentManager::PresentThread(std::stop_token token) {
    Common::SetCurrentThreadName("VulkanPresent");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::High);
    while (!token.stop_requested()) {
        std::unique_lock lock{queue_mutex};

        // Wait for presentation frames
        Common::CondvarWait(frame_cv, lock, token, [this] { return !present_queue.empty(); });
        if (token.stop_requested()) {
            return;
        }

        // Take the frame and notify anyone waiting
        Frame* frame = present_queue.front();
        present_queue.pop();
        frame_cv.notify_one();

        // By exchanging the lock ownership we take the swapchain lock
        // before the queue lock goes out of scope. This way the swapchain
        // lock in WaitPresent is guaranteed to occur after here.
        std::exchange(lock, std::unique_lock{swapchain_mutex});

        CopyToSwapchain(frame);

        ReleaseRenderFrame(frame);
    }
}

void PresentManager::RecreateSwapchain(Frame* frame) {
    if (!swapchain.Create(*surface, frame->width, frame->height)) {
        Common::TerminalFailStop("present.recreate", "surface_unavailable");
    }
    UpdateFramePipelineLimit();
}

void PresentManager::SetImageCount() {
    // We cannot have more than 7 images in flight at any given time.
    // FRAMES_IN_FLIGHT is 8, and the cache TICKS_TO_DESTROY is 8.
    // Mali drivers will give us 6.
    image_count = std::min<size_t>(swapchain.GetImageCount(), 7);
}

void PresentManager::UpdateFramePipelineLimit() {
    {
        std::scoped_lock lock{free_mutex};
#ifdef ANDROID
        limit_mailbox_frames = use_present_thread && swapchain.IsMailbox();
#else
        limit_mailbox_frames = false;
#endif
    }
    free_cv.notify_all();
}

void PresentManager::ReleaseRenderFrame(Frame* frame) {
    {
        std::scoped_lock lock{free_mutex};
        free_queue.push(frame);
        --frames_in_flight;
    }
    free_cv.notify_one();
}

void PresentManager::CopyToSwapchain(Frame* frame) {
    bool requires_recreation = false;
    bool copy_started = false;

    while (true) {
        try {
            // Recreate surface and swapchain if needed.
            if (requires_recreation) {
                auto candidate = CreateSurface(instance, render_window.GetWindowInfo());
                if (!swapchain.Create(*candidate, frame->width, frame->height)) {
                    Common::TerminalFailStop("present.recreate", "surface_unavailable");
                }
                // Create retired the old child swapchain before its surface owner.
                surface = std::move(candidate);
                UpdateFramePipelineLimit();
            }

            // Draw to swapchain.
            return CopyToSwapchainImpl(frame, copy_started);
        } catch (const vk::Exception& except) {
            if (except.GetResult() != VK_ERROR_SURFACE_LOST_KHR || copy_started) {
                Common::TerminalFailStop("present.copy", "vulkan_exception",
                                         static_cast<int>(except.GetResult()));
            }

            requires_recreation = true;
        } catch (...) {
            Common::TerminalFailStop("present.copy", "host_exception");
        }
    }
}

void PresentManager::CopyToSwapchainImpl(Frame* frame, bool& copy_started) {

    // If the size of the incoming frames has changed, recreate the swapchain
    // to account for that.
    const bool is_suboptimal = swapchain.NeedsRecreation();
    const bool size_changed =
        swapchain.GetWidth() != frame->width || swapchain.GetHeight() != frame->height;
    if (is_suboptimal || size_changed) {
        RecreateSwapchain(frame);
    }

    while (swapchain.AcquireNextImage()) {
        RecreateSwapchain(frame);
    }

    // Never replay after recording starts: render_ready/fence effects may survive.
    copy_started = true;
    const vk::CommandBuffer cmdbuf{frame->cmdbuf};
    cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });

    const VkImage image{swapchain.CurrentImage()};
    const VkExtent2D extent = swapchain.GetExtent();
    const std::array pre_barriers{
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *frame->image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };
    const std::array post_barriers{
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *frame->image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };

    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {},
                           {}, {}, pre_barriers);

    if (blit_supported) {
        cmdbuf.BlitImage(*frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         MakeImageBlit(frame->width, frame->height, extent.width, extent.height),
                         VK_FILTER_LINEAR);
    } else {
        cmdbuf.CopyImage(*frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         MakeImageCopy(frame->width, frame->height, extent.width, extent.height));
    }

    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, {},
                           {}, {}, post_barriers);

    cmdbuf.End();

    const VkSemaphore present_semaphore = swapchain.CurrentPresentSemaphore();
    const VkSemaphore render_semaphore = swapchain.CurrentRenderSemaphore();
    const std::array wait_semaphores = {present_semaphore, *frame->render_ready};

    static constexpr std::array<VkPipelineStageFlags, 2> wait_stage_masks{
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    };

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 2U,
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = cmdbuf.address(),
        .signalSemaphoreCount = 1U,
        .pSignalSemaphores = &render_semaphore,
    };

    // Submit the image copy/blit to the swapchain
    {
        std::scoped_lock submit_lock{scheduler.submit_mutex};
        switch (const VkResult result =
                    device.GetGraphicsQueue().Submit(submit_info, *frame->present_done)) {
        case VK_SUCCESS:
            break;
        case VK_ERROR_DEVICE_LOST:
            device.ReportLoss();
            [[fallthrough]];
        default:
            vk::Check(result);
            break;
        }
    }

    // Present
    swapchain.Present(render_semaphore);
}

} // namespace Vulkan
