// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <map>
#include <set>

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"
#include "core/hle/service/vi/vi_types.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::Nvnflinger {
class IHOSBinderDriver;
}

namespace Service::VI {

class Container;
class IManagerDisplayService;
class ISystemDisplayService;

class IApplicationDisplayService final : public ServiceFramework<IApplicationDisplayService> {
public:
    IApplicationDisplayService(Core::System& system_, std::shared_ptr<Container> container);
    ~IApplicationDisplayService() override;

    std::shared_ptr<Container> GetContainer() const {
        return m_container;
    }

public:
    Result GetRelayService(Out<SharedPointer<Nvnflinger::IHOSBinderDriver>> out_relay_service);
    Result GetSystemDisplayService(
        Out<SharedPointer<ISystemDisplayService>> out_system_display_service);
    Result GetManagerDisplayService(
        Out<SharedPointer<IManagerDisplayService>> out_manager_display_service);
    Result GetIndirectDisplayTransactionService(
        Out<SharedPointer<Nvnflinger::IHOSBinderDriver>> out_indirect_display_transaction_service);
    Result OpenDisplay(Out<u64> out_display_id, DisplayName display_name);
    Result OpenDefaultDisplay(Out<u64> out_display_id);
    Result CloseDisplay(u64 display_id);
    Result SetDisplayEnabled(u32 state, u64 display_id);
    Result GetDisplayResolution(Out<s64> out_width, Out<s64> out_height, u64 display_id);
    Result SetLayerScalingMode(NintendoScaleMode scale_mode, u64 layer_id);
    Result ListDisplays(Out<u64> out_count,
                        OutArray<DisplayInfo, BufferAttr_HipcMapAlias> out_displays);
    Result OpenLayer(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_native_window,
                     DisplayName display_name, u64 layer_id, ClientAppletResourceUserId aruid);
    Result CloseLayer(u64 layer_id);
    Result CreateStrayLayer(Out<u64> out_layer_id, Out<u64> out_size,
                            OutBuffer<BufferAttr_HipcMapAlias> out_native_window, u32 flags,
                            u64 display_id);
    Result DestroyStrayLayer(u64 layer_id);
    Result GetDisplayVsyncEvent(OutCopyHandle<Kernel::KReadableEvent> out_vsync_event,
                                u64 display_id);
    Result ConvertScalingMode(Out<ConvertedScaleMode> out_scaling_mode, NintendoScaleMode mode);
    Result GetIndirectLayerImageMap(
        Out<u64> out_size, Out<u64> out_stride,
        OutBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> out_buffer,
        s64 width, s64 height, u64 indirect_layer_consumer_handle,
        ClientAppletResourceUserId aruid);
    Result GetIndirectLayerImageRequiredMemoryInfo(Out<s64> out_size, Out<s64> out_alignment,
                                                   s64 width, s64 height);

private:
    const std::shared_ptr<Container> m_container;

    KernelHelpers::ServiceContext m_context;
    std::mutex m_lock{};
    std::set<u64> m_open_layer_ids{};
    std::set<u64> m_stray_layer_ids{};
    std::map<u64, Event> m_display_vsync_events{};
    bool m_vsync_event_fetched{false};
};

} // namespace Service::VI
