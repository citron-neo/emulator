// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_funcs.h"
#include "core/core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/ns_types.h"

namespace Service::NS {

IContentManagementInterface::IContentManagementInterface(Core::System& system_)
    : ServiceFramework{system_, "IContentManagementInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {11, D<&IContentManagementInterface::CalculateApplicationOccupiedSize>, "CalculateApplicationOccupiedSize"},
        {43, D<&IContentManagementInterface::CheckSdCardMountStatus>, "CheckSdCardMountStatus"},
        {47, D<&IContentManagementInterface::GetTotalSpaceSize>, "GetTotalSpaceSize"},
        {48, D<&IContentManagementInterface::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        {600, nullptr, "CountApplicationContentMeta"},
        {601, nullptr, "ListApplicationContentMetaStatus"},
        {605, nullptr, "ListApplicationContentMetaStatusWithRightsCheck"},
        {607, nullptr, "IsAnyApplicationRunning"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IContentManagementInterface::~IContentManagementInterface() = default;

Result IContentManagementInterface::CalculateApplicationOccupiedSize(
    Out<ApplicationOccupiedSize> out_size, u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, application_id={:016X}", application_id);

    using namespace Common::Literals;

    constexpr ApplicationOccupiedSizeEntity stub_entity{
        .storage_id = FileSys::StorageId::SdCard,
        .app_size = 8_GiB,
        .patch_size = 2_GiB,
        .aoc_size = 12_MiB,
    };

    for (auto& entity : out_size->entities) {
        entity = stub_entity;
    }

    R_SUCCEED();
}

Result IContentManagementInterface::CheckSdCardMountStatus() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IContentManagementInterface::GetTotalSpaceSize(Out<s64> out_total_space_size,
                                                      FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "(STUBBED) called, storage_id={}", storage_id);
    *out_total_space_size = system.GetFileSystemController().GetTotalSpaceSize(storage_id);
    R_SUCCEED();
}

Result IContentManagementInterface::GetFreeSpaceSize(Out<s64> out_free_space_size,
                                                     FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "(STUBBED) called, storage_id={}", storage_id);
    *out_free_space_size = system.GetFileSystemController().GetFreeSpaceSize(storage_id);
    R_SUCCEED();
}

} // namespace Service::NS
