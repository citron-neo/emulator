// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/bit_field.h"
#include "common/common_types.h"
#include "shader_recompiler/frontend/ir/modifiers.h"
#include "shader_recompiler/frontend/maxwell/translate/impl/impl.h"

namespace Shader::Maxwell {
namespace {
enum class PrecisionTFS : u64 {
    F16,
    F32,
};

union EncodingTFS {
    u64 raw;
    BitField<59, 1, PrecisionTFS> precision;
    BitField<53, 4, u64> encoding;
    BitField<49, 1, u64> nodep;
    BitField<28, 8, IR::Reg> dest_reg_b;
    BitField<0, 8, IR::Reg> dest_reg_a;
    BitField<8, 8, IR::Reg> src_reg_a;
    BitField<20, 8, IR::Reg> src_reg_b;
    BitField<36, 13, u64> cbuf_offset;
    BitField<50, 3, u64> swizzle;
};

void CheckAlignmentTFS(IR::Reg reg, size_t alignment) {
    if (!IR::IsAligned(reg, alignment)) {
        throw NotImplementedException("Unaligned source register {}", reg);
    }
}

template <typename... Args>
IR::Value Composite(TranslatorVisitor& v, Args... regs) {
    return v.ir.CompositeConstruct(v.F(regs)...);
}

IR::F32 ReadArray(TranslatorVisitor& v, const IR::U32& value) {
    return v.ir.ConvertUToF(32, 16, v.ir.BitFieldExtract(value, v.ir.Imm32(0), v.ir.Imm32(16)));
}

IR::Value FetchSample(TranslatorVisitor& v, u64 insn) {
    const EncodingTFS texs{insn};
    const IR::U32 handle{v.ir.Imm32(static_cast<u32>(texs.cbuf_offset * 4))};
    const IR::F32 zero{v.ir.Imm32(0.0f)};
    const IR::Reg reg_a{texs.src_reg_a};
    const IR::Reg reg_b{texs.src_reg_b};
    IR::TextureInstInfo info{};
    if (texs.precision == PrecisionTFS::F16) {
        info.relaxed_precision.Assign(1);
    }
    switch (texs.encoding) {
    case 0: // 1D.LZ
        info.type.Assign(TextureType::Color1D);
        return v.ir.ImageSampleExplicitLod(handle, v.F(reg_a), zero, {}, info);
    case 1: // 2D
        info.type.Assign(TextureType::Color2D);
        return v.ir.ImageSampleImplicitLod(handle, Composite(v, reg_a, reg_b), {}, {}, {}, info);
    case 2: // 2D.LZ
        info.type.Assign(TextureType::Color2D);
        return v.ir.ImageSampleExplicitLod(handle, Composite(v, reg_a, reg_b), zero, {}, info);
    case 3: // 2D.LL
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::Color2D);
        return v.ir.ImageSampleExplicitLod(handle, Composite(v, reg_a, reg_a + 1), v.F(reg_b), {},
                                           info);
    case 4: // 2D.DC
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::Color2D);
        info.is_depth.Assign(1);
        return v.ir.ImageSampleDrefImplicitLod(handle, Composite(v, reg_a, reg_a + 1), v.F(reg_b),
                                               {}, {}, {}, info);
    case 5: // 2D.LL.DC
        CheckAlignmentTFS(reg_a, 2);
        CheckAlignmentTFS(reg_b, 2);
        info.type.Assign(TextureType::Color2D);
        info.is_depth.Assign(1);
        return v.ir.ImageSampleDrefExplicitLod(handle, Composite(v, reg_a, reg_a + 1),
                                               v.F(reg_b + 1), v.F(reg_b), {}, info);
    case 6: // 2D.LZ.DC
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::Color2D);
        info.is_depth.Assign(1);
        return v.ir.ImageSampleDrefExplicitLod(handle, Composite(v, reg_a, reg_a + 1), v.F(reg_b),
                                               zero, {}, info);
    case 7: // ARRAY_2D
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::ColorArray2D);
        return v.ir.ImageSampleImplicitLod(
            handle, v.ir.CompositeConstruct(v.F(reg_a + 1), v.F(reg_b), ReadArray(v, v.X(reg_a))),
            {}, {}, {}, info);
    case 8: // ARRAY_2D.LZ
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::ColorArray2D);
        return v.ir.ImageSampleExplicitLod(
            handle, v.ir.CompositeConstruct(v.F(reg_a + 1), v.F(reg_b), ReadArray(v, v.X(reg_a))),
            zero, {}, info);
    case 9: // ARRAY_2D.LZ.DC
        CheckAlignmentTFS(reg_a, 2);
        CheckAlignmentTFS(reg_b, 2);
        info.type.Assign(TextureType::ColorArray2D);
        info.is_depth.Assign(1);
        return v.ir.ImageSampleDrefExplicitLod(
            handle, v.ir.CompositeConstruct(v.F(reg_a + 1), v.F(reg_b), ReadArray(v, v.X(reg_a))),
            v.F(reg_b + 1), zero, {}, info);
    case 10: // 3D
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::Color3D);
        return v.ir.ImageSampleImplicitLod(handle, Composite(v, reg_a, reg_a + 1, reg_b), {}, {},
                                           {}, info);
    case 11: // 3D.LZ
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::Color3D);
        return v.ir.ImageSampleExplicitLod(handle, Composite(v, reg_a, reg_a + 1, reg_b), zero, {},
                                           info);
    case 12: // CUBE
        CheckAlignmentTFS(reg_a, 2);
        info.type.Assign(TextureType::ColorCube);
        return v.ir.ImageSampleImplicitLod(handle, Composite(v, reg_a, reg_a + 1, reg_b), {}, {},
                                           {}, info);
    case 13: // CUBE.LL
        CheckAlignmentTFS(reg_a, 2);
        CheckAlignmentTFS(reg_b, 2);
        info.type.Assign(TextureType::ColorCube);
        return v.ir.ImageSampleExplicitLod(handle, Composite(v, reg_a, reg_a + 1, reg_b),
                                           v.F(reg_b + 1), {}, info);
    default:
        throw NotImplementedException("Illegal encoding {}", texs.encoding.Value());
    }
}

