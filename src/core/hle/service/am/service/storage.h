// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class LibraryAppletStorage;
class IStorageAccessor;
class ITransferStorageAccessor;

class IStorage final : public ServiceFramework<IStorage> {
public:
    explicit IStorage(Core::System& system_, std::shared_ptr<LibraryAppletStorage> impl);
    explicit IStorage(Core::System& system_, std::vector<u8>&& buffer);
    ~IStorage() override;

    std::shared_ptr<LibraryAppletStorage> GetImpl() const {
        return m_impl;
    }

    std::vector<u8> GetData() const;

private:
    Result Open(Out<SharedPointer<IStorageAccessor>> out_storage_accessor);
    Result OpenTransferStorage(
        Out<SharedPointer<ITransferStorageAccessor>> out_transfer_storage_accessor);

    const std::shared_ptr<LibraryAppletStorage> m_impl;
};

} // namespace Service::AM
