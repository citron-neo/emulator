// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <optional>

#include <boost/container/flat_set.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include "common/alignment.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/breadth_first_search.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/value.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace Shader::Optimization {
namespace {
/// Address in constant buffers to the storage buffer descriptor
struct StorageBufferAddr {
    auto operator<=>(const StorageBufferAddr&) const noexcept = default;

    u32 index;
    u32 offset;
};

/// Block iterator to a global memory instruction and the storage buffer it uses
struct StorageInst {
    StorageBufferAddr storage_buffer;
    IR::Inst* inst;
    IR::Block* block;
};

/// Bias towards a certain range of constant buffers when looking for storage buffers
struct Bias {
    u32 index;
    u32 offset_begin;
    u32 offset_end;
    u32 alignment;
};

using boost::container::flat_set;
using boost::container::small_vector;
using StorageBufferSet =
    flat_set<StorageBufferAddr, std::less<StorageBufferAddr>, small_vector<StorageBufferAddr, 16>>;
using StorageInstVector = small_vector<StorageInst, 24>;
using StorageWritesSet =
    flat_set<StorageBufferAddr, std::less<StorageBufferAddr>, small_vector<StorageBufferAddr, 16>>;

struct StorageInfo {
    StorageBufferSet set;
    StorageInstVector to_replace;
    StorageWritesSet writes;
};

/// Returns true when the instruction is a global memory instruction
bool IsGlobalMemory(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::LoadGlobalS8:
    case IR::Opcode::LoadGlobalU8:
    case IR::Opcode::LoadGlobalS16:
    case IR::Opcode::LoadGlobalU16:
    case IR::Opcode::LoadGlobal32:
    case IR::Opcode::LoadGlobal64:
    case IR::Opcode::LoadGlobal128:
    case IR::Opcode::WriteGlobalS8:
    case IR::Opcode::WriteGlobalU8:
    case IR::Opcode::WriteGlobalS16:
    case IR::Opcode::WriteGlobalU16:
    case IR::Opcode::WriteGlobal32:
    case IR::Opcode::WriteGlobal64:
    case IR::Opcode::WriteGlobal128:
    case IR::Opcode::GlobalAtomicIAdd32:
    case IR::Opcode::GlobalAtomicSMin32:
    case IR::Opcode::GlobalAtomicUMin32:
    case IR::Opcode::GlobalAtomicSMax32:
    case IR::Opcode::GlobalAtomicUMax32:
    case IR::Opcode::GlobalAtomicInc32:
    case IR::Opcode::GlobalAtomicDec32:
    case IR::Opcode::GlobalAtomicAnd32:
    case IR::Opcode::GlobalAtomicOr32:
    case IR::Opcode::GlobalAtomicXor32:
    case IR::Opcode::GlobalAtomicExchange32:
    case IR::Opcode::GlobalAtomicIAdd64:
    case IR::Opcode::GlobalAtomicSMin64:
    case IR::Opcode::GlobalAtomicUMin64:
    case IR::Opcode::GlobalAtomicSMax64:
    case IR::Opcode::GlobalAtomicUMax64:
    case IR::Opcode::GlobalAtomicAnd64:
    case IR::Opcode::GlobalAtomicOr64:
    case IR::Opcode::GlobalAtomicXor64:
    case IR::Opcode::GlobalAtomicExchange64:
    case IR::Opcode::GlobalAtomicIAdd32x2:
    case IR::Opcode::GlobalAtomicSMin32x2:
    case IR::Opcode::GlobalAtomicUMin32x2:
    case IR::Opcode::GlobalAtomicSMax32x2:
    case IR::Opcode::GlobalAtomicUMax32x2:
    case IR::Opcode::GlobalAtomicAnd32x2:
    case IR::Opcode::GlobalAtomicOr32x2:
    case IR::Opcode::GlobalAtomicXor32x2:
    case IR::Opcode::GlobalAtomicExchange32x2:
    case IR::Opcode::GlobalAtomicAddF32:
    case IR::Opcode::GlobalAtomicAddF16x2:
    case IR::Opcode::GlobalAtomicAddF32x2:
    case IR::Opcode::GlobalAtomicMinF16x2:
    case IR::Opcode::GlobalAtomicMinF32x2:
    case IR::Opcode::GlobalAtomicMaxF16x2:
    case IR::Opcode::GlobalAtomicMaxF32x2:
        return true;
    default:
        return false;
    }
}

