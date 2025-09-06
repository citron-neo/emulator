// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <mutex>
#include <optional>

#include "core/hle/service/vi/conductor.h"
#include "core/hle/service/vi/display_list.h"
#include "core/hle/service/vi/layer_list.h"
#include "core/hle/service/vi/shared_buffer_manager.h"

union Result;

namespace Service::android {
class BufferQueueProducer;
}

namespace Service::Nvnflinger {
class IHOSBinderDriver;
class SurfaceFlinger;
} // namespace Service::Nvnflinger

namespace Service {
class Event;
}

namespace Service::VI {

class SharedBufferManager;

class Container {
public:
    explicit Container(Core::System& system);
    ~Container();

    void OnTerminate();

    SharedBufferManager* GetSharedBufferManager();

    Result GetBinderDriver(std::shared_ptr<Nvnflinger::IHOSBinderDriver>* out_binder_driver);
    Result GetLayerProducerHandle(std::shared_ptr<android::BufferQueueProducer>* out_producer,
                                  u64 layer_id);

    Result OpenDisplay(u64* out_display_id, const DisplayName& display_name);
    Result CloseDisplay(u64 display_id);

    // Managed layers are created by the interaction between am and ommdisp
    // on behalf of an applet. Their lifetime ends with the lifetime of the
    // applet's ISelfController.
    Result CreateManagedLayer(u64* out_layer_id, u64 display_id, u64 owner_aruid);
    Result DestroyManagedLayer(u64 layer_id);
    Result OpenLayer(s32* out_producer_binder_id, u64 layer_id, u64 aruid);
    Result CloseLayer(u64 layer_id);

    // Stray layers are created by non-applet sysmodules. Their lifetime ends
    // with the lifetime of the IApplicationDisplayService which created them.
    Result CreateStrayLayer(s32* out_producer_binder_id, u64* out_layer_id, u64 display_id);
    Result DestroyStrayLayer(u64 layer_id);

    Result SetLayerVisibility(u64 layer_id, bool visible);
    Result SetLayerBlending(u64 layer_id, bool enabled);

    void LinkVsyncEvent(u64 display_id, Event* event);
    void UnlinkVsyncEvent(u64 display_id, Event* event);

private:
    Result CreateLayerLocked(u64* out_layer_id, u64 display_id, u64 owner_aruid);
    Result DestroyLayerLocked(u64 layer_id);
    Result OpenLayerLocked(s32* out_producer_binder_id, u64 layer_id, u64 aruid);
    Result CloseLayerLocked(u64 layer_id);

public:
    bool ComposeOnDisplay(s32* out_swap_interval, f32* out_compose_speed_scale, u64 display_id);

private:
    std::mutex m_lock{};
    DisplayList m_displays{};
    LayerList m_layers{};
    std::shared_ptr<Nvnflinger::IHOSBinderDriver> m_binder_driver{};
    std::shared_ptr<Nvnflinger::SurfaceFlinger> m_surface_flinger{};
    std::optional<SharedBufferManager> m_shared_buffer_manager{};
    std::optional<Conductor> m_conductor{};
    bool m_is_shut_down{};
};

} // namespace Service::VI
