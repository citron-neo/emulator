// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <fstream>
#include <optional>
#include <span>

#include <fstream>
#include <variant>
#ifdef ARCHITECTURE_x86_64
// xbyak hates human beings
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wshadow"
#endif
#include <xbyak/xbyak.h>
#endif

#include "common/assert.h"
#include "common/scope_exit.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/container_hash.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/draw_manager.h"
#include "video_core/dirty_flags.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/macro.h"

#include "common/assert.h"
#include "common/bit_field.h"
#include "common/logging.h"
#ifdef ARCHITECTURE_x86_64
#include "common/x64/xbyak_abi.h"
#include "common/x64/xbyak_util.h"
#endif
#include "video_core/engines/maxwell_3d.h"

namespace Tegra {

using Maxwell3D = Engines::Maxwell3D;

namespace {

bool IsTopologySafe(Maxwell3D::Regs::PrimitiveTopology topology) {
    switch (topology) {
    case Maxwell3D::Regs::PrimitiveTopology::Points:
    case Maxwell3D::Regs::PrimitiveTopology::Lines:
    case Maxwell3D::Regs::PrimitiveTopology::LineLoop:
    case Maxwell3D::Regs::PrimitiveTopology::LineStrip:
    case Maxwell3D::Regs::PrimitiveTopology::Triangles:
    case Maxwell3D::Regs::PrimitiveTopology::TriangleStrip:
    case Maxwell3D::Regs::PrimitiveTopology::TriangleFan:
    case Maxwell3D::Regs::PrimitiveTopology::LinesAdjacency:
    case Maxwell3D::Regs::PrimitiveTopology::LineStripAdjacency:
    case Maxwell3D::Regs::PrimitiveTopology::TrianglesAdjacency:
    case Maxwell3D::Regs::PrimitiveTopology::TriangleStripAdjacency:
    case Maxwell3D::Regs::PrimitiveTopology::Patches:
        return true;
    case Maxwell3D::Regs::PrimitiveTopology::Quads:
    case Maxwell3D::Regs::PrimitiveTopology::QuadStrip:
    case Maxwell3D::Regs::PrimitiveTopology::Polygon:
    default:
        return false;
    }
}

} // Anonymous namespace

void HLE_DrawArraysIndirect::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    auto topology = static_cast<Maxwell3D::Regs::PrimitiveTopology>(parameters[0]);
    if (!maxwell3d.AnyParametersDirty() || !IsTopologySafe(topology)) {
        Fallback(maxwell3d, parameters);
        return;
    }

    auto& params = maxwell3d.draw_manager->GetIndirectParams();
    params.is_byte_count = false;
    params.is_indexed = false;
    params.include_count = false;
    params.count_start_address = 0;
    params.indirect_start_address = maxwell3d.GetMacroAddress(1);
    params.buffer_size = 4 * sizeof(u32);
    params.max_draw_counts = 1;
    params.stride = 0;

    if (extended) {
        maxwell3d.engine_state = Maxwell3D::EngineHint::OnHLEMacro;
        maxwell3d.SetHLEReplacementAttributeType(0, 0x640, Maxwell3D::HLEReplacementAttributeType::BaseInstance);
    }

    maxwell3d.draw_manager->DrawArrayIndirect(topology);

    if (extended) {
        maxwell3d.engine_state = Maxwell3D::EngineHint::None;
        maxwell3d.replace_table.clear();
    }
}
void HLE_DrawArraysIndirect::Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters) {
    SCOPE_EXIT {
        if (extended) {
            maxwell3d.engine_state = Maxwell3D::EngineHint::None;
            maxwell3d.replace_table.clear();
        }
    };
    maxwell3d.RefreshParameters();
    const u32 instance_count = (maxwell3d.GetRegisterValue(0xD1B) & parameters[2]);
    auto topology = Maxwell3D::Regs::PrimitiveTopology(parameters[0]);
    const u32 vertex_first = parameters[3];
    const u32 vertex_count = parameters[1];
    if (!IsTopologySafe(topology) && size_t(maxwell3d.GetMaxCurrentVertices()) < size_t(vertex_first) + size_t(vertex_count)) {
        ASSERT(false && "Faulty draw!");
        return;
    }
    const u32 base_instance = parameters[4];
    if (extended) {
        maxwell3d.regs.global_base_instance_index = base_instance;
        maxwell3d.engine_state = Maxwell3D::EngineHint::OnHLEMacro;
        maxwell3d.SetHLEReplacementAttributeType(0, 0x640, Maxwell3D::HLEReplacementAttributeType::BaseInstance);
    }
    maxwell3d.draw_manager->DrawArray(topology, vertex_first, vertex_count, base_instance, instance_count);
    if (extended) {
        maxwell3d.regs.global_base_instance_index = 0;
        maxwell3d.engine_state = Maxwell3D::EngineHint::None;
        maxwell3d.replace_table.clear();
    }
}

