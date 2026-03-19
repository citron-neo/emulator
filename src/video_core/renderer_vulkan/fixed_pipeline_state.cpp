// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>

#include "common/bit_cast.h"
#include "common/cityhash.h"
#include "common/common_types.h"
#include <ranges>
#include "common/settings.h"
#include "video_core/engines/draw_manager.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/vk_state_tracker.h"

namespace Vulkan {
namespace {
void RefreshXfbState(VideoCommon::TransformFeedbackState& state, const Tegra::Engines::Maxwell3D::Regs& regs) {
    std::ranges::transform(regs.transform_feedback.controls, state.layouts.begin(),
                           [](const auto& layout) {
                               return VideoCommon::TransformFeedbackState::Layout{
                                   .stream = layout.stream,
                                   .varying_count = layout.varying_count,
                                   .stride = layout.stride,
                               };
                           });
    state.varyings = regs.stream_out_layout;
}
} // Anonymous namespace

void FixedPipelineState::Refresh(Tegra::Engines::Maxwell3D& maxwell3d, DynamicFeatures& features) {
    const Tegra::Engines::Maxwell3D::Regs& regs = maxwell3d.regs;
    const auto topology_ = maxwell3d.draw_manager->GetDrawState().topology;

    raw1 = 0;
    extended_dynamic_state.Assign(features.has_extended_dynamic_state ? 1 : 0);
    extended_dynamic_state_2.Assign(features.has_extended_dynamic_state_2 ? 1 : 0);
    extended_dynamic_state_2_extra.Assign(features.has_extended_dynamic_state_2_extra ? 1 : 0);
    extended_dynamic_state_3_blend.Assign(features.has_extended_dynamic_state_3_blend ? 1 : 0);
    extended_dynamic_state_3_enables.Assign(features.has_extended_dynamic_state_3_enables ? 1 : 0);
    dynamic_vertex_input.Assign(features.has_dynamic_vertex_input ? 1 : 0);
    xfb_enabled.Assign(regs.transform_feedback_enabled != 0);
    ndc_minus_one_to_one.Assign(regs.depth_mode == Tegra::Engines::Maxwell3D::Regs::DepthMode::MinusOneToOne ? 1 : 0);
    polygon_mode.Assign(PackPolygonMode(regs.polygon_mode_front));
    tessellation_primitive.Assign(static_cast<u32>(regs.tessellation.params.domain_type.Value()));
    tessellation_spacing.Assign(static_cast<u32>(regs.tessellation.params.spacing.Value()));
    tessellation_clockwise.Assign(regs.tessellation.params.output_primitives.Value() ==
                                  Tegra::Engines::Maxwell3D::Regs::Tessellation::OutputPrimitives::Triangles_CW);
    patch_control_points_minus_one.Assign(regs.patch_vertices - 1);
    topology.Assign(topology_);
    msaa_mode.Assign(regs.anti_alias_samples_mode);

    raw2 = 0;

    const auto test_func =
        regs.alpha_test_enabled != 0 ? regs.alpha_test_func : Tegra::Engines::Maxwell3D::Regs::ComparisonOp::Always_GL;
    alpha_test_func.Assign(PackComparisonOp(test_func));
    early_z.Assign(regs.mandated_early_z != 0 ? 1 : 0);
    depth_enabled.Assign(regs.zeta_enable != 0 ? 1 : 0);
    depth_format.Assign(static_cast<u32>(regs.zeta.format));
    y_negate.Assign(regs.window_origin.mode != Tegra::Engines::Maxwell3D::Regs::WindowOrigin::Mode::UpperLeft ? 1 : 0);
    provoking_vertex_last.Assign(regs.provoking_vertex == Tegra::Engines::Maxwell3D::Regs::ProvokingVertex::Last ? 1 : 0);
    conservative_raster_enable.Assign(regs.conservative_raster_enable != 0 ? 1 : 0);
    smooth_lines.Assign(regs.line_anti_alias_enable != 0 ? 1 : 0);
    alpha_to_coverage_enabled.Assign(regs.anti_alias_alpha_control.alpha_to_coverage != 0 ? 1 : 0);
    alpha_to_one_enabled.Assign(regs.anti_alias_alpha_control.alpha_to_one != 0 ? 1 : 0);
    app_stage.Assign(maxwell3d.engine_state);

    for (size_t i = 0; i < regs.rt.size(); ++i) {
        color_formats[i] = static_cast<u8>(regs.rt[i].format);
    }
    alpha_test_ref = Common::BitCast<u32>(regs.alpha_test_ref);
    point_size = Common::BitCast<u32>(regs.point_size);

    if (maxwell3d.dirty.flags[Dirty::VertexInput]) {
        if (features.has_dynamic_vertex_input) {
            // Dirty flag will be reset by the command buffer update
            static constexpr std::array LUT{
                0u, // Invalid
                1u, // SignedNorm
                1u, // UnsignedNorm
                2u, // SignedInt
                3u, // UnsignedInt
                1u, // UnsignedScaled
                1u, // SignedScaled
                1u, // Float
            };
            const auto& attrs = regs.vertex_attrib_format;
            attribute_types = 0;
            for (size_t i = 0; i < Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes; ++i) {
                const u32 mask = attrs[i].constant != 0 ? 0 : 3;
                const u32 type = LUT[static_cast<size_t>(attrs[i].type.Value())];
                attribute_types |= static_cast<u64>(type & mask) << (i * 2);
            }
        } else {
            maxwell3d.dirty.flags[Dirty::VertexInput] = false;
            enabled_divisors = 0;
            for (size_t index = 0; index < Tegra::Engines::Maxwell3D::Regs::NumVertexArrays; ++index) {
                const bool is_enabled = regs.vertex_stream_instances.IsInstancingEnabled(index);
                binding_divisors[index] = is_enabled ? regs.vertex_streams[index].frequency : 0;
                enabled_divisors |= (is_enabled ? u64{1} : 0) << index;
            }
            for (size_t index = 0; index < Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes; ++index) {
                const auto& input = regs.vertex_attrib_format[index];
                auto& attribute = attributes[index];
                attribute.raw = 0;
                attribute.enabled.Assign(input.constant ? 0 : 1);
                attribute.buffer.Assign(input.buffer);
                attribute.offset.Assign(input.offset);
                attribute.type.Assign(static_cast<u32>(input.type.Value()));
                attribute.size.Assign(static_cast<u32>(input.size.Value()));
            }
        }
    }
    if (maxwell3d.dirty.flags[Dirty::ViewportSwizzles]) {
        maxwell3d.dirty.flags[Dirty::ViewportSwizzles] = false;
        const auto& transform = regs.viewport_transform;
        std::ranges::transform(transform, viewport_swizzles.begin(), [](const auto& viewport) {
            return static_cast<u16>(viewport.swizzle.raw);
        });
    }
    dynamic_state.raw1 = 0;
    dynamic_state.raw2 = 0;
    if (!extended_dynamic_state) {
        dynamic_state.Refresh(regs);
        std::ranges::transform(regs.vertex_streams, vertex_strides.begin(), [](const auto& array) {
            return static_cast<u16>(array.stride.Value());
        });
    }
    if (!extended_dynamic_state_2_extra) {
        dynamic_state.Refresh2(regs, topology_, extended_dynamic_state_2);
    }
    if (!extended_dynamic_state_3_blend) {
        if (maxwell3d.dirty.flags[Dirty::Blending]) {
            maxwell3d.dirty.flags[Dirty::Blending] = false;
            for (size_t index = 0; index < attachments.size(); ++index) {
                attachments[index].Refresh(regs, index);
            }
        }
    }
    if (!extended_dynamic_state_3_enables) {
        dynamic_state.Refresh3(regs);
    }
    if (xfb_enabled) {
        RefreshXfbState(xfb_state, regs);
    }
}

void FixedPipelineState::BlendingAttachment::Refresh(const Tegra::Engines::Maxwell3D::Regs& regs, size_t index) {
    const auto& mask = regs.color_mask[regs.color_mask_common ? 0 : index];

    raw = 0;
    mask_r.Assign(mask.R);
    mask_g.Assign(mask.G);
    mask_b.Assign(mask.B);
    mask_a.Assign(mask.A);

    // TODO: C++20 Use templated lambda to deduplicate code
    if (!regs.blend.enable[index]) {
        return;
    }

    const auto setup_blend = [&]<typename T>(const T& src) {
        equation_rgb.Assign(PackBlendEquation(src.color_op));
        equation_a.Assign(PackBlendEquation(src.alpha_op));
        factor_source_rgb.Assign(PackBlendFactor(src.color_source));
        factor_dest_rgb.Assign(PackBlendFactor(src.color_dest));
        factor_source_a.Assign(PackBlendFactor(src.alpha_source));
        factor_dest_a.Assign(PackBlendFactor(src.alpha_dest));
        enable.Assign(1);
    };

    if (!regs.blend_per_target_enabled) {
        setup_blend(regs.blend);
        return;
    }
    setup_blend(regs.blend_per_target[index]);
}

void FixedPipelineState::DynamicState::Refresh(const Tegra::Engines::Maxwell3D::Regs& regs) {
    u32 packed_front_face = PackFrontFace(regs.gl_front_face);
    if (regs.window_origin.flip_y != 0) {
        // Flip front face
        packed_front_face = 1 - packed_front_face;
    }

    front.action_stencil_fail.Assign(PackStencilOp(regs.stencil_front_op.fail));
    front.action_depth_fail.Assign(PackStencilOp(regs.stencil_front_op.zfail));
    front.action_depth_pass.Assign(PackStencilOp(regs.stencil_front_op.zpass));
    front.test_func.Assign(PackComparisonOp(regs.stencil_front_op.func));
    if (regs.stencil_two_side_enable) {
        back.action_stencil_fail.Assign(PackStencilOp(regs.stencil_back_op.fail));
        back.action_depth_fail.Assign(PackStencilOp(regs.stencil_back_op.zfail));
        back.action_depth_pass.Assign(PackStencilOp(regs.stencil_back_op.zpass));
        back.test_func.Assign(PackComparisonOp(regs.stencil_back_op.func));
    } else {
        back.action_stencil_fail.Assign(front.action_stencil_fail);
        back.action_depth_fail.Assign(front.action_depth_fail);
        back.action_depth_pass.Assign(front.action_depth_pass);
        back.test_func.Assign(front.test_func);
    }
    // OPTIMIZED FOR LOW GPU ACCURACY - disable stencil test for performance
    stencil_enable.Assign(Settings::IsGPULevelLow() ? 0 : regs.stencil_enable);
    depth_write_enable.Assign(regs.depth_write_enabled);
    // OPTIMIZED FOR LOW GPU ACCURACY - disable depth bounds test for performance
    depth_bounds_enable.Assign(Settings::IsGPULevelLow() ? 0 : regs.depth_bounds_enable);
    depth_test_enable.Assign(regs.depth_test_enable);
    front_face.Assign(packed_front_face);
    depth_test_func.Assign(PackComparisonOp(regs.depth_test_func));
    cull_face.Assign(PackCullFace(regs.gl_cull_face));
    cull_enable.Assign(regs.gl_cull_test_enabled != 0 ? 1 : 0);
}

void FixedPipelineState::DynamicState::Refresh2(const Tegra::Engines::Maxwell3D::Regs& regs,
                                                Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology_,
                                                bool base_features_supported) {
    logic_op.Assign(PackLogicOp(regs.logic_op.op));

    if (base_features_supported) {
        return;
    }

    const std::array enabled_lut{
        regs.polygon_offset_point_enable,
        regs.polygon_offset_line_enable,
        regs.polygon_offset_fill_enable,
    };
    const u32 topology_index = static_cast<u32>(topology_);

    rasterize_enable.Assign(regs.rasterize_enable != 0 ? 1 : 0);
    primitive_restart_enable.Assign(regs.primitive_restart.enabled != 0 ? 1 : 0);

    constexpr size_t POLYGON_ENABLE_POINT = 0;
    constexpr size_t POLYGON_ENABLE_LINE = 1;
    constexpr size_t POLYGON_ENABLE_POLYGON = 2;
    constexpr std::array POLYGON_OFFSET_ENABLE_LUT = {
        POLYGON_ENABLE_POINT,   // Points
        POLYGON_ENABLE_LINE,    // Lines
        POLYGON_ENABLE_LINE,    // LineLoop
        POLYGON_ENABLE_LINE,    // LineStrip
        POLYGON_ENABLE_POLYGON, // Triangles
        POLYGON_ENABLE_POLYGON, // TriangleStrip
        POLYGON_ENABLE_POLYGON, // TriangleFan
        POLYGON_ENABLE_POLYGON, // Quads
        POLYGON_ENABLE_POLYGON, // QuadStrip
        POLYGON_ENABLE_POLYGON, // Polygon
        POLYGON_ENABLE_LINE,    // LinesAdjacency
        POLYGON_ENABLE_LINE,    // LineStripAdjacency
        POLYGON_ENABLE_POLYGON, // TrianglesAdjacency
        POLYGON_ENABLE_POLYGON, // TriangleStripAdjacency
        POLYGON_ENABLE_POLYGON, // Patches
    };
    depth_bias_enable.Assign(enabled_lut[POLYGON_OFFSET_ENABLE_LUT[topology_index]] != 0 ? 1 : 0);
}

void FixedPipelineState::DynamicState::Refresh3(const Tegra::Engines::Maxwell3D::Regs& regs) {
    logic_op_enable.Assign(regs.logic_op.enable != 0 ? 1 : 0);
    depth_clamp_disabled.Assign(regs.viewport_clip_control.geometry_clip ==
                                    Tegra::Engines::Maxwell3D::Regs::ViewportClipControl::GeometryClip::Passthrough ||
                                regs.viewport_clip_control.geometry_clip ==
                                    Tegra::Engines::Maxwell3D::Regs::ViewportClipControl::GeometryClip::FrustumXYZ ||
                                regs.viewport_clip_control.geometry_clip ==
                                    Tegra::Engines::Maxwell3D::Regs::ViewportClipControl::GeometryClip::FrustumZ);
}

size_t FixedPipelineState::Hash() const noexcept {
    const u64 hash = Common::CityHash64(reinterpret_cast<const char*>(this), Size());
    return static_cast<size_t>(hash);
}

bool FixedPipelineState::operator==(const FixedPipelineState& rhs) const noexcept {
    return std::memcmp(this, &rhs, Size()) == 0;
}

u32 FixedPipelineState::PackComparisonOp(Tegra::Engines::Maxwell3D::Regs::ComparisonOp op) noexcept {
    // OpenGL enums go from 0x200 to 0x207 and the others from 1 to 8
    // If we subtract 0x200 to OpenGL enums and 1 to the others we get a 0-7 range.
    // Perfect for a hash.
    const u32 value = static_cast<u32>(op);
    return value - (value >= 0x200 ? 0x200 : 1);
}

Tegra::Engines::Maxwell3D::Regs::ComparisonOp FixedPipelineState::UnpackComparisonOp(u32 packed) noexcept {
    // Read PackComparisonOp for the logic behind this.
    return static_cast<Tegra::Engines::Maxwell3D::Regs::ComparisonOp>(packed + 1);
}

u32 FixedPipelineState::PackStencilOp(Tegra::Engines::Maxwell3D::Regs::StencilOp::Op op) noexcept {
    switch (op) {
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Keep_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Keep_GL:
        return 0;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Zero_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Zero_GL:
        return 1;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Replace_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Replace_GL:
        return 2;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::IncrSaturate_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::IncrSaturate_GL:
        return 3;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::DecrSaturate_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::DecrSaturate_GL:
        return 4;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Invert_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Invert_GL:
        return 5;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Incr_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Incr_GL:
        return 6;
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Decr_D3D:
    case Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Decr_GL:
        return 7;
    }
    return 0;
}

Tegra::Engines::Maxwell3D::Regs::StencilOp::Op FixedPipelineState::UnpackStencilOp(u32 packed) noexcept {
    static constexpr std::array LUT = {
        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Keep_D3D,         Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Zero_D3D,
        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Replace_D3D,      Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::IncrSaturate_D3D,
        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::DecrSaturate_D3D, Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Invert_D3D,
        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Incr_D3D,         Tegra::Engines::Maxwell3D::Regs::StencilOp::Op::Decr_D3D};
    return LUT[packed];
}

u32 FixedPipelineState::PackCullFace(Tegra::Engines::Maxwell3D::Regs::CullFace cull) noexcept {
    // FrontAndBack is 0x408, by subtracting 0x406 in it we get 2.
    // Individual cull faces are in 0x404 and 0x405, subtracting 0x404 we get 0 and 1.
    const u32 value = static_cast<u32>(cull);
    return value - (value == 0x408 ? 0x406 : 0x404);
}

Tegra::Engines::Maxwell3D::Regs::CullFace FixedPipelineState::UnpackCullFace(u32 packed) noexcept {
    static constexpr std::array LUT = {Tegra::Engines::Maxwell3D::Regs::CullFace::Front, Tegra::Engines::Maxwell3D::Regs::CullFace::Back,
                                       Tegra::Engines::Maxwell3D::Regs::CullFace::FrontAndBack};
    return LUT[packed];
}

u32 FixedPipelineState::PackFrontFace(Tegra::Engines::Maxwell3D::Regs::FrontFace face) noexcept {
    return static_cast<u32>(face) - 0x900;
}

Tegra::Engines::Maxwell3D::Regs::FrontFace FixedPipelineState::UnpackFrontFace(u32 packed) noexcept {
    return static_cast<Tegra::Engines::Maxwell3D::Regs::FrontFace>(packed + 0x900);
}

u32 FixedPipelineState::PackPolygonMode(Tegra::Engines::Maxwell3D::Regs::PolygonMode mode) noexcept {
    return static_cast<u32>(mode) - 0x1B00;
}

Tegra::Engines::Maxwell3D::Regs::PolygonMode FixedPipelineState::UnpackPolygonMode(u32 packed) noexcept {
    return static_cast<Tegra::Engines::Maxwell3D::Regs::PolygonMode>(packed + 0x1B00);
}

u32 FixedPipelineState::PackLogicOp(Tegra::Engines::Maxwell3D::Regs::LogicOp::Op op) noexcept {
    return static_cast<u32>(op) - 0x1500;
}

Tegra::Engines::Maxwell3D::Regs::LogicOp::Op FixedPipelineState::UnpackLogicOp(u32 packed) noexcept {
    return static_cast<Tegra::Engines::Maxwell3D::Regs::LogicOp::Op>(packed + 0x1500);
}

u32 FixedPipelineState::PackBlendEquation(Tegra::Engines::Maxwell3D::Regs::Blend::Equation equation) noexcept {
    switch (equation) {
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Add_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Add_GL:
        return 0;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Subtract_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Subtract_GL:
        return 1;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::ReverseSubtract_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::ReverseSubtract_GL:
        return 2;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Min_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Min_GL:
        return 3;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Max_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Max_GL:
        return 4;
    }
    return 0;
}

Tegra::Engines::Maxwell3D::Regs::Blend::Equation FixedPipelineState::UnpackBlendEquation(u32 packed) noexcept {
    static constexpr std::array LUT = {
        Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Add_D3D, Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Subtract_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Equation::ReverseSubtract_D3D, Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Min_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Equation::Max_D3D};
    return LUT[packed];
}

u32 FixedPipelineState::PackBlendFactor(Tegra::Engines::Maxwell3D::Regs::Blend::Factor factor) noexcept {
    switch (factor) {
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Zero_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Zero_GL:
        return 0;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::One_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::One_GL:
        return 1;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceColor_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceColor_GL:
        return 2;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSourceColor_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSourceColor_GL:
        return 3;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceAlpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceAlpha_GL:
        return 4;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSourceAlpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSourceAlpha_GL:
        return 5;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::DestAlpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::DestAlpha_GL:
        return 6;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusDestAlpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusDestAlpha_GL:
        return 7;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::DestColor_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::DestColor_GL:
        return 8;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusDestColor_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusDestColor_GL:
        return 9;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceAlphaSaturate_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceAlphaSaturate_GL:
        return 10;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Source1Color_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Source1Color_GL:
        return 11;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSource1Color_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSource1Color_GL:
        return 12;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Source1Alpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Source1Alpha_GL:
        return 13;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSource1Alpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSource1Alpha_GL:
        return 14;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::BlendFactor_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::ConstantColor_GL:
        return 15;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusBlendFactor_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusConstantColor_GL:
        return 16;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::BothSourceAlpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::ConstantAlpha_GL:
        return 17;
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusBothSourceAlpha_D3D:
    case Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusConstantAlpha_GL:
        return 18;
    }
    UNIMPLEMENTED_MSG("Unknown blend factor {}", static_cast<u32>(factor));
    return 0;
}

Tegra::Engines::Maxwell3D::Regs::Blend::Factor FixedPipelineState::UnpackBlendFactor(u32 packed) noexcept {
    static constexpr std::array LUT = {
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Zero_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::One_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceColor_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSourceColor_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceAlpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSourceAlpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::DestAlpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusDestAlpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::DestColor_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusDestColor_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::SourceAlphaSaturate_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Source1Color_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSource1Color_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::Source1Alpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusSource1Alpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::BlendFactor_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusBlendFactor_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::BothSourceAlpha_D3D,
        Tegra::Engines::Maxwell3D::Regs::Blend::Factor::OneMinusBothSourceAlpha_D3D,
    };
    ASSERT(packed < LUT.size());
    return LUT[packed];
}

} // namespace Vulkan
