// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/am/library_applet_storage.h"
#include "core/hle/service/am/service/storage_accessor.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IStorageAccessor::IStorageAccessor(Core::System& system_,
                                   std::shared_ptr<LibraryAppletStorage> impl)
    : ServiceFramework{system_, "IStorageAccessor"}, m_impl{std::move(impl)} {
    static const FunctionInfo functions[] = {
        {0, D<&IStorageAccessor::GetSize>, "GetSize"},
        {10, D<&IStorageAccessor::Write>, "Write"},
        {11, D<&IStorageAccessor::Read>, "Read"},
    };

    RegisterHandlers(functions);
}

IStorageAccessor::~IStorageAccessor() = default;

Result IStorageAccessor::GetSize(Out<s64> out_size) {
    LOG_DEBUG(Service_AM, "called");
    *out_size = m_impl->GetSize();
    R_SUCCEED();
}

Result IStorageAccessor::Write(InBuffer<BufferAttr_HipcAutoSelect> buffer, s64 offset) {
    LOG_DEBUG(Service_AM, "called, offset={} size={}", offset, buffer.size());
    R_RETURN(m_impl->Write(offset, buffer.data(), buffer.size()));
}

Result IStorageAccessor::Read(OutBuffer<BufferAttr_HipcAutoSelect> out_buffer, s64 offset) {
    LOG_DEBUG(Service_AM, "called, offset={} size={}", offset, out_buffer.size());
    R_RETURN(m_impl->Read(offset, out_buffer.data(), out_buffer.size()));
}

ITransferStorageAccessor::ITransferStorageAccessor(Core::System& system_,
                                                   std::shared_ptr<LibraryAppletStorage> impl)
    : ServiceFramework{system_, "ITransferStorageAccessor"}, m_impl{std::move(impl)} {
    static const FunctionInfo functions[] = {
        {0, D<&ITransferStorageAccessor::GetSize>, "GetSize"},
        {1, D<&ITransferStorageAccessor::GetHandle>, "GetHandle"},
    };

    RegisterHandlers(functions);
}

ITransferStorageAccessor::~ITransferStorageAccessor() = default;

Result ITransferStorageAccessor::GetSize(Out<s64> out_size) {
    LOG_DEBUG(Service_AM, "called");
    *out_size = m_impl->GetSize();
    R_SUCCEED();
}

Result ITransferStorageAccessor::GetHandle(Out<s64> out_size,
                                           OutCopyHandle<Kernel::KTransferMemory> out_handle) {
    LOG_INFO(Service_AM, "called");
    *out_size = m_impl->GetSize();
    *out_handle = m_impl->GetHandle();
    R_SUCCEED();
}

} // namespace Service::AM