void HLE_DrawIndexedIndirect::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    auto topology = static_cast<Maxwell3D::Regs::PrimitiveTopology>(parameters[0]);
    if (!maxwell3d.AnyParametersDirty() || !IsTopologySafe(topology)) {
        Fallback(maxwell3d, parameters);
        return;
    }

    const u32 estimate = u32(maxwell3d.EstimateIndexBufferSize());
    const u32 element_base = parameters[4];
    const u32 base_instance = parameters[5];
    maxwell3d.regs.vertex_id_base = element_base;
    maxwell3d.regs.global_base_vertex_index = element_base;
    maxwell3d.regs.global_base_instance_index = base_instance;
    maxwell3d.dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
    if (extended) {
        maxwell3d.engine_state = Maxwell3D::EngineHint::OnHLEMacro;
        maxwell3d.SetHLEReplacementAttributeType(0, 0x640, Maxwell3D::HLEReplacementAttributeType::BaseVertex);
        maxwell3d.SetHLEReplacementAttributeType(0, 0x644, Maxwell3D::HLEReplacementAttributeType::BaseInstance);
    }
    auto& params = maxwell3d.draw_manager->GetIndirectParams();
    params.is_byte_count = false;
    params.is_indexed = true;
    params.include_count = false;
    params.count_start_address = 0;
    params.indirect_start_address = maxwell3d.GetMacroAddress(1);
    params.buffer_size = 5 * sizeof(u32);
    params.max_draw_counts = 1;
    params.stride = 0;
    maxwell3d.dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
    maxwell3d.draw_manager->DrawIndexedIndirect(topology, 0, estimate);
    maxwell3d.regs.vertex_id_base = 0x0;
    maxwell3d.regs.global_base_vertex_index = 0x0;
    maxwell3d.regs.global_base_instance_index = 0x0;
    if (extended) {
        maxwell3d.engine_state = Maxwell3D::EngineHint::None;
        maxwell3d.replace_table.clear();
    }
}
void HLE_DrawIndexedIndirect::Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters) {
    maxwell3d.RefreshParameters();
    const u32 instance_count = (maxwell3d.GetRegisterValue(0xD1B) & parameters[2]);
    const u32 element_base = parameters[4];
    const u32 base_instance = parameters[5];
    maxwell3d.regs.vertex_id_base = element_base;
    maxwell3d.regs.global_base_vertex_index = element_base;
    maxwell3d.regs.global_base_instance_index = base_instance;
    maxwell3d.dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
    if (extended) {
        maxwell3d.engine_state = Maxwell3D::EngineHint::OnHLEMacro;
        maxwell3d.SetHLEReplacementAttributeType(0, 0x640, Maxwell3D::HLEReplacementAttributeType::BaseVertex);
        maxwell3d.SetHLEReplacementAttributeType(0, 0x644, Maxwell3D::HLEReplacementAttributeType::BaseInstance);
    }
    maxwell3d.draw_manager->DrawIndex(Tegra::Maxwell3D::Regs::PrimitiveTopology(parameters[0]), parameters[3], parameters[1], element_base, base_instance, instance_count);
    maxwell3d.regs.vertex_id_base = 0x0;
    maxwell3d.regs.global_base_vertex_index = 0x0;
    maxwell3d.regs.global_base_instance_index = 0x0;
    if (extended) {
        maxwell3d.engine_state = Maxwell3D::EngineHint::None;
        maxwell3d.replace_table.clear();
    }
}
void HLE_MultiLayerClear::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    ASSERT(parameters.size() == 1);

    const Maxwell3D::Regs::ClearSurface clear_params{parameters[0]};
    const u32 rt_index = clear_params.RT;
    const u32 num_layers = maxwell3d.regs.rt[rt_index].depth;
    ASSERT(clear_params.layer == 0);

    maxwell3d.regs.clear_surface.raw = clear_params.raw;
    maxwell3d.draw_manager->Clear(num_layers);
}
void HLE_MultiDrawIndexedIndirectCount::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    const auto topology = Maxwell3D::Regs::PrimitiveTopology(parameters[2]);
    if (!IsTopologySafe(topology)) {
        Fallback(maxwell3d, parameters);
        return;
    }

    const u32 start_indirect = parameters[0];
    const u32 end_indirect = parameters[1];
    if (start_indirect >= end_indirect) {
        // Nothing to do.
        return;
    }

    const u32 padding = parameters[3]; // padding is in words

    // size of each indirect segment
    const u32 indirect_words = 5 + padding;
    const u32 stride = indirect_words * sizeof(u32);
    const std::size_t draw_count = end_indirect - start_indirect;
    const u32 estimate = static_cast<u32>(maxwell3d.EstimateIndexBufferSize());
    maxwell3d.dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
    auto& params = maxwell3d.draw_manager->GetIndirectParams();
    params.is_byte_count = false;
    params.is_indexed = true;
    params.include_count = true;
    params.count_start_address = maxwell3d.GetMacroAddress(4);
    params.indirect_start_address = maxwell3d.GetMacroAddress(5);
    params.buffer_size = stride * draw_count;
    params.max_draw_counts = draw_count;
    params.stride = stride;
    maxwell3d.dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
    maxwell3d.engine_state = Maxwell3D::EngineHint::OnHLEMacro;
    maxwell3d.SetHLEReplacementAttributeType(0, 0x640, Maxwell3D::HLEReplacementAttributeType::BaseVertex);
    maxwell3d.SetHLEReplacementAttributeType(0, 0x644, Maxwell3D::HLEReplacementAttributeType::BaseInstance);
    maxwell3d.SetHLEReplacementAttributeType(0, 0x648, Maxwell3D::HLEReplacementAttributeType::DrawID);
    maxwell3d.draw_manager->DrawIndexedIndirect(topology, 0, estimate);
    maxwell3d.engine_state = Maxwell3D::EngineHint::None;
    maxwell3d.replace_table.clear();
}
void HLE_MultiDrawIndexedIndirectCount::Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters) {
    SCOPE_EXIT {
        // Clean everything.
        maxwell3d.regs.vertex_id_base = 0x0;
        maxwell3d.engine_state = Maxwell3D::EngineHint::None;
        maxwell3d.replace_table.clear();
    };
    maxwell3d.RefreshParameters();
    const u32 start_indirect = parameters[0];
    const u32 end_indirect = parameters[1];
    if (start_indirect >= end_indirect) {
        // Nothing to do.
        return;
    }
    const auto topology = static_cast<Maxwell3D::Regs::PrimitiveTopology>(parameters[2]);
    const u32 padding = parameters[3];
    const std::size_t max_draws = parameters[4];
    const u32 indirect_words = 5 + padding;
    const std::size_t first_draw = start_indirect;
    const std::size_t effective_draws = end_indirect - start_indirect;
    const std::size_t last_draw = start_indirect + (std::min)(effective_draws, max_draws);
    for (std::size_t index = first_draw; index < last_draw; index++) {
        const std::size_t base = index * indirect_words + 5;
        const u32 base_vertex = parameters[base + 3];
        const u32 base_instance = parameters[base + 4];
        maxwell3d.regs.vertex_id_base = base_vertex;
        maxwell3d.engine_state = Maxwell3D::EngineHint::OnHLEMacro;
        maxwell3d.SetHLEReplacementAttributeType(0, 0x640, Maxwell3D::HLEReplacementAttributeType::BaseVertex);
        maxwell3d.SetHLEReplacementAttributeType(0, 0x644, Maxwell3D::HLEReplacementAttributeType::BaseInstance);
        maxwell3d.CallMethod(0x8e3, 0x648, true);
        maxwell3d.CallMethod(0x8e4, static_cast<u32>(index), true);
        maxwell3d.dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
        maxwell3d.draw_manager->DrawIndex(topology, parameters[base + 2], parameters[base], base_vertex, base_instance, parameters[base + 1]);
    }
}
void HLE_DrawIndirectByteCount::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    const bool force = maxwell3d.Rasterizer().HasDrawTransformFeedback();
    auto topology = Maxwell3D::Regs::PrimitiveTopology(parameters[0] & 0xFFFFU);
    if (!force && (!maxwell3d.AnyParametersDirty() || !IsTopologySafe(topology))) {
        Fallback(maxwell3d, parameters);
        return;
    }
    auto& params = maxwell3d.draw_manager->GetIndirectParams();
    params.is_byte_count = true;
    params.is_indexed = false;
    params.include_count = false;
    params.count_start_address = 0;
    params.indirect_start_address = maxwell3d.GetMacroAddress(2);
    params.buffer_size = 4;
    params.max_draw_counts = 1;
    params.stride = parameters[1];
    maxwell3d.regs.draw.begin = parameters[0];
    maxwell3d.regs.draw_auto_stride = parameters[1];
    maxwell3d.regs.draw_auto_byte_count = parameters[2];
    maxwell3d.draw_manager->DrawArrayIndirect(topology);
}
void HLE_DrawIndirectByteCount::Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters) {
    maxwell3d.RefreshParameters();

    maxwell3d.regs.draw.begin = parameters[0];
    maxwell3d.regs.draw_auto_stride = parameters[1];
    maxwell3d.regs.draw_auto_byte_count = parameters[2];

    maxwell3d.draw_manager->DrawArray(
        maxwell3d.regs.draw.topology, 0,
        maxwell3d.regs.draw_auto_byte_count / maxwell3d.regs.draw_auto_stride, 0, 1);
}
void HLE_C713C83D8F63CCF3::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    const u32 offset = (parameters[0] & 0x3FFFFFFF) << 2;
    const u32 address = maxwell3d.regs.shadow_scratch[24];
    auto& const_buffer = maxwell3d.regs.const_buffer;
    const_buffer.size = 0x7000;
    const_buffer.address_high = (address >> 24) & 0xFF;
    const_buffer.address_low = address << 8;
    const_buffer.offset = offset;
}
void HLE_D7333D26E0A93EDE::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    const size_t index = parameters[0];
    const u32 address = maxwell3d.regs.shadow_scratch[42 + index];
    const u32 size = maxwell3d.regs.shadow_scratch[47 + index];
    auto& const_buffer = maxwell3d.regs.const_buffer;
    const_buffer.size = size;
    const_buffer.address_high = (address >> 24) & 0xFF;
    const_buffer.address_low = address << 8;
}
void HLE_BindShader::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    auto& regs = maxwell3d.regs;
    const u32 index = parameters[0];
    if ((parameters[1] - regs.shadow_scratch[28 + index]) == 0) {
        return;
    }

    regs.pipelines[index & 0xF].offset = parameters[2];
    maxwell3d.dirty.flags[VideoCommon::Dirty::Shaders] = true;
    regs.shadow_scratch[28 + index] = parameters[1];
    regs.shadow_scratch[34 + index] = parameters[2];

    const u32 address = parameters[4];
    auto& const_buffer = regs.const_buffer;
    const_buffer.size = 0x10000;
    const_buffer.address_high = (address >> 24) & 0xFF;
    const_buffer.address_low = address << 8;

    const size_t bind_group_id = parameters[3] & 0x7F;
    auto& bind_group = regs.bind_groups[bind_group_id];
    bind_group.raw_config = 0x11;
    maxwell3d.ProcessCBBind(bind_group_id);
}
void HLE_SetRasterBoundingBox::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    const u32 raster_mode = parameters[0];
    auto& regs = maxwell3d.regs;
    const u32 raster_enabled = maxwell3d.regs.conservative_raster_enable;
    const u32 scratch_data = maxwell3d.regs.shadow_scratch[52];
    regs.raster_bounding_box.raw = raster_mode & 0xFFFFF00F;
    regs.raster_bounding_box.pad.Assign(scratch_data & raster_enabled);
}
void HLE_ClearConstBuffer::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    static constexpr std::array<u32, 0x7000> zeroes{}; //must be bigger than either 7000 or 5F00
    maxwell3d.RefreshParameters();
    auto& regs = maxwell3d.regs;
    regs.const_buffer.size = u32(base_size);
    regs.const_buffer.address_high = parameters[0];
    regs.const_buffer.address_low = parameters[1];
    regs.const_buffer.offset = 0;
    maxwell3d.ProcessCBMultiData(zeroes.data(), parameters[2] * 4);
}
void HLE_ClearMemory::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    const u32 needed_memory = parameters[2] / sizeof(u32);
    if (needed_memory > zero_memory.size()) {
        zero_memory.resize(needed_memory, 0);
    }
    auto& regs = maxwell3d.regs;
    regs.upload.line_length_in = parameters[2];
    regs.upload.line_count = 1;
    regs.upload.dest.address_high = parameters[0];
    regs.upload.dest.address_low = parameters[1];
    maxwell3d.CallMethod(size_t(MAXWELL3D_REG_INDEX(launch_dma)), 0x1011, true);
    maxwell3d.CallMultiMethod(size_t(MAXWELL3D_REG_INDEX(inline_data)), zero_memory.data(), needed_memory, needed_memory);
}
void HLE_TransformFeedbackSetup::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method) {
    maxwell3d.RefreshParameters();
    auto& regs = maxwell3d.regs;
    regs.transform_feedback_enabled = 1;
    regs.transform_feedback.buffers[0].start_offset = 0;
    regs.transform_feedback.buffers[1].start_offset = 0;
    regs.transform_feedback.buffers[2].start_offset = 0;
    regs.transform_feedback.buffers[3].start_offset = 0;
    regs.upload.line_length_in = 4;
    regs.upload.line_count = 1;
    regs.upload.dest.address_high = parameters[0];
    regs.upload.dest.address_low = parameters[1];
    maxwell3d.CallMethod(size_t(MAXWELL3D_REG_INDEX(launch_dma)), 0x1011, true);
    maxwell3d.CallMethod(size_t(MAXWELL3D_REG_INDEX(inline_data)), regs.transform_feedback.controls[0].stride, true);
    maxwell3d.Rasterizer().RegisterTransformFeedback(regs.upload.dest.Address());
}

