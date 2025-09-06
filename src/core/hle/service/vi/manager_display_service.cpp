// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"

namespace Service::VI {

IManagerDisplayService::IManagerDisplayService(Core::System& system_,
                                               std::shared_ptr<Container> container)
    : ServiceFramework{system_, "IManagerDisplayService"}, m_container{std::move(container)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {200, nullptr, "AllocateProcessHeapBlock"},
        {201, nullptr, "FreeProcessHeapBlock"},
        {1102, nullptr, "GetDisplayResolution"},
        {2010, C<&IManagerDisplayService::CreateManagedLayer>, "CreateManagedLayer"},
        {2011, C<&IManagerDisplayService::DestroyManagedLayer>, "DestroyManagedLayer"},
        {2012, nullptr, "CreateStrayLayer"},
        {2050, nullptr, "CreateIndirectLayer"},
        {2051, nullptr, "DestroyIndirectLayer"},
        {2052, nullptr, "CreateIndirectProducerEndPoint"},
        {2053, nullptr, "DestroyIndirectProducerEndPoint"},
        {2054, nullptr, "CreateIndirectConsumerEndPoint"},
        {2055, nullptr, "DestroyIndirectConsumerEndPoint"},
        {2060, nullptr, "CreateWatermarkCompositor"},
        {2062, nullptr, "SetWatermarkText"},
        {2063, nullptr, "SetWatermarkLayerStacks"},
        {2300, nullptr, "AcquireLayerTexturePresentingEvent"},
        {2301, nullptr, "ReleaseLayerTexturePresentingEvent"},
        {2302, nullptr, "GetDisplayHotplugEvent"},
        {2303, nullptr, "GetDisplayModeChangedEvent"},
        {2402, nullptr, "GetDisplayHotplugState"},
        {2501, nullptr, "GetCompositorErrorInfo"},
        {2601, nullptr, "GetDisplayErrorEvent"},
        {2701, nullptr, "GetDisplayFatalErrorEvent"},
        {4201, nullptr, "SetDisplayAlpha"},
        {4203, nullptr, "SetDisplayLayerStack"},
        {4205, nullptr, "SetDisplayPowerState"},
        {4206, nullptr, "SetDefaultDisplay"},
        {4207, nullptr, "ResetDisplayPanel"},
        {4208, nullptr, "SetDisplayFatalErrorEnabled"},
        {4209, nullptr, "IsDisplayPanelOn"},
        {4300, nullptr, "GetInternalPanelId"},
        {6000, C<&IManagerDisplayService::AddToLayerStack>, "AddToLayerStack"},
        {6001, nullptr, "RemoveFromLayerStack"},
        {6002, C<&IManagerDisplayService::SetLayerVisibility>, "SetLayerVisibility"},
        {6003, nullptr, "SetLayerConfig"},
        {6004, nullptr, "AttachLayerPresentationTracer"},
        {6005, nullptr, "DetachLayerPresentationTracer"},
        {6006, nullptr, "StartLayerPresentationRecording"},
        {6007, nullptr, "StopLayerPresentationRecording"},
        {6008, nullptr, "StartLayerPresentationFenceWait"},
        {6009, nullptr, "StopLayerPresentationFenceWait"},
        {6010, nullptr, "GetLayerPresentationAllFencesExpiredEvent"},
        {6011, nullptr, "EnableLayerAutoClearTransitionBuffer"},
        {6012, nullptr, "DisableLayerAutoClearTransitionBuffer"},
        {6013, nullptr, "SetLayerOpacity"},
        {6014, nullptr, "AttachLayerWatermarkCompositor"},
        {6015, nullptr, "DetachLayerWatermarkCompositor"},
        {7000, nullptr, "SetContentVisibility"},
        {8000, nullptr, "SetConductorLayer"},
        {8001, nullptr, "SetTimestampTracking"},
        {8100, nullptr, "SetIndirectProducerFlipOffset"},
        {8200, nullptr, "CreateSharedBufferStaticStorage"},
        {8201, nullptr, "CreateSharedBufferTransferMemory"},
        {8202, nullptr, "DestroySharedBuffer"},
        {8203, nullptr, "BindSharedLowLevelLayerToManagedLayer"},
        {8204, nullptr, "BindSharedLowLevelLayerToIndirectLayer"},
        {8207, nullptr, "UnbindSharedLowLevelLayer"},
        {8208, nullptr, "ConnectSharedLowLevelLayerToSharedBuffer"},
        {8209, nullptr, "DisconnectSharedLowLevelLayerFromSharedBuffer"},
        {8210, nullptr, "CreateSharedLayer"},
        {8211, nullptr, "DestroySharedLayer"},
        {8216, nullptr, "AttachSharedLayerToLowLevelLayer"},
        {8217, nullptr, "ForceDetachSharedLayerFromLowLevelLayer"},
        {8218, nullptr, "StartDetachSharedLayerFromLowLevelLayer"},
        {8219, nullptr, "FinishDetachSharedLayerFromLowLevelLayer"},
        {8220, nullptr, "GetSharedLayerDetachReadyEvent"},
        {8221, nullptr, "GetSharedLowLevelLayerSynchronizedEvent"},
        {8222, nullptr, "CheckSharedLowLevelLayerSynchronized"},
        {8223, nullptr, "RegisterSharedBufferImporterAruid"},
        {8224, nullptr, "UnregisterSharedBufferImporterAruid"},
        {8227, nullptr, "CreateSharedBufferProcessHeap"},
        {8228, nullptr, "GetSharedLayerLayerStacks"},
        {8229, nullptr, "SetSharedLayerLayerStacks"},
        {8291, nullptr, "PresentDetachedSharedFrameBufferToLowLevelLayer"},
        {8292, nullptr, "FillDetachedSharedFrameBufferColor"},
        {8293, nullptr, "GetDetachedSharedFrameBufferImage"},
        {8294, nullptr, "SetDetachedSharedFrameBufferImage"},
        {8295, nullptr, "CopyDetachedSharedFrameBufferImage"},
        {8296, nullptr, "SetDetachedSharedFrameBufferSubImage"},
        {8297, nullptr, "GetSharedFrameBufferContentParameter"},
        {8298, nullptr, "ExpandStartupLogoOnSharedFrameBuffer"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IManagerDisplayService::~IManagerDisplayService() = default;

Result IManagerDisplayService::CreateSharedLayerSession(Kernel::KProcess* owner_process,
                                                        u64* out_buffer_id, u64* out_layer_handle,
                                                        u64 display_id, bool enable_blending) {
    R_RETURN(m_container->GetSharedBufferManager()->CreateSession(
        owner_process, out_buffer_id, out_layer_handle, display_id, enable_blending));
}

void IManagerDisplayService::DestroySharedLayerSession(Kernel::KProcess* owner_process) {
    m_container->GetSharedBufferManager()->DestroySession(owner_process);
}

Result IManagerDisplayService::SetLayerBlending(bool enabled, u64 layer_id) {
    R_RETURN(m_container->SetLayerBlending(layer_id, enabled));
}

Result IManagerDisplayService::CreateManagedLayer(Out<u64> out_layer_id, u32 flags, u64 display_id,
                                                  AppletResourceUserId aruid) {
    LOG_DEBUG(Service_VI, "called. flags={}, display={}, aruid={}", flags, display_id, aruid.pid);
    R_RETURN(m_container->CreateManagedLayer(out_layer_id, display_id, aruid.pid));
}

Result IManagerDisplayService::DestroyManagedLayer(u64 layer_id) {
    LOG_DEBUG(Service_VI, "called. layer_id={}", layer_id);
    R_RETURN(m_container->DestroyManagedLayer(layer_id));
}

Result IManagerDisplayService::AddToLayerStack(u32 stack_id, u64 layer_id) {
    LOG_WARNING(Service_VI, "(STUBBED) called. stack_id={}, layer_id={}", stack_id, layer_id);
    R_SUCCEED();
}

Result IManagerDisplayService::SetLayerVisibility(bool visible, u64 layer_id) {
    LOG_DEBUG(Service_VI, "called, layer_id={}, visible={}", layer_id, visible);
    R_RETURN(m_container->SetLayerVisibility(layer_id, visible));
}

} // namespace Service::VI
