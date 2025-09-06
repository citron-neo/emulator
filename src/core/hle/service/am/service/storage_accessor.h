// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class IStorageAccessor final : public ServiceFramework<IStorageAccessor> {
public:
    explicit IStorageAccessor(Core::System& system_, std::shared_ptr<LibraryAppletStorage> impl);
    ~IStorageAccessor() override;

private:
    Result GetSize(Out<s64> out_size);
    Result Write(InBuffer<BufferAttr_HipcAutoSelect> buffer, s64 offset);
    Result Read(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer, s64 offset);

    const std::shared_ptr<LibraryAppletStorage> m_impl;
};

class ITransferStorageAccessor final : public ServiceFramework<ITransferStorageAccessor> {
public:
    explicit ITransferStorageAccessor(Core::System& system_,
                                      std::shared_ptr<LibraryAppletStorage> impl);
    ~ITransferStorageAccessor() override;

private:
    Result GetSize(Out<s64> out_size);
    Result GetHandle(Out<s64> out_size, OutCopyHandle<Kernel::KTransferMemory> out_handle);

    const std::shared_ptr<LibraryAppletStorage> m_impl;
};

} // namespace Service::AM