#define HLE_MACRO_LIST \
    HLE_MACRO_ELEM(0x0D61FC9FAAC9FCADULL, HLE_DrawArraysIndirect, (false)) \
    HLE_MACRO_ELEM(0x8A4D173EB99A8603ULL, HLE_DrawArraysIndirect, (true)) \
    HLE_MACRO_ELEM(0x771BB18C62444DA0ULL, HLE_DrawIndexedIndirect, (false)) \
    HLE_MACRO_ELEM(0x0217920100488FF7ULL, HLE_DrawIndexedIndirect, (true)) \
    HLE_MACRO_ELEM(0x3F5E74B9C9A50164ULL, HLE_MultiDrawIndexedIndirectCount, ()) \
    HLE_MACRO_ELEM(0xEAD26C3E2109B06BULL, HLE_MultiLayerClear, ()) \
    HLE_MACRO_ELEM(0xC713C83D8F63CCF3ULL, HLE_C713C83D8F63CCF3, ()) \
    HLE_MACRO_ELEM(0xD7333D26E0A93EDEULL, HLE_D7333D26E0A93EDE, ()) \
    HLE_MACRO_ELEM(0xEB29B2A09AA06D38ULL, HLE_BindShader, ()) \
    HLE_MACRO_ELEM(0xDB1341DBEB4C8AF7ULL, HLE_SetRasterBoundingBox, ()) \
    HLE_MACRO_ELEM(0x6C97861D891EDf7EULL, HLE_ClearConstBuffer, (0x5F00)) \
    HLE_MACRO_ELEM(0xD246FDDF3A6173D7ULL, HLE_ClearConstBuffer, (0x7000)) \
    HLE_MACRO_ELEM(0xEE4D0004BEC8ECF4ULL, HLE_ClearMemory, ()) \
    HLE_MACRO_ELEM(0xFC0CF27F5FFAA661ULL, HLE_TransformFeedbackSetup, ()) \
    HLE_MACRO_ELEM(0xB5F74EDB717278ECULL, HLE_DrawIndirectByteCount, ()) \

// Allocates and returns a cached macro if the hash matches a known function.
[[nodiscard]] inline AnyCachedMacro GetHLEProgram(u64 hash) noexcept {
    // Compiler will make you a GREAT job at making an ad-hoc hash table :)
    switch (hash) {
#define HLE_MACRO_ELEM(HASH, TY, VAL) case HASH: return TY VAL;
    HLE_MACRO_LIST
#undef HLE_MACRO_ELEM
    default: return std::monostate{};
    }
}
[[nodiscard]] inline bool CanBeHLEProgram(u64 hash) noexcept {
    switch (hash) {
#define HLE_MACRO_ELEM(HASH, TY, VAL) case HASH: return true;
    HLE_MACRO_LIST
#undef HLE_MACRO_ELEM
    default: return false;
    }
}

void MacroInterpreterImpl::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> params, u32 method) {
    Reset();

    registers[1] = params[0];
    parameters.resize(params.size());
    std::memcpy(parameters.data(), params.data(), params.size() * sizeof(u32));

    // Execute the code until we hit an exit condition.
    bool keep_executing = true;
    while (keep_executing) {
        keep_executing = Step(maxwell3d, false);
    }

    // Assert the the macro used all the input parameters
    ASSERT(next_parameter_index == parameters.size());
}

/// Resets the execution engine state, zeroing registers, etc.
void MacroInterpreterImpl::Reset() {
    registers = {};
    pc = 0;
    delayed_pc = {};
    method_address.raw = 0;
    // Vector must hold its last indices otherwise wonky shit will happen
    // The next parameter index starts at 1, because $r1 already has the value of the first
    // parameter.
    next_parameter_index = 1;
    carry_flag = false;
}

