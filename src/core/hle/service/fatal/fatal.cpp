// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <ctime>
#include <fmt/chrono.h>
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/swap.h"
#include "core/core.h"
#include "core/hle/service/fatal/fatal.h"
#include "core/hle/service/fatal/fatal_p.h"
#include "core/hle/service/fatal/fatal_u.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/reporter.h"

namespace Service::Fatal {

Module::Interface::Interface(std::shared_ptr<Module> module_, Core::System& system_,
                             const char* name)
    : ServiceFramework{system_, name}, module{std::move(module_)} {}

Module::Interface::~Interface() = default;

struct FatalInfo {
    enum class Architecture : s32 {
        AArch64,
        AArch32,
    };

    const char* ArchAsString() const {
        return arch == Architecture::AArch64 ? "AArch64" : "AArch32";
    }

    std::array<u64_le, 31> registers{};
    u64_le sp{};
    u64_le pc{};
    u64_le pstate{};
    u64_le afsr0{};
    u64_le afsr1{};
    u64_le esr{};
    u64_le far{};

    std::array<u64_le, 32> backtrace{};
    u64_le program_entry_point{};

    // Bit flags that indicate which registers have been set with values
    // for this context. The service itself uses these to determine which
    // registers to specifically print out.
    u64_le set_flags{};

