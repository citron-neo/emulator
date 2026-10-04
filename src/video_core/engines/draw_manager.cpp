// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <exception>

#include "common/settings.h"
#include "video_core/arm64_register_guard.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/draw_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra::Engines {

namespace {

#if CITRON_ARM64_REGISTER_GUARD_SUPPORTED
struct DrawCorruptionTag;
struct DrawIndirectCorruptionTag;

thread_local std::exception_ptr rasterizer_draw_exception;

extern "C" __attribute__((noinline, no_stack_protector, used)) void
CitronRasterizerDrawThunk(VideoCore::RasterizerInterface* rasterizer, bool draw_indexed,
                          u32 instance_count) noexcept;

extern "C" __attribute__((naked, noinline)) u32
CitronRasterizerDrawPreservingRegisters(VideoCore::RasterizerInterface* rasterizer,
                                        bool draw_indexed, u32 instance_count);

extern "C" __attribute__((noinline, no_stack_protector, used)) void
CitronRasterizerDrawIndirectThunk(VideoCore::RasterizerInterface* rasterizer) noexcept;

extern "C" __attribute__((naked, noinline)) u32
CitronRasterizerDrawIndirectPreservingRegisters(VideoCore::RasterizerInterface* rasterizer);

extern "C" __attribute__((noinline, no_stack_protector, used)) void
CitronRasterizerDrawThunk(VideoCore::RasterizerInterface* rasterizer, bool draw_indexed,
                          u32 instance_count) noexcept {
    try {
        rasterizer->Draw(draw_indexed, instance_count);
    } catch (...) {
        rasterizer_draw_exception = std::current_exception();
    }
}

// Contain AArch64 ABI violations at the complete rasterizer draw boundary. Bits 0-9 report
// x19-x28 respectively; bits 10-11 report lower/upper guard damage; bit 12 reports x29;
// bits 13-14 report saved-link-register damage/no majority.
extern "C" __attribute__((naked, noinline)) u32
CitronRasterizerDrawPreservingRegisters(VideoCore::RasterizerInterface*, bool, u32) {
    CITRON_ARM64_PRESERVE_REGISTERS(CitronRasterizerDrawThunk);
}

extern "C" __attribute__((noinline, no_stack_protector, used)) void
CitronRasterizerDrawIndirectThunk(VideoCore::RasterizerInterface* rasterizer) noexcept {
    try {
        rasterizer->DrawIndirect();
    } catch (...) {
        rasterizer_draw_exception = std::current_exception();
    }
}

// Indirect draws enter the same Vulkan PrepareDraw path and require the same complete boundary.
extern "C" __attribute__((naked, noinline)) u32
CitronRasterizerDrawIndirectPreservingRegisters(VideoCore::RasterizerInterface*) {
    CITRON_ARM64_PRESERVE_REGISTERS(CitronRasterizerDrawIndirectThunk);
}
#endif

} // Anonymous namespace

DrawManager::DrawManager(Maxwell3D* maxwell3d_) : maxwell3d(maxwell3d_) {}

