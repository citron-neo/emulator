// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>
#include <variant>
#include <vector>
#include <ankerl/unordered_dense.h>
#include "common/bit_field.h"
#include "common/common_types.h"

namespace Tegra {

namespace Engines {
class Maxwell3D;
}

namespace Macro {
constexpr std::size_t NUM_MACRO_REGISTERS = 8;
enum class Operation : u32 {
    ALU = 0,
    AddImmediate = 1,
    ExtractInsert = 2,
    ExtractShiftLeftImmediate = 3,
    ExtractShiftLeftRegister = 4,
    Read = 5,
    Unused = 6, // This operation doesn't seem to be a valid encoding.
    Branch = 7,
};

enum class ALUOperation : u32 {
    Add = 0,
    AddWithCarry = 1,
    Subtract = 2,
    SubtractWithBorrow = 3,
    // Operations 4-7 don't seem to be valid encodings.
    Xor = 8,
    Or = 9,
    And = 10,
    AndNot = 11,
    Nand = 12
};

enum class ResultOperation : u32 {
    IgnoreAndFetch = 0,
    Move = 1,
    MoveAndSetMethod = 2,
    FetchAndSend = 3,
    MoveAndSend = 4,
    FetchAndSetMethod = 5,
    MoveAndSetMethodFetchAndSend = 6,
    MoveAndSetMethodSend = 7
};

enum class BranchCondition : u32 {
    Zero = 0,
    NotZero = 1,
};

union Opcode {
    u32 raw;
    BitField<0, 3, Operation> operation;
    BitField<4, 3, ResultOperation> result_operation;
    BitField<4, 1, BranchCondition> branch_condition;
    // If set on a branch, then the branch doesn't have a delay slot.
    BitField<5, 1, u32> branch_annul;
    BitField<7, 1, u32> is_exit;
    BitField<8, 3, u32> dst;
    BitField<11, 3, u32> src_a;
    BitField<14, 3, u32> src_b;
    // The signed immediate overlaps the second source operand and the alu operation.
    BitField<14, 18, s32> immediate;

    BitField<17, 5, ALUOperation> alu_operation;

    // Bitfield instructions data
    BitField<17, 5, u32> bf_src_bit;
    BitField<22, 5, u32> bf_size;
    BitField<27, 5, u32> bf_dst_bit;

    u32 GetBitfieldMask() const {
        return (1 << bf_size) - 1;
    }

