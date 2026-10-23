// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/x64/emit_x64.h"

#include <iterator>

#include "dynarmic/common/assert.h"
#include <boost/variant/detail/apply_visitor_binary.hpp>
#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"
#include "dynarmic/common/container/unordered_map.h"
#include "dynarmic/common/container/unordered_set.h"

#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/nzcv_util.h"
#include "dynarmic/backend/x64/perf_map.h"
#include "dynarmic/backend/x64/stack_layout.h"
#include "dynarmic/backend/x64/verbose_debugging_output.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"

// TODO: Have ARM flags in host flags and not have them use up GPR registers unless necessary.
// TODO: Actually implement that proper instruction selector you've always wanted to sweetheart.

namespace Dynarmic::Backend::X64 {

using namespace Xbyak::util;

EmitContext::EmitContext(RegAlloc& reg_alloc, IR::Block& block, boost::container::stable_vector<Xbyak::Label>& shared_labels)
    : reg_alloc(reg_alloc)
    , block(block)
    , shared_labels(shared_labels)
{}

EmitContext::~EmitContext() = default;

EmitX64::EmitX64(BlockOfCode& code)
        : code(code) {
    exception_handler.Register(code);
}

EmitX64::~EmitX64() = default;

std::optional<EmitX64::BlockDescriptor> EmitX64::GetBasicBlock(IR::LocationDescriptor descriptor) const {
    const auto iter = block_descriptors.find(descriptor);
    if (iter == block_descriptors.end()) {
        return std::nullopt;
    }
    return iter->second;
}

void EmitX64::EmitInvalid(EmitContext&, IR::Inst* inst) {
    UNREACHABLE();
}

void EmitX64::EmitVoid(EmitContext&, IR::Inst*) {
}

void EmitX64::EmitIdentity(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    if (!args[0].IsImmediate()) {
        ctx.reg_alloc.DefineValue(code, inst, args[0]);
    }
}

void EmitX64::EmitBreakpoint(EmitContext&, IR::Inst*) {
    code.int3();
}

constexpr bool IsWithin2G(uintptr_t ref, uintptr_t target) {
    const u64 distance = target - (ref + 5);
    return !(distance >= 0x8000'0000ULL && distance <= ~0x8000'0000ULL);
}

void EmitX64::EmitCallHostFunction(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ctx.reg_alloc.HostCall(code, nullptr, args[1], args[2], args[3]);
    auto target = args[0].GetImmediateU64();
    if (IsWithin2G(uintptr_t(code.getCurr()), target)) {
        auto const f = std::bit_cast<void(*)(void)>(target);
        code.call(f);
    } else {
        code.mov(rax, target);
        code.call(rax);
    }
}

void EmitX64::PushRSBHelper(Xbyak::Reg64 loc_desc_reg, Xbyak::Reg64 index_reg, IR::LocationDescriptor target) {
    using namespace Xbyak::util;

    const auto iter = block_descriptors.find(target);
    CodePtr target_code_ptr = iter != block_descriptors.end()
                                ? iter->second.entrypoint
                                : code.GetReturnFromRunCodeAddress();

    code.mov(index_reg.cvt32(), dword[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_rsb_ptr]);
    code.mov(loc_desc_reg, target.Value());
    patch_information[target].mov_rcx.push_back(code.getCurr());
    EmitPatchMovRcx(target_code_ptr);
    code.mov(qword[code.ABI_JIT_PTR + index_reg * 8 + code.GetJitStateInfo().offsetof_rsb_location_descriptors], loc_desc_reg);
    code.mov(qword[code.ABI_JIT_PTR + index_reg * 8 + code.GetJitStateInfo().offsetof_rsb_codeptrs], rcx);
    // Byte size hack
    DEBUG_ASSERT(code.GetJitStateInfo().rsb_ptr_mask <= 0xFF);
    code.add(index_reg.cvt32(), 1); //flags trashed, 1 single byte, haswell doesn't care
    code.and_(index_reg.cvt32(), u32(code.GetJitStateInfo().rsb_ptr_mask)); //trashes flags
    // Results ready and sort by least needed: give OOO some break
    code.mov(dword[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_rsb_ptr], index_reg.cvt32());
}

#ifndef NDEBUG
void EmitX64::EmitVerboseDebuggingOutput(RegAlloc& reg_alloc) {
    code.lea(rsp, ptr[rsp - sizeof(RegisterData)]);
    code.stmxcsr(dword[rsp + offsetof(RegisterData, mxcsr)]);
    for (int i = 0; i < 16; i++) {
        if (rsp.getIdx() == i) {
            continue;
        }
        code.mov(qword[rsp + offsetof(RegisterData, gprs) + sizeof(u64) * i], Xbyak::Reg64{i});
    }
    for (int i = 0; i < 16; i++) {
        code.movaps(xword[rsp + offsetof(RegisterData, xmms) + 2 * sizeof(u64) * i], Xbyak::Xmm{i});
    }
    code.lea(rax, ptr[rsp + sizeof(RegisterData) + offsetof(StackLayout, spill)]);
    code.mov(qword[rsp + offsetof(RegisterData, spill)], rax);

    reg_alloc.EmitVerboseDebuggingOutput(code);

    for (int i = 0; i < 16; i++) {
        if (rsp.getIdx() == i) {
            continue;
        }
        code.mov(Xbyak::Reg64{i}, qword[rsp + offsetof(RegisterData, gprs) + sizeof(u64) * i]);
    }
    for (int i = 0; i < 16; i++) {
        code.movaps(Xbyak::Xmm{i}, xword[rsp + offsetof(RegisterData, xmms) + 2 * sizeof(u64) * i]);
    }
    code.ldmxcsr(dword[rsp + offsetof(RegisterData, mxcsr)]);
    code.add(rsp, sizeof(RegisterData));
}
#endif

void EmitX64::EmitPushRSB(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);
    ASSERT(args[0].IsImmediate());
    const u64 unique_hash_of_target = args[0].GetImmediateU64();