void DrawManager::ProcessMethodCall(u32 method, u32 argument) {
    const auto& regs{maxwell3d->regs};
    switch (method) {
    case MAXWELL3D_REG_INDEX(clear_surface):
        return Clear(1);
    case MAXWELL3D_REG_INDEX(draw.begin):
        return DrawBegin();
    case MAXWELL3D_REG_INDEX(draw.end):
        return DrawEnd();
    case MAXWELL3D_REG_INDEX(vertex_buffer.first):
    case MAXWELL3D_REG_INDEX(vertex_buffer.count):
    case MAXWELL3D_REG_INDEX(index_buffer.first):
        break;
    case MAXWELL3D_REG_INDEX(index_buffer.count):
        draw_state.draw_indexed = true;
        break;
    case MAXWELL3D_REG_INDEX(index_buffer32_subsequent):
    case MAXWELL3D_REG_INDEX(index_buffer16_subsequent):
    case MAXWELL3D_REG_INDEX(index_buffer8_subsequent):
        draw_state.instance_count++;
        [[fallthrough]];
    case MAXWELL3D_REG_INDEX(index_buffer32_first):
    case MAXWELL3D_REG_INDEX(index_buffer16_first):
    case MAXWELL3D_REG_INDEX(index_buffer8_first):
        return DrawIndexSmall(argument);
    case MAXWELL3D_REG_INDEX(draw_inline_index):
        SetInlineIndexBuffer(argument);
        break;
    case MAXWELL3D_REG_INDEX(inline_index_2x16.even):
        SetInlineIndexBuffer(regs.inline_index_2x16.even);
        SetInlineIndexBuffer(regs.inline_index_2x16.odd);
        break;
    case MAXWELL3D_REG_INDEX(inline_index_4x8.index0):
        SetInlineIndexBuffer(regs.inline_index_4x8.index0);
        SetInlineIndexBuffer(regs.inline_index_4x8.index1);
        SetInlineIndexBuffer(regs.inline_index_4x8.index2);
        SetInlineIndexBuffer(regs.inline_index_4x8.index3);
        break;
    case MAXWELL3D_REG_INDEX(vertex_array_instance_first):
        DrawArrayInstanced(regs.vertex_array_instance_first.topology.Value(),
                           regs.vertex_array_instance_first.start.Value(),
                           regs.vertex_array_instance_first.count.Value(), false);
        break;
    case MAXWELL3D_REG_INDEX(vertex_array_instance_subsequent): {
        DrawArrayInstanced(regs.vertex_array_instance_subsequent.topology.Value(),
                           regs.vertex_array_instance_subsequent.start.Value(),
                           regs.vertex_array_instance_subsequent.count.Value(), true);
        break;
    }
    case MAXWELL3D_REG_INDEX(draw_texture.src_y0): {
        DrawTexture();
        break;
    }
    default:
        break;
    }
}

void DrawManager::Clear(u32 layer_count) {
    if (maxwell3d->ShouldExecute()) {
        maxwell3d->rasterizer->Clear(layer_count);
    }
}

void DrawManager::DrawDeferred() {
    if (draw_state.draw_mode != DrawMode::Instance || draw_state.instance_count == 0) {
        return;
    }
    DrawEnd(draw_state.instance_count + 1, true);
    draw_state.instance_count = 0;
}

void DrawManager::DrawArray(PrimitiveTopology topology, u32 vertex_first, u32 vertex_count,
                            u32 base_instance, u32 num_instances) {
    draw_state.topology = topology;
    draw_state.vertex_buffer.first = vertex_first;
    draw_state.vertex_buffer.count = vertex_count;
    draw_state.base_instance = base_instance;
    ProcessDraw(false, num_instances);
}

void DrawManager::DrawArrayInstanced(PrimitiveTopology topology, u32 vertex_first, u32 vertex_count,
                                     bool subsequent) {
    draw_state.topology = topology;
    draw_state.vertex_buffer.first = vertex_first;
    draw_state.vertex_buffer.count = vertex_count;

    if (!subsequent) {
        draw_state.instance_count = 1;
    }

    draw_state.base_instance = draw_state.instance_count - 1;
    draw_state.draw_mode = DrawMode::Instance;
    draw_state.instance_count++;
    ProcessDraw(false, 1);
}

void DrawManager::DrawIndex(PrimitiveTopology topology, u32 index_first, u32 index_count,
                            u32 base_index, u32 base_instance, u32 num_instances) {
    const auto& regs{maxwell3d->regs};
    draw_state.topology = topology;
    draw_state.index_buffer = regs.index_buffer;
    draw_state.index_buffer.first = index_first;
    draw_state.index_buffer.count = index_count;
    draw_state.base_index = base_index;
    draw_state.base_instance = base_instance;
    ProcessDraw(true, num_instances);
}

void DrawManager::DrawArrayIndirect(PrimitiveTopology topology) {
    draw_state.topology = topology;

    ProcessDrawIndirect();
}

void DrawManager::DrawIndexedIndirect(PrimitiveTopology topology, u32 index_first,
                                      u32 index_count) {
    const auto& regs{maxwell3d->regs};
    draw_state.topology = topology;
    draw_state.index_buffer = regs.index_buffer;
    draw_state.index_buffer.first = index_first;
    draw_state.index_buffer.count = index_count;

    ProcessDrawIndirect();
}