    s32 GetBranchTarget() const {
        return static_cast<s32>(immediate * sizeof(u32));
    }
};

union MethodAddress {
    u32 raw;
    BitField<0, 12, u32> address;
    BitField<12, 6, u32> increment;
};

} // namespace Macro

struct HLEMacro {
};
/// @note: these macros have two versions, a normal and extended version, with the extended version
/// also assigning the base vertex/instance.
struct HLE_DrawArraysIndirect final {
    HLE_DrawArraysIndirect(bool extended_) noexcept : extended{extended_} {}
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
    void Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters);
    bool extended;
};
/// @note: these macros have two versions, a normal and extended version, with the extended version
/// also assigning the base vertex/instance.
struct HLE_DrawIndexedIndirect final {
    explicit HLE_DrawIndexedIndirect(bool extended_) noexcept : extended{extended_} {}
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
    void Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters);
    bool extended;
};
struct HLE_MultiLayerClear final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
};
struct HLE_MultiDrawIndexedIndirectCount final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
    void Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters);
};
struct HLE_DrawIndirectByteCount final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
    void Fallback(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters);
};
struct HLE_C713C83D8F63CCF3 final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
};
struct HLE_D7333D26E0A93EDE final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
};
struct HLE_BindShader final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
};
struct HLE_SetRasterBoundingBox final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
};
struct HLE_ClearConstBuffer final {
    HLE_ClearConstBuffer(size_t base_size_) noexcept : base_size{base_size_} {}
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
    size_t base_size;
};
struct HLE_ClearMemory final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
    std::vector<u32> zero_memory;
};
struct HLE_TransformFeedbackSetup final {
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, [[maybe_unused]] u32 method);
};
struct MacroInterpreterImpl final {
    MacroInterpreterImpl() {}
    MacroInterpreterImpl(std::span<const u32> code_) : code{code_} {}
    void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> params, u32 method);
    void Reset();
    bool Step(Engines::Maxwell3D& maxwell3d, bool is_delay_slot);
    u32 GetALUResult(Macro::ALUOperation operation, u32 src_a, u32 src_b);
    void ProcessResult(Engines::Maxwell3D& maxwell3d, Macro::ResultOperation operation, u32 reg, u32 result);
    bool EvaluateBranchCondition(Macro::BranchCondition cond, u32 value) const;
    Macro::Opcode GetOpcode() const;
    u32 GetRegister(u32 register_id) const;
    void SetRegister(u32 register_id, u32 value);
    /// Sets the method address to use for the next Send instruction.
    [[nodiscard]] inline void SetMethodAddress(u32 address) noexcept {
        method_address.raw = address;
    }
    void Send(Engines::Maxwell3D& maxwell3d, u32 value);
    u32 Read(Engines::Maxwell3D& maxwell3d, u32 method) const;
    u32 FetchParameter();
    /// General purpose macro registers.
    std::array<u32, Macro::NUM_MACRO_REGISTERS> registers = {};
    /// Input parameters of the current macro.
    std::vector<u32> parameters;
    std::span<const u32> code;
    /// Program counter to execute at after the delay slot is executed.
    std::optional<u32> delayed_pc;
    /// Method address to use for the next Send instruction.
    Macro::MethodAddress method_address = {};
    /// Current program counter
    u32 pc{};
    /// Index of the next parameter that will be fetched by the 'parm' instruction.
    u32 next_parameter_index = 0;
    bool carry_flag = false;
};
struct DynamicCachedMacro {
    virtual ~DynamicCachedMacro() = default;
    /// Executes the macro code with the specified input parameters.
    /// @param parameters The parameters of the macro
    /// @param method     The method to execute
    virtual void Execute(Engines::Maxwell3D& maxwell3d, std::span<const u32> parameters, u32 method) = 0;
};

using AnyCachedMacro = std::variant<
    std::monostate,
    HLEMacro,
    HLE_DrawArraysIndirect,
    HLE_DrawIndexedIndirect,
    HLE_MultiDrawIndexedIndirectCount,
    HLE_MultiLayerClear,
    HLE_C713C83D8F63CCF3,
    HLE_D7333D26E0A93EDE,
    HLE_BindShader,
    HLE_SetRasterBoundingBox,
    HLE_ClearConstBuffer,
    HLE_ClearMemory,
    HLE_TransformFeedbackSetup,
    HLE_DrawIndirectByteCount,
    MacroInterpreterImpl,
    // Used for JIT x86 macro
    std::unique_ptr<DynamicCachedMacro>
>;

struct MacroEngine {
    MacroEngine(bool is_interpreted_) noexcept : is_interpreted{is_interpreted_} {}
    // Store the uploaded macro code to compile them when they're called.
    inline void AddCode(u32 method, u32 data) noexcept {
        uploaded_macro_code[method].push_back(data);
    }
    // Clear the code associated with a method.
    inline void ClearCode(u32 method) noexcept {
        macro_cache.erase(method);
        uploaded_macro_code.erase(method);
    }
    // Compiles the macro if its not in the cache, and executes the compiled macro
    void Execute(Engines::Maxwell3D& maxwell3d, u32 method, std::span<const u32> parameters);
    AnyCachedMacro Compile(Engines::Maxwell3D& maxwell3d, std::span<const u32> code);
    struct CacheInfo {
        AnyCachedMacro program;
        u64 hash{};
    };
    ankerl::unordered_dense::map<u32, CacheInfo> macro_cache;
    ankerl::unordered_dense::map<u32, std::vector<u32>> uploaded_macro_code;
    bool is_interpreted;
};

} // namespace Tegra
