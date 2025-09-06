// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Capture {
class AlbumManager;
enum class ShimLibraryVersion : u64;

class IAlbumControlService final : public ServiceFramework<IAlbumControlService> {
public:
    explicit IAlbumControlService(Core::System& system_,
                                  std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumControlService() override;

private:
    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);

    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
