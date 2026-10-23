// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2018 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/frontend/A64/translate/a64_translate.h"

#include "dynarmic/frontend/A64/a64_location_descriptor.h"
#include "dynarmic/frontend/A64/decoder/a64.h"
#include "dynarmic/frontend/A64/translate/impl/impl.h"
#include "dynarmic/ir/basic_block.h"
#include "dynarmic/ir/terminal.h"

namespace Dynarmic::A64 {

void Translate(IR::Block& block, LocationDescriptor descriptor, MemoryReadCodeFuncType memory_read_code, TranslationOptions options) {
    const bool single_step = descriptor.SingleStepping();
    TranslatorVisitor visitor{block, descriptor, std::move(options)};

    bool should_continue = true;
    do {
        const u64 pc = visitor.ir.current_location->PC();
        if (const auto instruction = memory_read_code(pc)) {
            if (auto decoder = Decode<TranslatorVisitor, bool>(visitor, *instruction)) {
                should_continue = *decoder;
            } else {
                should_continue = visitor.RaiseException(Exception::UnallocatedEncoding);
            }
        } else {
            should_continue = visitor.RaiseException(Exception::NoExecuteFault);
        }
        visitor.ir.current_location = visitor.ir.current_location->AdvancePC(4);
        block.CycleCount()++;
    } while (should_continue && !single_step);

    if (single_step && should_continue) {
        visitor.ir.SetTerm(IR::Term::LinkBlock{*visitor.ir.current_location});
    }
    ASSERT(block.HasTerminal() && "Terminal has not been set");
    block.SetEndLocation(*visitor.ir.current_location);
}

bool TranslateSingleInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction) {
    TranslatorVisitor visitor{block, descriptor, {}};

    bool should_continue = false;
    if (auto const decoder = Decode<TranslatorVisitor, bool>(visitor, instruction))
        should_continue = *decoder;

    visitor.ir.current_location = visitor.ir.current_location->AdvancePC(4);
    block.CycleCount()++;
    block.SetEndLocation(*visitor.ir.current_location);
    return should_continue;
}

}  // namespace Dynarmic::A64

// ls -l | awk '{print "#include \"dynarmic/frontend/A64/translate/impl/" $9 "\""}'
#include "dynarmic/frontend/A64/translate/impl/a64_branch.cpp"
#include "dynarmic/frontend/A64/translate/impl/a64_exception_generating.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_addsub.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_bitfield.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_conditional_compare.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_conditional_select.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_crc32.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_logical.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_multiply.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_pcrel.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_register.cpp"
#include "dynarmic/frontend/A64/translate/impl/data_processing_shift.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_compare.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_conditional_compare.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_conditional_select.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_conversion_fixed_point.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_conversion_integer.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_data_processing_one_register.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_data_processing_three_register.cpp"
#include "dynarmic/frontend/A64/translate/impl/floating_point_data_processing_two_register.cpp"
#include "dynarmic/frontend/A64/translate/impl/impl.cpp"
#include "dynarmic/frontend/A64/translate/impl/impl.h"
#include "dynarmic/frontend/A64/translate/impl/load_store_exclusive.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_load_literal.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_multiple_structures.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_no_allocate_pair.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_register_immediate.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_register_pair.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_register_register_offset.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_register_unprivileged.cpp"
#include "dynarmic/frontend/A64/translate/impl/load_store_single_structure.cpp"
#include "dynarmic/frontend/A64/translate/impl/move_wide.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_across_lanes.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_aes.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_copy.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_crypto_four_register.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_crypto_three_register.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_extract.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_modified_immediate.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_permute.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_scalar_pairwise.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_scalar_shift_by_immediate.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_scalar_three_same.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_scalar_two_register_misc.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_scalar_x_indexed_element.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_sha512.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_sha.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_shift_by_immediate.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_table_lookup.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_three_different.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_three_same.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_three_same_extra.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_two_register_misc.cpp"
#include "dynarmic/frontend/A64/translate/impl/simd_vector_x_indexed_element.cpp"
#include "dynarmic/frontend/A64/translate/impl/sys_dc.cpp"
#include "dynarmic/frontend/A64/translate/impl/sys_ic.cpp"
#include "dynarmic/frontend/A64/translate/impl/system.cpp"
#include "dynarmic/frontend/A64/translate/impl/system_flag_format.cpp"
#include "dynarmic/frontend/A64/translate/impl/system_flag_manipulation.cpp"
