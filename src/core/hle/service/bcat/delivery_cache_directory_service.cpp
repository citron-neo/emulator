// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/string_util.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "core/hle/service/bcat/bcat_result.h"
#include "core/hle/service/bcat/bcat_util.h"
#include "core/hle/service/bcat/delivery_cache_directory_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

// The digest is only used to determine if a file is unique compared to others of the same name.
// Since the algorithm isn't ever checked in game, MD5 is safe.
static BcatDigest DigestFile(const FileSys::VirtualFile& file) {
    BcatDigest out{};
    const auto bytes = file->ReadAllBytes();
    mbedtls_md5_ret(bytes.data(), bytes.size(), out.data());
    return out;
}

IDeliveryCacheDirectoryService::IDeliveryCacheDirectoryService(Core::System& system_,
                                                               FileSys::VirtualDir root_)
    : ServiceFramework{system_, "IDeliveryCacheDirectoryService"}, root(std::move(root_)) {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDeliveryCacheDirectoryService::Open>, "Open"},
        {1, D<&IDeliveryCacheDirectoryService::Read>, "Read"},
        {2, D<&IDeliveryCacheDirectoryService::GetCount>, "GetCount"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDeliveryCacheDirectoryService::~IDeliveryCacheDirectoryService() = default;

Result IDeliveryCacheDirectoryService::Open(const DirectoryName& dir_name_raw) {
    const auto dir_name =
        Common::StringFromFixedZeroTerminatedBuffer(dir_name_raw.data(), dir_name_raw.size());

    LOG_DEBUG(Service_BCAT, "called, dir_name={}", dir_name);

    R_TRY(VerifyNameValidDir(dir_name_raw));
    R_UNLESS(current_dir == nullptr, ResultEntityAlreadyOpen);

    const auto dir = root->GetSubdirectory(dir_name);
    R_UNLESS(dir != nullptr, ResultFailedOpenEntity);

    R_SUCCEED();
}

Result IDeliveryCacheDirectoryService::Read(
    Out<s32> out_count, OutArray<DeliveryCacheDirectoryEntry, BufferAttr_HipcMapAlias> out_buffer) {
    LOG_DEBUG(Service_BCAT, "called, write_size={:016X}", out_buffer.size());

    R_UNLESS(current_dir != nullptr, ResultNoOpenEntry);

    const auto files = current_dir->GetFiles();
    *out_count = static_cast<s32>(std::min(files.size(), out_buffer.size()));
    std::transform(files.begin(), files.begin() + *out_count, out_buffer.begin(),
                   [](const auto& file) {
                       FileName name{};
                       std::memcpy(name.data(), file->GetName().data(),
                                   std::min(file->GetName().size(), name.size()));
                       return DeliveryCacheDirectoryEntry{name, file->GetSize(), DigestFile(file)};
                   });
    R_SUCCEED();
}

Result IDeliveryCacheDirectoryService::GetCount(Out<s32> out_count) {
    LOG_DEBUG(Service_BCAT, "called");

    R_UNLESS(current_dir != nullptr, ResultNoOpenEntry);

    *out_count = static_cast<s32>(current_dir->GetFiles().size());
    R_SUCCEED();
}

} // namespace Service::BCAT
