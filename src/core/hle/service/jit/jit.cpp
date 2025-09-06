// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/arm/debug.h"
#include "core/arm/symbols.h"
#include "core/core.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/result.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/jit/jit.h"
#include "core/hle/service/jit/jit_code_memory.h"
#include "core/hle/service/jit/jit_context.h"
#include "core/hle/service/server_manager.h"
#include "core/memory.h"

namespace Service::JIT {

struct CodeRange {
    u64 offset;
    u64 size;
};

using Struct32 = std::array<u64, 4>;
static_assert(sizeof(Struct32) == 32, "Struct32 has wrong size");

class IJitEnvironment final : public ServiceFramework<IJitEnvironment> {
public:
    explicit IJitEnvironment(Core::System& system_,
                             Kernel::KScopedAutoObject<Kernel::KProcess> process_,
                             CodeMemory&& user_rx_, CodeMemory&& user_ro_)
        : ServiceFramework{system_, "IJitEnvironment"}, process{std::move(process_)},
          user_rx{std::move(user_rx_)}, user_ro{std::move(user_ro_)},
          context{system_.ApplicationMemory()} {

        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&IJitEnvironment::GenerateCode>, "GenerateCode"},
            {1, C<&IJitEnvironment::Control>, "Control"},
            {1000, C<&IJitEnvironment::LoadPlugin>, "LoadPlugin"},
            {1001, C<&IJitEnvironment::GetCodeAddress>, "GetCodeAddress"},
        };
        // clang-format on

        RegisterHandlers(functions);

        // Identity map user code range into sysmodule context
        configuration.user_rx_memory.size = user_rx.GetSize();
        configuration.user_rx_memory.offset = user_rx.GetAddress();
        configuration.user_ro_memory.size = user_ro.GetSize();
        configuration.user_ro_memory.offset = user_ro.GetAddress();

