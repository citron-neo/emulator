// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/nvdrv/devices/nvdisp_disp0.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvnflinger/display.h"
#include "core/hle/service/nvnflinger/hos_binder_driver_server.h"
#include "core/hle/service/nvnflinger/surface_flinger.h"
#include "core/hle/service/sm/sm.h"

#include "core/hle/service/nvnflinger/buffer_queue_consumer.h"
#include "core/hle/service/nvnflinger/buffer_queue_core.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"

namespace Service::Nvnflinger {

SurfaceFlinger::SurfaceFlinger(Core::System& system, HosBinderDriverServer& server)
    : m_system(system), m_server(server), m_context(m_system, "SurfaceFlinger") {
    nvdrv = m_system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true)->GetModule();
    disp_fd = nvdrv->Open("/dev/nvdisp_disp0", {});
}

SurfaceFlinger::~SurfaceFlinger() {
    nvdrv->Close(disp_fd);
}

void SurfaceFlinger::AddDisplay(u64 display_id) {
    m_displays.emplace_back(display_id);
}

void SurfaceFlinger::RemoveDisplay(u64 display_id) {
    std::erase_if(m_displays, [&](auto& display) { return display.id == display_id; });
}

bool SurfaceFlinger::ComposeDisplay(s32* out_swap_interval, f32* out_compose_speed_scale,
                                    u64 display_id) {
    auto* const display = this->FindDisplay(display_id);
    if (!display || !display->stack.HasLayers()) {
        return false;
    }

    *out_swap_interval =
        m_composer.ComposeLocked(out_compose_speed_scale, *display,
                                 *nvdrv->GetDevice<Nvidia::Devices::nvdisp_disp0>(disp_fd));
    return true;
}

void SurfaceFlinger::CreateLayer(s32 consumer_binder_id) {
    auto binder = std::static_pointer_cast<android::BufferQueueConsumer>(
        m_server.TryGetBinder(consumer_binder_id));
    if (!binder) {
        return;
    }

    auto buffer_item_consumer = std::make_shared<android::BufferItemConsumer>(std::move(binder));
    buffer_item_consumer->Connect(false);

    m_layers.layers.emplace_back(
        std::make_shared<Layer>(std::move(buffer_item_consumer), consumer_binder_id));
}

void SurfaceFlinger::DestroyLayer(s32 consumer_binder_id) {
    std::erase_if(m_layers.layers,
                  [&](auto& layer) { return layer->consumer_id == consumer_binder_id; });
}

void SurfaceFlinger::AddLayerToDisplayStack(u64 display_id, s32 consumer_binder_id) {
    auto* const display = this->FindDisplay(display_id);
    auto layer = this->FindLayer(consumer_binder_id);

    if (!display || !layer) {
        return;
    }

    display->stack.layers.emplace_back(std::move(layer));
}

void SurfaceFlinger::RemoveLayerFromDisplayStack(u64 display_id, s32 consumer_binder_id) {
    auto* const display = this->FindDisplay(display_id);
    if (!display) {
        return;
    }

    m_composer.RemoveLayerLocked(*display, consumer_binder_id);
    std::erase_if(display->stack.layers,
                  [&](auto& layer) { return layer->consumer_id == consumer_binder_id; });
}

void SurfaceFlinger::SetLayerVisibility(s32 consumer_binder_id, bool visible) {
    if (const auto layer = this->FindLayer(consumer_binder_id); layer != nullptr) {
        layer->visible = visible;
        return;
    }
}

void SurfaceFlinger::SetLayerBlending(s32 consumer_binder_id, LayerBlending blending) {
    if (const auto layer = this->FindLayer(consumer_binder_id); layer != nullptr) {
        layer->blending = blending;
        return;
    }
}

Display* SurfaceFlinger::FindDisplay(u64 display_id) {
    for (auto& display : m_displays) {
        if (display.id == display_id) {
            return &display;
        }
    }

    return nullptr;
}

std::shared_ptr<Layer> SurfaceFlinger::FindLayer(s32 consumer_binder_id) {
    for (auto& layer : m_layers.layers) {
        if (layer->consumer_id == consumer_binder_id) {
            return layer;
        }
    }

    return nullptr;
}

void SurfaceFlinger::CreateBufferQueue(s32* out_consumer_binder_id, s32* out_producer_binder_id) {
    auto& nvmap = nvdrv->GetContainer().GetNvMapFile();
    auto core = std::make_shared<android::BufferQueueCore>();
    auto producer = std::make_shared<android::BufferQueueProducer>(m_context, core, nvmap);
    auto consumer = std::make_shared<android::BufferQueueConsumer>(core);

    *out_consumer_binder_id = m_server.RegisterBinder(std::move(consumer));
    *out_producer_binder_id = m_server.RegisterBinder(std::move(producer));
}

void SurfaceFlinger::DestroyBufferQueue(s32 consumer_binder_id, s32 producer_binder_id) {
    m_server.UnregisterBinder(producer_binder_id);
    m_server.UnregisterBinder(consumer_binder_id);
}

} // namespace Service::Nvnflinger
