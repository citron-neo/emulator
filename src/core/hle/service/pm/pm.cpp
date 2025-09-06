// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/pm/pm.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::PM {

namespace {

constexpr Result ResultProcessNotFound{ErrorModule::PM, 1};
[[maybe_unused]] constexpr Result ResultAlreadyStarted{ErrorModule::PM, 2};
[[maybe_unused]] constexpr Result ResultNotTerminated{ErrorModule::PM, 3};
[[maybe_unused]] constexpr Result ResultDebugHookInUse{ErrorModule::PM, 4};
[[maybe_unused]] constexpr Result ResultApplicationRunning{ErrorModule::PM, 5};
[[maybe_unused]] constexpr Result ResultInvalidSize{ErrorModule::PM, 6};

constexpr u64 NO_PROCESS_FOUND_PID{0};

using ProcessList = std::list<Kernel::KScopedAutoObject<Kernel::KProcess>>;

template <typename F>
Kernel::KScopedAutoObject<Kernel::KProcess> SearchProcessList(ProcessList& process_list,
                                                              F&& predicate) {
    const auto iter = std::find_if(process_list.begin(), process_list.end(), predicate);

    if (iter == process_list.end()) {
        return nullptr;
    }

    return iter->GetPointerUnsafe();
}

void GetApplicationPidGeneric(HLERequestContext& ctx, ProcessList& process_list) {
    auto process = SearchProcessList(process_list, [](auto& p) { return p->IsApplication(); });

    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push(process.IsNull() ? NO_PROCESS_FOUND_PID : process->GetProcessId());
}

} // Anonymous namespace

class BootMode final : public ServiceFramework<BootMode> {
public:
    explicit BootMode(Core::System& system_) : ServiceFramework{system_, "pm:bm"} {
        static const FunctionInfo functions[] = {
            {0, &BootMode::GetBootMode, "GetBootMode"},
            {1, &BootMode::SetMaintenanceBoot, "SetMaintenanceBoot"},
            {2, nullptr, "UnknownCmd2"},
            {3, nullptr, "UnknownCmd3"},
        };
        RegisterHandlers(functions);
    }

private:
    void GetBootMode(HLERequestContext& ctx) {
        LOG_DEBUG(Service_PM, "called");

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.PushEnum(boot_mode);
    }

    void SetMaintenanceBoot(HLERequestContext& ctx) {
        LOG_DEBUG(Service_PM, "called");

        boot_mode = SystemBootMode::Maintenance;

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    SystemBootMode boot_mode = SystemBootMode::Normal;
};

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor(Core::System& system_) : ServiceFramework{system_, "pm:dmnt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetJitDebugProcessIdList"},
            {1, nullptr, "StartProcess"},
            {2, &DebugMonitor::GetProcessId, "GetProcessId"},
            {3, nullptr, "HookToCreateProcess"},
            {4, &DebugMonitor::GetApplicationProcessId, "GetApplicationProcessId"},
            {5, nullptr, "HookToCreateApplicationProcess"},
            {6, nullptr, "ClearHook"},
            {7, nullptr, "GetProgramId"},
            {65000, &DebugMonitor::AtmosphereGetProcessInfo, "AtmosphereGetProcessInfo"},
            {65001, nullptr, "AtmosphereGetCurrentLimitInfo"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetProcessId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto program_id = rp.PopRaw<u64>();

        LOG_DEBUG(Service_PM, "called, program_id={:016X}", program_id);

        auto list = kernel.GetProcessList();
        auto process = SearchProcessList(
            list, [program_id](auto& p) { return p->GetProgramId() == program_id; });

        if (process.IsNull()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultProcessNotFound);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(process->GetProcessId());
    }

    void GetApplicationProcessId(HLERequestContext& ctx) {
        LOG_DEBUG(Service_PM, "called");
        auto list = kernel.GetProcessList();
        GetApplicationPidGeneric(ctx, list);
    }