        configuration.sys_rx_memory = configuration.user_rx_memory;
        configuration.sys_ro_memory = configuration.user_ro_memory;
    }

    Result GenerateCode(Out<s32> out_return_value, Out<CodeRange> out_range0,
                        Out<CodeRange> out_range1, OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                        u32 data_size, u64 command, CodeRange range0, CodeRange range1,
                        Struct32 data, InBuffer<BufferAttr_HipcMapAlias> buffer) {
        // Function call prototype:
        // void GenerateCode(s32* ret, CodeRange* c0_out, CodeRange* c1_out, JITConfiguration* cfg,
        //                   u64 cmd, u8* input_buf, size_t input_size, CodeRange* c0_in,
        //                   CodeRange* c1_in, Struct32* data, size_t data_size, u8* output_buf,
        //                   size_t output_size);
        //
        // The command argument is used to control the behavior of the plugin during code
        // generation. The configuration allows the plugin to access the output code ranges, and the
        // other arguments are used to transfer state between the game and the plugin.

        const VAddr ret_ptr{context.AddHeap(0u)};
        const VAddr c0_in_ptr{context.AddHeap(range0)};
        const VAddr c1_in_ptr{context.AddHeap(range1)};
        const VAddr c0_out_ptr{context.AddHeap(ClearSize(range0))};
        const VAddr c1_out_ptr{context.AddHeap(ClearSize(range1))};

        const VAddr input_ptr{context.AddHeap(buffer.data(), buffer.size())};
        const VAddr output_ptr{context.AddHeap(out_buffer.data(), out_buffer.size())};
        const VAddr data_ptr{context.AddHeap(data)};
        const VAddr configuration_ptr{context.AddHeap(configuration)};

        // The callback does not directly return a value, it only writes to the output pointer
        context.CallFunction(callbacks.GenerateCode, ret_ptr, c0_out_ptr, c1_out_ptr,
                             configuration_ptr, command, input_ptr, buffer.size(), c0_in_ptr,
                             c1_in_ptr, data_ptr, data_size, output_ptr, out_buffer.size());

        *out_return_value = context.GetHeap<s32>(ret_ptr);
        *out_range0 = context.GetHeap<CodeRange>(c0_out_ptr);
        *out_range1 = context.GetHeap<CodeRange>(c1_out_ptr);
        context.GetHeap(output_ptr, out_buffer.data(), out_buffer.size());

        if (*out_return_value != 0) {
            LOG_WARNING(Service_JIT, "plugin GenerateCode callback failed");
            R_THROW(ResultUnknown);
        }

        R_SUCCEED();
    }

    Result Control(Out<s32> out_return_value, InBuffer<BufferAttr_HipcMapAlias> in_data,
                   OutBuffer<BufferAttr_HipcMapAlias> out_data, u64 command) {
        // Function call prototype:
        // u64 Control(s32* ret, JITConfiguration* cfg, u64 cmd, u8* input_buf, size_t input_size,
        //             u8* output_buf, size_t output_size);
        //
        // This function is used to set up the state of the plugin before code generation, generally
        // passing objects like pointers to VM state from the game. It is usually called once.

        const VAddr ret_ptr{context.AddHeap(0u)};
        const VAddr configuration_ptr{context.AddHeap(configuration)};
        const VAddr input_ptr{context.AddHeap(in_data.data(), in_data.size())};
        const VAddr output_ptr{context.AddHeap(out_data.data(), out_data.size())};

        const u64 wrapper_value{context.CallFunction(callbacks.Control, ret_ptr, configuration_ptr,
                                                     command, input_ptr, in_data.size(), output_ptr,
                                                     out_data.size())};

        *out_return_value = context.GetHeap<s32>(ret_ptr);
        context.GetHeap(output_ptr, out_data.data(), out_data.size());

        if (wrapper_value == 0 && *out_return_value == 0) {
            R_SUCCEED();
        }

        LOG_WARNING(Service_JIT, "plugin Control callback failed");
        R_THROW(ResultUnknown);
    }

    Result LoadPlugin(u64 tmem_size, InCopyHandle<Kernel::KTransferMemory> tmem,
                      InBuffer<BufferAttr_HipcMapAlias> nrr,
                      InBuffer<BufferAttr_HipcMapAlias> nro) {
        if (!tmem) {
            LOG_ERROR(Service_JIT, "Invalid transfer memory handle!");
            R_THROW(ResultUnknown);
        }

        // Set up the configuration with the required TransferMemory address
        configuration.transfer_memory.offset = GetInteger(tmem->GetSourceAddress());
        configuration.transfer_memory.size = tmem_size;

        // Gather up all the callbacks from the loaded plugin
        auto symbols{Core::Symbols::GetSymbols(nro, true)};
        const auto GetSymbol{[&](const std::string& name) { return symbols[name].first; }};

        callbacks.rtld_fini = GetSymbol("_fini");
        callbacks.rtld_init = GetSymbol("_init");
        callbacks.Control = GetSymbol("nnjitpluginControl");
        callbacks.ResolveBasicSymbols = GetSymbol("nnjitpluginResolveBasicSymbols");
        callbacks.SetupDiagnostics = GetSymbol("nnjitpluginSetupDiagnostics");
        callbacks.Configure = GetSymbol("nnjitpluginConfigure");
        callbacks.GenerateCode = GetSymbol("nnjitpluginGenerateCode");
        callbacks.GetVersion = GetSymbol("nnjitpluginGetVersion");
        callbacks.OnPrepared = GetSymbol("nnjitpluginOnPrepared");
        callbacks.Keeper = GetSymbol("nnjitpluginKeeper");

        if (callbacks.GetVersion == 0 || callbacks.Configure == 0 || callbacks.GenerateCode == 0 ||
            callbacks.OnPrepared == 0) {
            LOG_ERROR(Service_JIT, "plugin does not implement all necessary functionality");
            R_THROW(ResultUnknown);
        }

        if (!context.LoadNRO(nro)) {
            LOG_ERROR(Service_JIT, "failed to load plugin");
            R_THROW(ResultUnknown);
        }

        context.MapProcessMemory(configuration.sys_ro_memory.offset,
                                 configuration.sys_ro_memory.size);
        context.MapProcessMemory(configuration.sys_rx_memory.offset,
                                 configuration.sys_rx_memory.size);
        context.MapProcessMemory(configuration.transfer_memory.offset,
                                 configuration.transfer_memory.size);

        // Run ELF constructors, if needed
        if (callbacks.rtld_init != 0) {
            context.CallFunction(callbacks.rtld_init);
        }

        // Function prototype:
        // u64 GetVersion();
        const auto version{context.CallFunction(callbacks.GetVersion)};
        if (version != 1) {
            LOG_ERROR(Service_JIT, "unknown plugin version {}", version);
            R_THROW(ResultUnknown);
        }

        // Function prototype:
        // void ResolveBasicSymbols(void (*resolver)(const char* name));
        const auto resolve{context.GetHelper("_resolve")};
        if (callbacks.ResolveBasicSymbols != 0) {
            context.CallFunction(callbacks.ResolveBasicSymbols, resolve);
        }

        // Function prototype:
        // void SetupDiagnostics(u32 enabled, void (**resolver)(const char* name));
        const auto resolve_ptr{context.AddHeap(resolve)};
        if (callbacks.SetupDiagnostics != 0) {
            context.CallFunction(callbacks.SetupDiagnostics, 0u, resolve_ptr);
        }

        // Function prototype:
        // void Configure(u32* memory_flags);
        context.CallFunction(callbacks.Configure, 0ull);

        // Function prototype:
        // void OnPrepared(JITConfiguration* cfg);
        const auto configuration_ptr{context.AddHeap(configuration)};
        context.CallFunction(callbacks.OnPrepared, configuration_ptr);

        R_SUCCEED();
    }

    Result GetCodeAddress(Out<u64> rx_offset, Out<u64> ro_offset) {
        LOG_DEBUG(Service_JIT, "called");

        *rx_offset = configuration.user_rx_memory.offset;
        *ro_offset = configuration.user_ro_memory.offset;

        R_SUCCEED();
    }

