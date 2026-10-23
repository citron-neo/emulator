// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <oaknut/oaknut.hpp>

#include "dynarmic/backend/arm64/a32_jitstate.h"
#include "dynarmic/backend/arm64/abi.h"
#include "dynarmic/backend/arm64/emit_arm64.h"
#include "dynarmic/backend/arm64/emit_context.h"
#include "dynarmic/backend/arm64/reg_alloc.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

template<std::size_t bitsize, typename EmitFn>
static void EmitCRC(oaknut::CodeGenerator&, EmitContext& ctx, IR::Inst* inst, EmitFn emit_fn) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    auto Woutput = ctx.reg_alloc.WriteW(inst);
    auto Winput = ctx.reg_alloc.ReadW(args[0]);
    auto Rdata = ctx.reg_alloc.ReadReg<bitsize>(args[1]);
    RegAlloc::Realize(Woutput, Winput, Rdata);

    emit_fn(Woutput, Winput, Rdata);
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<32>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Wdata) { code.CRC32CB(Woutput, Winput, Wdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<32>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Wdata) { code.CRC32CH(Woutput, Winput, Wdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<32>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Wdata) { code.CRC32CW(Woutput, Winput, Wdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32Castagnoli64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<64>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Xdata) { code.CRC32CX(Woutput, Winput, Xdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32ISO8>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<32>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Wdata) { code.CRC32B(Woutput, Winput, Wdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32ISO16>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<32>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Wdata) { code.CRC32H(Woutput, Winput, Wdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32ISO32>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<32>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Wdata) { code.CRC32W(Woutput, Winput, Wdata); });
}

template<>
void EmitIR<IR::Opcode::CRC32ISO64>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    EmitCRC<64>(code, ctx, inst, [&](auto& Woutput, auto& Winput, auto& Xdata) { code.CRC32X(Woutput, Winput, Xdata); });
}

template<>
void EmitIR<IR::Opcode::AESDecryptSingleRound>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qoutput = ctx.reg_alloc.WriteQ(inst);
    auto Qinput = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qoutput, Qinput);

    code.MOVI(Qoutput->toD(), oaknut::RepImm{0});
    code.AESD(Qoutput->B16(), Qinput->B16());
}

template<>
void EmitIR<IR::Opcode::AESEncryptSingleRound>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qoutput = ctx.reg_alloc.WriteQ(inst);
    auto Qinput = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qoutput, Qinput);

    code.MOVI(Qoutput->toD(), oaknut::RepImm{0});
    code.AESE(Qoutput->B16(), Qinput->B16());
}

template<>
void EmitIR<IR::Opcode::AESInverseMixColumns>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qoutput = ctx.reg_alloc.WriteQ(inst);
    auto Qinput = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qoutput, Qinput);

    code.AESIMC(Qoutput->B16(), Qinput->B16());
}

template<>
void EmitIR<IR::Opcode::AESMixColumns>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qoutput = ctx.reg_alloc.WriteQ(inst);
    auto Qinput = ctx.reg_alloc.ReadQ(args[0]);
    RegAlloc::Realize(Qoutput, Qinput);

    code.AESMC(Qoutput->B16(), Qinput->B16());
}

template<>
void EmitIR<IR::Opcode::SM4AccessSubstitutionBox>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    (void)code;
    (void)ctx;
    (void)inst;
    UNREACHABLE();
}

template<>
void EmitIR<IR::Opcode::SHA256Hash>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    const bool part1 = args[3].GetImmediateU1();

    if (part1) {
        auto Qx = ctx.reg_alloc.ReadWriteQ(args[0], inst);
        auto Qy = ctx.reg_alloc.ReadQ(args[1]);
        auto Qz = ctx.reg_alloc.ReadQ(args[2]);
        RegAlloc::Realize(Qx, Qy, Qz);

        code.SHA256H(Qx, Qy, Qz->S4());
    } else {
        auto Qx = ctx.reg_alloc.ReadQ(args[0]);
        auto Qy = ctx.reg_alloc.ReadWriteQ(args[1], inst);
        auto Qz = ctx.reg_alloc.ReadQ(args[2]);
        RegAlloc::Realize(Qx, Qy, Qz);

        code.SHA256H2(Qy, Qx, Qz->S4());  // Yes x and y are swapped
    }
}

template<>
void EmitIR<IR::Opcode::SHA256MessageSchedule0>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qa = ctx.reg_alloc.ReadWriteQ(args[0], inst);
    auto Qb = ctx.reg_alloc.ReadQ(args[1]);
    RegAlloc::Realize(Qa, Qb);

    code.SHA256SU0(Qa->S4(), Qb->S4());
}

template<>
void EmitIR<IR::Opcode::SHA256MessageSchedule1>(oaknut::CodeGenerator& code, EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    auto Qa = ctx.reg_alloc.ReadWriteQ(args[0], inst);
    auto Qb = ctx.reg_alloc.ReadQ(args[1]);
    auto Qc = ctx.reg_alloc.ReadQ(args[2]);
    RegAlloc::Realize(Qa, Qb, Qc);

    code.SHA256SU1(Qa->S4(), Qb->S4(), Qc->S4());
}

}  // namespace Dynarmic::Backend::Arm64
