/* This file is part of the dynarmic project.
 * Copyright (c) 2021 MerryMage
 * SPDX-License-Identifier: 0BSD
 */
#pragma once

#include <cstdint>
#include <optional>

namespace Dynarmic::A32 {

using VAddr = std::uint32_t;

class IREmitter;

struct TranslateCallbacks {
    // All reads through this callback are 4-byte aligned.
    // Memory must be interpreted as little endian.
    virtual std::optional<std::uint32_t> MemoryReadCode(VAddr vaddr) = 0;

    // This function is called before the instruction at pc is read.
    // IR code can be emitted by the callee prior to instruction handling.
    // By returning false the callee precludes the translation of the instruction;
    // in such case the callee is responsible for setting the terminal.
    virtual bool PreCodeReadHook(bool is_thumb, VAddr pc, A32::IREmitter& ir) = 0;

    // This function is called before the instruction at pc is interpreted.
    // IR code can be emitted by the callee prior to translation of the instruction.
    virtual void PreCodeTranslationHook(bool is_thumb, VAddr pc, A32::IREmitter& ir) = 0;

    // How many ticks should this instruction take to execute?
    virtual std::uint64_t GetTicksForCode(bool is_thumb, VAddr vaddr, std::uint32_t instruction) = 0;
};

}  // namespace Dynarmic::A32
