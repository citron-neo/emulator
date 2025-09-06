// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/nvnflinger/hos_binder_driver_server.h"
#include "core/hle/service/nvnflinger/surface_flinger.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/vi_results.h"

namespace Service::VI {

Container::Container(Core::System& system) {
    m_displays.CreateDisplay(DisplayName{"Default"});
    m_displays.CreateDisplay(DisplayName{"External"});
    m_displays.CreateDisplay(DisplayName{"Edid"});
    m_displays.CreateDisplay(DisplayName{"Internal"});
    m_displays.CreateDisplay(DisplayName{"Null"});

    m_binder_driver =
        system.ServiceManager().GetService<Nvnflinger::IHOSBinderDriver>("dispdrv", true);
    m_surface_flinger = m_binder_driver->GetSurfaceFlinger();

    const auto nvdrv =
        system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true)->GetModule();
    m_shared_buffer_manager.emplace(system, *this, nvdrv);

    m_displays.ForEachDisplay(
        [&](auto& display) { m_surface_flinger->AddDisplay(display.GetId()); });

    m_conductor.emplace(system, *this, m_displays);
}

Container::~Container() {
    this->OnTerminate();
}

void Container::OnTerminate() {
    std::scoped_lock lk{m_lock};

    m_is_shut_down = true;

    m_layers.ForEachLayer([&](auto& layer) { this->DestroyLayerLocked(layer.GetId()); });

    m_displays.ForEachDisplay(
        [&](auto& display) { m_surface_flinger->RemoveDisplay(display.GetId()); });
}

SharedBufferManager* Container::GetSharedBufferManager() {
    return std::addressof(*m_shared_buffer_manager);
}

Result Container::GetBinderDriver(
    std::shared_ptr<Nvnflinger::IHOSBinderDriver>* out_binder_driver) {
    *out_binder_driver = m_binder_driver;
    R_SUCCEED();
}

Result Container::GetLayerProducerHandle(
    std::shared_ptr<android::BufferQueueProducer>* out_producer, u64 layer_id) {
    std::scoped_lock lk{m_lock};

    auto* const layer = m_layers.GetLayerById(layer_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);

    const auto binder = m_binder_driver->GetServer()->TryGetBinder(layer->GetProducerBinderId());
    R_UNLESS(binder != nullptr, VI::ResultNotFound);

    *out_producer = std::static_pointer_cast<android::BufferQueueProducer>(binder);
    R_SUCCEED();
}

Result Container::OpenDisplay(u64* out_display_id, const DisplayName& display_name) {
    auto* const display = m_displays.GetDisplayByName(display_name);
    R_UNLESS(display != nullptr, VI::ResultNotFound);

    *out_display_id = display->GetId();
    R_SUCCEED();
}

Result Container::CloseDisplay(u64 display_id) {
    R_SUCCEED();
}

Result Container::CreateManagedLayer(u64* out_layer_id, u64 display_id, u64 owner_aruid) {
    std::scoped_lock lk{m_lock};
    R_RETURN(this->CreateLayerLocked(out_layer_id, display_id, owner_aruid));
}

Result Container::DestroyManagedLayer(u64 layer_id) {
    std::scoped_lock lk{m_lock};

    // Try to close, if open, but don't fail if not.
    this->CloseLayerLocked(layer_id);

    R_RETURN(this->DestroyLayerLocked(layer_id));
}

Result Container::OpenLayer(s32* out_producer_binder_id, u64 layer_id, u64 aruid) {
    std::scoped_lock lk{m_lock};
    R_RETURN(this->OpenLayerLocked(out_producer_binder_id, layer_id, aruid));
}

Result Container::CloseLayer(u64 layer_id) {
    std::scoped_lock lk{m_lock};
    R_RETURN(this->CloseLayerLocked(layer_id));
}

Result Container::SetLayerVisibility(u64 layer_id, bool visible) {
    std::scoped_lock lk{m_lock};

    auto* const layer = m_layers.GetLayerById(layer_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);

    m_surface_flinger->SetLayerVisibility(layer->GetConsumerBinderId(), visible);
    R_SUCCEED();
}

