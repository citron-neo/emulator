// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "common/common_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/nvnflinger/hardware_composer.h"

namespace Core {
class System;
}

namespace Service::Nvidia {
class Module;
}

// TODO: ISurfaceComposer
// TODO: ISurfaceComposerClient

namespace Service::Nvnflinger {

struct Display;
class HosBinderDriverServer;
enum class LayerBlending : u32;
struct Layer;

class SurfaceFlinger {
public:
    explicit SurfaceFlinger(Core::System& system, HosBinderDriverServer& server);
    ~SurfaceFlinger();

    void AddDisplay(u64 display_id);
    void RemoveDisplay(u64 display_id);
    bool ComposeDisplay(s32* out_swap_interval, f32* out_compose_speed_scale, u64 display_id);

    void CreateLayer(s32 consumer_binder_id);
    void DestroyLayer(s32 consumer_binder_id);

    void AddLayerToDisplayStack(u64 display_id, s32 consumer_binder_id);
    void RemoveLayerFromDisplayStack(u64 display_id, s32 consumer_binder_id);

    void SetLayerVisibility(s32 consumer_binder_id, bool visible);
    void SetLayerBlending(s32 consumer_binder_id, LayerBlending blending);

private:
    Display* FindDisplay(u64 display_id);
    std::shared_ptr<Layer> FindLayer(s32 consumer_binder_id);

public:
    // TODO: these don't belong here
    void CreateBufferQueue(s32* out_consumer_binder_id, s32* out_producer_binder_id);
    void DestroyBufferQueue(s32 consumer_binder_id, s32 producer_binder_id);

private:
    Core::System& m_system;
    HosBinderDriverServer& m_server;
    KernelHelpers::ServiceContext m_context;

    std::vector<Display> m_displays;
    LayerStack m_layers;
    std::shared_ptr<Nvidia::Module> nvdrv;
    s32 disp_fd;
    HardwareComposer m_composer;
};

} // namespace Service::Nvnflinger