/// Returns true when the instruction is a global memory instruction
bool IsGlobalMemoryWrite(const IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::WriteGlobalS8:
    case IR::Opcode::WriteGlobalU8:
    case IR::Opcode::WriteGlobalS16:
    case IR::Opcode::WriteGlobalU16:
    case IR::Opcode::WriteGlobal32:
    case IR::Opcode::WriteGlobal64:
    case IR::Opcode::WriteGlobal128:
    case IR::Opcode::GlobalAtomicIAdd32:
    case IR::Opcode::GlobalAtomicSMin32:
    case IR::Opcode::GlobalAtomicUMin32:
    case IR::Opcode::GlobalAtomicSMax32:
    case IR::Opcode::GlobalAtomicUMax32:
    case IR::Opcode::GlobalAtomicInc32:
    case IR::Opcode::GlobalAtomicDec32:
    case IR::Opcode::GlobalAtomicAnd32:
    case IR::Opcode::GlobalAtomicOr32:
    case IR::Opcode::GlobalAtomicXor32:
    case IR::Opcode::GlobalAtomicExchange32:
    case IR::Opcode::GlobalAtomicIAdd64:
    case IR::Opcode::GlobalAtomicSMin64:
    case IR::Opcode::GlobalAtomicUMin64:
    case IR::Opcode::GlobalAtomicSMax64:
    case IR::Opcode::GlobalAtomicUMax64:
    case IR::Opcode::GlobalAtomicAnd64:
    case IR::Opcode::GlobalAtomicOr64:
    case IR::Opcode::GlobalAtomicXor64:
    case IR::Opcode::GlobalAtomicExchange64:
    case IR::Opcode::GlobalAtomicIAdd32x2:
    case IR::Opcode::GlobalAtomicSMin32x2:
    case IR::Opcode::GlobalAtomicUMin32x2:
    case IR::Opcode::GlobalAtomicSMax32x2:
    case IR::Opcode::GlobalAtomicUMax32x2:
    case IR::Opcode::GlobalAtomicAnd32x2:
    case IR::Opcode::GlobalAtomicOr32x2:
    case IR::Opcode::GlobalAtomicXor32x2:
    case IR::Opcode::GlobalAtomicExchange32x2:
    case IR::Opcode::GlobalAtomicAddF32:
    case IR::Opcode::GlobalAtomicAddF16x2:
    case IR::Opcode::GlobalAtomicAddF32x2:
    case IR::Opcode::GlobalAtomicMinF16x2:
    case IR::Opcode::GlobalAtomicMinF32x2:
    case IR::Opcode::GlobalAtomicMaxF16x2:
    case IR::Opcode::GlobalAtomicMaxF32x2:
        return true;
    default:
        return false;
    }
}

/// Converts a global memory opcode to its storage buffer equivalent
IR::Opcode GlobalToStorage(IR::Opcode opcode) {
    switch (opcode) {
    case IR::Opcode::LoadGlobalS8:
        return IR::Opcode::LoadStorageS8;
    case IR::Opcode::LoadGlobalU8:
        return IR::Opcode::LoadStorageU8;
    case IR::Opcode::LoadGlobalS16:
        return IR::Opcode::LoadStorageS16;
    case IR::Opcode::LoadGlobalU16:
        return IR::Opcode::LoadStorageU16;
    case IR::Opcode::LoadGlobal32:
        return IR::Opcode::LoadStorage32;
    case IR::Opcode::LoadGlobal64:
        return IR::Opcode::LoadStorage64;
    case IR::Opcode::LoadGlobal128:
        return IR::Opcode::LoadStorage128;
    case IR::Opcode::WriteGlobalS8:
        return IR::Opcode::WriteStorageS8;
    case IR::Opcode::WriteGlobalU8:
        return IR::Opcode::WriteStorageU8;
    case IR::Opcode::WriteGlobalS16:
        return IR::Opcode::WriteStorageS16;
    case IR::Opcode::WriteGlobalU16:
        return IR::Opcode::WriteStorageU16;
    case IR::Opcode::WriteGlobal32:
        return IR::Opcode::WriteStorage32;
    case IR::Opcode::WriteGlobal64:
        return IR::Opcode::WriteStorage64;
    case IR::Opcode::WriteGlobal128:
        return IR::Opcode::WriteStorage128;
    case IR::Opcode::GlobalAtomicIAdd32:
        return IR::Opcode::StorageAtomicIAdd32;
    case IR::Opcode::GlobalAtomicSMin32:
        return IR::Opcode::StorageAtomicSMin32;
    case IR::Opcode::GlobalAtomicUMin32:
        return IR::Opcode::StorageAtomicUMin32;
    case IR::Opcode::GlobalAtomicSMax32:
        return IR::Opcode::StorageAtomicSMax32;
    case IR::Opcode::GlobalAtomicUMax32:
        return IR::Opcode::StorageAtomicUMax32;
    case IR::Opcode::GlobalAtomicInc32:
        return IR::Opcode::StorageAtomicInc32;
    case IR::Opcode::GlobalAtomicDec32:
        return IR::Opcode::StorageAtomicDec32;
    case IR::Opcode::GlobalAtomicAnd32:
        return IR::Opcode::StorageAtomicAnd32;
    case IR::Opcode::GlobalAtomicOr32:
        return IR::Opcode::StorageAtomicOr32;
    case IR::Opcode::GlobalAtomicXor32:
        return IR::Opcode::StorageAtomicXor32;
    case IR::Opcode::GlobalAtomicExchange32:
        return IR::Opcode::StorageAtomicExchange32;
    case IR::Opcode::GlobalAtomicIAdd64:
        return IR::Opcode::StorageAtomicIAdd64;
    case IR::Opcode::GlobalAtomicSMin64:
        return IR::Opcode::StorageAtomicSMin64;
    case IR::Opcode::GlobalAtomicUMin64:
        return IR::Opcode::StorageAtomicUMin64;
    case IR::Opcode::GlobalAtomicSMax64:
        return IR::Opcode::StorageAtomicSMax64;
    case IR::Opcode::GlobalAtomicUMax64:
        return IR::Opcode::StorageAtomicUMax64;
    case IR::Opcode::GlobalAtomicAnd64:
        return IR::Opcode::StorageAtomicAnd64;
    case IR::Opcode::GlobalAtomicOr64:
        return IR::Opcode::StorageAtomicOr64;
    case IR::Opcode::GlobalAtomicXor64:
        return IR::Opcode::StorageAtomicXor64;
    case IR::Opcode::GlobalAtomicExchange64:
        return IR::Opcode::StorageAtomicExchange64;
    case IR::Opcode::GlobalAtomicIAdd32x2:
        return IR::Opcode::StorageAtomicIAdd32x2;
    case IR::Opcode::GlobalAtomicSMin32x2:
        return IR::Opcode::StorageAtomicSMin32x2;
    case IR::Opcode::GlobalAtomicUMin32x2:
        return IR::Opcode::StorageAtomicUMin32x2;
    case IR::Opcode::GlobalAtomicSMax32x2:
        return IR::Opcode::StorageAtomicSMax32x2;
    case IR::Opcode::GlobalAtomicUMax32x2:
        return IR::Opcode::StorageAtomicUMax32x2;
    case IR::Opcode::GlobalAtomicAnd32x2:
        return IR::Opcode::StorageAtomicAnd32x2;
    case IR::Opcode::GlobalAtomicOr32x2:
        return IR::Opcode::StorageAtomicOr32x2;
    case IR::Opcode::GlobalAtomicXor32x2:
        return IR::Opcode::StorageAtomicXor32x2;
    case IR::Opcode::GlobalAtomicExchange32x2:
        return IR::Opcode::StorageAtomicExchange32x2;
    case IR::Opcode::GlobalAtomicAddF32:
        return IR::Opcode::StorageAtomicAddF32;
    case IR::Opcode::GlobalAtomicAddF16x2:
        return IR::Opcode::StorageAtomicAddF16x2;
    case IR::Opcode::GlobalAtomicMinF16x2:
        return IR::Opcode::StorageAtomicMinF16x2;
    case IR::Opcode::GlobalAtomicMaxF16x2:
        return IR::Opcode::StorageAtomicMaxF16x2;
    case IR::Opcode::GlobalAtomicAddF32x2:
        return IR::Opcode::StorageAtomicAddF32x2;
    case IR::Opcode::GlobalAtomicMinF32x2:
        return IR::Opcode::StorageAtomicMinF32x2;
    case IR::Opcode::GlobalAtomicMaxF32x2:
        return IR::Opcode::StorageAtomicMaxF32x2;
    default:
        throw InvalidArgument("Invalid global memory opcode {}", opcode);
    }
}