unsigned FetchSwizzle(u64 insn) {
#define R 1
#define G 2
#define B 4
#define A 8
    constexpr std::array<unsigned, 8> RG_LUT{
        R,     //
        G,     //
        B,     //
        A,     //
        R | G, //
        R | A, //
        G | A, //
        B | A, //
    };
    constexpr std::array<unsigned, 5> RGBA_LUT{
        R | G | B,     //
        R | G | A,     //
        R | B | A,     //
        G | B | A,     //
        R | G | B | A, //
    };
#undef R
#undef G
#undef B
#undef A
    const EncodingTFS texs{insn};
    const size_t encoding{texs.swizzle};
    if (texs.dest_reg_b == IR::Reg::RZ) {
        if (encoding >= RG_LUT.size()) {
            throw NotImplementedException("Illegal RG encoding {}", encoding);
        }
        return RG_LUT[encoding];
    } else {
        if (encoding >= RGBA_LUT.size()) {
            throw NotImplementedException("Illegal RGBA encoding {}", encoding);
        }
        return RGBA_LUT[encoding];
    }
}

IR::F32 FetchExtract(TranslatorVisitor& v, const IR::Value& sample, unsigned component) {
    const bool is_shadow{sample.Type() == IR::Type::F32};
    if (is_shadow) {
        const bool is_alpha{component == 3};
        return is_alpha ? v.ir.Imm32(1.0f) : IR::F32{sample};
    } else {
        return IR::F32{v.ir.CompositeExtract(sample, component)};
    }
}

IR::Reg FetchRegStoreComponent32(u64 insn, unsigned index) {
    const EncodingTFS texs{insn};
    switch (index) {
    case 0:
        return texs.dest_reg_a;
    case 1:
        CheckAlignmentTFS(texs.dest_reg_a, 2);
        return texs.dest_reg_a + 1;
    case 2:
        return texs.dest_reg_b;
    case 3:
        CheckAlignmentTFS(texs.dest_reg_b, 2);
        return texs.dest_reg_b + 1;
    }
    throw LogicError("Invalid store index {}", index);
}

void FetchStore32(TranslatorVisitor& v, u64 insn, const IR::Value& sample) {
    const unsigned swizzle{FetchSwizzle(insn)};
    unsigned store_index{0};
    for (unsigned component = 0; component < 4; ++component) {
        if (((swizzle >> component) & 1) == 0) {
            continue;
        }
        const IR::Reg dest{FetchRegStoreComponent32(insn, store_index)};
        v.F(dest, FetchExtract(v, sample, component));
        ++store_index;
    }
}

IR::U32 FetchPack(TranslatorVisitor& v, const IR::F32& lhs, const IR::F32& rhs) {
    return v.ir.PackHalf2x16(v.ir.CompositeConstruct(lhs, rhs));
}

void FetchStore16(TranslatorVisitor& v, u64 insn, const IR::Value& sample) {
    const unsigned swizzle{FetchSwizzle(insn)};
    unsigned store_index{0};
    std::array<IR::F32, 4> swizzled;
    for (unsigned component = 0; component < 4; ++component) {
        if (((swizzle >> component) & 1) == 0) {
            continue;
        }
        swizzled[store_index] = FetchExtract(v, sample, component);
        ++store_index;
    }
    const IR::F32 zero{v.ir.Imm32(0.0f)};
    const EncodingTFS texs{insn};
    switch (store_index) {
    case 1:
        v.X(texs.dest_reg_a, FetchPack(v, swizzled[0], zero));
        break;
    case 2:
    case 3:
    case 4:
        v.X(texs.dest_reg_a, FetchPack(v, swizzled[0], swizzled[1]));
        switch (store_index) {
        case 2:
            break;
        case 3:
            v.X(texs.dest_reg_b, FetchPack(v, swizzled[2], zero));
            break;
        case 4:
            v.X(texs.dest_reg_b, FetchPack(v, swizzled[2], swizzled[3]));
            break;
        }
        break;
    }
}
} // Anonymous namespace

void TranslatorVisitor::TEXS(u64 insn) {
    const IR::Value sample{FetchSample(*this, insn)};
    if (EncodingTFS{insn}.precision == PrecisionTFS::F32) {
        FetchStore32(*this, insn, sample);
    } else {
        FetchStore16(*this, insn, sample);
    }
}

} // namespace Shader::Maxwell
