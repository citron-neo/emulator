// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KProcess;
}

namespace Service::VI {

class Container;

class IManagerDisplayService final : public ServiceFramework<IManagerDisplayService> {
public:
    explicit IManagerDisplayService(Core::System& system_, std::shared_ptr<Container> container);
    ~IManagerDisplayService() override;

    Result CreateSharedLayerSession(Kernel::KProcess* owner_process, u64* out_buffer_id,
                                    u64* out_layer_handle, u64 display_id, bool enable_blending);
    void DestroySharedLayerSession(Kernel::KProcess* owner_process);

    Result SetLayerBlending(bool enabled, u64 layer_id);

public:
    Result CreateManagedLayer(Out<u64> out_layer_id, u32 flags, u64 display_id,
                              AppletResourceUserId aruid);
    Result DestroyManagedLayer(u64 layer_id);
    Result AddToLayerStack(u32 stack_id, u64 layer_id);
    Result SetLayerVisibility(bool visible, u64 layer_id);

private:
    const std::shared_ptr<Container> m_container;
};

} // namespace Service::VI