/// Returns true when a storage buffer address satisfies a bias
bool MeetsBias(const StorageBufferAddr& storage_buffer, const Bias& bias) noexcept {
    return storage_buffer.index == bias.index && storage_buffer.offset >= bias.offset_begin &&
           storage_buffer.offset < bias.offset_end;
}

struct LowAddrInfo {
    IR::U32 value;
    s32 imm_offset;
};

/// Tries to track the first 32-bits of a global memory instruction
std::optional<LowAddrInfo> TrackLowAddress(IR::Inst* inst) {
    // The first argument is the low level GPU pointer to the global memory instruction
    const IR::Value addr{inst->Arg(0)};
    if (addr.IsImmediate()) {
        // Not much we can do if it's an immediate
        return std::nullopt;
    }
    // This address is expected to either be a PackUint2x32, a IAdd64, or a CompositeConstructU32x2
    IR::Inst* addr_inst{inst->ArgInst(0)};
    s32 imm_offset{0};
    if (addr_inst->GetOpcode() == IR::Opcode::IAdd64) {
        // If it's an IAdd64, get the immediate offset it is applying and grab the address
        // instruction. This expects for the instruction to be canonicalized having the address on
        // the first argument and the immediate offset on the second one.
        const IR::U64 imm_offset_value{addr_inst->Arg(1)};
        if (!imm_offset_value.IsImmediate()) {
            return std::nullopt;
        }
        imm_offset = static_cast<s32>(static_cast<s64>(imm_offset_value.U64()));
        const IR::U64 iadd_addr{addr_inst->Arg(0)};
        if (iadd_addr.IsImmediate()) {
            return std::nullopt;
        }
        addr_inst = addr_inst->ArgInst(0);
    }
    // With IAdd64 handled, now PackUint2x32 is expected
    if (addr_inst->GetOpcode() == IR::Opcode::PackUint2x32) {
        // PackUint2x32 is expected to be generated from a vector
        const IR::Value vector{addr_inst->Arg(0)};
        if (vector.IsImmediate()) {
            return std::nullopt;
        }
        addr_inst = addr_inst->ArgInst(0);
    }
    // The vector is expected to be a CompositeConstructU32x2
    if (addr_inst->GetOpcode() != IR::Opcode::CompositeConstructU32x2) {
        return std::nullopt;
    }
    // Grab the first argument from the CompositeConstructU32x2, this is the low address.
    return LowAddrInfo{
        .value{IR::U32{addr_inst->Arg(0)}},
        .imm_offset = imm_offset,
    };
}

/// Tries to track the storage buffer address used by a global memory instruction
std::optional<StorageBufferAddr> Track(const IR::InstStorage& storage, const IR::Value& value,
                                       const Bias* bias) {
    const auto pred{[bias](const IR::Inst* inst) -> std::optional<StorageBufferAddr> {
        if (inst->GetOpcode() != IR::Opcode::GetCbufU32 &&
            inst->GetOpcode() != IR::Opcode::GetCbufU32x2) {
            return std::nullopt;
        }
        const IR::Value index{inst->Arg(0)};
        const IR::Value offset{inst->Arg(1)};
        if (!index.IsImmediate()) {
            // Definitely not a storage buffer if it's read from a
            // non-immediate index
            return std::nullopt;
        }
        if (!offset.IsImmediate()) {
            // TODO: Support SSBO arrays
            return std::nullopt;
        }
        const StorageBufferAddr storage_buffer{
            .index = index.U32(),
            .offset = offset.U32(),
        };
        const u32 alignment{bias ? bias->alignment : 8U};
        if (!Common::IsAligned(storage_buffer.offset, alignment)) {
            // The SSBO pointer has to be aligned
            return std::nullopt;
        }
        if (bias && !MeetsBias(storage_buffer, *bias)) {
            // We have to blacklist some addresses in case we wrongly
            // point to them
            return std::nullopt;
        }
        return storage_buffer;
    }};
    return BreadthFirstSearch(storage, value, pred);
}