/// @brief Executes a single macro instruction located at the current program counter. Returns whether
/// the interpreter should keep running.
/// @param is_delay_slot Whether the current step is being executed due to a delay slot in a previous instruction.
bool MacroInterpreterImpl::Step(Engines::Maxwell3D& maxwell3d, bool is_delay_slot) {
    u32 base_address = pc;

    Macro::Opcode opcode = GetOpcode();
    pc += 4;

    // Update the program counter if we were delayed
    if (delayed_pc) {
        ASSERT(is_delay_slot);
        pc = *delayed_pc;
        delayed_pc = {};
    }

    switch (opcode.operation) {
    case Macro::Operation::ALU: {
        u32 result = GetALUResult(opcode.alu_operation, GetRegister(opcode.src_a), GetRegister(opcode.src_b));
        ProcessResult(maxwell3d, opcode.result_operation, opcode.dst, result);
        break;
    }
    case Macro::Operation::AddImmediate: {
        ProcessResult(maxwell3d, opcode.result_operation, opcode.dst, GetRegister(opcode.src_a) + opcode.immediate);
        break;
    }
    case Macro::Operation::ExtractInsert: {
        u32 dst = GetRegister(opcode.src_a);
        u32 src = GetRegister(opcode.src_b);

        src = (src >> opcode.bf_src_bit) & opcode.GetBitfieldMask();
        dst &= ~(opcode.GetBitfieldMask() << opcode.bf_dst_bit);
        dst |= src << opcode.bf_dst_bit;
        ProcessResult(maxwell3d, opcode.result_operation, opcode.dst, dst);
        break;
    }
    case Macro::Operation::ExtractShiftLeftImmediate: {
        u32 dst = GetRegister(opcode.src_a);
        u32 src = GetRegister(opcode.src_b);

        u32 result = ((src >> dst) & opcode.GetBitfieldMask()) << opcode.bf_dst_bit;

        ProcessResult(maxwell3d, opcode.result_operation, opcode.dst, result);
        break;
    }
    case Macro::Operation::ExtractShiftLeftRegister: {
        u32 dst = GetRegister(opcode.src_a);
        u32 src = GetRegister(opcode.src_b);

        u32 result = ((src >> opcode.bf_src_bit) & opcode.GetBitfieldMask()) << dst;

        ProcessResult(maxwell3d, opcode.result_operation, opcode.dst, result);
        break;
    }
    case Macro::Operation::Read: {
        u32 result = Read(maxwell3d, GetRegister(opcode.src_a) + opcode.immediate);
        ProcessResult(maxwell3d, opcode.result_operation, opcode.dst, result);
        break;
    }
    case Macro::Operation::Branch: {
        ASSERT_MSG(!is_delay_slot, "Executing a branch in a delay slot is not valid");
        u32 value = GetRegister(opcode.src_a);
        bool taken = EvaluateBranchCondition(opcode.branch_condition, value);
        if (taken) {
            // Ignore the delay slot if the branch has the annul bit.
            if (opcode.branch_annul) {
                pc = base_address + opcode.GetBranchTarget();
                return true;
            }

            delayed_pc = base_address + opcode.GetBranchTarget();
            // Execute one more instruction due to the delay slot.
            return Step(maxwell3d, true);
        }
        break;
    }
    default:
        UNIMPLEMENTED_MSG("Unimplemented macro operation {}", opcode.operation.Value());
        break;
    }

    // An instruction with the Exit flag will not actually
    // cause an exit if it's executed inside a delay slot.
    if (opcode.is_exit && !is_delay_slot) {
        // Exit has a delay slot, execute the next instruction
        Step(maxwell3d, true);
        return false;
    }
    return true;
}

/// Calculates the result of an ALU operation. src_a OP src_b;
u32 MacroInterpreterImpl::GetALUResult(Macro::ALUOperation operation, u32 src_a, u32 src_b) {
    switch (operation) {
    case Macro::ALUOperation::Add: {
        const u64 result{static_cast<u64>(src_a) + src_b};
        carry_flag = result > 0xffffffff;
        return static_cast<u32>(result);
    }
    case Macro::ALUOperation::AddWithCarry: {
        const u64 result{static_cast<u64>(src_a) + src_b + (carry_flag ? 1ULL : 0ULL)};
        carry_flag = result > 0xffffffff;
        return static_cast<u32>(result);
    }
    case Macro::ALUOperation::Subtract: {
        const u64 result{static_cast<u64>(src_a) - src_b};
        carry_flag = result < 0x100000000;
        return static_cast<u32>(result);
    }
    case Macro::ALUOperation::SubtractWithBorrow: {
        const u64 result{static_cast<u64>(src_a) - src_b - (carry_flag ? 0ULL : 1ULL)};
        carry_flag = result < 0x100000000;
        return static_cast<u32>(result);
    }
    case Macro::ALUOperation::Xor:
        return src_a ^ src_b;
    case Macro::ALUOperation::Or:
        return src_a | src_b;
    case Macro::ALUOperation::And:
        return src_a & src_b;
    case Macro::ALUOperation::AndNot:
        return src_a & ~src_b;
    case Macro::ALUOperation::Nand:
        return ~(src_a & src_b);

    default:
        UNIMPLEMENTED_MSG("Unimplemented ALU operation {}", operation);
        return 0;
    }
}

/// Performs the result operation on the input result and stores it in the specified register (if necessary).
void MacroInterpreterImpl::ProcessResult(Engines::Maxwell3D& maxwell3d, Macro::ResultOperation operation, u32 reg, u32 result) {
    switch (operation) {
    case Macro::ResultOperation::IgnoreAndFetch:
        // Fetch parameter and ignore result.
        SetRegister(reg, FetchParameter());
        break;
    case Macro::ResultOperation::Move:
        // Move result.
        SetRegister(reg, result);
        break;
    case Macro::ResultOperation::MoveAndSetMethod:
        // Move result and use as Method Address.
        SetRegister(reg, result);
        SetMethodAddress(result);
        break;
    case Macro::ResultOperation::FetchAndSend:
        // Fetch parameter and send result.
        SetRegister(reg, FetchParameter());
        Send(maxwell3d, result);
        break;
    case Macro::ResultOperation::MoveAndSend:
        // Move and send result.
        SetRegister(reg, result);
        Send(maxwell3d, result);
        break;
    case Macro::ResultOperation::FetchAndSetMethod:
        // Fetch parameter and use result as Method Address.
        SetRegister(reg, FetchParameter());
        SetMethodAddress(result);
        break;
    case Macro::ResultOperation::MoveAndSetMethodFetchAndSend:
        // Move result and use as Method Address, then fetch and send parameter.
        SetRegister(reg, result);
        SetMethodAddress(result);
        Send(maxwell3d, FetchParameter());
        break;
    case Macro::ResultOperation::MoveAndSetMethodSend:
        // Move result and use as Method Address, then send bits 12:17 of result.
        SetRegister(reg, result);
        SetMethodAddress(result);
        Send(maxwell3d, (result >> 12) & 0b111111);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented result operation {}", operation);
        break;
    }
}

/// Evaluates the branch condition and returns whether the branch should be taken or not.
bool MacroInterpreterImpl::EvaluateBranchCondition(Macro::BranchCondition cond, u32 value) const {
    switch (cond) {
    case Macro::BranchCondition::Zero:
        return value == 0;
    case Macro::BranchCondition::NotZero:
        return value != 0;
    }
    UNREACHABLE();
}

