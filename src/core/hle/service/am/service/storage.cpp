// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/am/service/storage_accessor.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IStorage::IStorage(Core::System& system_, std::shared_ptr<LibraryAppletStorage> impl)
    : ServiceFramework{system_, "IStorage"}, m_impl{std::move(impl)} {
    static const FunctionInfo functions[] = {
        {0, D<&IStorage::Open>, "Open"},
        {1, D<&IStorage::OpenTransferStorage>, "OpenTransferStorage"},
    };

    RegisterHandlers(functions);
}

IStorage::IStorage(Core::System& system_, std::vector<u8>&& data)
    : IStorage(system_, CreateStorage(std::move(data))) {}

IStorage::~IStorage() = default;

Result IStorage::Open(Out<SharedPointer<IStorageAccessor>> out_storage_accessor) {
    LOG_DEBUG(Service_AM, "called");

    R_UNLESS(m_impl->GetHandle() == nullptr, AM::ResultInvalidStorageType);

    *out_storage_accessor = std::make_shared<IStorageAccessor>(system, m_impl);
    R_SUCCEED();
}

Result IStorage::OpenTransferStorage(
    Out<SharedPointer<ITransferStorageAccessor>> out_transfer_storage_accessor) {
    R_UNLESS(m_impl->GetHandle() != nullptr, AM::ResultInvalidStorageType);

    *out_transfer_storage_accessor = std::make_shared<ITransferStorageAccessor>(system, m_impl);
    R_SUCCEED();
}

std::vector<u8> IStorage::GetData() const {
    return m_impl->GetData();
}

} // namespace Service::AM