/// Tracks the storage buffer a global memory low address is based on
std::optional<StorageBufferAddr> TrackStorageBuffer(const IR::InstStorage& storage,
                                                    const IR::U32& low_addr) {
    // NVN puts storage buffers in a specific range, we have to bias towards these addresses to
    // avoid getting false positives. Additional biases cover games (e.g. Zelda TOTK) that use
    // storage buffers at index 6 or extended offset ranges.
    static constexpr Bias nvn_bias{
        .index = 0,
        .offset_begin = 0x110,
        .offset_end = 0x810,
        .alignment = 16,
    };
    static constexpr Bias extended_index_bias{
        .index = 6,
        .offset_begin = 0,
        .offset_end = 0x800,
        .alignment = 16,
    };
    std::optional<StorageBufferAddr> storage_buffer{Track(storage, low_addr, &nvn_bias)};
    if (!storage_buffer) {
        storage_buffer = Track(storage, low_addr, &extended_index_bias);
    }
    if (!storage_buffer) {
        storage_buffer = Track(storage, low_addr, nullptr);
        if (!storage_buffer) {
            LOG_DEBUG(Shader, "Storage buffer failed to track, using global memory fallbacks");
            return std::nullopt;
        }
        LOG_DEBUG(Shader, "Storage buffer tracked without bias, index {} offset {}",
                  storage_buffer->index, storage_buffer->offset);
    }
    return storage_buffer;
}

/// Collects the storage buffer used by a global memory instruction and the instruction itself
void CollectStorageBuffers(IR::Block& block, IR::Inst& inst, StorageInfo& info) {
    // Track the low address of the instruction
    const std::optional<LowAddrInfo> low_addr_info{TrackLowAddress(&inst)};
    if (!low_addr_info) {
        return;
    }
    const std::optional<StorageBufferAddr> storage_buffer{
        TrackStorageBuffer(inst.Storage(), low_addr_info->value)};
    if (!storage_buffer) {
        return;
    }
    // Collect storage buffer and the instruction
    if (IsGlobalMemoryWrite(inst)) {
        info.writes.insert(*storage_buffer);
    }
    info.set.insert(*storage_buffer);
    info.to_replace.push_back(StorageInst{
        .storage_buffer{*storage_buffer},
        .inst = &inst,
        .block = &block,
    });
}

/// Returns the offset in indices (not bytes) for an equivalent storage instruction
IR::U32 StorageOffset(IR::Block& block, IR::Inst& inst, StorageBufferAddr buffer, u32 alignment) {
    IR::IREmitter ir{block, block.IteratorTo(inst)};
    IR::U32 offset;
    if (const std::optional<LowAddrInfo> low_addr{TrackLowAddress(&inst)}) {
        offset = low_addr->value;
        if (low_addr->imm_offset != 0) {
            offset = ir.IAdd(offset, ir.Imm32(low_addr->imm_offset));
        }
    } else {
        offset = ir.UConvert(32, IR::U64{inst.Arg(0)});
    }
    // Subtract the least significant 32 bits from the guest offset. The result is the storage
    // buffer offset in bytes.
    IR::U32 low_cbuf{ir.GetCbuf(ir.Imm32(buffer.index), ir.Imm32(buffer.offset))};

    // Align the offset base to match the host alignment requirements
    low_cbuf = ir.BitwiseAnd(low_cbuf, ir.Imm32(~(alignment - 1U)));
    return ir.ISub(offset, low_cbuf);
}

/// Replace a global memory load instruction with its storage buffer equivalent
void ReplaceLoad(IR::Block& block, IR::Inst& inst, const IR::U32& storage_index,
                 const IR::U32& offset) {
    const IR::Opcode new_opcode{GlobalToStorage(inst.GetOpcode())};
    const auto it{block.IteratorTo(inst)};
    const IR::Value value{*block.PrependNewInst(it, new_opcode, {storage_index, offset})};
    inst.ReplaceUsesWith(value);
}

/// Replace a global memory write instruction with its storage buffer equivalent
void ReplaceWrite(IR::Block& block, IR::Inst& inst, const IR::U32& storage_index,
                  const IR::U32& offset) {
    const IR::Opcode new_opcode{GlobalToStorage(inst.GetOpcode())};
    const auto it{block.IteratorTo(inst)};
    block.PrependNewInst(it, new_opcode, {storage_index, offset, inst.Arg(1)});
    inst.Invalidate();
}

/// Replace an atomic operation on global memory instruction with its storage buffer equivalent
void ReplaceAtomic(IR::Block& block, IR::Inst& inst, const IR::U32& storage_index,
                   const IR::U32& offset) {
    const IR::Opcode new_opcode{GlobalToStorage(inst.GetOpcode())};
    const auto it{block.IteratorTo(inst)};
    const IR::Value value{
        *block.PrependNewInst(it, new_opcode, {storage_index, offset, inst.Arg(1)})};
    inst.ReplaceUsesWith(value);
}