/// Reads an opcode at the current program counter location.
Macro::Opcode MacroInterpreterImpl::GetOpcode() const {
    ASSERT((pc % sizeof(u32)) == 0);
    ASSERT(pc < code.size() * sizeof(u32));
    return {code[pc / sizeof(u32)]};
}

/// Returns the specified register's value. Register 0 is hardcoded to always return 0.
u32 MacroInterpreterImpl::GetRegister(u32 register_id) const {
    return registers[register_id];
}

/// Sets the register to the input value.
void MacroInterpreterImpl::SetRegister(u32 register_id, u32 value) {
    // Register 0 is hardwired as the zero register.
    // Ensure no writes to it actually occur.
    if (register_id == 0)
        return;
    registers[register_id] = value;
}

/// Calls a GPU Engine method with the input parameter.
void MacroInterpreterImpl::Send(Engines::Maxwell3D& maxwell3d, u32 value) {
    maxwell3d.CallMethod(method_address.address, value, true);
    // Increment the method address by the method increment.
    method_address.address.Assign(method_address.address.Value() + method_address.increment.Value());
}

/// Reads a GPU register located at the method address.
u32 MacroInterpreterImpl::Read(Engines::Maxwell3D& maxwell3d, u32 method) const {
    return maxwell3d.GetRegisterValue(method);
}

/// Returns the next parameter in the parameter queue.
u32 MacroInterpreterImpl::FetchParameter() {
    ASSERT(next_parameter_index < parameters.size());
    return parameters[next_parameter_index++];
}

#ifdef ARCHITECTURE_x86_64
namespace {
constexpr Xbyak::Reg64 STATE = Xbyak::util::rbx;
constexpr Xbyak::Reg32 RESULT = Xbyak::util::r10d;
constexpr Xbyak::Reg64 MAX_PARAMETER = Xbyak::util::r11;
constexpr Xbyak::Reg64 PARAMETERS = Xbyak::util::r12;
constexpr Xbyak::Reg32 METHOD_ADDRESS = Xbyak::util::r14d;
constexpr Xbyak::Reg64 BRANCH_HOLDER = Xbyak::util::r15;

constexpr std::bitset<32> PERSISTENT_REGISTERS = Common::X64::BuildRegSet({
    STATE,
    RESULT,
    MAX_PARAMETER,
    PARAMETERS,
    METHOD_ADDRESS,
    BRANCH_HOLDER,
});

// Arbitrarily chosen based on current booting games.
constexpr size_t MAX_CODE_SIZE = 0x10000;

std::bitset<32> PersistentCallerSavedRegs() {
    return PERSISTENT_REGISTERS & Common::X64::ABI_ALL_CALLER_SAVED;
}

/// @brief Must enforce W^X constraints, as we yet don't havea  global "NO_EXECUTE" support flag
/// the speed loss is minimal, and in fact may be negligible, however for your peace of mind
/// I simply included known OSes whom had W^X issues
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
static const auto default_cg_mode = Xbyak::DontSetProtectRWE;
#else
static const auto default_cg_mode = nullptr; //Allow RWE
#endif

struct MacroJITx64Impl final : public Xbyak::CodeGenerator, public DynamicCachedMacro {
    explicit MacroJITx64Impl(std::span<const u32> code_)
        : Xbyak::CodeGenerator(MAX_CODE_SIZE, default_cg_mode)
        , code{code_}
    {
        Compile();
    }

    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, u32 method) override;

    void Compile_ALU(Macro::Opcode opcode);
    void Compile_AddImmediate(Macro::Opcode opcode);
    void Compile_ExtractInsert(Macro::Opcode opcode);
    void Compile_ExtractShiftLeftImmediate(Macro::Opcode opcode);
    void Compile_ExtractShiftLeftRegister(Macro::Opcode opcode);
    void Compile_Read(Macro::Opcode opcode);
    void Compile_Branch(Macro::Opcode opcode);

    void Optimizer_ScanFlags();
    void Compile();
    bool Compile_NextInstruction();
    Xbyak::Reg32 Compile_FetchParameter();
    Xbyak::Reg32 Compile_GetRegister(u32 index, Xbyak::Reg32 dst);
    void Compile_ProcessResult(Macro::ResultOperation operation, u32 reg);
    void Compile_Send(Xbyak::Reg32 value);
    Macro::Opcode GetOpCode() const;

    struct JITState {
        Engines::Maxwell3D* maxwell3d{};
        std::array<u32, Macro::NUM_MACRO_REGISTERS> registers{};
        u32 carry_flag{};
    };
    static_assert(offsetof(JITState, maxwell3d) == 0, "Maxwell3D is not at 0x0");
    using ProgramType = void (*)(JITState*, const u32*, const u32*);

    struct OptimizerState {
        bool can_skip_carry{};
        bool has_delayed_pc{};
        bool zero_reg_skip{};
        bool skip_dummy_addimmediate{};
        bool optimize_for_method_move{};
        bool enable_asserts{};
    };
    OptimizerState optimizer{};
    std::optional<Macro::Opcode> next_opcode{};
    ProgramType program{nullptr};
    std::array<Xbyak::Label, MAX_CODE_SIZE> labels;
    std::array<Xbyak::Label, MAX_CODE_SIZE> delay_skip;
    Xbyak::Label end_of_code{};
    bool is_delay_slot{};
    u32 pc{};
    std::span<const u32> code;
};

void MacroJITx64Impl::Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, u32 method) {
    ASSERT_OR_EXECUTE(program != nullptr, { return; });
    JITState state{};
    state.maxwell3d = &maxwell3d;
    state.registers = {};
    program(&state, parameters.data(), parameters.data() + parameters.size());
}

