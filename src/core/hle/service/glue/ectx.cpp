// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/glue/ectx.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Glue {

// This is nn::err::context::IContextRegistrar
class IContextRegistrar : public ServiceFramework<IContextRegistrar> {
public:
    IContextRegistrar(Core::System& system_) : ServiceFramework{system_, "IContextRegistrar"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IContextRegistrar::Complete, "Complete"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

    ~IContextRegistrar() override = default;

private:
    void Complete(HLERequestContext& ctx) {
        struct InputParameters {
            u32 unk;
        };
        struct OutputParameters {
            u32 unk;
        };

        IPC::RequestParser rp{ctx};
        [[maybe_unused]] auto input = rp.PopRaw<InputParameters>();
        [[maybe_unused]] auto value = ctx.ReadBuffer();

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(0);
    }
};

ECTX_AW::ECTX_AW(Core::System& system_) : ServiceFramework{system_, "ectx:aw"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ECTX_AW::CreateContextRegistrar, "CreateContextRegistrar"},
        {1, nullptr, "CommitContext"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ECTX_AW::~ECTX_AW() = default;

void ECTX_AW::CreateContextRegistrar(HLERequestContext& ctx) {
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<IContextRegistrar>(std::make_shared<IContextRegistrar>(system));
}

ECTX_R::ECTX_R(Core::System& system_) : ServiceFramework{system_, "ectx:r"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "CommitContext"}, // Stub - context reading functionality
    };
    // clang-format on

    RegisterHandlers(functions);
}

ECTX_R::~ECTX_R() = default;

} // namespace Service::Glue