    void AtmosphereGetProcessInfo(HLERequestContext& ctx) {
        // https://github.com/Atmosphere-NX/Atmosphere/blob/master/stratosphere/pm/source/impl/pm_process_manager.cpp#L614
        // This implementation is incomplete; only a handle to the process is returned.
        IPC::RequestParser rp{ctx};
        const auto pid = rp.PopRaw<u64>();

        LOG_WARNING(Service_PM, "(Partial Implementation) called, pid={:016X}", pid);

        auto list = kernel.GetProcessList();
        auto process = SearchProcessList(list, [pid](auto& p) { return p->GetProcessId() == pid; });

        if (process.IsNull()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultProcessNotFound);
            return;
        }

        struct ProgramLocation {
            u64 program_id;
            u8 storage_id;
        };
        static_assert(sizeof(ProgramLocation) == 0x10, "ProgramLocation has an invalid size");

        struct OverrideStatus {
            u64 keys_held;
            u64 flags;
        };
        static_assert(sizeof(OverrideStatus) == 0x10, "OverrideStatus has an invalid size");

        OverrideStatus override_status{};
        ProgramLocation program_location{
            .program_id = process->GetProgramId(),
            .storage_id = 0,
        };

        IPC::ResponseBuilder rb{ctx, 10, 1};
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(*process);
        rb.PushRaw(program_location);
        rb.PushRaw(override_status);
    }
};

class Info final : public ServiceFramework<Info> {
public:
    explicit Info(Core::System& system_) : ServiceFramework{system_, "pm:info"} {
        static const FunctionInfo functions[] = {
            {0, &Info::GetProgramId, "GetProgramId"},
            {1, nullptr, "GetAppletCurrentResourceLimitValues"},
            {2, nullptr, "GetAppletPeakResourceLimitValues"},
            {65000, &Info::AtmosphereGetProcessId, "AtmosphereGetProcessId"},
            {65001, nullptr, "AtmosphereHasLaunchedProgram"},
            {65002, nullptr, "AtmosphereGetProcessInfo"},
        };
        RegisterHandlers(functions);
    }

private:
    void GetProgramId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto process_id = rp.PopRaw<u64>();

        LOG_DEBUG(Service_PM, "called, process_id={:016X}", process_id);

        auto list = kernel.GetProcessList();
        auto process = SearchProcessList(
            list, [process_id](auto& p) { return p->GetProcessId() == process_id; });

        if (process.IsNull()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultProcessNotFound);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(process->GetProgramId());
    }

    void AtmosphereGetProcessId(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto program_id = rp.PopRaw<u64>();

        LOG_DEBUG(Service_PM, "called, program_id={:016X}", program_id);

        auto list = system.Kernel().GetProcessList();
        auto process = SearchProcessList(
            list, [program_id](auto& p) { return p->GetProgramId() == program_id; });

        if (process.IsNull()) {
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(ResultProcessNotFound);
            return;
        }

        IPC::ResponseBuilder rb{ctx, 4};
        rb.Push(ResultSuccess);
        rb.Push(process->GetProcessId());
    }
};

class Shell final : public ServiceFramework<Shell> {
public:
    explicit Shell(Core::System& system_) : ServiceFramework{system_, "pm:shell"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "LaunchProgram"},
            {1, nullptr, "TerminateProcess"},
            {2, nullptr, "TerminateProgram"},
            {3, nullptr, "GetProcessEventHandle"},
            {4, nullptr, "GetProcessEventInfo"},
            {5, nullptr, "NotifyBootFinished"},
            {6, &Shell::GetApplicationProcessIdForShell, "GetApplicationProcessIdForShell"},
            {7, nullptr, "BoostSystemMemoryResourceLimit"},
            {8, nullptr, "BoostApplicationThreadResourceLimit"},
            {9, nullptr, "GetBootFinishedEventHandle"},
            {10, nullptr, "BoostSystemThreadResourceLimit"},
            {12, nullptr, "GetProcessId"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetApplicationProcessIdForShell(HLERequestContext& ctx) {
        LOG_DEBUG(Service_PM, "called");
        auto list = kernel.GetProcessList();
        GetApplicationPidGeneric(ctx, list);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pm:bm", std::make_shared<BootMode>(system));
    server_manager->RegisterNamedService("pm:dmnt", std::make_shared<DebugMonitor>(system));
    server_manager->RegisterNamedService("pm:info", std::make_shared<Info>(system));
    server_manager->RegisterNamedService("pm:shell", std::make_shared<Shell>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PM