void MacroJITx64Impl::Compile_ALU(Macro::Opcode opcode) {
    const bool is_a_zero = opcode.src_a == 0;
    const bool is_b_zero = opcode.src_b == 0;
    const bool valid_operation = !is_a_zero && !is_b_zero;
    [[maybe_unused]] const bool is_move_operation = !is_a_zero && is_b_zero;
    const bool has_zero_register = is_a_zero || is_b_zero;
    const bool no_zero_reg_skip = opcode.alu_operation == Macro::ALUOperation::AddWithCarry ||
                                  opcode.alu_operation == Macro::ALUOperation::SubtractWithBorrow;

    Xbyak::Reg32 src_a;
    Xbyak::Reg32 src_b;

    if (!optimizer.zero_reg_skip || no_zero_reg_skip) {
        src_a = Compile_GetRegister(opcode.src_a, RESULT);
        src_b = Compile_GetRegister(opcode.src_b, eax);
    } else {
        if (!is_a_zero) {
            src_a = Compile_GetRegister(opcode.src_a, RESULT);
        }
        if (!is_b_zero) {
            src_b = Compile_GetRegister(opcode.src_b, eax);
        }
    }

    bool has_emitted = false;

    switch (opcode.alu_operation) {
    case Macro::ALUOperation::Add:
        if (optimizer.zero_reg_skip) {
            if (valid_operation) {
                add(src_a, src_b);
            }
        } else {
            add(src_a, src_b);
        }

        if (!optimizer.can_skip_carry) {
            setc(byte[STATE + offsetof(JITState, carry_flag)]);
        }
        break;
    case Macro::ALUOperation::AddWithCarry:
        bt(dword[STATE + offsetof(JITState, carry_flag)], 0);
        adc(src_a, src_b);
        setc(byte[STATE + offsetof(JITState, carry_flag)]);
        break;
    case Macro::ALUOperation::Subtract:
        if (optimizer.zero_reg_skip) {
            if (valid_operation) {
                sub(src_a, src_b);
                has_emitted = true;
            }
        } else {
            sub(src_a, src_b);
            has_emitted = true;
        }
        if (!optimizer.can_skip_carry && has_emitted) {
            setc(byte[STATE + offsetof(JITState, carry_flag)]);
        }
        break;
    case Macro::ALUOperation::SubtractWithBorrow:
        bt(dword[STATE + offsetof(JITState, carry_flag)], 0);
        sbb(src_a, src_b);
        setc(byte[STATE + offsetof(JITState, carry_flag)]);
        break;
    case Macro::ALUOperation::Xor:
        if (optimizer.zero_reg_skip) {
            if (valid_operation) {
                xor_(src_a, src_b);
            }
        } else {
            xor_(src_a, src_b);
        }
        break;
    case Macro::ALUOperation::Or:
        if (optimizer.zero_reg_skip) {
            if (valid_operation) {
                or_(src_a, src_b);
            }
        } else {
            or_(src_a, src_b);
        }
        break;
    case Macro::ALUOperation::And:
        if (optimizer.zero_reg_skip) {
            if (!has_zero_register) {
                and_(src_a, src_b);
            }
        } else {
            and_(src_a, src_b);
        }
        break;
    case Macro::ALUOperation::AndNot:
        if (optimizer.zero_reg_skip) {
            if (!is_a_zero) {
                not_(src_b);
                and_(src_a, src_b);
            }
        } else {
            not_(src_b);
            and_(src_a, src_b);
        }
        break;
    case Macro::ALUOperation::Nand:
        if (optimizer.zero_reg_skip) {
            if (!is_a_zero) {
                and_(src_a, src_b);
                not_(src_a);
            }
        } else {
            and_(src_a, src_b);
            not_(src_a);
        }
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented ALU operation {}", opcode.alu_operation.Value());
        break;
    }
    Compile_ProcessResult(opcode.result_operation, opcode.dst);
}

void MacroJITx64Impl::Compile_AddImmediate(Macro::Opcode opcode) {
    if (optimizer.skip_dummy_addimmediate) {
        // Games tend to use this as an exit instruction placeholder. It's to encode an instruction
        // without doing anything. In our case we can just not emit anything.
        if (opcode.result_operation == Macro::ResultOperation::Move && opcode.dst == 0) {
            return;
        }
    }
    // Check for redundant moves
    if (optimizer.optimize_for_method_move &&
        opcode.result_operation == Macro::ResultOperation::MoveAndSetMethod) {
        if (next_opcode.has_value()) {
            const auto next = *next_opcode;
            if (next.result_operation == Macro::ResultOperation::MoveAndSetMethod &&
                opcode.dst == next.dst) {
                return;
            }
        }
    }
    if (optimizer.zero_reg_skip && opcode.src_a == 0) {
        if (opcode.immediate == 0) {
            xor_(RESULT, RESULT);
        } else {
            mov(RESULT, opcode.immediate);
        }
    } else {
        auto result = Compile_GetRegister(opcode.src_a, RESULT);
        if (opcode.immediate > 2) {
            add(result, opcode.immediate);
        } else if (opcode.immediate == 1) {
            inc(result);
        } else if (opcode.immediate < 0) {
            sub(result, opcode.immediate * -1);
        }
    }
    Compile_ProcessResult(opcode.result_operation, opcode.dst);
}

void MacroJITx64Impl::Compile_ExtractInsert(Macro::Opcode opcode) {
    auto dst = Compile_GetRegister(opcode.src_a, RESULT);
    auto src = Compile_GetRegister(opcode.src_b, eax);

    const u32 mask = ~(opcode.GetBitfieldMask() << opcode.bf_dst_bit);
    and_(dst, mask);
    shr(src, opcode.bf_src_bit);
    and_(src, opcode.GetBitfieldMask());
    shl(src, opcode.bf_dst_bit);
    or_(dst, src);

    Compile_ProcessResult(opcode.result_operation, opcode.dst);
}

void MacroJITx64Impl::Compile_ExtractShiftLeftImmediate(Macro::Opcode opcode) {
    const auto dst = Compile_GetRegister(opcode.src_a, ecx);
    const auto src = Compile_GetRegister(opcode.src_b, RESULT);

    shr(src, dst.cvt8());
    and_(src, opcode.GetBitfieldMask());
    shl(src, opcode.bf_dst_bit);

    Compile_ProcessResult(opcode.result_operation, opcode.dst);
}

void MacroJITx64Impl::Compile_ExtractShiftLeftRegister(Macro::Opcode opcode) {
    const auto dst = Compile_GetRegister(opcode.src_a, ecx);
    const auto src = Compile_GetRegister(opcode.src_b, RESULT);

    shr(src, opcode.bf_src_bit);
    and_(src, opcode.GetBitfieldMask());
    shl(src, dst.cvt8());

    Compile_ProcessResult(opcode.result_operation, opcode.dst);
}

void MacroJITx64Impl::Compile_Read(Macro::Opcode opcode) {
    if (optimizer.zero_reg_skip && opcode.src_a == 0) {
        if (opcode.immediate == 0) {
            xor_(RESULT, RESULT);
        } else {
            mov(RESULT, opcode.immediate);
        }
    } else {
        auto result = Compile_GetRegister(opcode.src_a, RESULT);
        if (opcode.immediate > 2) {
            add(result, opcode.immediate);
        } else if (opcode.immediate == 1) {
            inc(result);
        } else if (opcode.immediate < 0) {
            sub(result, opcode.immediate * -1);
        }
    }

    // Equivalent to Engines::Maxwell3D::GetRegisterValue:
    if (optimizer.enable_asserts) {
        Xbyak::Label pass_range_check;
        cmp(RESULT, static_cast<u32>(Engines::Maxwell3D::Regs::NUM_REGS));
        jb(pass_range_check);
        int3();
        L(pass_range_check);
    }
    mov(rax, qword[STATE]);
    mov(RESULT,
        dword[rax + offsetof(Engines::Maxwell3D, regs) +
              offsetof(Engines::Maxwell3D::Regs, reg_array) + RESULT.cvt64() * sizeof(u32)]);

    Compile_ProcessResult(opcode.result_operation, opcode.dst);
}

static void MacroJIT_SendThunk(Engines::Maxwell3D* maxwell3d, Macro::MethodAddress method_address, u32 value) {
    maxwell3d->CallMethod(method_address.address, value, true);
}

void MacroJITx64Impl::Compile_Send(Xbyak::Reg32 value) {
    Common::X64::ABI_PushRegistersAndAdjustStack(*this, PersistentCallerSavedRegs(), 0);
    mov(Common::X64::ABI_PARAM1, qword[STATE]);
    mov(Common::X64::ABI_PARAM2.cvt32(), METHOD_ADDRESS);
    mov(Common::X64::ABI_PARAM3.cvt32(), value);
    Common::X64::CallFarFunction(*this, &MacroJIT_SendThunk);
    Common::X64::ABI_PopRegistersAndAdjustStack(*this, PersistentCallerSavedRegs(), 0);

    Xbyak::Label dont_process{};
    // Get increment
    test(METHOD_ADDRESS, 0x3f000);
    // If zero, method address doesn't update
    je(dont_process);

    mov(ecx, METHOD_ADDRESS);
    and_(METHOD_ADDRESS, 0xfff);
    shr(ecx, 12);
    and_(ecx, 0x3f);
    lea(eax, ptr[rcx + METHOD_ADDRESS.cvt64()]);
    sal(ecx, 12);
    or_(eax, ecx);

    mov(METHOD_ADDRESS, eax);

    L(dont_process);
}