/// Replace a global memory instruction with its storage buffer equivalent
void Replace(IR::Block& block, IR::Inst& inst, const IR::U32& storage_index,
             const IR::U32& offset) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::LoadGlobalS8:
    case IR::Opcode::LoadGlobalU8:
    case IR::Opcode::LoadGlobalS16:
    case IR::Opcode::LoadGlobalU16:
    case IR::Opcode::LoadGlobal32:
    case IR::Opcode::LoadGlobal64:
    case IR::Opcode::LoadGlobal128:
        return ReplaceLoad(block, inst, storage_index, offset);
    case IR::Opcode::WriteGlobalS8:
    case IR::Opcode::WriteGlobalU8:
    case IR::Opcode::WriteGlobalS16:
    case IR::Opcode::WriteGlobalU16:
    case IR::Opcode::WriteGlobal32:
    case IR::Opcode::WriteGlobal64:
    case IR::Opcode::WriteGlobal128:
        return ReplaceWrite(block, inst, storage_index, offset);
    case IR::Opcode::GlobalAtomicIAdd32:
    case IR::Opcode::GlobalAtomicSMin32:
    case IR::Opcode::GlobalAtomicUMin32:
    case IR::Opcode::GlobalAtomicSMax32:
    case IR::Opcode::GlobalAtomicUMax32:
    case IR::Opcode::GlobalAtomicInc32:
    case IR::Opcode::GlobalAtomicDec32:
    case IR::Opcode::GlobalAtomicAnd32:
    case IR::Opcode::GlobalAtomicOr32:
    case IR::Opcode::GlobalAtomicXor32:
    case IR::Opcode::GlobalAtomicExchange32:
    case IR::Opcode::GlobalAtomicIAdd64:
    case IR::Opcode::GlobalAtomicSMin64:
    case IR::Opcode::GlobalAtomicUMin64:
    case IR::Opcode::GlobalAtomicSMax64:
    case IR::Opcode::GlobalAtomicUMax64:
    case IR::Opcode::GlobalAtomicAnd64:
    case IR::Opcode::GlobalAtomicOr64:
    case IR::Opcode::GlobalAtomicXor64:
    case IR::Opcode::GlobalAtomicExchange64:
    case IR::Opcode::GlobalAtomicIAdd32x2:
    case IR::Opcode::GlobalAtomicSMin32x2:
    case IR::Opcode::GlobalAtomicUMin32x2:
    case IR::Opcode::GlobalAtomicSMax32x2:
    case IR::Opcode::GlobalAtomicUMax32x2:
    case IR::Opcode::GlobalAtomicAnd32x2:
    case IR::Opcode::GlobalAtomicOr32x2:
    case IR::Opcode::GlobalAtomicXor32x2:
    case IR::Opcode::GlobalAtomicExchange32x2:
    case IR::Opcode::GlobalAtomicAddF32:
    case IR::Opcode::GlobalAtomicAddF16x2:
    case IR::Opcode::GlobalAtomicAddF32x2:
    case IR::Opcode::GlobalAtomicMinF16x2:
    case IR::Opcode::GlobalAtomicMinF32x2:
    case IR::Opcode::GlobalAtomicMaxF16x2:
    case IR::Opcode::GlobalAtomicMaxF32x2:
        return ReplaceAtomic(block, inst, storage_index, offset);
    default:
        throw InvalidArgument("Invalid global memory opcode {}", inst.GetOpcode());
    }
}