    u32_le backtrace_size{};
    Architecture arch{};
    u32_le unk10{}; // TODO(ogniK): Is this even used or is it just padding?
};
static_assert(sizeof(FatalInfo) == 0x250, "FatalInfo is an invalid size");

enum class FatalType : u32 {
    ErrorReportAndScreen = 0,
    ErrorReport = 1,
    ErrorScreen = 2,
};

static void GenerateErrorReport(Core::System& system, Result error_code, const FatalInfo& info) {
    const auto title_id = system.GetApplicationProcessProgramID();
    const auto module = static_cast<u32>(error_code.GetModule());
    const auto description = static_cast<u32>(error_code.GetDescription());

    // Check if this is an undefined/unknown module
    std::string module_note;
    if (module == 38) {
        module_note = fmt::format(
            "\n⚠️  WARNING: Error module 38 is undefined/unknown!\n"
            "This error may be game-generated or from an unimplemented service.\n"
            "Error code: 2038-{:04d} (0x{:08X})\n"
            "If you're experiencing multiplayer issues, this may be a stubbing issue.\n\n",
            description, error_code.raw);
    } else if (module == 56) {
        module_note = fmt::format(
            "\n⚠️  WARNING: Error module 56 is undefined/unknown!\n"
            "This error may be game-generated or from an unimplemented service.\n"
            "Error code: 2056-{:04d} (0x{:08X})\n"
            "This may be related to online services or network functionality.\n\n",
            description, error_code.raw);
    }

    std::string crash_report = fmt::format(
        "Citron {}-{} crash report\n"
        "Title ID:                        {:016x}\n"
        "Result:                          0x{:X} ({:04}-{:04d})\n"
        "Set flags:                       0x{:16X}\n"
        "Program entry point:             0x{:16X}\n"
        "{}"
        "\n",
        Common::g_scm_branch, Common::g_scm_desc, title_id, error_code.raw,
        2000 + module, description, info.set_flags, info.program_entry_point, module_note);
    if (info.backtrace_size != 0x0) {
        crash_report += "Registers:\n";
        for (size_t i = 0; i < info.registers.size(); i++) {
            crash_report +=
                fmt::format("    X[{:02d}]:                       {:016x}\n", i, info.registers[i]);
        }
        crash_report += fmt::format("    SP:                          {:016x}\n", info.sp);
        crash_report += fmt::format("    PC:                          {:016x}\n", info.pc);
        crash_report += fmt::format("    PSTATE:                      {:016x}\n", info.pstate);
        crash_report += fmt::format("    AFSR0:                       {:016x}\n", info.afsr0);
        crash_report += fmt::format("    AFSR1:                       {:016x}\n", info.afsr1);
        crash_report += fmt::format("    ESR:                         {:016x}\n", info.esr);
        crash_report += fmt::format("    FAR:                         {:016x}\n", info.far);
        crash_report += "\nBacktrace:\n";
        for (u32 i = 0; i < std::min<u32>(info.backtrace_size, 32); i++) {
            crash_report +=
                fmt::format("    Backtrace[{:02d}]:               {:016x}\n", i, info.backtrace[i]);
        }

        crash_report += fmt::format("Architecture:                    {}\n", info.ArchAsString());
        crash_report += fmt::format("Unknown 10:                      0x{:016x}\n", info.unk10);
    }

    LOG_ERROR(Service_Fatal, "{}", crash_report);

    system.GetReporter().SaveCrashReport(
        title_id, error_code, info.set_flags, info.program_entry_point, info.sp, info.pc,
        info.pstate, info.afsr0, info.afsr1, info.esr, info.far, info.registers, info.backtrace,
        info.backtrace_size, info.ArchAsString(), info.unk10);
}

static void ThrowFatalError(Core::System& system, Result error_code, FatalType fatal_type,
                            const FatalInfo& info) {
    const auto module = static_cast<u32>(error_code.GetModule());
    LOG_ERROR(Service_Fatal, "Threw fatal error type {} with error code 0x{:X}", fatal_type,
              error_code.raw);

    switch (fatal_type) {
    case FatalType::ErrorReportAndScreen:
        GenerateErrorReport(system, error_code, info);
        [[fallthrough]];
    case FatalType::ErrorScreen:
        // For Module 56 errors (unknown/game-generated), log and continue instead of crashing
        // These are often related to online services being unavailable
        if (module == 56) {
            LOG_WARNING(Service_Fatal,
                        "Module 56 error detected - likely game-generated due to unavailable "
                        "online services. Continuing execution instead of crashing.");
            break;
        }
        // Since we have no fatal:u error screen. We should just kill execution instead
        ASSERT(false);
        break;
        // Should not throw a fatal screen but should generate an error report
    case FatalType::ErrorReport:
        GenerateErrorReport(system, error_code, info);
        break;
    }
}

void Module::Interface::ThrowFatal(HLERequestContext& ctx) {
    LOG_ERROR(Service_Fatal, "called");
    IPC::RequestParser rp{ctx};
    const auto error_code = rp.Pop<Result>();

    ThrowFatalError(system, error_code, FatalType::ErrorScreen, {});
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ThrowFatalWithPolicy(HLERequestContext& ctx) {
    LOG_ERROR(Service_Fatal, "called");
    IPC::RequestParser rp(ctx);
    const auto error_code = rp.Pop<Result>();
    const auto fatal_type = rp.PopEnum<FatalType>();

    ThrowFatalError(system, error_code, fatal_type,
                    {}); // No info is passed with ThrowFatalWithPolicy
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void Module::Interface::ThrowFatalWithCpuContext(HLERequestContext& ctx) {
    LOG_ERROR(Service_Fatal, "called");
    IPC::RequestParser rp(ctx);
    const auto error_code = rp.Pop<Result>();
    const auto fatal_type = rp.PopEnum<FatalType>();
    const auto fatal_info = ctx.ReadBuffer();
    FatalInfo info{};

    ASSERT_MSG(fatal_info.size() == sizeof(FatalInfo), "Invalid fatal info buffer size!");
    std::memcpy(&info, fatal_info.data(), sizeof(FatalInfo));

    ThrowFatalError(system, error_code, fatal_type, info);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    auto module = std::make_shared<Module>();

    server_manager->RegisterNamedService("fatal:p", std::make_shared<Fatal_P>(module, system));
    server_manager->RegisterNamedService("fatal:u", std::make_shared<Fatal_U>(module, system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Fatal
