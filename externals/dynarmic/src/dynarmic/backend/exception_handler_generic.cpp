// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/exception_handler.h"

namespace Dynarmic::Backend {

struct ExceptionHandler::Impl final {
};

ExceptionHandler::ExceptionHandler() = default;
ExceptionHandler::~ExceptionHandler() = default;

#if defined(ARCHITECTURE_x86_64)
void ExceptionHandler::Register(X64::BlockOfCode&) {
    // Do nothing
}
#elif defined(ARCHITECTURE_arm64)
void ExceptionHandler::Register(oaknut::CodeBlock&, std::size_t) {
    // Do nothing
}
#elif defined(ARCHITECTURE_riscv64)
void ExceptionHandler::Register(RV64::CodeBlock&, std::size_t) {
    // Do nothing
}
#elif defined(ARCHITECTURE_loongarch64)
void ExceptionHandler::Register(LoongArch64::CodeBlock&, std::size_t) {
    // Do nothing
}
#else
#    error "Invalid architecture"
#endif

bool ExceptionHandler::SupportsFastmem() const noexcept {
    return false;
}

void ExceptionHandler::SetFastmemCallback(std::function<FakeCall(u64)>) {
    // Do nothing
}

}  // namespace Dynarmic::Backend
