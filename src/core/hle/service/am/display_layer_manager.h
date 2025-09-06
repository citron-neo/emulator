// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <set>

#include "common/common_types.h"
#include "core/hle/result.h"
#include "core/hle/service/am/am_types.h"

namespace Core {
class System;
}

namespace Kernel {
class KProcess;
}

namespace Service::VI {
class IApplicationDisplayService;
class IManagerDisplayService;
} // namespace Service::VI

namespace Service::AM {

class DisplayLayerManager {
public:
    explicit DisplayLayerManager();
    ~DisplayLayerManager();

    void Initialize(Core::System& system, Kernel::KProcess* process, AppletId applet_id,
                    LibraryAppletMode mode);
    void Finalize();

    Result CreateManagedDisplayLayer(u64* out_layer_id);
    Result CreateManagedDisplaySeparableLayer(u64* out_layer_id, u64* out_recording_layer_id);

    Result IsSystemBufferSharingEnabled();
    Result GetSystemSharedLayerHandle(u64* out_system_shared_buffer_id,
                                      u64* out_system_shared_layer_id);

    void SetWindowVisibility(bool visible);
    bool GetWindowVisibility() const;

    Result WriteAppletCaptureBuffer(bool* out_was_written, s32* out_fbshare_layer_index);

private:
    Kernel::KProcess* m_process{};
    std::shared_ptr<VI::IApplicationDisplayService> m_display_service{};
    std::shared_ptr<VI::IManagerDisplayService> m_manager_display_service{};
    std::set<u64> m_managed_display_layers{};
    std::set<u64> m_managed_display_recording_layers{};
    u64 m_system_shared_buffer_id{};
    u64 m_system_shared_layer_id{};
    AppletId m_applet_id{};
    bool m_buffer_sharing_enabled{};
    bool m_blending_enabled{};
    bool m_visible{true};
};

} // namespace Service::AM