void DrawManager::SetInlineIndexBuffer(u32 index) {
    draw_state.inline_index_draw_indexes.push_back(static_cast<u8>(index & 0x000000ff));
    draw_state.inline_index_draw_indexes.push_back(static_cast<u8>((index & 0x0000ff00) >> 8));
    draw_state.inline_index_draw_indexes.push_back(static_cast<u8>((index & 0x00ff0000) >> 16));
    draw_state.inline_index_draw_indexes.push_back(static_cast<u8>((index & 0xff000000) >> 24));
    draw_state.draw_mode = DrawMode::InlineIndex;
}

void DrawManager::DrawBegin() {
    const auto& regs{maxwell3d->regs};
    auto reset_instance_count = regs.draw.instance_id == Maxwell3D::Regs::Draw::InstanceId::First;
    auto increment_instance_count =
        regs.draw.instance_id == Maxwell3D::Regs::Draw::InstanceId::Subsequent;
    if (reset_instance_count) {
        DrawDeferred();
        draw_state.instance_count = 0;
        draw_state.draw_mode = DrawMode::General;
    } else if (increment_instance_count) {
        draw_state.instance_count++;
        draw_state.draw_mode = DrawMode::Instance;
    }

    draw_state.topology = regs.draw.topology;
}

void DrawManager::DrawEnd(u32 instance_count, bool force_draw) {
    const auto& regs{maxwell3d->regs};
    switch (draw_state.draw_mode) {
    case DrawMode::Instance:
        if (!force_draw) {
            break;
        }
        [[fallthrough]];
    case DrawMode::General:
        draw_state.base_instance = regs.global_base_instance_index;
        draw_state.base_index = regs.global_base_vertex_index;
        if (draw_state.draw_indexed) {
            draw_state.index_buffer = regs.index_buffer;
            ProcessDraw(true, instance_count);
        } else {
            draw_state.vertex_buffer = regs.vertex_buffer;
            ProcessDraw(false, instance_count);
        }
        draw_state.draw_indexed = false;
        break;
    case DrawMode::InlineIndex:
        draw_state.base_instance = regs.global_base_instance_index;
        draw_state.base_index = regs.global_base_vertex_index;
        draw_state.index_buffer = regs.index_buffer;
        draw_state.index_buffer.count =
            static_cast<u32>(draw_state.inline_index_draw_indexes.size() / 4);
        draw_state.index_buffer.format = Maxwell3D::Regs::IndexFormat::UnsignedInt;
        maxwell3d->dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
        ProcessDraw(true, instance_count);
        draw_state.inline_index_draw_indexes.clear();
        break;
    }
}

void DrawManager::DrawIndexSmall(u32 argument) {
    const auto& regs{maxwell3d->regs};
    IndexBufferSmall index_small_params{argument};
    draw_state.base_instance = regs.global_base_instance_index;
    draw_state.base_index = regs.global_base_vertex_index;
    draw_state.index_buffer = regs.index_buffer;
    draw_state.index_buffer.first = index_small_params.first;
    draw_state.index_buffer.count = index_small_params.count;
    draw_state.topology = index_small_params.topology;
    maxwell3d->dirty.flags[VideoCommon::Dirty::IndexBuffer] = true;
    ProcessDraw(true, 1);
}

void DrawManager::DrawTexture() {
    const auto& regs{maxwell3d->regs};
    draw_texture_state.dst_x0 = static_cast<float>(regs.draw_texture.dst_x0) / 4096.f;
    draw_texture_state.dst_y0 = static_cast<float>(regs.draw_texture.dst_y0) / 4096.f;
    const auto dst_width = static_cast<float>(regs.draw_texture.dst_width) / 4096.f;
    const auto dst_height = static_cast<float>(regs.draw_texture.dst_height) / 4096.f;
    const bool lower_left{regs.window_origin.mode !=
                          Maxwell3D::Regs::WindowOrigin::Mode::UpperLeft};
    if (lower_left) {
        draw_texture_state.dst_y0 =
            static_cast<f32>(regs.surface_clip.height) - draw_texture_state.dst_y0;
    }
    draw_texture_state.dst_x1 = draw_texture_state.dst_x0 + dst_width;
    draw_texture_state.dst_y1 = draw_texture_state.dst_y0 + dst_height;
    draw_texture_state.src_x0 = static_cast<float>(regs.draw_texture.src_x0) / 4096.f;
    draw_texture_state.src_y0 = static_cast<float>(regs.draw_texture.src_y0) / 4096.f;
    draw_texture_state.src_x1 =
        (static_cast<float>(regs.draw_texture.dx_du) / 4294967296.f) * dst_width +
        draw_texture_state.src_x0;
    draw_texture_state.src_y1 =
        (static_cast<float>(regs.draw_texture.dy_dv) / 4294967296.f) * dst_height +
        draw_texture_state.src_y0;
    draw_texture_state.src_sampler = regs.draw_texture.src_sampler;
    draw_texture_state.src_texture = regs.draw_texture.src_texture;
    maxwell3d->rasterizer->DrawTexture();
}

