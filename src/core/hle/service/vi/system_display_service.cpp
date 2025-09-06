// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/system_display_service.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::VI {

ISystemDisplayService::ISystemDisplayService(Core::System& system_,
                                             std::shared_ptr<Container> container)
    : ServiceFramework{system_, "ISystemDisplayService"}, m_container{std::move(container)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1200, nullptr, "GetZOrderCountMin"},
        {1202, nullptr, "GetZOrderCountMax"},
        {1203, nullptr, "GetDisplayLogicalResolution"},
        {1204, nullptr, "SetDisplayMagnification"},
        {2201, nullptr, "SetLayerPosition"},
        {2203, nullptr, "SetLayerSize"},
        {2204, nullptr, "GetLayerZ"},
        {2205, C<&ISystemDisplayService::SetLayerZ>, "SetLayerZ"},
        {2207, C<&ISystemDisplayService::SetLayerVisibility>, "SetLayerVisibility"},
        {2209, nullptr, "SetLayerAlpha"},
        {2210, nullptr, "SetLayerPositionAndSize"},
        {2312, nullptr, "CreateStrayLayer"},
        {2400, nullptr, "OpenIndirectLayer"},
        {2401, nullptr, "CloseIndirectLayer"},
        {2402, nullptr, "FlipIndirectLayer"},
        {3000, C<&ISystemDisplayService::ListDisplayModes>, "ListDisplayModes"},
        {3001, nullptr, "ListDisplayRgbRanges"},
        {3002, nullptr, "ListDisplayContentTypes"},
        {3200, C<&ISystemDisplayService::GetDisplayMode>, "GetDisplayMode"},
        {3201, nullptr, "SetDisplayMode"},
        {3202, nullptr, "GetDisplayUnderscan"},
        {3203, nullptr, "SetDisplayUnderscan"},
        {3204, nullptr, "GetDisplayContentType"},
        {3205, nullptr, "SetDisplayContentType"},
        {3206, nullptr, "GetDisplayRgbRange"},
        {3207, nullptr, "SetDisplayRgbRange"},
        {3208, nullptr, "GetDisplayCmuMode"},
        {3209, nullptr, "SetDisplayCmuMode"},
        {3210, nullptr, "GetDisplayContrastRatio"},
        {3211, nullptr, "SetDisplayContrastRatio"},
        {3214, nullptr, "GetDisplayGamma"},
        {3215, nullptr, "SetDisplayGamma"},
        {3216, nullptr, "GetDisplayCmuLuma"},
        {3217, nullptr, "SetDisplayCmuLuma"},
        {3218, nullptr, "SetDisplayCrcMode"},
        {6013, nullptr, "GetLayerPresentationSubmissionTimestamps"},
        {8225, C<&ISystemDisplayService::GetSharedBufferMemoryHandleId>, "GetSharedBufferMemoryHandleId"},
        {8250, C<&ISystemDisplayService::OpenSharedLayer>, "OpenSharedLayer"},
        {8251, nullptr, "CloseSharedLayer"},
        {8252, C<&ISystemDisplayService::ConnectSharedLayer>, "ConnectSharedLayer"},
        {8253, nullptr, "DisconnectSharedLayer"},
        {8254, C<&ISystemDisplayService::AcquireSharedFrameBuffer>, "AcquireSharedFrameBuffer"},
        {8255, C<&ISystemDisplayService::PresentSharedFrameBuffer>, "PresentSharedFrameBuffer"},
        {8256, C<&ISystemDisplayService::GetSharedFrameBufferAcquirableEvent>, "GetSharedFrameBufferAcquirableEvent"},
        {8257, nullptr, "FillSharedFrameBufferColor"},
        {8258, C<&ISystemDisplayService::CancelSharedFrameBuffer>, "CancelSharedFrameBuffer"},
        {9000, nullptr, "GetDp2hdmiController"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

ISystemDisplayService::~ISystemDisplayService() = default;

Result ISystemDisplayService::SetLayerZ(u32 z_value, u64 layer_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called. layer_id={}, z_value={}", layer_id, z_value);
    R_SUCCEED();
}

// This function currently does nothing but return a success error code in
// the vi library itself, so do the same thing, but log out the passed in values.
Result ISystemDisplayService::SetLayerVisibility(bool visible, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called, layer_id={}, visible={}", layer_id, visible);
    R_SUCCEED();
}

Result ISystemDisplayService::ListDisplayModes(
    Out<u64> out_count, u64 display_id,
    OutArray<DisplayMode, BufferAttr_HipcMapAlias> out_display_modes) {
    LOG_WARNING(Service_VI, "(STUBBED) called, display_id={}", display_id);

    if (!out_display_modes.empty()) {
        out_display_modes[0] = {
            .width = 1920,
            .height = 1080,
            .refresh_rate = 60.f,
            .unknown = {},
        };
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result ISystemDisplayService::GetDisplayMode(Out<DisplayMode> out_display_mode, u64 display_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called, display_id={}", display_id);

    if (Settings::IsDockedMode()) {
        out_display_mode->width = static_cast<u32>(DisplayResolution::DockedWidth);
        out_display_mode->height = static_cast<u32>(DisplayResolution::DockedHeight);
    } else {
        out_display_mode->width = static_cast<u32>(DisplayResolution::UndockedWidth);
        out_display_mode->height = static_cast<u32>(DisplayResolution::UndockedHeight);
    }

    out_display_mode->refresh_rate = 60.f; // This wouldn't seem to be correct for 30 fps games.
    out_display_mode->unknown = 0;

    R_SUCCEED();
}

Result ISystemDisplayService::GetSharedBufferMemoryHandleId(
    Out<s32> out_nvmap_handle, Out<u64> out_size,
    OutLargeData<SharedMemoryPoolLayout, BufferAttr_HipcMapAlias> out_pool_layout, u64 buffer_id,
    ClientAppletResourceUserId aruid) {
    LOG_INFO(Service_VI, "called. buffer_id={}, aruid={:#x}", buffer_id, aruid.pid);

    R_RETURN(m_container->GetSharedBufferManager()->GetSharedBufferMemoryHandleId(
        out_size, out_nvmap_handle, out_pool_layout, buffer_id, aruid.pid));
}

Result ISystemDisplayService::OpenSharedLayer(u64 layer_id) {
    LOG_INFO(Service_VI, "(STUBBED) called. layer_id={}", layer_id);
    R_SUCCEED();
}

Result ISystemDisplayService::ConnectSharedLayer(u64 layer_id) {
    LOG_INFO(Service_VI, "(STUBBED) called. layer_id={}", layer_id);
    R_SUCCEED();
}

Result ISystemDisplayService::AcquireSharedFrameBuffer(Out<android::Fence> out_fence,
                                                       Out<std::array<s32, 4>> out_slots,
                                                       Out<s64> out_target_slot, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->AcquireSharedFrameBuffer(
        out_fence, *out_slots, out_target_slot, layer_id));
}

Result ISystemDisplayService::PresentSharedFrameBuffer(android::Fence fence,
                                                       Common::Rectangle<s32> crop_region,
                                                       u32 window_transform, s32 swap_interval,
                                                       u64 layer_id, s64 surface_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->PresentSharedFrameBuffer(
        fence, crop_region, window_transform, swap_interval, layer_id, surface_id));
}

Result ISystemDisplayService::GetSharedFrameBufferAcquirableEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->GetSharedFrameBufferAcquirableEvent(out_event,
                                                                                        layer_id));
}

Result ISystemDisplayService::CancelSharedFrameBuffer(u64 layer_id, s64 slot) {
    LOG_DEBUG(Service_VI, "called");
    R_RETURN(m_container->GetSharedBufferManager()->CancelSharedFrameBuffer(layer_id, slot));
}

} // namespace Service::VI
