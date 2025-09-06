// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/nvnflinger/parcel.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"
#include "core/hle/service/vi/system_display_service.h"
#include "core/hle/service/vi/vi_results.h"

namespace Service::VI {

IApplicationDisplayService::IApplicationDisplayService(Core::System& system_,
                                                       std::shared_ptr<Container> container)
    : ServiceFramework{system_, "IApplicationDisplayService"},
      m_container{std::move(container)}, m_context{system, "IApplicationDisplayService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {100, C<&IApplicationDisplayService::GetRelayService>, "GetRelayService"},
        {101, C<&IApplicationDisplayService::GetSystemDisplayService>, "GetSystemDisplayService"},
        {102, C<&IApplicationDisplayService::GetManagerDisplayService>, "GetManagerDisplayService"},
        {103, C<&IApplicationDisplayService::GetIndirectDisplayTransactionService>, "GetIndirectDisplayTransactionService"},
        {1000, C<&IApplicationDisplayService::ListDisplays>, "ListDisplays"},
        {1010, C<&IApplicationDisplayService::OpenDisplay>, "OpenDisplay"},
        {1011, C<&IApplicationDisplayService::OpenDefaultDisplay>, "OpenDefaultDisplay"},
        {1020, C<&IApplicationDisplayService::CloseDisplay>, "CloseDisplay"},
        {1101, C<&IApplicationDisplayService::SetDisplayEnabled>, "SetDisplayEnabled"},
        {1102, C<&IApplicationDisplayService::GetDisplayResolution>, "GetDisplayResolution"},
        {2020, C<&IApplicationDisplayService::OpenLayer>, "OpenLayer"},
        {2021, C<&IApplicationDisplayService::CloseLayer>, "CloseLayer"},
        {2030, C<&IApplicationDisplayService::CreateStrayLayer>, "CreateStrayLayer"},
        {2031, C<&IApplicationDisplayService::DestroyStrayLayer>, "DestroyStrayLayer"},
        {2101, C<&IApplicationDisplayService::SetLayerScalingMode>, "SetLayerScalingMode"},
        {2102, C<&IApplicationDisplayService::ConvertScalingMode>, "ConvertScalingMode"},
        {2450, C<&IApplicationDisplayService::GetIndirectLayerImageMap>, "GetIndirectLayerImageMap"},
        {2451, nullptr, "GetIndirectLayerImageCropMap"},
        {2460, C<&IApplicationDisplayService::GetIndirectLayerImageRequiredMemoryInfo>, "GetIndirectLayerImageRequiredMemoryInfo"},
        {5202, C<&IApplicationDisplayService::GetDisplayVsyncEvent>, "GetDisplayVsyncEvent"},
        {5203, nullptr, "GetDisplayVsyncEventForDebug"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationDisplayService::~IApplicationDisplayService() {
    for (auto& [display_id, event] : m_display_vsync_events) {
        m_container->UnlinkVsyncEvent(display_id, &event);
    }
    for (const auto layer_id : m_open_layer_ids) {
        m_container->CloseLayer(layer_id);
    }
    for (const auto layer_id : m_stray_layer_ids) {
        m_container->DestroyStrayLayer(layer_id);
    }
}

Result IApplicationDisplayService::GetRelayService(
    Out<SharedPointer<Nvnflinger::IHOSBinderDriver>> out_relay_service) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    R_RETURN(m_container->GetBinderDriver(out_relay_service));
}

Result IApplicationDisplayService::GetSystemDisplayService(
    Out<SharedPointer<ISystemDisplayService>> out_system_display_service) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    *out_system_display_service = std::make_shared<ISystemDisplayService>(system, m_container);
    R_SUCCEED();
}

Result IApplicationDisplayService::GetManagerDisplayService(
    Out<SharedPointer<IManagerDisplayService>> out_manager_display_service) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    *out_manager_display_service = std::make_shared<IManagerDisplayService>(system, m_container);
    R_SUCCEED();
}

Result IApplicationDisplayService::GetIndirectDisplayTransactionService(
    Out<SharedPointer<Nvnflinger::IHOSBinderDriver>> out_indirect_display_transaction_service) {
    LOG_WARNING(Service_VI, "(STUBBED) called");
    R_RETURN(m_container->GetBinderDriver(out_indirect_display_transaction_service));
}

