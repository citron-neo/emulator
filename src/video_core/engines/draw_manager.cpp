// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/draw_manager.h"
#include "video_core/rasterizer_interface.h"

namespace Tegra::Engines {

namespace {

#if defined(ANDROID) && defined(ARCHITECTURE_arm64) && defined(__clang__)
extern "C" __attribute__((noinline, no_stack_protector, used)) void
CitronRasterizerDrawThunk(VideoCore::RasterizerInterface* rasterizer, bool draw_indexed,
                          u32 instance_count);

extern "C" __attribute__((naked, noinline)) u32
CitronRasterizerDrawPreservingRegisters(VideoCore::RasterizerInterface* rasterizer,
                                        bool draw_indexed, u32 instance_count);

extern "C" __attribute__((noinline, no_stack_protector, used)) void
CitronRasterizerDrawThunk(VideoCore::RasterizerInterface* rasterizer, bool draw_indexed,
                          u32 instance_count) {
    rasterizer->Draw(draw_indexed, instance_count);
}

// Contain AArch64 ABI violations at the complete rasterizer draw boundary. Bits 0-9 report
// x19-x28 respectively; bits 10-11 report lower/upper guard damage; bit 12 reports x29.
extern "C" __attribute__((naked, noinline)) u32
CitronRasterizerDrawPreservingRegisters(VideoCore::RasterizerInterface*, bool, u32) {
    asm volatile("sub sp, sp, #272\n"
                 "movz x9, #0x4752\n"
                 "movk x9, #0x4f4e, lsl #16\n"
                 "movk x9, #0x5452, lsl #32\n"
                 "movk x9, #0x4349, lsl #48\n"
                 "str x9, [sp, #0]\n"
                 "str x18, [sp, #8]\n"
                 "stp x19, x20, [sp, #16]\n"
                 "stp x21, x22, [sp, #32]\n"
                 "stp x23, x24, [sp, #48]\n"
                 "stp x25, x26, [sp, #64]\n"
                 "stp x27, x28, [sp, #80]\n"
                 "stp x29, x30, [sp, #96]\n"
                 "stp q8, q9, [sp, #128]\n"
                 "stp q10, q11, [sp, #160]\n"
                 "stp q12, q13, [sp, #192]\n"
                 "stp q14, q15, [sp, #224]\n"
                 "str x9, [sp, #256]\n"
                 "bl CitronRasterizerDrawThunk\n"
                 "mov w0, wzr\n"
                 "ldr x9, [sp, #16]\n"
                 "cmp x19, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10\n"
                 "ldr x9, [sp, #24]\n"
                 "cmp x20, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #1\n"
                 "ldr x9, [sp, #32]\n"
                 "cmp x21, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #2\n"
                 "ldr x9, [sp, #40]\n"
                 "cmp x22, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #3\n"
                 "ldr x9, [sp, #48]\n"
                 "cmp x23, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #4\n"
                 "ldr x9, [sp, #56]\n"
                 "cmp x24, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #5\n"
                 "ldr x9, [sp, #64]\n"
                 "cmp x25, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #6\n"
                 "ldr x9, [sp, #72]\n"
                 "cmp x26, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #7\n"
                 "ldr x9, [sp, #80]\n"
                 "cmp x27, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #8\n"
                 "ldr x9, [sp, #88]\n"
                 "cmp x28, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #9\n"
                 "ldr x9, [sp, #96]\n"
                 "cmp x29, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #12\n"
                 "movz x9, #0x4752\n"
                 "movk x9, #0x4f4e, lsl #16\n"
                 "movk x9, #0x5452, lsl #32\n"
                 "movk x9, #0x4349, lsl #48\n"
                 "ldr x10, [sp, #0]\n"
                 "cmp x10, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #10\n"
                 "ldr x10, [sp, #256]\n"
                 "cmp x10, x9\n"
                 "cset w10, ne\n"
                 "orr w0, w0, w10, lsl #11\n"
                 "ldp q14, q15, [sp, #224]\n"
                 "ldp q12, q13, [sp, #192]\n"
                 "ldp q10, q11, [sp, #160]\n"
                 "ldp q8, q9, [sp, #128]\n"
                 "ldp x29, x30, [sp, #96]\n"
                 "ldp x27, x28, [sp, #80]\n"
                 "ldp x25, x26, [sp, #64]\n"
                 "ldp x23, x24, [sp, #48]\n"
                 "ldp x21, x22, [sp, #32]\n"
                 "ldp x19, x20, [sp, #16]\n"
                 "ldr x18, [sp, #8]\n"
                 "add sp, sp, #272\n"
                 "ret\n");
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
#if defined(ANDROID) && defined(ARCHITECTURE_arm64) && defined(__clang__)
        const u32 draw_corruption = CitronRasterizerDrawPreservingRegisters(
            maxwell3d->rasterizer, draw_indexed, instance_count);
        if (draw_corruption != 0) {
            LOG_ERROR(HW_GPU,
                      "ARM64 RasterizerInterface::Draw corrupted callee-saved state mask={:#x}",
                      draw_corruption);
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
        maxwell3d->rasterizer->DrawIndirect();
    }
}
} // namespace Tegra::Engines
