// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <type_traits>

#include "common/bit_field.h"
#include "common/common_types.h"

#include "video_core/engines/maxwell_3d.h"
#include "video_core/surface.h"
#include "video_core/transform_feedback.h"

namespace Vulkan {

struct DynamicFeatures {
    bool has_extended_dynamic_state;
    bool has_extended_dynamic_state_2;
    bool has_extended_dynamic_state_2_extra;
    bool has_extended_dynamic_state_3_blend;
    bool has_extended_dynamic_state_3_enables;
    bool has_dynamic_vertex_input;
    bool has_transform_feedback;
};

struct FixedPipelineState {
    static u32 PackComparisonOp(Tegra::Engines::Maxwell3D::Regs::ComparisonOp op) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::ComparisonOp UnpackComparisonOp(u32 packed) noexcept;

    static u32 PackStencilOp(Tegra::Engines::Maxwell3D::Regs::StencilOp::Op op) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::StencilOp::Op UnpackStencilOp(u32 packed) noexcept;

    static u32 PackCullFace(Tegra::Engines::Maxwell3D::Regs::CullFace cull) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::CullFace UnpackCullFace(u32 packed) noexcept;

    static u32 PackFrontFace(Tegra::Engines::Maxwell3D::Regs::FrontFace face) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::FrontFace UnpackFrontFace(u32 packed) noexcept;

    static u32 PackPolygonMode(Tegra::Engines::Maxwell3D::Regs::PolygonMode mode) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::PolygonMode UnpackPolygonMode(u32 packed) noexcept;

    static u32 PackLogicOp(Tegra::Engines::Maxwell3D::Regs::LogicOp::Op op) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::LogicOp::Op UnpackLogicOp(u32 packed) noexcept;

    static u32 PackBlendEquation(Tegra::Engines::Maxwell3D::Regs::Blend::Equation equation) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::Blend::Equation UnpackBlendEquation(u32 packed) noexcept;

    static u32 PackBlendFactor(Tegra::Engines::Maxwell3D::Regs::Blend::Factor factor) noexcept;
    static Tegra::Engines::Maxwell3D::Regs::Blend::Factor UnpackBlendFactor(u32 packed) noexcept;

    struct BlendingAttachment {
        union {
            u32 raw;
            BitField<0, 1, u32> mask_r;
            BitField<1, 1, u32> mask_g;
            BitField<2, 1, u32> mask_b;
            BitField<3, 1, u32> mask_a;
            BitField<4, 3, u32> equation_rgb;
            BitField<7, 3, u32> equation_a;
            BitField<10, 5, u32> factor_source_rgb;
            BitField<15, 5, u32> factor_dest_rgb;
            BitField<20, 5, u32> factor_source_a;
            BitField<25, 5, u32> factor_dest_a;
            BitField<30, 1, u32> enable;
        };

        void Refresh(const Tegra::Engines::Maxwell3D::Regs& regs, size_t index);

        std::array<bool, 4> Mask() const noexcept {
            return {mask_r != 0, mask_g != 0, mask_b != 0, mask_a != 0};
        }

