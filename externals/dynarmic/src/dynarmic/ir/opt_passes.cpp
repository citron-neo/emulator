// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <algorithm>
#include <cstdio>
#include <map>
#include <bit>

#include "dynarmic/common/container/unordered_map.h"
#include "boost/container/small_vector.hpp"
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "dynarmic/frontend/A64/a64_ir_emitter.h"
#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/translate/a64_translate.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/interface/optimization_flags.h"
#include "dynarmic/common/safe_ops.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/microinstruction.h"
#include "dynarmic/ir/opcodes.h"
#include "dynarmic/ir/opt_passes.h"
#include "dynarmic/ir/type.h"
#include "dynarmic/mcl/bit.hpp"

namespace Dynarmic::Optimization {

static void ConstantMemoryReads(IR::Block& block, A32::UserCallbacks* cb) {
    for (auto& inst : block.instructions) {
        switch (inst.GetOpcode()) {
        case IR::Opcode::A32ReadMemory8:
        case IR::Opcode::A64ReadMemory8: {
            if (inst.AreAllArgsImmediates()) {
                const u32 vaddr = inst.GetArg(1).GetU32();
                if (cb->IsReadOnlyMemory(vaddr)) {
                    const u8 value_from_memory = cb->MemoryRead8(vaddr);
                    inst.ReplaceUsesWith(IR::Value{value_from_memory});
                }
            }
            break;
        }
        case IR::Opcode::A32ReadMemory16:
        case IR::Opcode::A64ReadMemory16: {
            if (inst.AreAllArgsImmediates()) {
                const u32 vaddr = inst.GetArg(1).GetU32();
                if (cb->IsReadOnlyMemory(vaddr)) {
                    const u16 value_from_memory = cb->MemoryRead16(vaddr);
                    inst.ReplaceUsesWith(IR::Value{value_from_memory});
                }
            }
            break;
        }
        case IR::Opcode::A32ReadMemory32:
        case IR::Opcode::A64ReadMemory32: {
            if (inst.AreAllArgsImmediates()) {
                const u32 vaddr = inst.GetArg(1).GetU32();
                if (cb->IsReadOnlyMemory(vaddr)) {
                    const u32 value_from_memory = cb->MemoryRead32(vaddr);
                    inst.ReplaceUsesWith(IR::Value{value_from_memory});
                }
            }
            break;
        }
        case IR::Opcode::A32ReadMemory64:
        case IR::Opcode::A64ReadMemory64: {
            if (inst.AreAllArgsImmediates()) {
                const u32 vaddr = inst.GetArg(1).GetU32();
                if (cb->IsReadOnlyMemory(vaddr)) {
                    const u64 value_from_memory = cb->MemoryRead64(vaddr);
                    inst.ReplaceUsesWith(IR::Value{value_from_memory});
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

static void FlagsPass(IR::Block& block) {
    using Iterator = typename std::reverse_iterator<IR::Block::iterator>;

    struct FlagInfo {
        bool set_not_required = false;
        bool has_value_request = false;
        Iterator value_request = {};
    };
    struct ValuelessFlagInfo {
        bool set_not_required = false;
    };
    ValuelessFlagInfo nzcvq;
    ValuelessFlagInfo nzcv;
    ValuelessFlagInfo nz;
    FlagInfo c_flag;
    FlagInfo ge;

    auto do_set = [&](FlagInfo& info, IR::Value value, Iterator inst) {
        if (info.has_value_request) {
            info.value_request->ReplaceUsesWith(value);
        }
        info.has_value_request = false;

        if (info.set_not_required) {
            inst->Invalidate();
        }
        info.set_not_required = true;
    };

    auto do_set_valueless = [&](ValuelessFlagInfo& info, Iterator inst) {
        if (info.set_not_required) {
            inst->Invalidate();
        }
        info.set_not_required = true;
    };

    auto do_get = [](FlagInfo& info, Iterator inst) {
        if (info.has_value_request) {
            info.value_request->ReplaceUsesWith(IR::Value{&*inst});
        }
        info.has_value_request = true;
        info.value_request = inst;
    };

    A32::IREmitter ir{block, A32::LocationDescriptor{block.Location()}, {}};

    for (auto inst = block.instructions.rbegin(); inst != block.instructions.rend(); ++inst) {
        auto const opcode = inst->GetOpcode();
        switch (opcode) {
        case IR::Opcode::A32GetCFlag: {
            do_get(c_flag, inst);
            break;
        }
        case IR::Opcode::A32SetCpsrNZCV: {
            if (c_flag.has_value_request) {
                ir.SetInsertionPointBefore(inst.base());  // base is one ahead
                IR::U1 c = ir.GetCFlagFromNZCV(IR::NZCV{inst->GetArg(0)});
                c_flag.value_request->ReplaceUsesWith(c);
                c_flag.has_value_request = false;
                break;  // This case will be executed again because of the above
            }

            do_set_valueless(nzcv, inst);

            nz = {.set_not_required = true};
            c_flag = {.set_not_required = true};
            break;
        }
        case IR::Opcode::A32SetCpsrNZCVRaw: {
            if (c_flag.has_value_request) {
                nzcv.set_not_required = false;
            }

            do_set_valueless(nzcv, inst);

            nzcvq = {};
            nz = {.set_not_required = true};
            c_flag = {.set_not_required = true};
            break;
        }
        case IR::Opcode::A32SetCpsrNZCVQ: {
            if (c_flag.has_value_request) {
                nzcvq.set_not_required = false;
            }

            do_set_valueless(nzcvq, inst);

            nzcv = {.set_not_required = true};
            nz = {.set_not_required = true};
            c_flag = {.set_not_required = true};
            break;
        }
        case IR::Opcode::A32SetCpsrNZ: {
            do_set_valueless(nz, inst);

            nzcvq = {};
            nzcv = {};
            break;
        }
        case IR::Opcode::A32SetCpsrNZC: {
            if (c_flag.has_value_request) {
                c_flag.value_request->ReplaceUsesWith(inst->GetArg(1));
                c_flag.has_value_request = false;
            }

            if (!inst->GetArg(1).IsImmediate() && inst->GetArg(1).GetInstRecursive()->GetOpcode() == IR::Opcode::A32GetCFlag) {
                const auto nz_value = inst->GetArg(0);

                inst->Invalidate();

                ir.SetInsertionPointBefore(inst.base());
                ir.SetCpsrNZ(IR::NZCV{nz_value});

                nzcvq = {};
                nzcv = {};
                nz = {.set_not_required = true};
                break;
            }

            if (nz.set_not_required && c_flag.set_not_required) {
                inst->Invalidate();
            } else if (nz.set_not_required) {
                inst->SetArg(0, IR::Value::EmptyNZCVImmediateMarker());
            }
            nz.set_not_required = true;
            c_flag.set_not_required = true;

            nzcv = {};
            nzcvq = {};
            break;
        }
        case IR::Opcode::A32SetGEFlags: {
            do_set(ge, inst->GetArg(0), inst);
            break;
        }
        case IR::Opcode::A32GetGEFlags: {
            do_get(ge, inst);
            break;
        }
        case IR::Opcode::A32SetGEFlagsCompressed: {
            ge = {.set_not_required = true};
            break;
        }
        case IR::Opcode::A32OrQFlag: {
            break;
        }
        default: {
            if (ReadsFromCPSR(opcode) || WritesToCPSR(opcode)) {
                nzcvq = {};
                nzcv = {};
                nz = {};
                c_flag = {};
                ge = {};
            }
            break;
        }
        }
    }
}

static void RegisterPass(IR::Block& block) {
    using Iterator = IR::Block::iterator;

    struct RegInfo {
        IR::Value register_value;
        std::optional<Iterator> last_set_instruction;
    };
    std::array<RegInfo, 15> reg_info;

    const auto do_get = [](RegInfo& info, Iterator get_inst) {
        if (info.register_value.IsEmpty()) {
            info.register_value = IR::Value(&*get_inst);
            return;
        }
        get_inst->ReplaceUsesWith(info.register_value);
    };

    const auto do_set = [](RegInfo& info, IR::Value value, Iterator set_inst) {
        if (info.last_set_instruction) {
            (*info.last_set_instruction)->Invalidate();
        }
        info = {
            .register_value = value,
            .last_set_instruction = set_inst,
        };
    };

    enum class ExtValueType {
        Empty,
        Single,
        Double,
        VectorDouble,
        VectorQuad,
    };
    struct ExtRegInfo {
        ExtValueType value_type = {};
        IR::Value register_value;
        std::optional<Iterator> last_set_instruction;
    };
    std::array<ExtRegInfo, 64> ext_reg_info;

    const auto do_ext_get = [](ExtValueType type, std::initializer_list<std::reference_wrapper<ExtRegInfo>> infos, Iterator get_inst) {
        if (!std::all_of(infos.begin(), infos.end(), [type](const auto& info) { return info.get().value_type == type; })) {
            for (auto& info : infos) {
                info.get() = {
                    .value_type = type,
                    .register_value = IR::Value(&*get_inst),
                    .last_set_instruction = std::nullopt,
                };
            }
            return;
        }
        get_inst->ReplaceUsesWith(std::data(infos)[0].get().register_value);
    };

    const auto do_ext_set = [](ExtValueType type, std::initializer_list<std::reference_wrapper<ExtRegInfo>> infos, IR::Value value, Iterator set_inst) {
        if (std::all_of(infos.begin(), infos.end(), [type](const auto& info) { return info.get().value_type == type; })) {
            if (std::data(infos)[0].get().last_set_instruction) {
                (*std::data(infos)[0].get().last_set_instruction)->Invalidate();
            }
        }
        for (auto& info : infos) {
            info.get() = {
                .value_type = type,
                .register_value = value,
                .last_set_instruction = set_inst,
            };
        }
    };

    // Location and version don't matter here.
    A32::IREmitter ir{block, A32::LocationDescriptor{block.Location()}, {}};

    for (auto inst = block.instructions.begin(); inst != block.instructions.end(); ++inst) {
        auto const opcode = inst->GetOpcode();
        switch (opcode) {
        case IR::Opcode::A32GetRegister: {
            const A32::Reg reg = inst->GetArg(0).GetA32RegRef();
            ASSERT(reg != A32::Reg::PC);
            const size_t reg_index = size_t(reg);
            do_get(reg_info[reg_index], inst);
            break;
        }
        case IR::Opcode::A32SetRegister: {
            const A32::Reg reg = inst->GetArg(0).GetA32RegRef();
            if (reg == A32::Reg::PC) {
                break;
            }
            const auto reg_index = size_t(reg);
            do_set(reg_info[reg_index], inst->GetArg(1), inst);
            break;
        }
        case IR::Opcode::A32GetExtendedRegister32: {
            const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
            const size_t reg_index = A32::RegNumber(reg);
            do_ext_get(ExtValueType::Single, {ext_reg_info[reg_index]}, inst);
            break;
        }
        case IR::Opcode::A32SetExtendedRegister32: {
            const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
            const size_t reg_index = A32::RegNumber(reg);
            do_ext_set(ExtValueType::Single, {ext_reg_info[reg_index]}, inst->GetArg(1), inst);
            break;
        }
        case IR::Opcode::A32GetExtendedRegister64: {
            const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
            const size_t reg_index = A32::RegNumber(reg);
            do_ext_get(ExtValueType::Double,
                       {
                           ext_reg_info[reg_index * 2 + 0],
                           ext_reg_info[reg_index * 2 + 1],
                       },
                       inst);
            break;
        }
        case IR::Opcode::A32SetExtendedRegister64: {
            const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
            const size_t reg_index = A32::RegNumber(reg);
            do_ext_set(ExtValueType::Double,
                       {
                           ext_reg_info[reg_index * 2 + 0],
                           ext_reg_info[reg_index * 2 + 1],
                       },
                       inst->GetArg(1),
                       inst);
            break;
        }
        case IR::Opcode::A32GetVector: {
            const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
            const size_t reg_index = A32::RegNumber(reg);
            if (A32::IsDoubleExtReg(reg)) {
                do_ext_get(ExtValueType::VectorDouble,
                           {
                               ext_reg_info[reg_index * 2 + 0],
                               ext_reg_info[reg_index * 2 + 1],
                           },
                           inst);
            } else {
                DEBUG_ASSERT(A32::IsQuadExtReg(reg));
                do_ext_get(ExtValueType::VectorQuad,
                           {
                               ext_reg_info[reg_index * 4 + 0],
                               ext_reg_info[reg_index * 4 + 1],
                               ext_reg_info[reg_index * 4 + 2],
                               ext_reg_info[reg_index * 4 + 3],
                           },
                           inst);
            }
            break;
        }
        case IR::Opcode::A32SetVector: {
            const A32::ExtReg reg = inst->GetArg(0).GetA32ExtRegRef();
            const size_t reg_index = A32::RegNumber(reg);
            if (A32::IsDoubleExtReg(reg)) {
                ir.SetInsertionPointAfter(inst);
                const IR::U128 stored_value = ir.VectorZeroUpper(IR::U128{inst->GetArg(1)});
                do_ext_set(ExtValueType::VectorDouble,
                           {
                               ext_reg_info[reg_index * 2 + 0],
                               ext_reg_info[reg_index * 2 + 1],
                           },
                           stored_value,
                           inst);
            } else {
                DEBUG_ASSERT(A32::IsQuadExtReg(reg));
                do_ext_set(ExtValueType::VectorQuad,
                           {
                               ext_reg_info[reg_index * 4 + 0],
                               ext_reg_info[reg_index * 4 + 1],
                               ext_reg_info[reg_index * 4 + 2],
                               ext_reg_info[reg_index * 4 + 3],
                           },
                           inst->GetArg(1),
                           inst);
            }
            break;
        }
        default: {
            if (ReadsFromCoreRegister(opcode) || WritesToCoreRegister(opcode)) {
                reg_info = {};
                ext_reg_info = {};
            }
            break;
        }
        }
    }
}

struct A32GetSetEliminationOptions {
    bool convert_nzc_to_nz = false;
    bool convert_nz_to_nzc = false;
};

static void A32GetSetElimination(IR::Block& block, A32GetSetEliminationOptions) {
    FlagsPass(block);
    RegisterPass(block);
}

static void A64CallbackConfigPass(IR::Block& block, const A64::UserConfig& conf) {
    if (conf.hook_data_cache_operations) {
        return;
    }

    for (auto& inst : block.instructions) {
        if (inst.GetOpcode() != IR::Opcode::A64DataCacheOperationRaised) {
            continue;
        }

        const auto op = static_cast<A64::DataCacheOperation>(inst.GetArg(1).GetU64());
        if (op == A64::DataCacheOperation::ZeroByVA) {
            A64::IREmitter ir{block};
            ir.current_location = A64::LocationDescriptor{IR::LocationDescriptor{inst.GetArg(0).GetU64()}};
            ir.SetInsertionPointBefore(&inst);

            size_t bytes = 4 << static_cast<size_t>(conf.dczid_el0 & 0b1111);
            IR::U64 addr{inst.GetArg(2)};

            const IR::U128 zero_u128 = ir.ZeroExtendToQuad(ir.Imm64(0));
            while (bytes >= 16) {
                ir.WriteMemory128(addr, zero_u128, IR::AccType::DCZVA);
                addr = ir.Add(addr, ir.Imm64(16));
                bytes -= 16;
            }

            while (bytes >= 8) {
                ir.WriteMemory64(addr, ir.Imm64(0), IR::AccType::DCZVA);
                addr = ir.Add(addr, ir.Imm64(8));
                bytes -= 8;
            }

            while (bytes >= 4) {
                ir.WriteMemory32(addr, ir.Imm32(0), IR::AccType::DCZVA);
                addr = ir.Add(addr, ir.Imm64(4));
                bytes -= 4;
            }
        }
        inst.Invalidate();
    }
}

// Tiny helper to avoid the need to store based off the opcode
// bit size all over the place within folding functions.
static void ReplaceUsesWith(IR::Inst& inst, bool is_32_bit, u64 value) {
    if (is_32_bit) {
        inst.ReplaceUsesWith(IR::Value{u32(value)});
    } else {
        inst.ReplaceUsesWith(IR::Value{value});
    }
}

static void A64GetSetElimination(IR::Block& block) {
    using Iterator = IR::Block::iterator;

    enum class TrackingType {
        W,
        X,
        S,
        D,
        Q,
        SP,
        NZCV,
        NZCVRaw,
    };
    struct RegisterInfo {
        IR::Value register_value;
        TrackingType tracking_type;
        Iterator last_set_instruction;
        bool set_instruction_present = false;
    };
    std::array<RegisterInfo, 31> reg_info;
    std::array<RegisterInfo, 32> vec_info;
    RegisterInfo sp_info;
    RegisterInfo nzcv_info;

    const auto do_set = [&block](RegisterInfo& info, IR::Value value, Iterator set_inst, TrackingType tracking_type) {
        if (info.set_instruction_present) {
            info.last_set_instruction->Invalidate();
            block.Instructions().erase(info.last_set_instruction);
        }
        info.register_value = value;
        info.tracking_type = tracking_type;
        info.set_instruction_present = true;
        info.last_set_instruction = set_inst;
    };
    const auto do_get = [&block](RegisterInfo& info, Iterator get_inst, TrackingType tracking_type) {
        if (!info.register_value.IsEmpty() && info.tracking_type == tracking_type) {
            get_inst->ReplaceUsesWith(info.register_value);
        } else if (!info.register_value.IsEmpty()
            && tracking_type == TrackingType::W
            && info.tracking_type == TrackingType::X) {
            // A sequence like
            // SetX r1 -> GetW r1, is just reading off the lowest 32-bits of the register
            if (info.register_value.IsImmediate()) {
                ReplaceUsesWith(*get_inst, true, u32(info.register_value.GetImmediateAsU64()));
            } else {
                A64::IREmitter ir{block};
                ir.SetInsertionPointBefore(&*get_inst);
                get_inst->ReplaceUsesWith(ir.LeastSignificantWord(IR::U64{info.register_value}));
            }
        } else {
            info = {};
            info.register_value = IR::Value(&*get_inst);
            info.tracking_type = tracking_type;
        }
    };
    for (auto inst = block.instructions.begin(); inst != block.instructions.end(); ++inst) {
        auto const opcode = inst->GetOpcode();
        switch (opcode) {
        case IR::Opcode::A64GetW: {
            const size_t index = A64::RegNumber(inst->GetArg(0).GetA64RegRef());
            do_get(reg_info[index], inst, TrackingType::W);
            break;
        }
        case IR::Opcode::A64GetX: {
            const size_t index = A64::RegNumber(inst->GetArg(0).GetA64RegRef());
            do_get(reg_info[index], inst, TrackingType::X);
            break;
        }
        case IR::Opcode::A64GetS: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_get(vec_info[index], inst, TrackingType::S);
            break;
        }
        case IR::Opcode::A64GetD: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_get(vec_info[index], inst, TrackingType::D);
            break;
        }
        case IR::Opcode::A64GetQ: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_get(vec_info[index], inst, TrackingType::Q);
            break;
        }
        case IR::Opcode::A64GetSP: {
            do_get(sp_info, inst, TrackingType::SP);
            break;
        }
        case IR::Opcode::A64GetNZCVRaw: {
            do_get(nzcv_info, inst, TrackingType::NZCVRaw);
            break;
        }
        case IR::Opcode::A64SetW: {
            const size_t index = A64::RegNumber(inst->GetArg(0).GetA64RegRef());
            do_set(reg_info[index], inst->GetArg(1), inst, TrackingType::W);
            break;
        }
        case IR::Opcode::A64SetX: {
            const size_t index = A64::RegNumber(inst->GetArg(0).GetA64RegRef());
            do_set(reg_info[index], inst->GetArg(1), inst, TrackingType::X);
            break;
        }
        case IR::Opcode::A64SetS: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_set(vec_info[index], inst->GetArg(1), inst, TrackingType::S);
            break;
        }
        case IR::Opcode::A64SetD: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_set(vec_info[index], inst->GetArg(1), inst, TrackingType::D);
            break;
        }
        case IR::Opcode::A64SetQ: {
            const size_t index = A64::VecNumber(inst->GetArg(0).GetA64VecRef());
            do_set(vec_info[index], inst->GetArg(1), inst, TrackingType::Q);
            break;
        }
        case IR::Opcode::A64SetSP: {
            do_set(sp_info, inst->GetArg(0), inst, TrackingType::SP);
            break;
        }
        case IR::Opcode::A64SetNZCV: {
            do_set(nzcv_info, inst->GetArg(0), inst, TrackingType::NZCV);
            break;
        }
        case IR::Opcode::A64SetNZCVRaw: {
            do_set(nzcv_info, inst->GetArg(0), inst, TrackingType::NZCVRaw);
            break;
        }
        default: {
            if (ReadsFromCPSR(opcode) || WritesToCPSR(opcode)) {
                nzcv_info = {};
            }
            if (ReadsFromCoreRegister(opcode) || WritesToCoreRegister(opcode)) {
                reg_info = {};
                vec_info = {};
                sp_info = {};
            }
            break;
        }
        }
    }
}

using Op = Dynarmic::IR::Opcode;

static IR::Value Value(bool is_32_bit, u64 value) {
    return is_32_bit ? IR::Value{u32(value)} : IR::Value{value};
}

template<typename ImmFn>
static bool FoldCommutative(IR::Inst& inst, bool is_32_bit, ImmFn imm_fn) {
    const auto lhs = inst.GetArg(0);
    const auto rhs = inst.GetArg(1);

    const bool is_lhs_immediate = lhs.IsImmediate();
    const bool is_rhs_immediate = rhs.IsImmediate();

    if (is_lhs_immediate && is_rhs_immediate) {
        const u64 result = imm_fn(lhs.GetImmediateAsU64(), rhs.GetImmediateAsU64());
        ReplaceUsesWith(inst, is_32_bit, result);
        return false;
    }

    if (is_lhs_immediate && !is_rhs_immediate) {
        const IR::Inst* rhs_inst = rhs.GetInstRecursive();
        if (rhs_inst->GetOpcode() == inst.GetOpcode() && rhs_inst->GetArg(1).IsImmediate()) {
            const u64 combined = imm_fn(lhs.GetImmediateAsU64(), rhs_inst->GetArg(1).GetImmediateAsU64());
            inst.SetArg(0, rhs_inst->GetArg(0));
            inst.SetArg(1, Value(is_32_bit, combined));
        } else {
            // Normalize
            inst.SetArg(0, rhs);
            inst.SetArg(1, lhs);
        }
    }

    if (!is_lhs_immediate && is_rhs_immediate) {
        const IR::Inst* lhs_inst = lhs.GetInstRecursive();
        if (lhs_inst->GetOpcode() == inst.GetOpcode() && lhs_inst->GetArg(1).IsImmediate()) {
            const u64 combined = imm_fn(rhs.GetImmediateAsU64(), lhs_inst->GetArg(1).GetImmediateAsU64());
            inst.SetArg(0, lhs_inst->GetArg(0));
            inst.SetArg(1, Value(is_32_bit, combined));
        }
    }

    return true;
}

static void FoldAdd(IR::Inst& inst, bool is_32_bit) {
    const auto lhs = inst.GetArg(0);
    const auto rhs = inst.GetArg(1);
    const auto carry = inst.GetArg(2);

    if (lhs.IsImmediate() && !rhs.IsImmediate()) {
        // Normalize
        inst.SetArg(0, rhs);
        inst.SetArg(1, lhs);
        FoldAdd(inst, is_32_bit);
        return;
    }

    if (inst.HasAssociatedPseudoOperation()) {
        return;
    }

    if (!lhs.IsImmediate() && rhs.IsImmediate()) {
        const IR::Inst* lhs_inst = lhs.GetInstRecursive();
        if (lhs_inst->GetOpcode() == inst.GetOpcode() && lhs_inst->GetArg(1).IsImmediate() && lhs_inst->GetArg(2).IsImmediate()) {
            const u64 combined = rhs.GetImmediateAsU64() + lhs_inst->GetArg(1).GetImmediateAsU64() + lhs_inst->GetArg(2).GetU1();
            if (combined == 0) {
                inst.ReplaceUsesWith(lhs_inst->GetArg(0));
                return;
            }
            inst.SetArg(0, lhs_inst->GetArg(0));
            inst.SetArg(1, Value(is_32_bit, combined));
            return;
        }
        if (rhs.IsZero() && carry.IsZero()) {
            inst.ReplaceUsesWith(lhs);
            return;
        }
    }

    if (inst.AreAllArgsImmediates()) {
        const u64 result = lhs.GetImmediateAsU64() + rhs.GetImmediateAsU64() + carry.GetU1();
        ReplaceUsesWith(inst, is_32_bit, result);
        return;
    }
}

/// Folds AND operations based on the following:
///
/// 1. imm_x & imm_y -> result
/// 2. x & 0 -> 0
/// 3. 0 & y -> 0
/// 4. x & y -> y (where x has all bits set to 1)
/// 5. x & y -> x (where y has all bits set to 1)
///
static void FoldAND(IR::Inst& inst, bool is_32_bit) {
    if (FoldCommutative(inst, is_32_bit, [](u64 a, u64 b) { return a & b; })) {
        const auto rhs = inst.GetArg(1);
        if (rhs.IsZero()) {
            ReplaceUsesWith(inst, is_32_bit, 0);
        } else if (rhs.HasAllBitsSet()) {
            inst.ReplaceUsesWith(inst.GetArg(0));
        }
    }
}

/// Folds byte reversal opcodes based on the following:
///
/// 1. imm -> swap(imm)
///
static void FoldByteReverse(IR::Inst& inst, Op op) {
    const auto operand = inst.GetArg(0);

    if (!operand.IsImmediate()) {
        return;
    }

    if (op == Op::ByteReverseWord) {
        const u32 result = mcl::bit::swap_bytes_32(u32(operand.GetImmediateAsU64()));
        inst.ReplaceUsesWith(IR::Value{result});
    } else if (op == Op::ByteReverseHalf) {
        const u16 result = mcl::bit::swap_bytes_16(u16(operand.GetImmediateAsU64()));
        inst.ReplaceUsesWith(IR::Value{result});
    } else {
        const u64 result = mcl::bit::swap_bytes_64(operand.GetImmediateAsU64());
        inst.ReplaceUsesWith(IR::Value{result});
    }
}

/// Folds leading zero population count
///
/// 1. imm -> countl_zero(imm)
///
static void FoldCountLeadingZeros(IR::Inst& inst, bool is_32_bit) {
    const auto operand = inst.GetArg(0);
    if (operand.IsImmediate()) {
        if (is_32_bit) {
            const u32 result = std::countl_zero(u32(operand.GetImmediateAsU64()));
            inst.ReplaceUsesWith(IR::Value{result});
        } else {
            const u64 result = std::countl_zero(operand.GetImmediateAsU64());
            inst.ReplaceUsesWith(IR::Value{result});
        }
    }
}

/// Folds division operations based on the following:
///
/// 1. x / 0 -> 0 (NOTE: This is an ARM-specific behavior defined in the architecture reference manual)
/// 2a. 0x8000_0000 / 0xFFFF_FFFF -> 0x8000_0000 (NOTE: More ARM errata)
/// 2b. 0x8000_0000_0000_0000 / 0xFFFF_FFFF_FFFF_FFFF -> 0x8000_0000_0000_0000
/// 3. imm_x / imm_y -> result
/// 4. x / 1 -> x
///
static void FoldDivide(IR::Inst& inst, bool is_32_bit, bool is_signed) {
    const auto rhs = inst.GetArg(1);
    const auto lhs = inst.GetArg(0);
    if (lhs.IsZero() || rhs.IsZero()) {
        ReplaceUsesWith(inst, is_32_bit, u64(0));
   } else if (!is_32_bit && lhs.IsUnsignedImmediate(u64(1ULL << 63)) && rhs.IsUnsignedImmediate(u64(-1))) {
       ReplaceUsesWith(inst, is_32_bit, u64(1ULL << 63));
    } else if (is_32_bit && lhs.IsUnsignedImmediate(u32(1ULL << 31)) && rhs.IsUnsignedImmediate(u32(-1))) {
        ReplaceUsesWith(inst, is_32_bit, u64(1ULL << 31));
    } else if (lhs.IsImmediate() && rhs.IsImmediate()) {
        if (is_signed) {
            auto const dl = lhs.GetImmediateAsS64();
            auto const dr = rhs.GetImmediateAsS64();
            const s64 result = dl / dr;
            ReplaceUsesWith(inst, is_32_bit, u64(result));
        } else {
            auto const dl = lhs.GetImmediateAsU64();
            auto const dr = rhs.GetImmediateAsU64();
            const u64 result = dl / dr;
            ReplaceUsesWith(inst, is_32_bit, result);
        }
    } else if (rhs.IsUnsignedImmediate(1)) {
        inst.ReplaceUsesWith(IR::Value{lhs});
    }
}

// Folds EOR operations based on the following:
//
// 1. imm_x ^ imm_y -> result
// 2. x ^ 0 -> x
// 3. 0 ^ y -> y
//
static void FoldEOR(IR::Inst& inst, bool is_32_bit) {
    if (FoldCommutative(inst, is_32_bit, [](u64 a, u64 b) { return a ^ b; })) {
        const auto rhs = inst.GetArg(1);
        if (rhs.IsZero()) {
            inst.ReplaceUsesWith(inst.GetArg(0));
        }
    }
}

static void FoldLeastSignificantByte(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const auto operand = inst.GetArg(0);
    inst.ReplaceUsesWith(IR::Value{static_cast<u8>(operand.GetImmediateAsU64())});
}

static void FoldLeastSignificantHalf(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const auto operand = inst.GetArg(0);
    inst.ReplaceUsesWith(IR::Value{static_cast<u16>(operand.GetImmediateAsU64())});
}

static void FoldLeastSignificantWord(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const auto operand = inst.GetArg(0);
    inst.ReplaceUsesWith(IR::Value{static_cast<u32>(operand.GetImmediateAsU64())});
}

static void FoldMostSignificantBit(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const auto operand = inst.GetArg(0);
    inst.ReplaceUsesWith(IR::Value{(operand.GetImmediateAsU64() >> 31) != 0});
}

static void FoldMostSignificantWord(IR::Inst& inst) {
    IR::Inst* carry_inst = inst.GetAssociatedPseudoOperation(Op::GetCarryFromOp);

    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const auto operand = inst.GetArg(0);
    if (carry_inst) {
        carry_inst->ReplaceUsesWith(IR::Value{mcl::bit::get_bit<31>(operand.GetImmediateAsU64())});
    }
    inst.ReplaceUsesWith(IR::Value{static_cast<u32>(operand.GetImmediateAsU64() >> 32)});
}

// Folds multiplication operations based on the following:
//
// 1. imm_x * imm_y -> result
// 2. x * 0 -> 0
// 3. 0 * y -> 0
// 4. x * 1 -> x
// 5. 1 * y -> y
//
static void FoldMultiply(IR::Inst& inst, bool is_32_bit) {
    if (FoldCommutative(inst, is_32_bit, [](u64 a, u64 b) { return a * b; })) {
        const auto rhs = inst.GetArg(1);
        if (rhs.IsZero()) {
            ReplaceUsesWith(inst, is_32_bit, 0);
        } else if (rhs.IsUnsignedImmediate(1)) {
            inst.ReplaceUsesWith(inst.GetArg(0));
        }
    }
}

// Folds NOT operations if the contained value is an immediate.
static void FoldNOT(IR::Inst& inst, bool is_32_bit) {
    const auto operand = inst.GetArg(0);

    if (!operand.IsImmediate()) {
        return;
    }

    const u64 result = ~operand.GetImmediateAsU64();
    ReplaceUsesWith(inst, is_32_bit, result);
}

// Folds OR operations based on the following:
//
// 1. imm_x | imm_y -> result
// 2. x | 0 -> x
// 3. 0 | y -> y
//
static void FoldOR(IR::Inst& inst, bool is_32_bit) {
    if (FoldCommutative(inst, is_32_bit, [](u64 a, u64 b) { return a | b; })) {
        const auto rhs = inst.GetArg(1);
        if (rhs.IsZero()) {
            inst.ReplaceUsesWith(inst.GetArg(0));
        }
    }
}

static bool FoldShifts(IR::Inst& inst) {
    IR::Inst* carry_inst = inst.GetAssociatedPseudoOperation(Op::GetCarryFromOp);

    // The 32-bit variants can contain 3 arguments, while the
    // 64-bit variants only contain 2.
    if (inst.NumArgs() == 3 && !carry_inst) {
        inst.SetArg(2, IR::Value(false));
    }

    const auto shift_amount = inst.GetArg(1);

    if (shift_amount.IsZero()) {
        if (carry_inst) {
            carry_inst->ReplaceUsesWith(inst.GetArg(2));
        }
        inst.ReplaceUsesWith(inst.GetArg(0));
        return false;
    }

    if (inst.NumArgs() == 3 && shift_amount.IsImmediate() && !shift_amount.IsZero()) {
        inst.SetArg(2, IR::Value(false));
    }

    if (!inst.AreAllArgsImmediates() || carry_inst) {
        return false;
    }

    return true;
}

static void FoldSignExtendXToWord(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const s64 value = inst.GetArg(0).GetImmediateAsS64();
    inst.ReplaceUsesWith(IR::Value{static_cast<u32>(value)});
}

static void FoldSignExtendXToLong(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const s64 value = inst.GetArg(0).GetImmediateAsS64();
    inst.ReplaceUsesWith(IR::Value{static_cast<u64>(value)});
}

static void FoldSub(IR::Inst& inst, bool is_32_bit) {
    if (!inst.AreAllArgsImmediates() || inst.HasAssociatedPseudoOperation()) {
        return;
    }

    const auto lhs = inst.GetArg(0);
    const auto rhs = inst.GetArg(1);
    const auto carry = inst.GetArg(2);

    const u64 result = lhs.GetImmediateAsU64() + (~rhs.GetImmediateAsU64()) + carry.GetU1();
    ReplaceUsesWith(inst, is_32_bit, result);
}

static void FoldZeroExtendXToWord(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const u64 value = inst.GetArg(0).GetImmediateAsU64();
    inst.ReplaceUsesWith(IR::Value{static_cast<u32>(value)});
}

static void FoldZeroExtendXToLong(IR::Inst& inst) {
    if (!inst.AreAllArgsImmediates()) {
        return;
    }

    const u64 value = inst.GetArg(0).GetImmediateAsU64();
    inst.ReplaceUsesWith(IR::Value{value});
}

static void ConstantPropagation(IR::Block& block) {
    for (auto& inst : block.instructions) {
        auto const opcode = inst.GetOpcode();
        // skip NZCV so we dont end up discarding side effects :)
        // TODO(lizzie): hey stupid maybe fix the A64 codegen for folded constants AND
        // redirect the mfer properly?!??! just saying :)
        if (IR::MayGetNZCVFromOp(opcode) && inst.GetAssociatedPseudoOperation(IR::Opcode::GetNZCVFromOp))
            continue;
        switch (opcode) {
        case Op::LeastSignificantWord:
            FoldLeastSignificantWord(inst);
            break;
        case Op::MostSignificantWord:
            FoldMostSignificantWord(inst);
            break;
        case Op::LeastSignificantHalf:
            FoldLeastSignificantHalf(inst);
            break;
        case Op::LeastSignificantByte:
            FoldLeastSignificantByte(inst);
            break;
        case Op::MostSignificantBit:
            FoldMostSignificantBit(inst);
            break;
        case Op::IsZero32:
            if (inst.AreAllArgsImmediates()) {
                inst.ReplaceUsesWith(IR::Value{inst.GetArg(0).GetU32() == 0});
            }
            break;
        case Op::IsZero64:
            if (inst.AreAllArgsImmediates()) {
                inst.ReplaceUsesWith(IR::Value{inst.GetArg(0).GetU64() == 0});
            }
            break;
        case Op::LogicalShiftLeft32:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, true, Safe::LogicalShiftLeft<u32>(inst.GetArg(0).GetU32(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::LogicalShiftLeft64:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, false, Safe::LogicalShiftLeft<u64>(inst.GetArg(0).GetU64(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::LogicalShiftRight32:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, true, Safe::LogicalShiftRight<u32>(inst.GetArg(0).GetU32(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::LogicalShiftRight64:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, false, Safe::LogicalShiftRight<u64>(inst.GetArg(0).GetU64(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::ArithmeticShiftRight32:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, true, Safe::ArithmeticShiftRight<u32>(inst.GetArg(0).GetU32(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::ArithmeticShiftRight64:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, false, Safe::ArithmeticShiftRight<u64>(inst.GetArg(0).GetU64(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::BitRotateRight32:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, true, std::rotr<u32>(inst.GetArg(0).GetU32(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::BitRotateRight64:
            if (FoldShifts(inst)) {
                ReplaceUsesWith(inst, false, std::rotr<u64>(inst.GetArg(0).GetU64(), inst.GetArg(1).GetU8()));
            }
            break;
        case Op::LogicalShiftLeftMasked32:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, true, inst.GetArg(0).GetU32() << (inst.GetArg(1).GetU32() & 0x1f));
            }
            break;
        case Op::LogicalShiftLeftMasked64:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, false, inst.GetArg(0).GetU64() << (inst.GetArg(1).GetU64() & 0x3f));
            }
            break;
        case Op::LogicalShiftRightMasked32:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, true, inst.GetArg(0).GetU32() >> (inst.GetArg(1).GetU32() & 0x1f));
            }
            break;
        case Op::LogicalShiftRightMasked64:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, false, inst.GetArg(0).GetU64() >> (inst.GetArg(1).GetU64() & 0x3f));
            }
            break;
        case Op::ArithmeticShiftRightMasked32:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, true, s32(inst.GetArg(0).GetU32()) >> (inst.GetArg(1).GetU32() & 0x1f));
            }
            break;
        case Op::ArithmeticShiftRightMasked64:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, false, s64(inst.GetArg(0).GetU64()) >> (inst.GetArg(1).GetU64() & 0x3f));
            }
            break;
        case Op::RotateRightMasked32:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, true, std::rotr<u32>(inst.GetArg(0).GetU32(), inst.GetArg(1).GetU32()));
            }
            break;
        case Op::RotateRightMasked64:
            if (inst.AreAllArgsImmediates()) {
                ReplaceUsesWith(inst, false, std::rotr<u64>(inst.GetArg(0).GetU64(), inst.GetArg(1).GetU64()));
            }
            break;
        case Op::Add32:
        case Op::Add64:
            FoldAdd(inst, opcode == Op::Add32);
            break;
        case Op::Sub32:
        case Op::Sub64:
            FoldSub(inst, opcode == Op::Sub32);
            break;
        case Op::Mul32:
        case Op::Mul64:
            FoldMultiply(inst, opcode == Op::Mul32);
            break;
        case Op::SignedDiv32:
        case Op::SignedDiv64:
            FoldDivide(inst, opcode == Op::SignedDiv32, true);
            break;
        case Op::UnsignedDiv32:
        case Op::UnsignedDiv64:
            FoldDivide(inst, opcode == Op::UnsignedDiv32, false);
            break;
        case Op::And32:
        case Op::And64:
            FoldAND(inst, opcode == Op::And32);
            break;
        case Op::Eor32:
        case Op::Eor64:
            FoldEOR(inst, opcode == Op::Eor32);
            break;
        case Op::Or32:
        case Op::Or64:
            FoldOR(inst, opcode == Op::Or32);
            break;
        case Op::Not32:
        case Op::Not64:
            FoldNOT(inst, opcode == Op::Not32);
            break;
        case Op::SignExtendByteToWord:
        case Op::SignExtendHalfToWord:
            FoldSignExtendXToWord(inst);
            break;
        case Op::SignExtendByteToLong:
        case Op::SignExtendHalfToLong:
        case Op::SignExtendWordToLong:
            FoldSignExtendXToLong(inst);
            break;
        case Op::ZeroExtendByteToWord:
        case Op::ZeroExtendHalfToWord:
            FoldZeroExtendXToWord(inst);
            break;
        case Op::ZeroExtendByteToLong:
        case Op::ZeroExtendHalfToLong:
        case Op::ZeroExtendWordToLong:
            FoldZeroExtendXToLong(inst);
            break;
        case Op::ByteReverseWord:
        case Op::ByteReverseHalf:
        case Op::ByteReverseDual:
            FoldByteReverse(inst, opcode);
            break;
        case Op::CountLeadingZeros32:
        case Op::CountLeadingZeros64:
            FoldCountLeadingZeros(inst, opcode == Op::CountLeadingZeros32);
            break;
        default:
            break;
        }
    }
}

static void DeadCodeElimination(IR::Block& block) {
    // We iterate over the instructions in reverse order.
    // This is because removing an instruction reduces the number of uses for earlier instructions.
    for (auto it = block.instructions.rbegin(); it != block.instructions.rend(); ++it)
        if (!it->HasUses() && !MayHaveSideEffects(it->GetOpcode()))
            it->Invalidate();
}

static void IdentityRemovalPass(IR::Block& block) {
    boost::container::small_vector<IR::Inst*, 128> to_invalidate;
    for (auto it = block.instructions.begin(); it != block.instructions.end();) {
        auto const num_args = it->NumArgs();
        for (size_t i = 0; i < num_args; ++i)
            if (IR::Value arg = it->GetArg(i); arg.IsIdentity()) {
                do arg = arg.GetInst()->GetArg(0); while (arg.IsIdentity());
                it->SetArg(i, arg);
            }
        if (it->GetOpcode() == IR::Opcode::Identity || it->GetOpcode() == IR::Opcode::Void) {
            to_invalidate.push_back(&*it);
            it = block.Instructions().erase(it);
        } else {
            ++it;
        }
    }
    for (IR::Inst* const inst : to_invalidate)
        inst->Invalidate();
}

static void NamingPass(IR::Block& block) {
    u32 name = 1;
    for (auto& inst : block.instructions)
        inst.SetName(name++);
}

static void PolyfillSHA256MessageSchedule0(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 x = (IR::U128)inst.GetArg(0);
    const IR::U128 y = (IR::U128)inst.GetArg(1);

    const IR::U128 t = ir.VectorExtract(x, y, 32);

    IR::U128 result = ir.ZeroVector();
    for (size_t i = 0; i < 4; i++) {
        const IR::U32 modified_element = [&] {
            const IR::U32 element = ir.VectorGetElement(32, t, i);
            const IR::U32 tmp1 = ir.RotateRight(element, ir.Imm8(7));
            const IR::U32 tmp2 = ir.RotateRight(element, ir.Imm8(18));
            const IR::U32 tmp3 = ir.LogicalShiftRight(element, ir.Imm8(3));

            return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
        }();

        result = ir.VectorSetElement(32, result, i, modified_element);
    }
    result = ir.VectorAdd(32, result, x);

    inst.ReplaceUsesWith(result);
}

static void PolyfillSHA256MessageSchedule1(IR::IREmitter& ir, IR::Inst& inst) {
    const IR::U128 x = (IR::U128)inst.GetArg(0);
    const IR::U128 y = (IR::U128)inst.GetArg(1);
    const IR::U128 z = (IR::U128)inst.GetArg(2);

    const IR::U128 T0 = ir.VectorExtract(y, z, 32);

    const IR::U128 lower_half = [&] {
        const IR::U128 T = ir.VectorRotateWholeVectorRight(z, 64);
        const IR::U128 tmp1 = ir.VectorRotateRight(32, T, 17);
        const IR::U128 tmp2 = ir.VectorRotateRight(32, T, 19);
        const IR::U128 tmp3 = ir.VectorLogicalShiftRight(32, T, 10);
        const IR::U128 tmp4 = ir.VectorEor(tmp1, ir.VectorEor(tmp2, tmp3));
        const IR::U128 tmp5 = ir.VectorAdd(32, tmp4, ir.VectorAdd(32, x, T0));
        return ir.VectorZeroUpper(tmp5);
    }();

    const IR::U64 upper_half = [&] {
        const IR::U128 tmp1 = ir.VectorRotateRight(32, lower_half, 17);
        const IR::U128 tmp2 = ir.VectorRotateRight(32, lower_half, 19);
        const IR::U128 tmp3 = ir.VectorLogicalShiftRight(32, lower_half, 10);
        const IR::U128 tmp4 = ir.VectorEor(tmp1, ir.VectorEor(tmp2, tmp3));

        // Shuffle the top two 32-bit elements downwards [3, 2, 1, 0] -> [1, 0, 3, 2]
        const IR::U128 shuffled_d = ir.VectorRotateWholeVectorRight(x, 64);
        const IR::U128 shuffled_T0 = ir.VectorRotateWholeVectorRight(T0, 64);

        const IR::U128 tmp5 = ir.VectorAdd(32, tmp4, ir.VectorAdd(32, shuffled_d, shuffled_T0));
        return ir.VectorGetElement(64, tmp5, 0);
    }();

    const IR::U128 result = ir.VectorSetElement(64, lower_half, 1, upper_half);

    inst.ReplaceUsesWith(result);
}

static IR::U32 SHAchoose(IR::IREmitter& ir, IR::U32 x, IR::U32 y, IR::U32 z) {
    return ir.Eor(ir.And(ir.Eor(y, z), x), z);
}

static IR::U32 SHAmajority(IR::IREmitter& ir, IR::U32 x, IR::U32 y, IR::U32 z) {
    return ir.Or(ir.And(x, y), ir.And(ir.Or(x, y), z));
}

static IR::U32 SHAhashSIGMA0(IR::IREmitter& ir, IR::U32 x) {
    const IR::U32 tmp1 = ir.RotateRight(x, ir.Imm8(2));
    const IR::U32 tmp2 = ir.RotateRight(x, ir.Imm8(13));
    const IR::U32 tmp3 = ir.RotateRight(x, ir.Imm8(22));

    return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
}

static IR::U32 SHAhashSIGMA1(IR::IREmitter& ir, IR::U32 x) {
    const IR::U32 tmp1 = ir.RotateRight(x, ir.Imm8(6));
    const IR::U32 tmp2 = ir.RotateRight(x, ir.Imm8(11));
    const IR::U32 tmp3 = ir.RotateRight(x, ir.Imm8(25));

    return ir.Eor(tmp1, ir.Eor(tmp2, tmp3));
}

static void PolyfillSHA256Hash(IR::IREmitter& ir, IR::Inst& inst) {
    IR::U128 x = (IR::U128)inst.GetArg(0);
    IR::U128 y = (IR::U128)inst.GetArg(1);
    const IR::U128 w = (IR::U128)inst.GetArg(2);
    const bool part1 = inst.GetArg(3).GetU1();

    for (size_t i = 0; i < 4; i++) {
        const IR::U32 low_x = ir.VectorGetElement(32, x, 0);
        const IR::U32 after_low_x = ir.VectorGetElement(32, x, 1);
        const IR::U32 before_high_x = ir.VectorGetElement(32, x, 2);
        const IR::U32 high_x = ir.VectorGetElement(32, x, 3);

        const IR::U32 low_y = ir.VectorGetElement(32, y, 0);
        const IR::U32 after_low_y = ir.VectorGetElement(32, y, 1);
        const IR::U32 before_high_y = ir.VectorGetElement(32, y, 2);
        const IR::U32 high_y = ir.VectorGetElement(32, y, 3);

        const IR::U32 choice = SHAchoose(ir, low_y, after_low_y, before_high_y);
        const IR::U32 majority = SHAmajority(ir, low_x, after_low_x, before_high_x);

        const IR::U32 t = [&] {
            const IR::U32 w_element = ir.VectorGetElement(32, w, i);
            const IR::U32 sig = SHAhashSIGMA1(ir, low_y);

            return ir.Add(high_y, ir.Add(sig, ir.Add(choice, w_element)));
        }();

        const IR::U32 new_low_x = ir.Add(t, ir.Add(SHAhashSIGMA0(ir, low_x), majority));
        const IR::U32 new_low_y = ir.Add(t, high_x);

        // Shuffle all words left by 1 element: [3, 2, 1, 0] -> [2, 1, 0, 3]
        const IR::U128 shuffled_x = ir.VectorRotateWholeVectorRight(x, 96);
        const IR::U128 shuffled_y = ir.VectorRotateWholeVectorRight(y, 96);

        x = ir.VectorSetElement(32, shuffled_x, 0, new_low_x);
        y = ir.VectorSetElement(32, shuffled_y, 0, new_low_y);
    }

    inst.ReplaceUsesWith(part1 ? x : y);
}

template<size_t esize, bool is_signed>
static void PolyfillVectorMultiplyWiden(IR::IREmitter& ir, IR::Inst& inst) {
    IR::U128 n = (IR::U128)inst.GetArg(0);
    IR::U128 m = (IR::U128)inst.GetArg(1);

    const IR::U128 wide_n = is_signed ? ir.VectorSignExtend(esize, n) : ir.VectorZeroExtend(esize, n);
    const IR::U128 wide_m = is_signed ? ir.VectorSignExtend(esize, m) : ir.VectorZeroExtend(esize, m);

    const IR::U128 result = ir.VectorMultiply(esize * 2, wide_n, wide_m);

    inst.ReplaceUsesWith(result);
}

static void PolyfillPass(IR::Block& block, const PolyfillOptions& polyfill) {
    if (polyfill == PolyfillOptions{}) {
        return;
    }

    IR::IREmitter ir{block};

    for (auto& inst : block.instructions) {
        ir.SetInsertionPointBefore(&inst);

        switch (inst.GetOpcode()) {
        case IR::Opcode::SHA256MessageSchedule0:
            if (polyfill.sha256) {
                PolyfillSHA256MessageSchedule0(ir, inst);
            }
            break;
        case IR::Opcode::SHA256MessageSchedule1:
            if (polyfill.sha256) {
                PolyfillSHA256MessageSchedule1(ir, inst);
            }
            break;
        case IR::Opcode::SHA256Hash:
            if (polyfill.sha256) {
                PolyfillSHA256Hash(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplySignedWiden8:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<8, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplySignedWiden16:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<16, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplySignedWiden32:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<32, true>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplyUnsignedWiden8:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<8, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplyUnsignedWiden16:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<16, false>(ir, inst);
            }
            break;
        case IR::Opcode::VectorMultiplyUnsignedWiden32:
            if (polyfill.vector_multiply_widen) {
                PolyfillVectorMultiplyWiden<32, false>(ir, inst);
            }
            break;
        default:
            break;
        }
    }
}

static void VerificationPass(const IR::Block& block) {
    for (auto const& inst : block.instructions) {
        for (size_t i = 0; i < inst.NumArgs(); i++) {
            const IR::Type t1 = inst.GetArg(i).GetType();
            const IR::Type t2 = IR::GetArgTypeOf(inst.GetOpcode(), i);
            ASSERT(IR::AreTypesCompatible(t1, t2));
        }
    }
    ::Common::unordered_map<IR::Inst*, size_t> actual_uses;
    for (auto const& inst : block.instructions) {
        for (size_t i = 0; i < inst.NumArgs(); i++)
            if (IR::Value const arg = inst.GetArg(i); !arg.IsImmediate())
                actual_uses[arg.GetInst()]++;
    }
    for (auto const& pair : actual_uses)
        ASSERT(pair.first->UseCount() == pair.second);
}

void Optimize(IR::Block& block, const A32::UserConfig& conf, const Optimization::PolyfillOptions& polyfill_options) {
    Optimization::PolyfillPass(block, polyfill_options);
    Optimization::NamingPass(block);
    if (conf.HasOptimization(OptimizationFlag::GetSetElimination)) {
        Optimization::A32GetSetElimination(block, {.convert_nzc_to_nz = true});
        Optimization::DeadCodeElimination(block);
    }
    if (conf.HasOptimization(OptimizationFlag::ConstProp)) {
        Optimization::ConstantMemoryReads(block, conf.callbacks);
        Optimization::ConstantPropagation(block);
        Optimization::DeadCodeElimination(block);
    }
    Optimization::IdentityRemovalPass(block);
    if (!conf.HasOptimization(OptimizationFlag::DisableVerification)) {
        Optimization::VerificationPass(block);
    }
}

void Optimize(IR::Block& block, const A64::UserConfig& conf, const Optimization::PolyfillOptions& polyfill_options) {
    Optimization::PolyfillPass(block, polyfill_options);
    Optimization::A64CallbackConfigPass(block, conf);
    Optimization::NamingPass(block);
    if (conf.HasOptimization(OptimizationFlag::GetSetElimination) && !conf.check_halt_on_memory_access) {
        Optimization::A64GetSetElimination(block);
        Optimization::DeadCodeElimination(block);
    }
    if (conf.HasOptimization(OptimizationFlag::ConstProp)) {
        Optimization::ConstantPropagation(block);
        Optimization::DeadCodeElimination(block);
    }
    Optimization::IdentityRemovalPass(block);
    if (!conf.HasOptimization(OptimizationFlag::DisableVerification)) {
        Optimization::VerificationPass(block);
    }
}

}  // namespace Dynarmic::Optimization