void DrawManager::UpdateTopology() {
    const auto& regs{maxwell3d->regs};
    switch (regs.primitive_topology_control) {
    case PrimitiveTopologyControl::UseInBeginMethods:
        break;
    case PrimitiveTopologyControl::UseSeparateState:
        switch (regs.topology_override) {
        case PrimitiveTopologyOverride::None:
            break;
        case PrimitiveTopologyOverride::Points:
            draw_state.topology = PrimitiveTopology::Points;
            break;
        case PrimitiveTopologyOverride::Lines:
            draw_state.topology = PrimitiveTopology::Lines;
            break;
        case PrimitiveTopologyOverride::LineStrip:
            draw_state.topology = PrimitiveTopology::LineStrip;
            break;
        default:
            draw_state.topology = static_cast<PrimitiveTopology>(regs.topology_override);
            break;
        }
        break;
    }
}

void DrawManager::ProcessDraw(bool draw_indexed, u32 instance_count) {
    LOG_TRACE(HW_GPU, "called, topology={}, count={}", draw_state.topology,
              draw_indexed ? draw_state.index_buffer.count : draw_state.vertex_buffer.count);

    UpdateTopology();

    if (maxwell3d->ShouldExecute()) {
#if CITRON_ARM64_REGISTER_GUARD_SUPPORTED
        if (Settings::values.android_arm64_register_guards.GetValue()) {
            rasterizer_draw_exception = {};
            const u32 draw_corruption = CitronRasterizerDrawPreservingRegisters(
                maxwell3d->rasterizer, draw_indexed, instance_count);
            const std::exception_ptr draw_exception = rasterizer_draw_exception;
            rasterizer_draw_exception = {};
            if (draw_corruption != 0 &&
                VideoCore::IsFirstArm64RegisterCorruption<15, DrawCorruptionTag>(
                    draw_corruption)) {
                LOG_ERROR(
                    HW_GPU,
                    "ARM64 RasterizerInterface::Draw corrupted callee-saved state mask={:#x}",
                    draw_corruption);
            }
            if (draw_exception) {
                std::rethrow_exception(draw_exception);
            }
        } else {
            maxwell3d->rasterizer->Draw(draw_indexed, instance_count);
        }
#else
        maxwell3d->rasterizer->Draw(draw_indexed, instance_count);
#endif
    }
}

void DrawManager::ProcessDrawIndirect() {
    LOG_TRACE(
        HW_GPU,
        "called, topology={}, is_indexed={}, includes_count={}, buffer_size={}, max_draw_count={}",
        draw_state.topology, indirect_state.is_indexed, indirect_state.include_count,
        indirect_state.buffer_size, indirect_state.max_draw_counts);

    UpdateTopology();

    if (maxwell3d->ShouldExecute()) {
#if CITRON_ARM64_REGISTER_GUARD_SUPPORTED
        if (Settings::values.android_arm64_register_guards.GetValue()) {
            rasterizer_draw_exception = {};
            const u32 draw_corruption =
                CitronRasterizerDrawIndirectPreservingRegisters(maxwell3d->rasterizer);
            const std::exception_ptr draw_exception = rasterizer_draw_exception;
            rasterizer_draw_exception = {};
            if (draw_corruption != 0 &&
                VideoCore::IsFirstArm64RegisterCorruption<15, DrawIndirectCorruptionTag>(
                    draw_corruption)) {
                LOG_ERROR(HW_GPU,
                          "ARM64 RasterizerInterface::DrawIndirect corrupted callee-saved state "
                          "mask={:#x}",
                          draw_corruption);
            }
            if (draw_exception) {
                std::rethrow_exception(draw_exception);
            }
        } else {
            maxwell3d->rasterizer->DrawIndirect();
        }
#else
        maxwell3d->rasterizer->DrawIndirect();
#endif
    }
}
} // namespace Tegra::Engines
