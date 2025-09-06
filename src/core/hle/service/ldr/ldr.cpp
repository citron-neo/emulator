// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ldr/ldr.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::LDR {

class DebugMonitor final : public ServiceFramework<DebugMonitor> {
public:
    explicit DebugMonitor(Core::System& system_) : ServiceFramework{system_, "ldr:dmnt"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "SetProgramArgument"},
            {1, nullptr, "FlushArguments"},
            {2, nullptr, "GetProcessModuleInfo"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class ProcessManager final : public ServiceFramework<ProcessManager> {
public:
    explicit ProcessManager(Core::System& system_) : ServiceFramework{system_, "ldr:pm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "CreateProcess"},
            {1, nullptr, "GetProgramInfo"},
            {2, nullptr, "PinProgram"},
            {3, nullptr, "UnpinProgram"},
            {4, nullptr, "SetEnabledProgramVerification"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

class Shell final : public ServiceFramework<Shell> {
public:
    explicit Shell(Core::System& system_) : ServiceFramework{system_, "ldr:shel"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "SetProgramArgument"},
            {1, nullptr, "FlushArguments"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ldr:dmnt", std::make_shared<DebugMonitor>(system));
    server_manager->RegisterNamedService("ldr:pm", std::make_shared<ProcessManager>(system));
    server_manager->RegisterNamedService("ldr:shel", std::make_shared<Shell>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::LDR