Result IApplicationDisplayService::OpenDisplay(Out<u64> out_display_id, DisplayName display_name) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    display_name[display_name.size() - 1] = '\0';
    ASSERT_MSG(strcmp(display_name.data(), "Default") == 0,
               "Non-default displays aren't supported yet");

    R_RETURN(m_container->OpenDisplay(out_display_id, display_name));
}

Result IApplicationDisplayService::OpenDefaultDisplay(Out<u64> out_display_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(this->OpenDisplay(out_display_id, DisplayName{"Default"}));
}

Result IApplicationDisplayService::CloseDisplay(u64 display_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->CloseDisplay(display_id));
}

Result IApplicationDisplayService::SetDisplayEnabled(u32 state, u64 display_id) {
    LOG_DEBUG(Service_VI, "called");

    // This literally does nothing internally in the actual service itself,
    // and just returns a successful result code regardless of the input.
    R_SUCCEED();
}

Result IApplicationDisplayService::GetDisplayResolution(Out<s64> out_width, Out<s64> out_height,
                                                        u64 display_id) {
    LOG_DEBUG(Service_VI, "called. display_id={}", display_id);

    // This only returns the fixed values of 1280x720 and makes no distinguishing
    // between docked and undocked dimensions.
    *out_width = static_cast<s64>(DisplayResolution::UndockedWidth);
    *out_height = static_cast<s64>(DisplayResolution::UndockedHeight);
    R_SUCCEED();
}

Result IApplicationDisplayService::SetLayerScalingMode(NintendoScaleMode scale_mode, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. scale_mode={}, unknown=0x{:016X}", scale_mode, layer_id);

    if (scale_mode > NintendoScaleMode::PreserveAspectRatio) {
        LOG_ERROR(Service_VI, "Invalid scaling mode provided.");
        R_THROW(VI::ResultOperationFailed);
    }

    if (scale_mode != NintendoScaleMode::ScaleToWindow &&
        scale_mode != NintendoScaleMode::PreserveAspectRatio) {
        LOG_ERROR(Service_VI, "Unsupported scaling mode supplied.");
        R_THROW(VI::ResultNotSupported);
    }

    R_SUCCEED();
}

Result IApplicationDisplayService::ListDisplays(
    Out<u64> out_count, OutArray<DisplayInfo, BufferAttr_HipcMapAlias> out_displays) {
    LOG_WARNING(Service_VI, "(STUBBED) called");

    if (out_displays.size() > 0) {
        out_displays[0] = DisplayInfo{};
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result IApplicationDisplayService::OpenLayer(Out<u64> out_size,
                                             OutBuffer<BufferAttr_HipcMapAlias> out_native_window,
                                             DisplayName display_name, u64 layer_id,
                                             ClientAppletResourceUserId aruid) {
    display_name[display_name.size() - 1] = '\0';

    LOG_DEBUG(Service_VI, "called. layer_id={}, aruid={:#x}", layer_id, aruid.pid);

    u64 display_id;
    R_TRY(m_container->OpenDisplay(&display_id, display_name));

    s32 producer_binder_id;
    R_TRY(m_container->OpenLayer(&producer_binder_id, layer_id, aruid.pid));

    {
        std::scoped_lock lk{m_lock};
        m_open_layer_ids.insert(layer_id);
    }

    android::OutputParcel parcel;
    parcel.WriteInterface(NativeWindow{producer_binder_id});

    const auto buffer = parcel.Serialize();
    std::memcpy(out_native_window.data(), buffer.data(),
                std::min(out_native_window.size(), buffer.size()));
    *out_size = buffer.size();

    R_SUCCEED();
}

Result IApplicationDisplayService::CloseLayer(u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);

    {
        std::scoped_lock lk{m_lock};
        R_UNLESS(m_open_layer_ids.contains(layer_id), VI::ResultNotFound);
        m_open_layer_ids.erase(layer_id);
    }

    R_RETURN(m_container->CloseLayer(layer_id));
}

Result IApplicationDisplayService::CreateStrayLayer(
    Out<u64> out_layer_id, Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_native_window,
    u32 flags, u64 display_id) {
    LOG_DEBUG(Service_VI, "called. flags={}, display_id={}", flags, display_id);

    s32 producer_binder_id;
    R_TRY(m_container->CreateStrayLayer(&producer_binder_id, out_layer_id, display_id));

    std::scoped_lock lk{m_lock};
    m_stray_layer_ids.insert(*out_layer_id);

    android::OutputParcel parcel;
    parcel.WriteInterface(NativeWindow{producer_binder_id});

    const auto buffer = parcel.Serialize();
    std::memcpy(out_native_window.data(), buffer.data(),
                std::min(out_native_window.size(), buffer.size()));

    *out_size = buffer.size();

    R_SUCCEED();
}

Result IApplicationDisplayService::DestroyStrayLayer(u64 layer_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called. layer_id={}", layer_id);

    {
        std::scoped_lock lk{m_lock};
        R_UNLESS(m_stray_layer_ids.contains(layer_id), VI::ResultNotFound);
        m_stray_layer_ids.erase(layer_id);
    }

    R_RETURN(m_container->DestroyStrayLayer(layer_id));
}

Result IApplicationDisplayService::GetDisplayVsyncEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_vsync_event, u64 display_id) {
    LOG_DEBUG(Service_VI, "called. display_id={}", display_id);

    std::scoped_lock lk{m_lock};

    auto [it, created] = m_display_vsync_events.emplace(display_id, m_context);
    R_UNLESS(created, VI::ResultPermissionDenied);

    m_container->LinkVsyncEvent(display_id, &it->second);
    *out_vsync_event = it->second.GetHandle();

    R_SUCCEED();
}

