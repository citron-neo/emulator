// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <variant>
#include "dynarmic/backend/loongarch64/emit_loongarch64.h"

#include "dynarmic/backend/loongarch64/a32_jitstate.h"
#include "dynarmic/backend/loongarch64/abi.h"
#include "dynarmic/backend/loongarch64/emit_context.h"
#include "dynarmic/backend/loongarch64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::LoongArch64 {

template<IR::Opcode op>
void EmitIR(lagoon_assembler_t&, EmitContext&, IR::Inst*) {
    ASSERT(false && "Unimplemented opcode");
}

template<>
void EmitIR<IR::Opcode::Void>(lagoon_assembler_t&, EmitContext&, IR::Inst*) {}

template<>
void EmitIR<IR::Opcode::A32GetRegister>(lagoon_assembler_t&, EmitContext& ctx, IR::Inst* inst);

template<>
void EmitIR<IR::Opcode::A32SetRegister>(lagoon_assembler_t&, EmitContext& ctx, IR::Inst* inst);

template<>
void EmitIR<IR::Opcode::A32SetCpsrNZC>(lagoon_assembler_t&, EmitContext& ctx, IR::Inst* inst);

template<>
void EmitIR<IR::Opcode::LogicalShiftLeft32>(lagoon_assembler_t&, EmitContext& ctx, IR::Inst* inst);

template<>
void EmitIR<IR::Opcode::GetCarryFromOp>(lagoon_assembler_t&, EmitContext& ctx, IR::Inst* inst) {
    ASSERT(ctx.reg_alloc.IsValueLive(inst));
}

template<>
void EmitIR<IR::Opcode::GetNZFromOp>(lagoon_assembler_t& as, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Xvalue = ctx.reg_alloc.ReadX(args[0]);
    auto Xnz = ctx.reg_alloc.WriteX(inst);
    RegAlloc::Realize(Xvalue, Xnz);

    // Z flag (bit 30): set if value == 0
    la_sltui(&as, Xnz->index, Xvalue->index, 1);
    la_slli_d(&as, Xnz->index, Xnz->index, 30);
    // N flag (bit 31): set if value < 0 (signed)
    la_slt(&as, Xscratch0, Xvalue->index, LA_ZERO);
    la_slli_d(&as, Xscratch0, Xscratch0, 31);
    la_or(&as, Xnz->index, Xnz->index, Xscratch0);
}

EmittedBlockInfo EmitLoongArch64(lagoon_assembler_t& as, IR::Block block, const EmitConfig& emit_conf) {
    EmittedBlockInfo ebi;

    RegAlloc reg_alloc{as, std::vector<u32>(GPR_ORDER), std::vector<u32>(FPR_ORDER)};
    EmitContext ctx{block, reg_alloc, emit_conf, ebi};

    ebi.entry_point = reinterpret_cast<CodePtr>(as.cursor);

    for (auto iter = block.instructions.begin(); iter != block.instructions.end(); ++iter) {
        IR::Inst* inst = &*iter;

        switch (inst->GetOpcode()) {
#define OPCODE(name, type, ...)                            \
    case IR::Opcode::name:                                 \
        EmitIR<IR::Opcode::name>(as, ctx, inst);           \
        break;
#define A32OPC(name, type, ...)                                 \
    case IR::Opcode::A32##name:                                 \
        EmitIR<IR::Opcode::A32##name>(as, ctx, inst);           \
        break;
#define A64OPC(name, type, ...)                                 \
    case IR::Opcode::A64##name:                                 \
        EmitIR<IR::Opcode::A64##name>(as, ctx, inst);           \
        break;
#include "dynarmic/ir/opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
        default:
            UNREACHABLE();
        }

        reg_alloc.UpdateAllUses();
    }

    reg_alloc.UpdateAllUses();
    reg_alloc.AssertNoMoreUses();

    // TODO: Emit Terminal
    const auto term = block.GetTerminal();
    const IR::Term::LeafTerminal* leaft_term = std::get_if<IR::Term::LeafTerminal>(&term);
    const IR::Term::LinkBlock* link_block_term = std::get_if<IR::Term::LinkBlock>(leaft_term);
    ASSERT(link_block_term);
    la_load_immediate64(&as, Xscratch0, link_block_term->next.Value());
    la_st_w(&as, Xscratch0, Xstate, static_cast<int32_t>(offsetof(A32JitState, regs) + sizeof(u32) * 15));

    ebi.relocations.push_back(Relocation{
        reinterpret_cast<CodePtr>(as.cursor) - ebi.entry_point,
        LinkTarget::ReturnFromRunCode
    });
    la_nop(&as);

    ebi.size = reinterpret_cast<CodePtr>(as.cursor) - ebi.entry_point;
    return ebi;
}

}  // namespace Dynarmic::Backend::LoongArch64