    ctx.reg_alloc.ScratchGpr(code, HostLoc::RCX);
    const Xbyak::Reg64 loc_desc_reg = ctx.reg_alloc.ScratchGpr(code);
    const Xbyak::Reg64 index_reg = ctx.reg_alloc.ScratchGpr(code);

    PushRSBHelper(loc_desc_reg, index_reg, IR::LocationDescriptor{unique_hash_of_target});
}

void EmitX64::EmitGetCarryFromOp(EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.RegisterPseudoOperation(inst);
}

void EmitX64::EmitGetOverflowFromOp(EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.RegisterPseudoOperation(inst);
}

void EmitX64::EmitGetGEFromOp(EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.RegisterPseudoOperation(inst);
}

void EmitX64::EmitGetUpperFromOp(EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.RegisterPseudoOperation(inst);
}

void EmitX64::EmitGetLowerFromOp(EmitContext& ctx, IR::Inst* inst) {
    ctx.reg_alloc.RegisterPseudoOperation(inst);
}

void EmitX64::EmitGetNZFromOp(EmitContext& ctx, IR::Inst* inst) {
    if (ctx.reg_alloc.IsValueLive(inst)) {
        ctx.reg_alloc.RegisterPseudoOperation(inst);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const int bitsize = [&] {
        switch (args[0].GetType()) {
        case IR::Type::U8:
            return 8;
        case IR::Type::U16:
            return 16;
        case IR::Type::U32:
            return 32;
        case IR::Type::U64:
            return 64;
        default:
            UNREACHABLE();
        }
    }();

    const Xbyak::Reg64 nz = ctx.reg_alloc.ScratchGpr(code, HostLoc::RAX);
    const Xbyak::Reg value = ctx.reg_alloc.UseGpr(code, args[0]).changeBit(bitsize);
    code.test(value, value);
    code.lahf();
    code.movzx(eax, ah);
    ctx.reg_alloc.DefineValue(code, inst, nz);
}

void EmitX64::EmitGetNZCVFromOp(EmitContext& ctx, IR::Inst* inst) {
    if (ctx.reg_alloc.IsValueLive(inst)) {
        ctx.reg_alloc.RegisterPseudoOperation(inst);
        return;
    }

    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    const int bitsize = [&] {
        switch (args[0].GetType()) {
        case IR::Type::U8:
            return 8;
        case IR::Type::U16:
            return 16;
        case IR::Type::U32:
            return 32;
        case IR::Type::U64:
            return 64;
        default:
            UNREACHABLE();
        }
    }();

    const Xbyak::Reg64 nzcv = ctx.reg_alloc.ScratchGpr(code, HostLoc::RAX);
    const Xbyak::Reg value = ctx.reg_alloc.UseGpr(code, args[0]).changeBit(bitsize);
    code.test(value, value);
    code.lahf();
    code.xor_(al, al);
    ctx.reg_alloc.DefineValue(code, inst, nzcv);
}

void EmitX64::EmitGetCFlagFromNZCV(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (args[0].IsImmediate()) {
        const Xbyak::Reg32 result = ctx.reg_alloc.ScratchGpr(code).cvt32();
        const u32 value = (args[0].GetImmediateU32() >> 8) & 1;
        code.mov(result, value);
        ctx.reg_alloc.DefineValue(code, inst, result);
    } else {
        const Xbyak::Reg32 result = ctx.reg_alloc.UseScratchGpr(code, args[0]).cvt32();
        code.shr(result, 8);
        code.and_(result, 1);
        ctx.reg_alloc.DefineValue(code, inst, result);
    }
}

void EmitX64::EmitNZCVFromPackedFlags(EmitContext& ctx, IR::Inst* inst) {
    auto args = ctx.reg_alloc.GetArgumentInfo(inst);

    if (args[0].IsImmediate()) {
        const Xbyak::Reg32 nzcv = ctx.reg_alloc.ScratchGpr(code).cvt32();
        u32 value = 0;
        value |= mcl::bit::get_bit<31>(args[0].GetImmediateU32()) ? (1 << 15) : 0;
        value |= mcl::bit::get_bit<30>(args[0].GetImmediateU32()) ? (1 << 14) : 0;
        value |= mcl::bit::get_bit<29>(args[0].GetImmediateU32()) ? (1 << 8) : 0;
        value |= mcl::bit::get_bit<28>(args[0].GetImmediateU32()) ? (1 << 0) : 0;
        code.mov(nzcv, value);
        ctx.reg_alloc.DefineValue(code, inst, nzcv);
    } else if (code.HasHostFeature(HostFeature::FastBMI2)) {
        const Xbyak::Reg32 nzcv = ctx.reg_alloc.UseScratchGpr(code, args[0]).cvt32();
        const Xbyak::Reg32 tmp = ctx.reg_alloc.ScratchGpr(code).cvt32();

        code.shr(nzcv, 28);
        code.mov(tmp, NZCV::x64_mask);
        code.pdep(nzcv, nzcv, tmp);

        ctx.reg_alloc.DefineValue(code, inst, nzcv);
    } else {
        const Xbyak::Reg32 nzcv = ctx.reg_alloc.UseScratchGpr(code, args[0]).cvt32();

        code.shr(nzcv, 28);
        code.imul(nzcv, nzcv, NZCV::to_x64_multiplier);
        code.and_(nzcv, NZCV::x64_mask);
        ctx.reg_alloc.DefineValue(code, inst, nzcv);
    }
}

void EmitX64::EmitAddCycles(size_t cycles) {
    ASSERT(cycles < (std::numeric_limits<s32>::max)());
    code.sub(qword[rsp + ABI_SHADOW_SPACE + offsetof(StackLayout, cycles_remaining)], static_cast<u32>(cycles));
}

Xbyak::Label EmitX64::EmitCond(IR::Cond cond) {
    Xbyak::Label pass;

    code.mov(eax, dword[code.ABI_JIT_PTR + code.GetJitStateInfo().offsetof_cpsr_nzcv]);

    code.LoadRequiredFlagsForCondFromRax(cond);

    switch (cond) {
    case IR::Cond::EQ:
        code.jz(pass);
        break;
    case IR::Cond::NE:
        code.jnz(pass);
        break;
    case IR::Cond::CS:
        code.jc(pass);
        break;
    case IR::Cond::CC:
        code.jnc(pass);
        break;
    case IR::Cond::MI:
        code.js(pass);
        break;
    case IR::Cond::PL:
        code.jns(pass);
        break;
    case IR::Cond::VS:
        code.jo(pass);
        break;
    case IR::Cond::VC:
        code.jno(pass);
        break;
    case IR::Cond::HI:
        code.ja(pass);
        break;
    case IR::Cond::LS:
        code.jna(pass);
        break;
    case IR::Cond::GE:
        code.jge(pass);
        break;
    case IR::Cond::LT:
        code.jl(pass);
        break;
    case IR::Cond::GT:
        code.jg(pass);
        break;
    case IR::Cond::LE:
        code.jle(pass);
        break;
    default:
        UNREACHABLE();
    }
    return pass;
}

EmitX64::BlockDescriptor EmitX64::RegisterBlock(const IR::LocationDescriptor& descriptor, CodePtr entrypoint, size_t size) {
    PerfMapRegister(entrypoint, code.getCurr(), LocationDescriptorToFriendlyName(descriptor));
    Patch(descriptor, entrypoint);

    BlockDescriptor block_desc{entrypoint, size};
    block_descriptors.insert({IR::LocationDescriptor{descriptor.Value()}, block_desc});
    return block_desc;
}

void EmitX64::Patch(const IR::LocationDescriptor& target_desc, CodePtr target_code_ptr) {
    const CodePtr save_code_ptr = code.getCurr();
    const PatchInformation& patch_info = patch_information[target_desc];

    for (CodePtr location : patch_info.jg) {
        code.SetCodePtr(location);
        EmitPatchJg(target_desc, target_code_ptr);
    }

    for (CodePtr location : patch_info.jz) {
        code.SetCodePtr(location);
        EmitPatchJz(target_desc, target_code_ptr);
    }

    for (CodePtr location : patch_info.jmp) {
        code.SetCodePtr(location);
        EmitPatchJmp(target_desc, target_code_ptr);
    }

    for (CodePtr location : patch_info.mov_rcx) {
        code.SetCodePtr(location);
        EmitPatchMovRcx(target_code_ptr);
    }

    code.SetCodePtr(save_code_ptr);
}

void EmitX64::Unpatch(const IR::LocationDescriptor& target_desc) {
    if (patch_information.count(target_desc)) {
        Patch(target_desc, nullptr);
    }
}

void EmitX64::ClearCache() {
    block_descriptors.clear();
    patch_information.clear();

    PerfMapClear();
}

void EmitX64::InvalidateBasicBlocks(const ::Common::unordered_set<IR::LocationDescriptor>& locations) {
    code.EnableWriting();
    for (const auto& descriptor : locations) {
        if (auto const it = block_descriptors.find(descriptor); it != block_descriptors.end()) {
            Unpatch(descriptor);
            block_descriptors.erase(it);
        }
    }
    code.DisableWriting();
}

}  // namespace Dynarmic::Backend::X64