Result IApplicationDisplayService::ConvertScalingMode(Out<ConvertedScaleMode> out_scaling_mode,
                                                      NintendoScaleMode mode) {
    LOG_DEBUG(Service_VI, "called mode={}", mode);

    switch (mode) {
    case NintendoScaleMode::None:
        *out_scaling_mode = ConvertedScaleMode::None;
        R_SUCCEED();
    case NintendoScaleMode::Freeze:
        *out_scaling_mode = ConvertedScaleMode::Freeze;
        R_SUCCEED();
    case NintendoScaleMode::ScaleToWindow:
        *out_scaling_mode = ConvertedScaleMode::ScaleToWindow;
        R_SUCCEED();
    case NintendoScaleMode::ScaleAndCrop:
        *out_scaling_mode = ConvertedScaleMode::ScaleAndCrop;
        R_SUCCEED();
    case NintendoScaleMode::PreserveAspectRatio:
        *out_scaling_mode = ConvertedScaleMode::PreserveAspectRatio;
        R_SUCCEED();
    default:
        LOG_ERROR(Service_VI, "Invalid scaling mode specified, mode={}", mode);
        R_THROW(VI::ResultOperationFailed);
    }
}

Result IApplicationDisplayService::GetIndirectLayerImageMap(
    Out<u64> out_size, Out<u64> out_stride,
    OutBuffer<BufferAttr_HipcMapTransferAllowsNonSecure | BufferAttr_HipcMapAlias> out_buffer,
    s64 width, s64 height, u64 indirect_layer_consumer_handle, ClientAppletResourceUserId aruid) {
    LOG_WARNING(
        Service_VI,
        "(STUBBED) called, width={}, height={}, indirect_layer_consumer_handle={}, aruid={:#x}",
        width, height, indirect_layer_consumer_handle, aruid.pid);
    *out_size = 0;
    *out_stride = 0;
    R_SUCCEED();
}

Result IApplicationDisplayService::GetIndirectLayerImageRequiredMemoryInfo(Out<s64> out_size,
                                                                           Out<s64> out_alignment,
                                                                           s64 width, s64 height) {
    LOG_DEBUG(Service_VI, "called width={}, height={}", width, height);

    constexpr u64 base_size = 0x20000;
    const auto texture_size = width * height * 4;

    *out_alignment = 0x1000;
    *out_size = (texture_size + base_size - 1) / base_size * base_size;

    R_SUCCEED();
}

} // namespace Service::VI