        Tegra::Engines::Maxwell3D::Regs::Blend::Equation EquationRGB() const noexcept {
            return UnpackBlendEquation(equation_rgb.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::Blend::Equation EquationAlpha() const noexcept {
            return UnpackBlendEquation(equation_a.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::Blend::Factor SourceRGBFactor() const noexcept {
            return UnpackBlendFactor(factor_source_rgb.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::Blend::Factor DestRGBFactor() const noexcept {
            return UnpackBlendFactor(factor_dest_rgb.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::Blend::Factor SourceAlphaFactor() const noexcept {
            return UnpackBlendFactor(factor_source_a.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::Blend::Factor DestAlphaFactor() const noexcept {
            return UnpackBlendFactor(factor_dest_a.Value());
        }
    };

    union VertexAttribute {
        u32 raw;
        BitField<0, 1, u32> enabled;
        BitField<1, 5, u32> buffer;
        BitField<6, 14, u32> offset;
        BitField<20, 3, u32> type;
        BitField<23, 6, u32> size;

        Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type Type() const noexcept {
            return static_cast<Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type>(type.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size Size() const noexcept {
            return static_cast<Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size>(size.Value());
        }
    };

    template <size_t Position>
    union StencilFace {
        BitField<Position + 0, 3, u32> action_stencil_fail;
        BitField<Position + 3, 3, u32> action_depth_fail;
        BitField<Position + 6, 3, u32> action_depth_pass;
        BitField<Position + 9, 3, u32> test_func;

        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op ActionStencilFail() const noexcept {
            return UnpackStencilOp(action_stencil_fail);
        }

        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op ActionDepthFail() const noexcept {
            return UnpackStencilOp(action_depth_fail);
        }

        Tegra::Engines::Maxwell3D::Regs::StencilOp::Op ActionDepthPass() const noexcept {
            return UnpackStencilOp(action_depth_pass);
        }

        Tegra::Engines::Maxwell3D::Regs::ComparisonOp TestFunc() const noexcept {
            return UnpackComparisonOp(test_func);
        }
    };

    struct DynamicState {
        union {
            u32 raw1;
            BitField<0, 2, u32> cull_face;
            BitField<2, 1, u32> cull_enable;
            BitField<3, 1, u32> primitive_restart_enable;
            BitField<4, 1, u32> depth_bias_enable;
            BitField<5, 1, u32> rasterize_enable;
            BitField<6, 4, u32> logic_op;
            BitField<10, 1, u32> logic_op_enable;
            BitField<11, 1, u32> depth_clamp_disabled;
        };
        union {
            u32 raw2;
            StencilFace<0> front;
            StencilFace<12> back;
            BitField<24, 1, u32> stencil_enable;
            BitField<25, 1, u32> depth_write_enable;
            BitField<26, 1, u32> depth_bounds_enable;
            BitField<27, 1, u32> depth_test_enable;
            BitField<28, 1, u32> front_face;
            BitField<29, 3, u32> depth_test_func;
        };

        void Refresh(const Tegra::Engines::Maxwell3D::Regs& regs);
        void Refresh2(const Tegra::Engines::Maxwell3D::Regs& regs, Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology,
                      bool base_features_supported);
        void Refresh3(const Tegra::Engines::Maxwell3D::Regs& regs);

        Tegra::Engines::Maxwell3D::Regs::ComparisonOp DepthTestFunc() const noexcept {
            return UnpackComparisonOp(depth_test_func);
        }

        Tegra::Engines::Maxwell3D::Regs::CullFace CullFace() const noexcept {
            return UnpackCullFace(cull_face.Value());
        }

        Tegra::Engines::Maxwell3D::Regs::FrontFace FrontFace() const noexcept {
            return UnpackFrontFace(front_face.Value());
        }
    };

    union {
        u32 raw1;
        BitField<0, 1, u32> extended_dynamic_state;
        BitField<1, 1, u32> extended_dynamic_state_2;
        BitField<2, 1, u32> extended_dynamic_state_2_extra;
        BitField<3, 1, u32> extended_dynamic_state_3_blend;
        BitField<4, 1, u32> extended_dynamic_state_3_enables;
        BitField<5, 1, u32> dynamic_vertex_input;
        BitField<6, 1, u32> xfb_enabled;
        BitField<7, 1, u32> ndc_minus_one_to_one;
        BitField<8, 2, u32> polygon_mode;
        BitField<10, 2, u32> tessellation_primitive;
        BitField<12, 2, u32> tessellation_spacing;
        BitField<14, 1, u32> tessellation_clockwise;
        BitField<15, 5, u32> patch_control_points_minus_one;

        BitField<24, 4, Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology> topology;
        BitField<28, 4, Tegra::Texture::MsaaMode> msaa_mode;
    };
    union {
        u32 raw2;
        BitField<1, 3, u32> alpha_test_func;
        BitField<4, 1, u32> early_z;
        BitField<5, 1, u32> depth_enabled;
        BitField<6, 5, u32> depth_format;
        BitField<11, 1, u32> y_negate;
        BitField<12, 1, u32> provoking_vertex_last;
        BitField<13, 1, u32> conservative_raster_enable;
        BitField<14, 1, u32> smooth_lines;
        BitField<15, 1, u32> alpha_to_coverage_enabled;
        BitField<16, 1, u32> alpha_to_one_enabled;
        BitField<17, 3, Tegra::Engines::Maxwell3D::EngineHint> app_stage;
    };
    std::array<u8, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> color_formats;

    u32 alpha_test_ref;
    u32 point_size;
    std::array<u16, Tegra::Engines::Maxwell3D::Regs::NumViewports> viewport_swizzles;
    union {
        u64 attribute_types; // Used with VK_EXT_vertex_input_dynamic_state
        u64 enabled_divisors;
    };

    DynamicState dynamic_state;
    std::array<BlendingAttachment, Tegra::Engines::Maxwell3D::Regs::NumRenderTargets> attachments;
    std::array<VertexAttribute, Tegra::Engines::Maxwell3D::Regs::NumVertexAttributes> attributes;
    std::array<u32, Tegra::Engines::Maxwell3D::Regs::NumVertexArrays> binding_divisors;
    // Vertex stride is a 12 bits value, we have 4 bits to spare per element
    std::array<u16, Tegra::Engines::Maxwell3D::Regs::NumVertexArrays> vertex_strides;

    VideoCommon::TransformFeedbackState xfb_state;

    void Refresh(Tegra::Engines::Maxwell3D& maxwell3d, DynamicFeatures& features);

    size_t Hash() const noexcept;

    bool operator==(const FixedPipelineState& rhs) const noexcept;

    bool operator!=(const FixedPipelineState& rhs) const noexcept {
        return !operator==(rhs);
    }

    size_t Size() const noexcept {
        if (xfb_enabled) {
            // When transform feedback is enabled, use the whole struct
            return sizeof(*this);
        }
        if (dynamic_vertex_input && extended_dynamic_state_3_blend) {
            // Exclude dynamic state and attributes
            return offsetof(FixedPipelineState, dynamic_state);
        }
        if (dynamic_vertex_input) {
            // Exclude dynamic state
            return offsetof(FixedPipelineState, attributes);
        }
        if (extended_dynamic_state) {
            // Exclude dynamic state
            return offsetof(FixedPipelineState, vertex_strides);
        }
        // Default
        return offsetof(FixedPipelineState, xfb_state);
    }

    u32 DynamicAttributeType(size_t index) const noexcept {
        return (attribute_types >> (index * 2)) & 0b11;
    }
};
static_assert(std::has_unique_object_representations_v<FixedPipelineState>);
static_assert(std::is_trivially_copyable_v<FixedPipelineState>);
static_assert(std::is_trivially_constructible_v<FixedPipelineState>);

} // namespace Vulkan

namespace std {

template <>
struct hash<Vulkan::FixedPipelineState> {
    size_t operator()(const Vulkan::FixedPipelineState& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std
