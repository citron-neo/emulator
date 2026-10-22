/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/ir/opcodes.h"

#include <array>
#include <vector>

#include "dynarmic/ir/type.h"

namespace Dynarmic::IR {

// Opcode information

namespace OpcodeInfo {

constexpr Type Void = Type::Void;
constexpr Type A32Reg = Type::A32Reg;
constexpr Type A32ExtReg = Type::A32ExtReg;
constexpr Type A64Reg = Type::A64Reg;
constexpr Type A64Vec = Type::A64Vec;
constexpr Type Opaque = Type::Opaque;
constexpr Type U1 = Type::U1;
constexpr Type U8 = Type::U8;
constexpr Type U16 = Type::U16;
constexpr Type U32 = Type::U32;
constexpr Type U64 = Type::U64;
constexpr Type U128 = Type::U128;
constexpr Type CoprocInfo = Type::CoprocInfo;
constexpr Type NZCV = Type::NZCVFlags;
constexpr Type Cond = Type::Cond;
constexpr Type Table = Type::Table;
constexpr Type AccType = Type::AccType;

struct Meta {
    std::vector<Type> arg_types;
    Type type;
};

// Evil macro magic for Intel C++ compiler
// Helper macro to force expanding __VA_ARGS__ to satisfy MSVC compiler.
#define PP_EXPAND(x) x
#define PP_NARGS(...) PP_EXPAND(PP_ARG_N(__VA_ARGS__, 5, 4, 3, 2, 1, 0))
#define PP_ARG_N(_1, _2, _3, _4, _5, N, ...) N

alignas(64) static const Meta opcode_info[] = {
#define OPCODE(name, type, ...) Meta{{__VA_ARGS__}, type},
#define A32OPC(name, type, ...) Meta{{__VA_ARGS__}, type},
#define A64OPC(name, type, ...) Meta{{__VA_ARGS__}, type},
#include "./opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
};

// Be aware of trailing commas, they can cause PP_NARG to return 2!
static_assert(PP_EXPAND(PP_NARGS(u8,)) == 2);
static_assert(PP_EXPAND(PP_NARGS(u8)) == 1);
static_assert(PP_EXPAND(PP_NARGS(u8, u16)) == 2);
static_assert(PP_EXPAND(PP_NARGS(u8, u16, u32)) == 3);

}  // namespace OpcodeInfo

/// @brief Get return type of an opcode
Type GetTypeOf(Opcode op) noexcept {
    return OpcodeInfo::opcode_info[size_t(op)].type;
}

/// @brief Get the number of arguments an opcode accepts
size_t GetNumArgsOf(Opcode op) noexcept {
    return OpcodeInfo::opcode_info[size_t(op)].arg_types.size();
}

/// @brief Get the required type of an argument of an opcode
Type GetArgTypeOf(Opcode op, size_t arg_index) noexcept {
    return OpcodeInfo::opcode_info[size_t(op)].arg_types[arg_index];
}

/// @brief Get the name of an opcode.
std::string_view GetNameOf(Opcode op) noexcept {
    static const std::string_view opcode_names[] = {
#define OPCODE(name, type, ...) #name,
#define A32OPC(name, type, ...) #name,
#define A64OPC(name, type, ...) #name,
#include "./opcodes.inc"
#undef OPCODE
#undef A32OPC
#undef A64OPC
    };
    return opcode_names[size_t(op)];
}

}  // namespace Dynarmic::IR
