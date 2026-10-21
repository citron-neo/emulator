// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dynarmic/backend/loongarch64/lagoon_cpp.h"
#include "dynarmic/common/container/unordered_map.h"

#include "dynarmic/backend/loongarch64/code_block.h"
#include "dynarmic/backend/loongarch64/emit_loongarch64.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/halt_reason.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::Backend::LoongArch64 {

struct A32JitState;

class A32AddressSpace final {
public:
    explicit A32AddressSpace(const A32::UserConfig& conf);

    void GenerateIR(IR::Block& ir_block, IR::LocationDescriptor descriptor) const;
    CodePtr Get(IR::LocationDescriptor descriptor);
    CodePtr GetOrEmit(IR::LocationDescriptor descriptor);

    void ClearCache();

private:
    void EmitPrelude();

    template<typename T>
    T GetCursorPtr() {
        return reinterpret_cast<T>(cb.as.cursor);
    }

    template<typename T>
    T GetCursorPtr() const {
        return reinterpret_cast<T>(cb.as.cursor);
    }

    template<typename T>
    T GetMemPtr() const {
        return cb.ptr<T>();
    }

    void SetCursorPtr(CodePtr ptr);
    size_t GetRemainingSize();
    EmittedBlockInfo Emit(IR::Block block);
    void Link(EmittedBlockInfo& block_info);

    const A32::UserConfig conf;
    CodeBlock cb;

    ::Common::unordered_map<u64, CodePtr> block_entries;
    ::Common::unordered_map<u64, EmittedBlockInfo> block_infos;

public:
    struct PreludeInfo {
        CodePtr end_of_prelude;
        using RunCodeFuncType = HaltReason (*)(CodePtr entry_point, A32JitState* context, volatile u32* halt_reason);
        RunCodeFuncType run_code;
        CodePtr return_from_run_code;
    } prelude_info;
};

}  // namespace Dynarmic::Backend::LoongArch64
