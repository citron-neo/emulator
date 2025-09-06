// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <boost/container/small_vector.hpp>

#include "common/microprofile.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvnflinger/buffer_item.h"
#include "core/hle/service/nvnflinger/buffer_item_consumer.h"
#include "core/hle/service/nvnflinger/hardware_composer.h"
#include "core/hle/service/nvnflinger/hwc_layer.h"
#include "core/hle/service/nvnflinger/ui/graphic_buffer.h"

namespace Service::Nvnflinger {

namespace {

s32 NormalizeSwapInterval(f32* out_speed_scale, s32 swap_interval) {
    if (swap_interval <= 0) {
        // As an extension, treat nonpositive swap interval as speed multiplier.
        if (out_speed_scale) {
            *out_speed_scale = 2.f * static_cast<f32>(1 - swap_interval);
        }

        swap_interval = 1;
    }

    if (swap_interval >= 5) {
        // As an extension, treat high swap interval as precise speed control.
        if (out_speed_scale) {
            *out_speed_scale = static_cast<f32>(swap_interval) / 100.f;
        }

        swap_interval = 1;
    }

    return swap_interval;
}

} // namespace

HardwareComposer::HardwareComposer() = default;
HardwareComposer::~HardwareComposer() = default;

u32 HardwareComposer::ComposeLocked(f32* out_speed_scale, Display& display,
                                    Nvidia::Devices::nvdisp_disp0& nvdisp) {
    boost::container::small_vector<HwcLayer, 2> composition_stack;

    // Set default speed limit to 100%.
    *out_speed_scale = 1.0f;

    // Determine the number of vsync periods to wait before composing again.
    std::optional<s32> swap_interval{};
    bool has_acquired_buffer{};

    // Acquire all necessary framebuffers.
    for (auto& layer : display.stack.layers) {
        auto consumer_id = layer->consumer_id;

        // Try to fetch the framebuffer (either new or stale).
        const auto result = this->CacheFramebufferLocked(*layer, consumer_id);

        // If we failed, skip this layer.
        if (result == CacheStatus::NoBufferAvailable) {
            continue;
        }

        // If we acquired a new buffer, we need to present.
        if (result == CacheStatus::BufferAcquired) {
            has_acquired_buffer = true;
        }

        const auto& buffer = m_framebuffers[consumer_id];
        const auto& item = buffer.item;
        const auto& igbp_buffer = *item.graphic_buffer;

        // TODO: get proper Z-index from layer
        if (layer->visible) {
            composition_stack.emplace_back(HwcLayer{
                .buffer_handle = igbp_buffer.BufferId(),
                .offset = igbp_buffer.Offset(),
                .format = igbp_buffer.ExternalFormat(),
                .width = igbp_buffer.Width(),
                .height = igbp_buffer.Height(),
                .stride = igbp_buffer.Stride(),
                .z_index = 0,
                .blending = layer->blending,
                .transform = static_cast<android::BufferTransformFlags>(item.transform),
                .crop_rect = item.crop,
                .acquire_fence = item.fence,
            });
        }

        // We need to compose again either before this frame is supposed to
        // be released, or exactly on the vsync period it should be released.
        const s32 item_swap_interval = NormalizeSwapInterval(out_speed_scale, item.swap_interval);

        // TODO: handle cases where swap intervals are relatively prime. So far,
        // only swap intervals of 0, 1 and 2 have been observed, but if 3 were
        // to be introduced, this would cause an issue.
        if (swap_interval) {
            swap_interval = std::min(*swap_interval, item_swap_interval);
        } else {
            swap_interval = item_swap_interval;
        }
    }

    // If any new buffers were acquired, we can present.
    if (has_acquired_buffer) {
        // Sort by Z-index.
        std::stable_sort(composition_stack.begin(), composition_stack.end(),
                         [&](auto& l, auto& r) { return l.z_index < r.z_index; });

        // Composite.
        nvdisp.Composite(composition_stack);
    }

    // Render MicroProfile.
    MicroProfileFlip();

    // Advance by at least one frame.
    const u32 frame_advance = swap_interval.value_or(1);
    m_frame_number += frame_advance;

    // Release any necessary framebuffers.
    for (auto& [layer_id, framebuffer] : m_framebuffers) {
        if (framebuffer.release_frame_number > m_frame_number) {
            // Not yet ready to release this framebuffer.
            continue;
        }

        if (!framebuffer.is_acquired) {
            // Already released.
            continue;
        }

        if (const auto layer = display.stack.FindLayer(layer_id); layer != nullptr) {
            // TODO: support release fence
            // This is needed to prevent screen tearing
            layer->buffer_item_consumer->ReleaseBuffer(framebuffer.item, android::Fence::NoFence());
            framebuffer.is_acquired = false;
        }
    }

    return frame_advance;
}

void HardwareComposer::RemoveLayerLocked(Display& display, ConsumerId consumer_id) {
    // Check if we are tracking a slot with this consumer_id.
    const auto it = m_framebuffers.find(consumer_id);
    if (it == m_framebuffers.end()) {
        return;
    }

    // Try to release the buffer item.
    const auto layer = display.stack.FindLayer(consumer_id);
    if (layer && it->second.is_acquired) {
        layer->buffer_item_consumer->ReleaseBuffer(it->second.item, android::Fence::NoFence());
    }

    // Erase the slot.
    m_framebuffers.erase(it);
}

bool HardwareComposer::TryAcquireFramebufferLocked(Layer& layer, Framebuffer& framebuffer) {
    // Attempt the update.
    const auto status = layer.buffer_item_consumer->AcquireBuffer(&framebuffer.item, {}, false);
    if (status != android::Status::NoError) {
        return false;
    }

    // We succeeded, so set the new release frame info.
    framebuffer.release_frame_number =
        NormalizeSwapInterval(nullptr, framebuffer.item.swap_interval);
    framebuffer.is_acquired = true;

    return true;
}

HardwareComposer::CacheStatus HardwareComposer::CacheFramebufferLocked(Layer& layer,
                                                                       ConsumerId consumer_id) {
    // Check if this framebuffer is already present.
    const auto it = m_framebuffers.find(consumer_id);
    if (it != m_framebuffers.end()) {
        // If it's currently still acquired, we are done.
        if (it->second.is_acquired) {
            return CacheStatus::CachedBufferReused;
        }

        // Try to acquire a new item.
        if (this->TryAcquireFramebufferLocked(layer, it->second)) {
            // We got a new item.
            return CacheStatus::BufferAcquired;
        } else {
            // We didn't acquire a new item, but we can reuse the slot.
            return CacheStatus::CachedBufferReused;
        }
    }

    // Framebuffer is not present, so try to create it.
    Framebuffer framebuffer{};

    if (this->TryAcquireFramebufferLocked(layer, framebuffer)) {
        // Move the buffer item into a new slot.
        m_framebuffers.emplace(consumer_id, std::move(framebuffer));

        // We succeeded.
        return CacheStatus::BufferAcquired;
    }

    // We couldn't acquire the buffer item, so don't create a slot.
    return CacheStatus::NoBufferAvailable;
}

} // namespace Service::Nvnflinger