/// Evaluates value for a VertexId, reading the storage buffer address low word as cbuf_value.
/// Fails on any other input or after visiting budget instructions.
std::optional<u32> EvaluateForVertex(const IR::InstStorage& storage, const IR::Value& value,
                                     const StorageBufferAddr& storage_buffer, u32 vertex_id,
                                     u32 cbuf_value, u32& budget) {
    if (value.IsImmediate()) {
        if (value.Type() != IR::Type::U32) {
            return std::nullopt;
        }
        return value.U32();
    }
    if (budget == 0) {
        return std::nullopt;
    }
    --budget;
    const IR::Inst* const inst{storage.Get(value)};
    if (!inst) {
        return std::nullopt;
    }
    const auto arg{[&](size_t index) {
        return EvaluateForVertex(storage, inst->Arg(index), storage_buffer, vertex_id,
                                 cbuf_value, budget);
    }};
    const auto is_vertex_id{[&storage](const IR::Value& attribute) {
        const IR::Value resolved{storage.Resolve(attribute)};
        return resolved.Type() == IR::Type::Attribute &&
               resolved.Attribute() == IR::Attribute::VertexId;
    }};
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetAttributeU32:
        return is_vertex_id(inst->Arg(0)) ? std::optional<u32>{vertex_id} : std::nullopt;
    case IR::Opcode::BitCastU32F32: {
        const IR::Inst* const source{inst->ArgInst(0)};
        if (source && source->GetOpcode() == IR::Opcode::GetAttribute &&
            is_vertex_id(source->Arg(0))) {
            return vertex_id;
        }
        return std::nullopt;
    }
    case IR::Opcode::GetCbufU32: {
        const IR::Value index{inst->Arg(0)};
        const IR::Value offset{inst->Arg(1)};
        if (index.IsImmediate() && offset.IsImmediate() && index.U32() == storage_buffer.index &&
            offset.U32() == storage_buffer.offset) {
            return cbuf_value;
        }
        return std::nullopt;
    }
    // What address math (XMAD, ISCADD, LEA, IMUL32I) lowers to
    case IR::Opcode::IAdd32:
    case IR::Opcode::ISub32:
    case IR::Opcode::IMul32:
    case IR::Opcode::BitwiseAnd32:
    case IR::Opcode::BitwiseOr32:
    case IR::Opcode::ShiftLeftLogical32:
    case IR::Opcode::ShiftRightLogical32: {
        const std::optional<u32> lhs{arg(0)};
        const std::optional<u32> rhs{arg(1)};
        if (!lhs || !rhs) {
            return std::nullopt;
        }
        switch (inst->GetOpcode()) {
        case IR::Opcode::IAdd32:
            return *lhs + *rhs;
        case IR::Opcode::ISub32:
            return *lhs - *rhs;
        case IR::Opcode::IMul32:
            return *lhs * *rhs;
        case IR::Opcode::BitwiseAnd32:
            return *lhs & *rhs;
        case IR::Opcode::BitwiseOr32:
            return *lhs | *rhs;
        default:
            // Shifting by 32 or more is undefined
            if (*rhs >= 32) {
                return std::nullopt;
            }
            return inst->GetOpcode() == IR::Opcode::ShiftLeftLogical32 ? *lhs << *rhs
                                                                       : *lhs >> *rhs;
        }
    }
    case IR::Opcode::BitFieldUExtract: {
        const std::optional<u32> base{arg(0)};
        const std::optional<u32> offset{arg(1)};
        const std::optional<u32> count{arg(2)};
        if (!base || !offset || !count || *offset >= 32 || *count > 32 - *offset) {
            return std::nullopt;
        }
        return static_cast<u32>((u64{*base} >> *offset) & ((u64{1} << *count) - 1));
    }
    case IR::Opcode::BitFieldInsert: {
        const std::optional<u32> base{arg(0)};
        const std::optional<u32> insert{arg(1)};
        const std::optional<u32> offset{arg(2)};
        const std::optional<u32> count{arg(3)};
        if (!base || !insert || !offset || !count || *offset >= 32 || *count > 32 - *offset) {
            return std::nullopt;
        }
        const u32 mask{static_cast<u32>(((u64{1} << *count) - 1) << *offset)};
        return (*base & ~mask) | ((*insert << *offset) & mask);
    }
    default:
        return std::nullopt;
    }
}

/// 32-bit word a vertex shader loads from a storage buffer at (stride * VertexId + offset)
struct VertexIndexedWord {
    StorageBufferAddr storage_buffer;
    u32 stride;
    u32 offset;
};

/// Tracks a component of a global load from a VertexId-indexed storage buffer offset
std::optional<VertexIndexedWord> TrackVertexIndexedWord(IR::Inst& load, u32 component) {
    // Sample the offset to check it is linear in VertexId and independent of the buffer address
    static constexpr std::array<u32, 8> vertex_ids{0, 1, 2, 3, 7, 255, 65537, 1048573};
    static constexpr std::array<u32, 2> cbuf_values{0x00010000, 0x7ff31230};
    static constexpr u32 max_evaluated_insts{64};

    const std::optional<LowAddrInfo> low_addr{TrackLowAddress(&load)};
    if (!low_addr) {
        return std::nullopt;
    }
    const IR::InstStorage& storage{load.Storage()};
    const std::optional<StorageBufferAddr> storage_buffer{
        TrackStorageBuffer(storage, low_addr->value)};
    if (!storage_buffer) {
        return std::nullopt;
    }
    std::optional<VertexIndexedWord> word;
    for (const u32 cbuf_value : cbuf_values) {
        std::array<u32, vertex_ids.size()> offsets{};
        for (size_t index = 0; index < vertex_ids.size(); ++index) {
            u32 budget{max_evaluated_insts};
            const std::optional<u32> low{EvaluateForVertex(storage, low_addr->value,
                                                           *storage_buffer, vertex_ids[index],
                                                           cbuf_value, budget)};
            if (!low) {
                return std::nullopt;
            }
            offsets[index] = *low + static_cast<u32>(low_addr->imm_offset) - cbuf_value +
                             component * static_cast<u32>(sizeof(u32));
        }
        // vertex_ids starts with 0 and 1
        const u32 stride{offsets[1] - offsets[0]};
        for (size_t index = 0; index < vertex_ids.size(); ++index) {
            if (offsets[index] != offsets[0] + stride * vertex_ids[index]) {
                return std::nullopt;
            }
        }
        if (word && (word->stride != stride || word->offset != offsets[0])) {
            return std::nullopt;
        }
        word = VertexIndexedWord{
            .storage_buffer = *storage_buffer,
            .stride = stride,
            .offset = offsets[0],
        };
    }
    return word;
}

/// Tracks the storage buffer location a 32-bit word was loaded from
std::optional<VertexIndexedWord> TrackLoadedWord(IR::InstStorage& storage,
                                                 const IR::Value& value) {
    IR::Inst* const inst{storage.Get(value)};
    if (!inst) {
        return std::nullopt;
    }
    switch (inst->GetOpcode()) {
    case IR::Opcode::LoadGlobal32:
        return TrackVertexIndexedWord(*inst, 0);
    case IR::Opcode::CompositeExtractU32x2:
    case IR::Opcode::CompositeExtractU32x4: {
        IR::Inst* const load{inst->ArgInst(0)};
        const IR::Value component{inst->Arg(1)};
        const IR::Opcode load_opcode{inst->GetOpcode() == IR::Opcode::CompositeExtractU32x2
                                         ? IR::Opcode::LoadGlobal64
                                         : IR::Opcode::LoadGlobal128};
        if (!load || load->GetOpcode() != load_opcode || !component.IsImmediate()) {
            return std::nullopt;
        }
        return TrackVertexIndexedWord(*load, component.U32());
    }
    default:
        return std::nullopt;
    }
}

