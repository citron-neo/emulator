// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "dynarmic/common/common_types.h"

#if defined(ARCHITECTURE_x86_64)
namespace Dynarmic::Backend::X64 {
class BlockOfCode;
}  // namespace Dynarmic::Backend::X64
#elif defined(ARCHITECTURE_arm64)
namespace oaknut {
class CodeBlock;
}  // namespace oaknut
#elif defined(ARCHITECTURE_riscv64)
namespace Dynarmic::Backend::RV64 {
class CodeBlock;
}  // namespace Dynarmic::Backend::RV64
#elif defined(ARCHITECTURE_loongarch64)
namespace Dynarmic::Backend::LoongArch64 {
class CodeBlock;
}  // namespace Dynarmic::Backend::LoongArch64
#else
#    error "Invalid architecture"
#endif

namespace Dynarmic::Backend {

#if defined(ARCHITECTURE_x86_64)
struct FakeCall {
    u64 call_rip;
    u64 ret_rip;
};
#elif defined(ARCHITECTURE_arm64)
struct FakeCall {
    u64 call_pc;
};
#elif defined(ARCHITECTURE_riscv64)
struct FakeCall {
    u64 call_sepc;
};
#elif defined(ARCHITECTURE_loongarch64)
struct FakeCall {
    u64 call_pc;
};
#else
#    error "Invalid architecture"
#endif

class ExceptionHandler final {
public:
    ExceptionHandler();
    ~ExceptionHandler();

#if defined(ARCHITECTURE_x86_64)
    void Register(X64::BlockOfCode& code);
#elif defined(ARCHITECTURE_arm64)
    void Register(oaknut::CodeBlock& mem, std::size_t mem_size);
#elif defined(ARCHITECTURE_riscv64)
    void Register(RV64::CodeBlock& mem, std::size_t mem_size);
#elif defined(ARCHITECTURE_loongarch64)
    void Register(LoongArch64::CodeBlock& mem, std::size_t mem_size);
#else
#    error "Invalid architecture"
#endif

    bool SupportsFastmem() const noexcept;
    void SetFastmemCallback(std::function<FakeCall(u64)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace Dynarmic::Backend
