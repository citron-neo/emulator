// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class VoteOp : u64 {
    ALL,
    ANY,
    EQ,
};

[[nodiscard]] IR::U1 VoteOperation(IR::IREmitter& ir, const IR::U1& pred, VoteOp vote_op) {
    switch (vote_op) {
    case VoteOp::ALL:
        return ir.VoteAll(pred);
    case VoteOp::ANY:
        return ir.VoteAny(pred);
    case VoteOp::EQ:
        return ir.VoteEqual(pred);
    default:
        throw NotImplementedException("Invalid VOTE op {}", vote_op);
    }
}

void Vote(TranslatorVisitor& v, u64 insn) {
    union {
        u64 insn;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<39, 3, IR::Pred> pred_a;
        BitField<42, 1, u64> neg_pred_a;
        BitField<45, 3, IR::Pred> pred_b;
        BitField<48, 2, VoteOp> vote_op;
    } const vote{insn};

    const IR::U1 vote_pred{v.ir.GetPred(vote.pred_a, vote.neg_pred_a != 0)};
    v.ir.SetPred(vote.pred_b, VoteOperation(v.ir, vote_pred, vote.vote_op));
    v.X(vote.dest_reg, v.ir.SubgroupBallot(vote_pred));
}
} // Anonymous namespace

void TranslatorVisitor::VOTE(u64 insn) {
    Vote(*this, insn);
}

void TranslatorVisitor::VOTE_vtg(u64 insn) {
    // VOTE instruction in vertex/tessellation/geometry shaders
    // These stages process vertices independently without warp/subgroup synchronization
    // In these stages, vote operations are essentially no-ops or return conservative values
    union {
        u64 insn;
        BitField<0, 8, IR::Reg> dest_reg;
        BitField<39, 3, IR::Pred> pred_a;
        BitField<42, 1, u64> neg_pred_a;
        BitField<45, 3, IR::Pred> pred_b;
    } const vote{insn};

    // In VTG shaders, each thread is independent, so:
    // - "All" votes return the predicate itself (all of one thread = itself)
    // - Ballot returns 1 if predicate is true, 0 otherwise (only one thread)
    const IR::U1 vote_pred{ir.GetPred(vote.pred_a, vote.neg_pred_a != 0)};
    ir.SetPred(vote.pred_b, vote_pred);
    // Ballot for single thread: 1 if true, 0 if false
    X(vote.dest_reg, IR::U32{ir.Select(vote_pred, ir.Imm32(1), ir.Imm32(0))});
}

} // namespace Shader::Maxwell