Result Container::SetLayerBlending(u64 layer_id, bool enabled) {
    std::scoped_lock lk{m_lock};

    auto* const layer = m_layers.GetLayerById(layer_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);

    m_surface_flinger->SetLayerBlending(layer->GetConsumerBinderId(),
                                        enabled ? Nvnflinger::LayerBlending::Coverage
                                                : Nvnflinger::LayerBlending::None);
    R_SUCCEED();
}

void Container::LinkVsyncEvent(u64 display_id, Event* event) {
    std::scoped_lock lk{m_lock};
    m_conductor->LinkVsyncEvent(display_id, event);
}

void Container::UnlinkVsyncEvent(u64 display_id, Event* event) {
    std::scoped_lock lk{m_lock};
    m_conductor->UnlinkVsyncEvent(display_id, event);
}

Result Container::CreateStrayLayer(s32* out_producer_binder_id, u64* out_layer_id, u64 display_id) {
    std::scoped_lock lk{m_lock};
    R_TRY(this->CreateLayerLocked(out_layer_id, display_id, {}));
    R_RETURN(this->OpenLayerLocked(out_producer_binder_id, *out_layer_id, {}));
}

Result Container::DestroyStrayLayer(u64 layer_id) {
    std::scoped_lock lk{m_lock};
    R_TRY(this->CloseLayerLocked(layer_id));
    R_RETURN(this->DestroyLayerLocked(layer_id));
}

Result Container::CreateLayerLocked(u64* out_layer_id, u64 display_id, u64 owner_aruid) {
    auto* const display = m_displays.GetDisplayById(display_id);
    R_UNLESS(display != nullptr, VI::ResultNotFound);

    s32 consumer_binder_id, producer_binder_id;
    m_surface_flinger->CreateBufferQueue(&consumer_binder_id, &producer_binder_id);

    auto* const layer =
        m_layers.CreateLayer(owner_aruid, display, consumer_binder_id, producer_binder_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);

    m_surface_flinger->CreateLayer(consumer_binder_id);

    *out_layer_id = layer->GetId();
    R_SUCCEED();
}

Result Container::DestroyLayerLocked(u64 layer_id) {
    auto* const layer = m_layers.GetLayerById(layer_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);

    m_surface_flinger->DestroyLayer(layer->GetConsumerBinderId());
    m_surface_flinger->DestroyBufferQueue(layer->GetConsumerBinderId(),
                                          layer->GetProducerBinderId());
    m_layers.DestroyLayer(layer_id);

    R_SUCCEED();
}

Result Container::OpenLayerLocked(s32* out_producer_binder_id, u64 layer_id, u64 aruid) {
    R_UNLESS(!m_is_shut_down, VI::ResultOperationFailed);

    auto* const layer = m_layers.GetLayerById(layer_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);
    R_UNLESS(!layer->IsOpen(), VI::ResultOperationFailed);
    R_UNLESS(layer->GetOwnerAruid() == aruid, VI::ResultPermissionDenied);

    layer->Open();

    if (auto* display = layer->GetDisplay(); display != nullptr) {
        m_surface_flinger->AddLayerToDisplayStack(display->GetId(), layer->GetConsumerBinderId());
    }

    *out_producer_binder_id = layer->GetProducerBinderId();

    R_SUCCEED();
}

Result Container::CloseLayerLocked(u64 layer_id) {
    auto* const layer = m_layers.GetLayerById(layer_id);
    R_UNLESS(layer != nullptr, VI::ResultNotFound);
    R_UNLESS(layer->IsOpen(), VI::ResultOperationFailed);

    if (auto* display = layer->GetDisplay(); display != nullptr) {
        m_surface_flinger->RemoveLayerFromDisplayStack(display->GetId(),
                                                       layer->GetConsumerBinderId());
    }

    layer->Close();

    R_SUCCEED();
}

bool Container::ComposeOnDisplay(s32* out_swap_interval, f32* out_compose_speed_scale,
                                 u64 display_id) {
    std::scoped_lock lk{m_lock};
    return m_surface_flinger->ComposeDisplay(out_swap_interval, out_compose_speed_scale,
                                             display_id);
}

} // namespace Service::VI
