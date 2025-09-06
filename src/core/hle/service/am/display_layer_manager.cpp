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
    m_manager_display_service->SetLayerVisibility(m_visible, m_system_shared_layer_id);
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
            m_manager_display_service->SetLayerVisibility(m_visible, m_system_shared_layer_id);
        }

        for (const auto layer_id : m_managed_display_layers) {
            m_manager_display_service->SetLayerVisibility(m_visible, layer_id);
        }
    }
}

bool DisplayLayerManager::GetWindowVisibility() const {
    return m_visible;
}

Result DisplayLayerManager::WriteAppletCaptureBuffer(bool* out_was_written,
                                                     s32* out_fbshare_layer_index) {
    R_UNLESS(m_buffer_sharing_enabled, VI::ResultPermissionDenied);
    R_RETURN(m_display_service->GetContainer()->GetSharedBufferManager()->WriteAppletCaptureBuffer(
        out_was_written, out_fbshare_layer_index));
}

} // namespace Service::AM
