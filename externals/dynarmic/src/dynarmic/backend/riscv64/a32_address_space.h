// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2024 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <biscuit/assembler.hpp>
#include "dynarmic/common/container/unordered_map.h"

#include "dynarmic/backend/riscv64/code_block.h"
#include "dynarmic/backend/riscv64/emit_riscv64.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/halt_reason.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::RV64 {

struct A32JitState;

class A32AddressSpace final {
public:
    explicit A32AddressSpace(const A32::UserConfig& conf);

    void GenerateIR(IR::Block& ir_block, IR::LocationDescriptor) const;

    CodePtr Get(IR::LocationDescriptor descriptor);

    CodePtr GetOrEmit(IR::LocationDescriptor descriptor);

    void ClearCache();

private:
    friend class A32Core;

    void EmitPrelude();

    template<typename T>
    T GetMemPtr() {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, std::uintptr_t> || std::is_same_v<T, std::intptr_t>);
        return reinterpret_cast<T>(as.GetBufferPointer(0));
    }

    template<typename T>
    T GetMemPtr() const {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, std::uintptr_t> || std::is_same_v<T, std::intptr_t>);
        return reinterpret_cast<const T>(as.GetBufferPointer(0));
    }

    template<typename T>
    T GetCursorPtr() {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, std::uintptr_t> || std::is_same_v<T, std::intptr_t>);
        return reinterpret_cast<T>(as.GetCursorPointer());
    }

    template<typename T>
    T GetCursorPtr() const {
        static_assert(std::is_pointer_v<T> || std::is_same_v<T, std::uintptr_t> || std::is_same_v<T, std::intptr_t>);
        return reinterpret_cast<const T>(as.GetCursorPointer());
    }

    void SetCursorPtr(CodePtr ptr);

    size_t GetRemainingSize();
    EmittedBlockInfo Emit(IR::Block ir_block);
    void Link(EmittedBlockInfo& block);

    const A32::UserConfig conf;

    CodeBlock cb;
    biscuit::Assembler as;

    ::Common::unordered_map<u64, CodePtr> block_entries;
    ::Common::unordered_map<u64, EmittedBlockInfo> block_infos;

    struct PreludeInfo {
        CodePtr end_of_prelude;

        using RunCodeFuncType = HaltReason (*)(CodePtr entry_point, A32JitState* context, volatile u32* halt_reason);
        RunCodeFuncType run_code;
        CodePtr return_from_run_code;
    } prelude_info;
};

}  // namespace Dynarmic::Backend::RV64
