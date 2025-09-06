// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/scope_exit.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/kernel/kernel.h"

namespace Kernel {

KTransferMemory::KTransferMemory(KernelCore& kernel)
    : KAutoObjectWithSlabHeapAndContainer{kernel}, m_lock{kernel} {}

KTransferMemory::~KTransferMemory() = default;

Result KTransferMemory::Initialize(KProcessAddress addr, std::size_t size,
                                   Svc::MemoryPermission own_perm) {
    // Set members.
    m_owner = GetCurrentProcessPointer(m_kernel);

    // Get the owner page table.
    auto& page_table = m_owner->GetPageTable();

    // Construct the page group, guarding to make sure our state is valid on exit.
    m_page_group.emplace(m_kernel, page_table.GetBlockInfoManager());
    auto pg_guard = SCOPE_GUARD {
        m_page_group.reset();
    };

    // Lock the memory.
    R_TRY(page_table.LockForTransferMemory(std::addressof(*m_page_group), addr, size,
                                           ConvertToKMemoryPermission(own_perm)));

    // Set remaining tracking members.
    m_owner->Open();
    m_owner_perm = own_perm;
    m_address = addr;
    m_is_initialized = true;
    m_is_mapped = false;

    // We succeeded.
    pg_guard.Cancel();
    R_SUCCEED();
}

void KTransferMemory::Finalize() {
    // Unlock.
    if (!m_is_mapped) {
        const size_t size = m_page_group->GetNumPages() * PageSize;
        ASSERT(R_SUCCEEDED(
            m_owner->GetPageTable().UnlockForTransferMemory(m_address, size, *m_page_group)));
    }

    // Close the page group.
    m_page_group->Close();
    m_page_group->Finalize();
}

void KTransferMemory::PostDestroy(uintptr_t arg) {
    KProcess* owner = reinterpret_cast<KProcess*>(arg);
    owner->GetResourceLimit()->Release(LimitableResource::TransferMemoryCountMax, 1);
    owner->Close();
}

Result KTransferMemory::Map(KProcessAddress address, size_t size, Svc::MemoryPermission map_perm) {
    // Validate the size.
    R_UNLESS(m_page_group->GetNumPages() == Common::DivideUp(size, PageSize), ResultInvalidSize);

    // Validate the permission.
    R_UNLESS(m_owner_perm == map_perm, ResultInvalidState);

    // Lock ourselves.
    KScopedLightLock lk(m_lock);

    // Ensure we're not already mapped.
    R_UNLESS(!m_is_mapped, ResultInvalidState);

    // Map the memory.
    const KMemoryState state = (m_owner_perm == Svc::MemoryPermission::None)
                                   ? KMemoryState::Transferred
                                   : KMemoryState::SharedTransferred;
    R_TRY(GetCurrentProcess(m_kernel).GetPageTable().MapPageGroup(
        address, *m_page_group, state, KMemoryPermission::UserReadWrite));

    // Mark ourselves as mapped.
    m_is_mapped = true;

    R_SUCCEED();
}

Result KTransferMemory::Unmap(KProcessAddress address, size_t size) {
    // Validate the size.
    R_UNLESS(m_page_group->GetNumPages() == Common::DivideUp(size, PageSize), ResultInvalidSize);

    // Lock ourselves.
    KScopedLightLock lk(m_lock);

    // Unmap the memory.
    const KMemoryState state = (m_owner_perm == Svc::MemoryPermission::None)
                                   ? KMemoryState::Transferred
                                   : KMemoryState::SharedTransferred;
    R_TRY(GetCurrentProcess(m_kernel).GetPageTable().UnmapPageGroup(address, *m_page_group, state));

    // Mark ourselves as unmapped.
    ASSERT(m_is_mapped);
    m_is_mapped = false;

    R_SUCCEED();
}

size_t KTransferMemory::GetSize() const {
    return m_is_initialized ? m_page_group->GetNumPages() * PageSize : 0;
}

} // namespace Kernel