void MacroJITx64Impl::Compile_Branch(Macro::Opcode opcode) {
    ASSERT_MSG(!is_delay_slot, "Executing a branch in a delay slot is not valid");
    const s32 jump_address =
        static_cast<s32>(pc) + static_cast<s32>(opcode.GetBranchTarget() / sizeof(s32));

    Xbyak::Label end;
    auto value = Compile_GetRegister(opcode.src_a, eax);
    cmp(value, 0); // test(value, value);
    if (optimizer.has_delayed_pc) {
        switch (opcode.branch_condition) {
        case Macro::BranchCondition::Zero:
            jne(end, T_NEAR);
            break;
        case Macro::BranchCondition::NotZero:
            je(end, T_NEAR);
            break;
        }

        if (opcode.branch_annul) {
            xor_(BRANCH_HOLDER, BRANCH_HOLDER);
            jmp(labels[jump_address], T_NEAR);
        } else {
            Xbyak::Label handle_post_exit{};
            Xbyak::Label skip{};
            jmp(skip, T_NEAR);

            L(handle_post_exit);
            xor_(BRANCH_HOLDER, BRANCH_HOLDER);
            jmp(labels[jump_address], T_NEAR);

            L(skip);
            mov(BRANCH_HOLDER, handle_post_exit);
            jmp(delay_skip[pc], T_NEAR);
        }
    } else {
        switch (opcode.branch_condition) {
        case Macro::BranchCondition::Zero:
            je(labels[jump_address], T_NEAR);
            break;
        case Macro::BranchCondition::NotZero:
            jne(labels[jump_address], T_NEAR);
            break;
        }
    }

    L(end);
}

void MacroJITx64Impl::Optimizer_ScanFlags() {
    optimizer.can_skip_carry = true;
    optimizer.has_delayed_pc = false;
    for (auto raw_op : code) {
        Macro::Opcode op{};
        op.raw = raw_op;

        if (op.operation == Macro::Operation::ALU) {
            // Scan for any ALU operations which actually use the carry flag, if they don't exist in
            // our current code we can skip emitting the carry flag handling operations
            if (op.alu_operation == Macro::ALUOperation::AddWithCarry ||
                op.alu_operation == Macro::ALUOperation::SubtractWithBorrow) {
                optimizer.can_skip_carry = false;
            }
        }

        if (op.operation == Macro::Operation::Branch) {
            if (!op.branch_annul) {
                optimizer.has_delayed_pc = true;
            }
        }
    }
}

void MacroJITx64Impl::Compile() {
    labels.fill(Xbyak::Label());

    Common::X64::ABI_PushRegistersAndAdjustStack(*this, Common::X64::ABI_ALL_CALLEE_SAVED, 8);
    // JIT state
    mov(STATE, Common::X64::ABI_PARAM1);
    mov(PARAMETERS, Common::X64::ABI_PARAM2);
    mov(MAX_PARAMETER, Common::X64::ABI_PARAM3);
    xor_(RESULT, RESULT);
    xor_(METHOD_ADDRESS, METHOD_ADDRESS);
    xor_(BRANCH_HOLDER, BRANCH_HOLDER);

    mov(dword[STATE + offsetof(JITState, registers) + 4], Compile_FetchParameter());

    // Track get register for zero registers and mark it as no-op
    optimizer.zero_reg_skip = true;

    // AddImmediate tends to be used as a NOP instruction, if we detect this we can
    // completely skip the entire code path and no emit anything
    optimizer.skip_dummy_addimmediate = true;

    // SMO tends to emit a lot of unnecessary method moves, we can mitigate this by only emitting
    // one if our register isn't "dirty"
    optimizer.optimize_for_method_move = true;

    // Enable run-time assertions in JITted code
    optimizer.enable_asserts = false;

    // Check to see if we can skip emitting certain instructions
    Optimizer_ScanFlags();

    const u32 op_count = static_cast<u32>(code.size());
    for (u32 i = 0; i < op_count; i++) {
        if (i < op_count - 1) {
            pc = i + 1;
            next_opcode = GetOpCode();
        } else {
            next_opcode = {};
        }
        pc = i;
        Compile_NextInstruction();
    }

    L(end_of_code);

    Common::X64::ABI_PopRegistersAndAdjustStack(*this, Common::X64::ABI_ALL_CALLEE_SAVED, 8);
    ret();
    ready();
    program = getCode<ProgramType>();
}

bool MacroJITx64Impl::Compile_NextInstruction() {
    const auto opcode = GetOpCode();
    if (labels[pc].getAddress()) {
        return false;
    }

    L(labels[pc]);

    switch (opcode.operation) {
    case Macro::Operation::ALU:
        Compile_ALU(opcode);
        break;
    case Macro::Operation::AddImmediate:
        Compile_AddImmediate(opcode);
        break;
    case Macro::Operation::ExtractInsert:
        Compile_ExtractInsert(opcode);
        break;
    case Macro::Operation::ExtractShiftLeftImmediate:
        Compile_ExtractShiftLeftImmediate(opcode);
        break;
    case Macro::Operation::ExtractShiftLeftRegister:
        Compile_ExtractShiftLeftRegister(opcode);
        break;
    case Macro::Operation::Read:
        Compile_Read(opcode);
        break;
    case Macro::Operation::Branch:
        Compile_Branch(opcode);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented opcode {}", opcode.operation.Value());
        break;
    }

    if (optimizer.has_delayed_pc) {
        if (opcode.is_exit) {
            mov(rax, end_of_code);
            test(BRANCH_HOLDER, BRANCH_HOLDER);
            cmove(BRANCH_HOLDER, rax);
            // Jump to next instruction to skip delay slot check
            je(labels[pc + 1], T_NEAR);
        } else {
            // TODO(ogniK): Optimize delay slot branching
            Xbyak::Label no_delay_slot{};
            test(BRANCH_HOLDER, BRANCH_HOLDER);
            je(no_delay_slot, T_NEAR);
            mov(rax, BRANCH_HOLDER);
            xor_(BRANCH_HOLDER, BRANCH_HOLDER);
            jmp(rax);
            L(no_delay_slot);
        }
        L(delay_skip[pc]);
        if (opcode.is_exit) {
            return false;
        }
    } else {
        test(BRANCH_HOLDER, BRANCH_HOLDER);
        jne(end_of_code, T_NEAR);
        if (opcode.is_exit) {
            inc(BRANCH_HOLDER);
            return false;
        }
    }
    return true;
}

static void MacroJIT_ErrorThunk(uintptr_t parameter, uintptr_t max_parameter) {
    LOG_CRITICAL(HW_GPU, "Macro JIT: invalid parameter access 0x{:x} (0x{:x} is the last parameter)", parameter, max_parameter - sizeof(u32));
}

Xbyak::Reg32 MacroJITx64Impl::Compile_FetchParameter() {
    Xbyak::Label parameter_ok{};
    cmp(PARAMETERS, MAX_PARAMETER);
    jb(parameter_ok, T_NEAR);
    Common::X64::ABI_PushRegistersAndAdjustStack(*this, PersistentCallerSavedRegs(), 0);
    mov(Common::X64::ABI_PARAM1, PARAMETERS);
    mov(Common::X64::ABI_PARAM2, MAX_PARAMETER);
    Common::X64::CallFarFunction(*this, &MacroJIT_ErrorThunk);
    Common::X64::ABI_PopRegistersAndAdjustStack(*this, PersistentCallerSavedRegs(), 0);
    L(parameter_ok);
    mov(eax, dword[PARAMETERS]);
    add(PARAMETERS, sizeof(u32));
    return eax;
}