private:
    struct GuestCallbacks {
        VAddr rtld_fini;
        VAddr rtld_init;
        VAddr Control;
        VAddr ResolveBasicSymbols;
        VAddr SetupDiagnostics;
        VAddr Configure;
        VAddr GenerateCode;
        VAddr GetVersion;
        VAddr Keeper;
        VAddr OnPrepared;
    };

    struct JITConfiguration {
        CodeRange user_rx_memory;
        CodeRange user_ro_memory;
        CodeRange transfer_memory;
        CodeRange sys_rx_memory;
        CodeRange sys_ro_memory;
    };

    static CodeRange ClearSize(CodeRange in) {
        in.size = 0;
        return in;
    }

    Kernel::KScopedAutoObject<Kernel::KProcess> process;
    CodeMemory user_rx;
    CodeMemory user_ro;
    GuestCallbacks callbacks;
    JITConfiguration configuration;
    JITContext context;
};

class JITU final : public ServiceFramework<JITU> {
public:
    explicit JITU(Core::System& system_) : ServiceFramework{system_, "jit:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&JITU::CreateJitEnvironment>, "CreateJitEnvironment"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CreateJitEnvironment(Out<SharedPointer<IJitEnvironment>> out_jit_environment,
                                u64 rx_size, u64 ro_size, InCopyHandle<Kernel::KProcess> process,
                                InCopyHandle<Kernel::KCodeMemory> rx_mem,
                                InCopyHandle<Kernel::KCodeMemory> ro_mem) {
        if (!process) {
            LOG_ERROR(Service_JIT, "process is null");
            R_THROW(ResultUnknown);
        }
        if (!rx_mem) {
            LOG_ERROR(Service_JIT, "rx_mem is null");
            R_THROW(ResultUnknown);
        }
        if (!ro_mem) {
            LOG_ERROR(Service_JIT, "ro_mem is null");
            R_THROW(ResultUnknown);
        }

        CodeMemory rx, ro;

        R_TRY(rx.Initialize(*process, *rx_mem, rx_size, Kernel::Svc::MemoryPermission::ReadExecute,
                            generate_random));
        R_TRY(ro.Initialize(*process, *ro_mem, ro_size, Kernel::Svc::MemoryPermission::Read,
                            generate_random));

        *out_jit_environment =
            std::make_shared<IJitEnvironment>(system, process.Get(), std::move(rx), std::move(ro));
        R_SUCCEED();
    }

private:
    std::mt19937_64 generate_random{};
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("jit:u", std::make_shared<JITU>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::JIT
