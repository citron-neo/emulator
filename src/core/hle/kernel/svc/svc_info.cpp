// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_resource_limit.h"
#include "core/hle/kernel/svc.h"

namespace Kernel::Svc {

/// Gets system/memory information for the current process
Result GetInfo(Core::System& system, u64* result, InfoType info_id_type, Handle handle,
               u64 info_sub_id) {
    LOG_TRACE(Kernel_SVC, "called info_id=0x{:X}, info_sub_id=0x{:X}, handle=0x{:08X}",
              info_id_type, info_sub_id, handle);

    u32 info_id = static_cast<u32>(info_id_type);

    switch (info_id_type) {
    case InfoType::CoreMask:
    case InfoType::PriorityMask:
    case InfoType::AliasRegionAddress:
    case InfoType::AliasRegionSize:
    case InfoType::HeapRegionAddress:
    case InfoType::HeapRegionSize:
    case InfoType::AslrRegionAddress:
    case InfoType::AslrRegionSize:
    case InfoType::StackRegionAddress:
    case InfoType::StackRegionSize:
    case InfoType::TotalMemorySize:
    case InfoType::UsedMemorySize:
    case InfoType::SystemResourceSizeTotal:
    case InfoType::SystemResourceSizeUsed:
    case InfoType::ProgramId:
    case InfoType::UserExceptionContextAddress:
    case InfoType::TotalNonSystemMemorySize:
    case InfoType::UsedNonSystemMemorySize:
    case InfoType::IsApplication:
    case InfoType::FreeThreadCount: {
        R_UNLESS(info_sub_id == 0, ResultInvalidEnumValue);

        const auto& handle_table = GetCurrentProcess(system.Kernel()).GetHandleTable();
        KScopedAutoObject process = handle_table.GetObject<KProcess>(handle);
        R_UNLESS(process.IsNotNull(), ResultInvalidHandle);

        switch (info_id_type) {
        case InfoType::CoreMask:
            *result = process->GetCoreMask();
            R_SUCCEED();

        case InfoType::PriorityMask:
            *result = process->GetPriorityMask();
            R_SUCCEED();

        case InfoType::AliasRegionAddress:
            *result = GetInteger(process->GetPageTable().GetAliasRegionStart());
            R_SUCCEED();

        case InfoType::AliasRegionSize:
            *result = process->GetPageTable().GetAliasRegionSize();
            R_SUCCEED();

        case InfoType::HeapRegionAddress:
            *result = GetInteger(process->GetPageTable().GetHeapRegionStart());
            R_SUCCEED();

        case InfoType::HeapRegionSize:
            *result = process->GetPageTable().GetHeapRegionSize();
            R_SUCCEED();

        case InfoType::AslrRegionAddress:
            *result = GetInteger(process->GetPageTable().GetAliasCodeRegionStart());
            R_SUCCEED();

        case InfoType::AslrRegionSize:
            *result = process->GetPageTable().GetAliasCodeRegionSize();
            R_SUCCEED();

        case InfoType::StackRegionAddress:
            *result = GetInteger(process->GetPageTable().GetStackRegionStart());
            R_SUCCEED();

        case InfoType::StackRegionSize:
            *result = process->GetPageTable().GetStackRegionSize();
            R_SUCCEED();

        case InfoType::TotalMemorySize:
            *result = process->GetTotalUserPhysicalMemorySize();
            R_SUCCEED();

        case InfoType::UsedMemorySize:
            *result = process->GetUsedUserPhysicalMemorySize();
            R_SUCCEED();

        case InfoType::SystemResourceSizeTotal:
            *result = process->GetTotalSystemResourceSize();
            R_SUCCEED();

        case InfoType::SystemResourceSizeUsed:
            *result = process->GetUsedSystemResourceSize();
            R_SUCCEED();

        case InfoType::ProgramId:
            *result = process->GetProgramId();
            R_SUCCEED();

        case InfoType::UserExceptionContextAddress:
            *result = GetInteger(process->GetProcessLocalRegionAddress());
            R_SUCCEED();

        case InfoType::TotalNonSystemMemorySize:
            *result = process->GetTotalNonSystemUserPhysicalMemorySize();
            R_SUCCEED();

        case InfoType::UsedNonSystemMemorySize:
            *result = process->GetUsedNonSystemUserPhysicalMemorySize();
            R_SUCCEED();

        case InfoType::IsApplication:
            *result = process->IsApplication();
            R_SUCCEED();

        case InfoType::FreeThreadCount:
            if (KResourceLimit* resource_limit = process->GetResourceLimit();
                resource_limit != nullptr) {
                const auto current_value =
                    resource_limit->GetCurrentValue(Svc::LimitableResource::ThreadCountMax);
                const auto limit_value =
                    resource_limit->GetLimitValue(Svc::LimitableResource::ThreadCountMax);
                *result = limit_value - current_value;
            } else {
                *result = 0;
            }
            R_SUCCEED();

        default:
            break;
        }

        LOG_ERROR(Kernel_SVC, "Unimplemented svcGetInfo id=0x{:016X}", info_id);
        R_THROW(ResultInvalidEnumValue);
    }

    case InfoType::DebuggerAttached:
        *result = 0;
        R_SUCCEED();

    case InfoType::ResourceLimit: {
        R_UNLESS(handle == 0, ResultInvalidHandle);
        R_UNLESS(info_sub_id == 0, ResultInvalidCombination);

        KProcess* const current_process = GetCurrentProcessPointer(system.Kernel());
        KHandleTable& handle_table = current_process->GetHandleTable();
        const auto resource_limit = current_process->GetResourceLimit();
        if (!resource_limit) {
            *result = Svc::InvalidHandle;
            // Yes, the kernel considers this a successful operation.
            R_SUCCEED();
        }

        Handle resource_handle{};
        R_TRY(handle_table.Add(std::addressof(resource_handle), resource_limit));

        *result = resource_handle;
        R_SUCCEED();
    }

    case InfoType::RandomEntropy:
        R_UNLESS(handle == 0, ResultInvalidHandle);
        R_UNLESS(info_sub_id < 4, ResultInvalidCombination);

        *result = GetCurrentProcess(system.Kernel()).GetRandomEntropy(info_sub_id);
        R_SUCCEED();

    case InfoType::InitialProcessIdRange:
        LOG_WARNING(Kernel_SVC,
                    "(STUBBED) Attempted to query privileged process id bounds, returned 0");
        *result = 0;
        R_SUCCEED();

    case InfoType::ThreadTickCount: {
        const auto& handle_table = GetCurrentProcess(system.Kernel()).GetHandleTable();
        KScopedAutoObject thread = handle_table.GetObject<KThread>(handle);
        R_UNLESS(thread.IsNotNull(), ResultInvalidHandle);

        // Use GetCpuTime() instead of the non-existent GetYieldScheduleCount() and GetYieldScheduleBias()
        const s64 cpu_time = static_cast<s64>(thread->GetCpuTime());

        // For sub IDs, just return the CPU time or 0 based on the ID
        switch (info_sub_id) {
        case 0:
            *result = cpu_time;
            R_SUCCEED();
        case 1:
        case 2:
            // We don't have separate bias/other fields, just return 0
            *result = 0;
            R_SUCCEED();
        default:
            R_THROW(ResultInvalidEnumValue);
        }
    }

    case InfoType::IdleTickCount: {
        // Verify the input handle is invalid.
        R_UNLESS(handle == InvalidHandle, ResultInvalidHandle);

        // Verify the requested core is valid.
        const bool core_valid =
            (info_sub_id == 0xFFFFFFFFFFFFFFFF) ||
            (info_sub_id == static_cast<u64>(system.Kernel().CurrentPhysicalCoreIndex()));
        R_UNLESS(core_valid, ResultInvalidCombination);

        // Get the idle tick count.
        *result = system.Kernel().CurrentScheduler()->GetIdleThread()->GetCpuTime();
        R_SUCCEED();
    }

    case InfoType::TLSCapability: {
        // This is related to TLS capabilities
        // For now, let's return a successful result with a value that indicates TLS is supported
        LOG_WARNING(Kernel_SVC, "(STUBBED) TLS capability check requested, returning supported");
        *result = 1; // Indicate support
        R_SUCCEED();
    }

    case InfoType::MesosphereCurrentProcess: {
        // Verify the input handle is invalid.
        R_UNLESS(handle == InvalidHandle, ResultInvalidHandle);

        // Verify the sub-type is valid.
        R_UNLESS(info_sub_id == 0, ResultInvalidCombination);

        // Get the handle table.
        KProcess* current_process = GetCurrentProcessPointer(system.Kernel());
        KHandleTable& handle_table = current_process->GetHandleTable();

        // Get a new handle for the current process.
        Handle tmp;
        R_TRY(handle_table.Add(std::addressof(tmp), current_process));

        // Set the output.
        *result = tmp;

        // We succeeded.
        R_SUCCEED();
    }
    default:
        LOG_ERROR(Kernel_SVC, "Unimplemented svcGetInfo id=0x{:016X}", info_id);
        R_THROW(ResultInvalidEnumValue);
    }
}

Result GetSystemInfo(Core::System& system, uint64_t* out, SystemInfoType info_type, Handle handle,
                     uint64_t info_subtype) {
    UNIMPLEMENTED();
    R_THROW(ResultNotImplemented);
}

Result GetInfo64(Core::System& system, uint64_t* out, InfoType info_type, Handle handle,
                 uint64_t info_subtype) {
    R_RETURN(GetInfo(system, out, info_type, handle, info_subtype));
}

Result GetSystemInfo64(Core::System& system, uint64_t* out, SystemInfoType info_type, Handle handle,
                       uint64_t info_subtype) {
    R_RETURN(GetSystemInfo(system, out, info_type, handle, info_subtype));
}

Result GetInfo64From32(Core::System& system, uint64_t* out, InfoType info_type, Handle handle,
                       uint64_t info_subtype) {
    R_RETURN(GetInfo(system, out, info_type, handle, info_subtype));
}

Result GetSystemInfo64From32(Core::System& system, uint64_t* out, SystemInfoType info_type,
                             Handle handle, uint64_t info_subtype) {
    R_RETURN(GetSystemInfo(system, out, info_type, handle, info_subtype));
}

} // namespace Kernel::Svc
