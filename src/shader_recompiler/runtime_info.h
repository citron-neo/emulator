// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <map>
#include <optional>
#include <vector>

#include "common/common_types.h"
#include "shader_recompiler/varying_state.h"

namespace Shader {

enum class AttributeType : u8 {
    Float,
    SignedInt,
    UnsignedInt,
    SignedScaled,
    UnsignedScaled,
    Disabled,
};

enum class InputTopology {
    Points,
    Lines,
    LinesAdjacency,
    Triangles,
    TrianglesAdjacency,
};

namespace InputTopologyVertices {
    // Lookup table for vertex counts - faster than switch statement
    inline constexpr std::array<u32, 5> vertex_counts = {
        1, // Points
        2, // Lines
        4, // LinesAdjacency
        3, // Triangles
        6, // TrianglesAdjacency
    };

    // Force compile-time evaluation when possible
    inline constexpr u32 vertices(InputTopology input_topology) {
        return vertex_counts[static_cast<std::size_t>(input_topology)];
    }
}

enum class CompareFunction {
    Never,
    Less,
    Equal,
    LessThanEqual,
    Greater,
    NotEqual,
    GreaterThanEqual,
    Always,
};

enum class FragmentOutputType : u8 {
    Float,
    SignedInt,
    UnsignedInt,
};

enum class TessPrimitive {
    Isolines,
    Triangles,
    Quads,
};

enum class TessSpacing {
    Equal,
    FractionalOdd,
    FractionalEven,
};

struct TransformFeedbackVarying {
    u32 buffer{};
    u32 stride{};
    u32 offset{};
    u32 components{};
};

[[nodiscard]] constexpr std::array<u8, 32> IdentityGenericLocations() {
    std::array<u8, 32> locations{};
    for (size_t index = 0; index < locations.size(); ++index) {
        locations[index] = static_cast<u8>(index);
    }
    return locations;
}
static_assert(IdentityGenericLocations()[0] == 0 && IdentityGenericLocations()[31] == 31);

struct RuntimeInfo {
    std::array<AttributeType, 32> generic_input_types{};
    VaryingState previous_stage_stores;
    std::map<IR::Attribute, IR::Attribute> previous_stage_legacy_stores_mapping;

    /// Host location of each generic this stage reads from the previous stage and writes for
    /// the next one. Identity unless a geometry shader follows and the host allows fewer
    /// geometry input locations than the 32 Maxwell generics; the stage before the geometry
    /// shader then gets the same table as the geometry shader's inputs.
    std::array<u8, 32> input_generic_locations{IdentityGenericLocations()};
    std::array<u8, 32> output_generic_locations{IdentityGenericLocations()};

    bool convert_depth_mode{};
    bool force_early_z{};

    TessPrimitive tess_primitive{};
    TessSpacing tess_spacing{};
    bool tess_clockwise{};

    InputTopology input_topology{};

    std::optional<float> fixed_state_point_size;
    std::optional<CompareFunction> alpha_test_func;
    float alpha_test_reference{};
    bool alpha_to_coverage_enabled{};
    std::array<FragmentOutputType, 8> frag_color_types{};

    /// Static Y negate value
    bool y_negate{};
    /// Use storage buffers instead of global pointers on GLASM
    bool glasm_use_storage_buffers{};

    /// Transform feedback state for each varying
    std::array<TransformFeedbackVarying, 256> xfb_varyings{};
    u32 xfb_count{0};
};

} // namespace Shader