/// Returns the words a global store writes when all of them are immediates
std::optional<boost::container::static_vector<u32, 4>> ImmediateStoreWords(const IR::Inst& inst) {
    boost::container::static_vector<u32, 4> words;
    const auto add_word{[&words](const IR::Value& word) {
        if (!word.IsImmediate() || word.Type() != IR::Type::U32) {
            return false;
        }
        words.push_back(word.U32());
        return true;
    }};
    switch (inst.GetOpcode()) {
    case IR::Opcode::WriteGlobal32:
        return add_word(inst.Arg(1)) ? std::optional{words} : std::nullopt;
    case IR::Opcode::WriteGlobal64:
    case IR::Opcode::WriteGlobal128: {
        // U32x2 or U32x4, matching the store size
        const IR::Inst* const vector{inst.ArgInst(1)};
        if (!vector || (vector->GetOpcode() != IR::Opcode::CompositeConstructU32x2 &&
                        vector->GetOpcode() != IR::Opcode::CompositeConstructU32x4)) {
            return std::nullopt;
        }
        for (size_t index = 0; index < vector->NumArgs(); ++index) {
            if (!add_word(vector->Arg(index))) {
                return std::nullopt;
            }
        }
        return words;
    }
    default:
        return std::nullopt;
    }
}

/// Global memory address made of a 64-bit pointer and an immediate offset
struct PointerAddress {
    IR::Value pointer_low;
    IR::Value pointer_high;
    s32 offset;
};

/// Returns true when value is Select(carry of add, 1, 0)
bool IsCarryOf(const IR::InstStorage& storage, const IR::Value& value, const IR::Inst* add) {
    const IR::Inst* const select{storage.Get(value)};
    if (!select || select->GetOpcode() != IR::Opcode::SelectU32 || !select->Arg(1).IsImmediate() ||
        !select->Arg(2).IsImmediate() || select->Arg(1).U32() != 1 || select->Arg(2).U32() != 0) {
        return false;
    }
    const IR::Inst* const carry{select->ArgInst(0)};
    return carry && carry->GetOpcode() == IR::Opcode::GetCarryFromOp &&
           carry->ArgInst(0) == add;
}

/// Splits the address of a global memory instruction into its pointer words and offset
std::optional<PointerAddress> SplitPointerAddress(const IR::Inst& inst) {
    const IR::InstStorage& storage{inst.Storage()};
    const IR::Inst* addr_inst{inst.ArgInst(0)};
    if (!addr_inst) {
        return std::nullopt;
    }
    if (addr_inst->GetOpcode() == IR::Opcode::IAdd64 ||
        addr_inst->GetOpcode() == IR::Opcode::PackUint2x32) {
        s64 offset{};
        if (addr_inst->GetOpcode() == IR::Opcode::IAdd64) {
            // Immediates are canonicalized to the second argument
            const IR::Value imm{addr_inst->Arg(1)};
            if (!imm.IsImmediate()) {
                return std::nullopt;
            }
            offset = static_cast<s64>(imm.U64());
            addr_inst = addr_inst->ArgInst(0);
        }
        if (!addr_inst || addr_inst->GetOpcode() != IR::Opcode::PackUint2x32 ||
            offset != static_cast<s32>(offset)) {
            return std::nullopt;
        }
        const IR::Inst* const vector{addr_inst->ArgInst(0)};
        if (!vector || vector->GetOpcode() != IR::Opcode::CompositeConstructU32x2) {
            return std::nullopt;
        }
        return PointerAddress{
            .pointer_low = vector->Arg(0),
            .pointer_high = vector->Arg(1),
            .offset = static_cast<s32>(offset),
        };
    }
    if (addr_inst->GetOpcode() != IR::Opcode::CompositeConstructU32x2) {
        return std::nullopt;
    }
    // Lowered 64-bit math: low + offset, high + carry (+ all ones for negative offsets)
    PointerAddress address{
        .pointer_low = addr_inst->Arg(0),
        .pointer_high = addr_inst->Arg(1),
        .offset = 0,
    };
    const IR::Inst* const low_add{storage.Get(address.pointer_low)};
    if (!low_add || low_add->GetOpcode() != IR::Opcode::IAdd32 || !low_add->Arg(1).IsImmediate()) {
        return address;
    }
    const IR::Inst* high_add{storage.Get(address.pointer_high)};
    if (!high_add || high_add->GetOpcode() != IR::Opcode::IAdd32 ||
        !IsCarryOf(storage, high_add->Arg(1), low_add)) {
        return std::nullopt;
    }
    address.offset = static_cast<s32>(low_add->Arg(1).U32());
    address.pointer_low = low_add->Arg(0);
    address.pointer_high = high_add->Arg(0);
    if (address.offset < 0) {
        high_add = storage.Get(address.pointer_high);
        if (!high_add || high_add->GetOpcode() != IR::Opcode::IAdd32 ||
            !high_add->Arg(1).IsImmediate() || high_add->Arg(1).U32() != 0xffffffff) {
            return std::nullopt;
        }
        address.pointer_high = high_add->Arg(0);
    }
    return address;
}