Xbyak::Reg32 MacroJITx64Impl::Compile_GetRegister(u32 index, Xbyak::Reg32 dst) {
    if (index == 0) {
        // Register 0 is always zero
        xor_(dst, dst);
    } else {
        mov(dst, dword[STATE + offsetof(JITState, registers) + index * sizeof(u32)]);
    }

    return dst;
}

void MacroJITx64Impl::Compile_ProcessResult(Macro::ResultOperation operation, u32 reg) {
    const auto SetRegister = [this](u32 reg_index, const Xbyak::Reg32& result) {
        // Register 0 is supposed to always return 0. NOP is implemented as a store to the zero
        // register.
        if (reg_index == 0) {
            return;
        }
        mov(dword[STATE + offsetof(JITState, registers) + reg_index * sizeof(u32)], result);
    };
    const auto SetMethodAddress = [this](const Xbyak::Reg32& reg32) { mov(METHOD_ADDRESS, reg32); };

    switch (operation) {
    case Macro::ResultOperation::IgnoreAndFetch:
        SetRegister(reg, Compile_FetchParameter());
        break;
    case Macro::ResultOperation::Move:
        SetRegister(reg, RESULT);
        break;
    case Macro::ResultOperation::MoveAndSetMethod:
        SetRegister(reg, RESULT);
        SetMethodAddress(RESULT);
        break;
    case Macro::ResultOperation::FetchAndSend:
        // Fetch parameter and send result.
        SetRegister(reg, Compile_FetchParameter());
        Compile_Send(RESULT);
        break;
    case Macro::ResultOperation::MoveAndSend:
        // Move and send result.
        SetRegister(reg, RESULT);
        Compile_Send(RESULT);
        break;
    case Macro::ResultOperation::FetchAndSetMethod:
        // Fetch parameter and use result as Method Address.
        SetRegister(reg, Compile_FetchParameter());
        SetMethodAddress(RESULT);
        break;
    case Macro::ResultOperation::MoveAndSetMethodFetchAndSend:
        // Move result and use as Method Address, then fetch and send parameter.
        SetRegister(reg, RESULT);
        SetMethodAddress(RESULT);
        Compile_Send(Compile_FetchParameter());
        break;
    case Macro::ResultOperation::MoveAndSetMethodSend:
        // Move result and use as Method Address, then send bits 12:17 of result.
        SetRegister(reg, RESULT);
        SetMethodAddress(RESULT);
        shr(RESULT, 12);
        and_(RESULT, 0b111111);
        Compile_Send(RESULT);
        break;
    default:
        UNIMPLEMENTED_MSG("Unimplemented macro operation {}", operation);
        break;
    }
}

Macro::Opcode MacroJITx64Impl::GetOpCode() const {
    ASSERT(pc < code.size());
    return {code[pc]};
}
} // Anonymous namespace
#endif

static void Dump(u64 hash, std::span<const u32> code, bool decompiled = false) {
    const auto base_dir{Common::FS::GetCitronPath(Common::FS::CitronPath::DumpDir)};
    const auto macro_dir{base_dir / "macros"};
    if (!Common::FS::CreateDir(base_dir) || !Common::FS::CreateDir(macro_dir)) {
        LOG_ERROR(Common_Filesystem, "Failed to create macro dump directories");
        return;
    }
    auto name{macro_dir / fmt::format("{:016x}.macro", hash)};

    if (decompiled) {
        auto new_name{macro_dir / fmt::format("decompiled_{:016x}.macro", hash)};
        if (Common::FS::Exists(name)) {
            (void)Common::FS::RenameFile(name, new_name);
            return;
        }
        name = new_name;
    }

    std::fstream macro_file(name, std::ios::out | std::ios::binary);
    if (!macro_file) {
        LOG_ERROR(Common_Filesystem, "Unable to open or create file at {}", Common::FS::PathToUTF8String(name));
        return;
    }
    macro_file.write(reinterpret_cast<const char*>(code.data()), code.size_bytes());
}

void MacroEngine::Execute(Engines::Maxwell3D& maxwell3d, u32 method, std::span<const u32> parameters) {
    auto const execute_variant = [&maxwell3d, &parameters, method](AnyCachedMacro& acm) {
        if (auto a = std::get_if<HLE_DrawArraysIndirect>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_DrawIndexedIndirect>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_MultiDrawIndexedIndirectCount>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_MultiLayerClear>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_C713C83D8F63CCF3>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_D7333D26E0A93EDE>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_BindShader>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_SetRasterBoundingBox>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_ClearConstBuffer>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_ClearMemory>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_TransformFeedbackSetup>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<HLE_DrawIndirectByteCount>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<MacroInterpreterImpl>(&acm))
            a->Execute(maxwell3d, parameters, method);
        if (auto a = std::get_if<std::unique_ptr<DynamicCachedMacro>>(&acm))
            a->get()->Execute(maxwell3d, parameters, method);
    };
    if (auto const it = macro_cache.find(method); it != macro_cache.end()) {
        auto& ci = it->second;
        if (!CanBeHLEProgram(ci.hash) || Settings::values.disable_macro_hle)
            maxwell3d.RefreshParameters(); //LLE must reload parameters
        execute_variant(ci.program);
    } else {
        // Macro not compiled, check if it's uploaded and if so, compile it
        std::optional<u32> mid_method;
        const auto macro_code = uploaded_macro_code.find(method);
        if (macro_code == uploaded_macro_code.end()) {
            for (const auto& [method_base, code] : uploaded_macro_code) {
                if (method >= method_base && (method - method_base) < code.size()) {
                    mid_method = method_base;
                    break;
                }
            }
            if (!mid_method.has_value()) {
                ASSERT_MSG(false, "Macro 0x{0:x} was not uploaded", method);
                return;
            }
        }
        auto& ci = macro_cache[method];
        if (mid_method) {
            const auto& macro_cached = uploaded_macro_code[mid_method.value()];
            const auto rebased_method = method - mid_method.value();
            auto& code = uploaded_macro_code[method];
            code.resize(macro_cached.size() - rebased_method);
            std::memcpy(code.data(), macro_cached.data() + rebased_method, code.size() * sizeof(u32));
            ci.hash = Common::HashValue(code);
            ci.program = Compile(maxwell3d, code);
        } else {
            ci.program = Compile(maxwell3d, macro_code->second);
            ci.hash = Common::HashValue(macro_code->second);
        }
        if (CanBeHLEProgram(ci.hash) && !Settings::values.disable_macro_hle) {
            ci.program = GetHLEProgram(ci.hash);
        } else {
            maxwell3d.RefreshParameters();
        }
        execute_variant(ci.program);
        if (Settings::values.dump_macros) {
            Dump(ci.hash, macro_code->second, !std::holds_alternative<std::monostate>(ci.program));
        }
    }
}

AnyCachedMacro MacroEngine::Compile(Engines::Maxwell3D& maxwell3d, std::span<const u32> code) {
#ifdef ARCHITECTURE_x86_64
    if (!is_interpreted)
        return std::make_unique<MacroJITx64Impl>(code);
#endif
    return MacroInterpreterImpl(code);
}

} // namespace Tegra
