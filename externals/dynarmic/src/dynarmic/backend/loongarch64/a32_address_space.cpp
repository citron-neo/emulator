// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dynarmic/backend/loongarch64/a32_address_space.h"

#include "dynarmic/common/assert.h"

#include "dynarmic/backend/loongarch64/a32_jitstate.h"
#include "dynarmic/backend/loongarch64/abi.h"
#include "dynarmic/backend/loongarch64/emit_loongarch64.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/frontend/A32/translate/a32_translate.h"
#include "dynarmic/ir/opt_passes.h"

namespace Dynarmic::Backend::LoongArch64 {

A32AddressSpace::A32AddressSpace(const A32::UserConfig& conf)
        : conf(conf)
        , cb(conf.code_cache_size) {
    EmitPrelude();
}

void A32AddressSpace::GenerateIR(IR::Block& ir_block, IR::LocationDescriptor descriptor) const {
    A32::Translate(ir_block, A32::LocationDescriptor{descriptor}, conf.callbacks, {conf.arch_version, conf.define_unpredictable_behaviour, conf.hook_hint_instructions});
    Optimization::Optimize(ir_block, conf, {.sha256 = true});
}

CodePtr A32AddressSpace::Get(IR::LocationDescriptor descriptor) {
    if (const auto iter = block_entries.find(descriptor.Value()); iter != block_entries.end()) {
        return iter->second;
    }
    return nullptr;
}

CodePtr A32AddressSpace::GetOrEmit(IR::LocationDescriptor descriptor) {
    if (CodePtr block_entry = Get(descriptor)) {
        return block_entry;
    }

    IR::Block ir_block{descriptor};
    GenerateIR(ir_block, descriptor);
    const EmittedBlockInfo block_info = Emit(std::move(ir_block));

    block_infos.insert_or_assign(descriptor.Value(), block_info);
    block_entries.insert_or_assign(descriptor.Value(), block_info.entry_point);
    return block_info.entry_point;
}

void A32AddressSpace::ClearCache() {
    block_entries.clear();
    block_infos.clear();
    SetCursorPtr(prelude_info.end_of_prelude);
}

void A32AddressSpace::EmitPrelude() {
    prelude_info.run_code = GetCursorPtr<PreludeInfo::RunCodeFuncType>();

    // Save all GPRs except sp (r3) and tp (r2)
    la_addi_d(&cb.as, LA_SP, LA_SP, -64 * 8);
    for (u32 i = 1; i < 32; i++) {
        if (i == LA_SP || i == LA_TP)
            continue;
        la_st_d(&cb.as, static_cast<la_gpr_t>(i), LA_SP, static_cast<int32_t>(i * 8));
    }

    // Set up reserved registers and jump to block entry
    la_move(&cb.as, Xstate, LA_A1);  // Xstate = state ptr
    la_move(&cb.as, Xhalt, LA_A2);   // Xhalt  = halt reason ptr
    la_jr(&cb.as, LA_A0);            // jump to block_entry

    prelude_info.return_from_run_code = GetCursorPtr<CodePtr>();

    // Restore all GPRs except sp and tp
    for (u32 i = 1; i < 32; i++) {
        if (i == LA_SP || i == LA_TP)
            continue;
        la_ld_d(&cb.as, static_cast<la_gpr_t>(i), LA_SP, static_cast<int32_t>(i * 8));
    }
    la_addi_d(&cb.as, LA_SP, LA_SP, 64 * 8);
    la_ret(&cb.as);

    prelude_info.end_of_prelude = GetCursorPtr<CodePtr>();
}

void A32AddressSpace::SetCursorPtr(CodePtr ptr) {
    cb.as.cursor = reinterpret_cast<uint8_t*>(ptr);
}

size_t A32AddressSpace::GetRemainingSize() {
    return la_get_remaining_buffer_size(&cb.as);
}

EmittedBlockInfo A32AddressSpace::Emit(IR::Block block) {
    if (GetRemainingSize() < 1024 * 1024) {
        ClearCache();
    }

    EmitConfig emit_conf{};
    EmittedBlockInfo block_info = EmitLoongArch64(cb.as, std::move(block), emit_conf);
    Link(block_info);

    return block_info;
}

void A32AddressSpace::Link(EmittedBlockInfo& block_info) {
    for (const auto& reloc : block_info.relocations) {
        uint8_t* patch_location = reinterpret_cast<uint8_t*>(block_info.entry_point) + reloc.code_offset;

        switch (reloc.target) {
        case LinkTarget::ReturnFromRunCode: {
            std::ptrdiff_t off = reinterpret_cast<uint8_t*>(prelude_info.return_from_run_code) - patch_location;
            lagoon_assembler_t patch_as;
            la_init_assembler(&patch_as, patch_location, 4);
            la_b(&patch_as, static_cast<int32_t>(off));
            break;
        }
        default:
            ASSERT(false && "Invalid relocation target");
        }
    }
}

}  // namespace Dynarmic::Backend::LoongArch64
