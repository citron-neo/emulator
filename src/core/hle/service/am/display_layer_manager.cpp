// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/display_layer_manager.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_display_service.h"
#include "core/hle/service/vi/manager_root_service.h"
#include "core/hle/service/vi/shared_buffer_manager.h"
#include "core/hle/service/vi/vi_results.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::AM {

DisplayLayerManager::DisplayLayerManager() = default;
DisplayLayerManager::~DisplayLayerManager() {
    this->Finalize();
}

void DisplayLayerManager::Initialize(Core::System& system, Kernel::KProcess* process,
                                     AppletId applet_id, LibraryAppletMode mode) {
    R_ASSERT(system.ServiceManager()
                 .GetService<VI::IManagerRootService>("vi:m", true)
                 ->GetDisplayService(&m_display_service, VI::Policy::Compositor));
    R_ASSERT(m_display_service->GetManagerDisplayService(&m_manager_display_service));

    m_process = process;
    m_system_shared_buffer_id = 0;
    m_system_shared_layer_id = 0;
    m_applet_id = applet_id;
    m_buffer_sharing_enabled = false;
    m_blending_enabled = mode == LibraryAppletMode::PartialForeground ||
                         mode == LibraryAppletMode::PartialForegroundIndirectDisplay;
}

void DisplayLayerManager::Finalize() {
    if (!m_manager_display_service) {
        return;
    }

    // Clean up managed layers.
    for (const auto& layer : m_managed_display_layers) {
        m_manager_display_service->DestroyManagedLayer(layer);
    }

    for (const auto& layer : m_managed_display_recording_layers) {
        m_manager_display_service->DestroyManagedLayer(layer);
    }

    // Clean up shared layers.
    if (m_buffer_sharing_enabled) {
        m_manager_display_service->DestroySharedLayerSession(m_process);
    }

    m_manager_display_service = nullptr;
    m_display_service = nullptr;
}

Result DisplayLayerManager::CreateManagedDisplayLayer(u64* out_layer_id) {
    R_UNLESS(m_manager_display_service != nullptr, VI::ResultOperationFailed);

    // TODO(Subv): Find out how AM determines the display to use, for now just
    // create the layer in the Default display.
    u64 display_id;
    R_TRY(m_display_service->OpenDisplay(&display_id, VI::DisplayName{"Default"}));
    R_TRY(m_manager_display_service->CreateManagedLayer(
        out_layer_id, 0, display_id, Service::AppletResourceUserId{m_process->GetProcessId()}));

    m_manager_display_service->SetLayerVisibility(m_visible, *out_layer_id);

    if (m_applet_id != AppletId::Application) {
        (void)m_manager_display_service->SetLayerBlending(m_blending_enabled, *out_layer_id);
        if (m_applet_id == AppletId::OverlayDisplay) {
            (void)m_manager_display_service->SetLayerZIndex(-1, *out_layer_id);
            (void)m_display_service->GetContainer()->SetLayerIsOverlay(*out_layer_id, true);
        } else {
            (void)m_manager_display_service->SetLayerZIndex(1, *out_layer_id);
        }
    }

    m_managed_display_layers.emplace(*out_layer_id);

    R_SUCCEED();
}

Result DisplayLayerManager::CreateManagedDisplaySeparableLayer(u64* out_layer_id,
                                                               u64* out_recording_layer_id) {
    R_UNLESS(m_manager_display_service != nullptr, VI::ResultOperationFailed);

    // TODO(Subv): Find out how AM determines the display to use, for now just
    // create the layer in the Default display.
    // This calls nn::vi::CreateRecordingLayer() which creates another layer.
    // Currently we do not support more than 1 layer per display, output 1 layer id for now.
    // Outputting 1 layer id instead of the expected 2 has not been observed to cause any adverse
    // side effects.
    *out_recording_layer_id = 0;
    R_RETURN(this->CreateManagedDisplayLayer(out_layer_id));
}

Result DisplayLayerManager::IsSystemBufferSharingEnabled() {
    // Succeed if already enabled.
    R_SUCCEED_IF(m_buffer_sharing_enabled);

    // Ensure we can access shared layers.
    R_UNLESS(m_manager_display_service != nullptr, VI::ResultOperationFailed);
    R_UNLESS(m_applet_id != AppletId::Application, VI::ResultPermissionDenied);

    // Create the shared layer.
    u64 display_id;
    R_TRY(m_display_service->OpenDisplay(&display_id, VI::DisplayName{"Default"}));
    R_TRY(m_manager_display_service->CreateSharedLayerSession(m_process, &m_system_shared_buffer_id,
                                                              &m_system_shared_layer_id, display_id,
                                                              m_blending_enabled));

    // We succeeded, so set up remaining state.
    m_buffer_sharing_enabled = true;

    // Ensure the overlay layer is visible
    m_manager_display_service->SetLayerVisibility(m_visible, m_system_shared_layer_id);
    m_manager_display_service->SetLayerBlending(m_blending_enabled, m_system_shared_layer_id);
    s32 initial_z = 1;
    if (m_applet_id == AppletId::OverlayDisplay) {
        initial_z = -1;
        (void)m_display_service->GetContainer()->SetLayerIsOverlay(m_system_shared_layer_id, true);
    }
    m_manager_display_service->SetLayerZIndex(initial_z, m_system_shared_layer_id);
    R_SUCCEED();
}

Result DisplayLayerManager::GetSystemSharedLayerHandle(u64* out_system_shared_buffer_id,
                                                       u64* out_system_shared_layer_id) {
    R_TRY(this->IsSystemBufferSharingEnabled());

    *out_system_shared_buffer_id = m_system_shared_buffer_id;
    *out_system_shared_layer_id = m_system_shared_layer_id;

    R_SUCCEED();
}

void DisplayLayerManager::SetWindowVisibility(bool visible) {
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;

    if (m_manager_display_service) {
        if (m_system_shared_layer_id) {
            LOG_INFO(Service_VI, "shared_layer={} visible={} applet_id={}",
                     m_system_shared_layer_id, m_visible, static_cast<u32>(m_applet_id));
            m_manager_display_service->SetLayerVisibility(m_visible, m_system_shared_layer_id);
        }

        for (const auto layer_id : m_managed_display_layers) {
            LOG_INFO(Service_VI, "managed_layer={} visible={} applet_id={}", layer_id, m_visible,
                     static_cast<u32>(m_applet_id));
            m_manager_display_service->SetLayerVisibility(m_visible, layer_id);
        }
    }
}

bool DisplayLayerManager::GetWindowVisibility() const {
    return m_visible;
}

void DisplayLayerManager::SetOverlayZIndex(s32 z_index) {
    if (!m_manager_display_service) {
        return;
    }

    if (m_system_shared_layer_id) {
        m_manager_display_service->SetLayerZIndex(z_index, m_system_shared_layer_id);
        LOG_INFO(Service_VI, "called, shared_layer={} z={}", m_system_shared_layer_id, z_index);
    }

    for (const auto layer_id : m_managed_display_layers) {
        m_manager_display_service->SetLayerZIndex(z_index, layer_id);
        LOG_INFO(Service_VI, "called, managed_layer={} z={}", layer_id, z_index);
    }
}

Result DisplayLayerManager::WriteAppletCaptureBuffer(bool* out_was_written,
                                                     s32* out_fbshare_layer_index) {
    R_UNLESS(m_buffer_sharing_enabled, VI::ResultPermissionDenied);
    R_RETURN(m_display_service->GetContainer()->GetSharedBufferManager()->WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index));
}

} // namespace Service::AM
