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

class IAlbumApplicationService final : public ServiceFramework<IAlbumApplicationService> {
public:
    explicit IAlbumApplicationService(Core::System& system_,
                                      std::shared_ptr<AlbumManager> album_manager);
    ~IAlbumApplicationService() override;

private:
    Result SetShimLibraryVersion(ShimLibraryVersion library_version,
                                 ClientAppletResourceUserId aruid);

    Result GetAlbumFileList0AafeAruidDeprecated(
        Out<u64> out_entries_count, ContentType content_type, s64 start_posix_time,
        s64 end_posix_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumFileEntry, BufferAttr_HipcMapAlias> out_entries);

    Result GetAlbumFileList3AaeAruid(
        Out<u64> out_entries_count, ContentType content_type, AlbumFileDateTime start_date_time,
        AlbumFileDateTime end_date_time, ClientAppletResourceUserId aruid,
        OutArray<ApplicationAlbumEntry, BufferAttr_HipcMapAlias> out_entries);

    std::shared_ptr<AlbumManager> manager = nullptr;
};

} // namespace Service::Capture