/// Moves constant stores through VertexId-indexed pointers to info, returning whether any moved
bool ExtractPointerStores(IR::Block& block, Info& info) {
    bool extracted{};
    IR::InstStorage& storage{block.Storage()};
    for (IR::Inst& inst : block) {
        const auto words{ImmediateStoreWords(inst)};
        if (!words) {
            continue;
        }
        // Misaligned 32-bit stores fault on hardware
        const std::optional<PointerAddress> address{SplitPointerAddress(inst)};
        if (!address || address->offset % 4 != 0) {
            continue;
        }
        const std::optional<VertexIndexedWord> low{TrackLoadedWord(storage, address->pointer_low)};
        const std::optional<VertexIndexedWord> high{
            TrackLoadedWord(storage, address->pointer_high)};
        if (!low || !high || low->storage_buffer != high->storage_buffer ||
            low->stride != high->stride || high->offset != low->offset + 4) {
            continue;
        }
        for (size_t index = 0; index < words->size(); ++index) {
            info.pointer_store_descriptors.push_back({
                .cbuf_index = low->storage_buffer.index,
                .cbuf_offset = low->storage_buffer.offset,
                .pointer_stride = low->stride,
                .pointer_offset = low->offset,
                .store_offset = address->offset + static_cast<s32>(index * sizeof(u32)),
                .value = (*words)[index],
            });
        }
        inst.Invalidate();
        extracted = true;
    }
    return extracted;
}

/// Vertex shaders may store constants through pointers read per vertex from a storage buffer,
/// e.g. to reset indirect draw arguments. No bound storage buffer covers that memory, so the
/// rasterizer performs these stores instead. Only blocks every invocation runs qualify, which
/// keeps performing them once per vertex equivalent.
void ExtractVertexPointerStores(IR::Program& program) {
    bool extracted{};
    u32 depth{};
    for (const IR::AbstractSyntaxNode& node : program.syntax_list) {
        if (node.type == IR::AbstractSyntaxNode::Type::Return ||
            node.type == IR::AbstractSyntaxNode::Type::Unreachable) {
            // Later blocks may not run on every invocation
            break;
        }
        switch (node.type) {
        case IR::AbstractSyntaxNode::Type::Block:
            if (depth == 0) {
                extracted |= ExtractPointerStores(*node.data.block, program.info);
            }
            break;
        case IR::AbstractSyntaxNode::Type::If:
        case IR::AbstractSyntaxNode::Type::Loop:
            ++depth;
            break;
        case IR::AbstractSyntaxNode::Type::EndIf:
        case IR::AbstractSyntaxNode::Type::Repeat:
            --depth;
            break;
        default:
            break;
        }
    }
    if (extracted) {
        // Drop the now unused pointer loads so their storage buffer is not bound
        DeadCodeEliminationPass(program);
    }
}
} // Anonymous namespace

void GlobalMemoryToStorageBufferPass(IR::Program& program, const HostTranslateInfo& host_info) {
    if (program.stage == Stage::VertexB) {
        ExtractVertexPointerStores(program);
    }
    StorageInfo info;
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : *block) {
            if (!IsGlobalMemory(inst)) {
                continue;
            }
            CollectStorageBuffers(*block, inst, info);
        }
    }
    for (const StorageBufferAddr& storage_buffer : info.set) {
        program.info.storage_buffers_descriptors.push_back({
            .cbuf_index = storage_buffer.index,
            .cbuf_offset = storage_buffer.offset,
            .count = 1,
            .is_written = info.writes.contains(storage_buffer),
        });
    }
    for (const StorageInst& storage_inst : info.to_replace) {
        const StorageBufferAddr storage_buffer{storage_inst.storage_buffer};
        const auto it{info.set.find(storage_inst.storage_buffer)};
        const IR::U32 index{IR::Value{static_cast<u32>(info.set.index_of(it))}};
        IR::Block* const block{storage_inst.block};
        IR::Inst* const inst{storage_inst.inst};
        const IR::U32 offset{
            StorageOffset(*block, *inst, storage_buffer, host_info.min_ssbo_alignment)};
        Replace(*block, *inst, index, offset);
    }
}

template <typename Descriptors, typename Descriptor, typename Func>
static u32 Add(Descriptors& descriptors, const Descriptor& desc, Func&& pred) {
    // TODO: Handle arrays
    const auto it{std::ranges::find_if(descriptors, pred)};
    if (it != descriptors.end()) {
        return static_cast<u32>(std::distance(descriptors.begin(), it));
    }
    descriptors.push_back(desc);
    return static_cast<u32>(descriptors.size()) - 1;
}

void JoinStorageInfo(Info& base, Info& source) {
    auto& descriptors = base.storage_buffers_descriptors;
    for (auto& desc : source.storage_buffers_descriptors) {
        auto it{std::ranges::find_if(descriptors, [&desc](const auto& existing) {
            return desc.cbuf_index == existing.cbuf_index &&
                   desc.cbuf_offset == existing.cbuf_offset && desc.count == existing.count;
        })};
        if (it != descriptors.end()) {
            it->is_written |= desc.is_written;
            continue;
        }
        descriptors.push_back(desc);
    }
    base.pointer_store_descriptors.insert(base.pointer_store_descriptors.end(),
                                          source.pointer_store_descriptors.begin(),
                                          source.pointer_store_descriptors.end());
}

} // namespace Shader::Optimization
