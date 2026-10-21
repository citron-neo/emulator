// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/frontend/A32/translate/a32_translate.h"

#include "dynarmic/frontend/A32/a32_location_descriptor.h"
#include "dynarmic/ir/basic_block.h"

namespace Dynarmic::A32 {

void TranslateArm(IR::Block& block, LocationDescriptor descriptor, TranslateCallbacks* tcb, const TranslationOptions& options);
void TranslateThumb(IR::Block& block, LocationDescriptor descriptor, TranslateCallbacks* tcb, const TranslationOptions& options);

void Translate(IR::Block& block, LocationDescriptor descriptor, TranslateCallbacks* tcb, const TranslationOptions& options) {
    return (descriptor.TFlag() ? TranslateThumb : TranslateArm)(block, descriptor, tcb, options);
}

bool TranslateSingleArmInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction);
bool TranslateSingleThumbInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction);

bool TranslateSingleInstruction(IR::Block& block, LocationDescriptor descriptor, u32 instruction) {
    return (descriptor.TFlag() ? TranslateSingleThumbInstruction : TranslateSingleArmInstruction)(block, descriptor, instruction);
}

}  // namespace Dynarmic::A32

// ls -l | awk '{print "#include \"dynarmic/frontend/A32/translate/impl/" $9 "\""}'
#include "dynarmic/frontend/A32/translate/impl/a32_branch.cpp"
#include "dynarmic/frontend/A32/translate/impl/a32_crc32.cpp"
#include "dynarmic/frontend/A32/translate/impl/a32_exception_generating.cpp"
#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.cpp"
//#include "dynarmic/frontend/A32/translate/impl/a32_translate_impl.h"
#include "dynarmic/frontend/A32/translate/impl/asimd_load_store_structures.cpp"
#include "dynarmic/frontend/A32/translate/impl/asimd_misc.cpp"
#include "dynarmic/frontend/A32/translate/impl/asimd_one_reg_modified_immediate.cpp"
#include "dynarmic/frontend/A32/translate/impl/asimd_three_regs.cpp"
#include "dynarmic/frontend/A32/translate/impl/asimd_two_regs_misc.cpp"
#include "dynarmic/frontend/A32/translate/impl/asimd_two_regs_scalar.cpp"
#include "dynarmic/frontend/A32/translate/impl/asimd_two_regs_shift.cpp"
#include "dynarmic/frontend/A32/translate/impl/barrier.cpp"
#include "dynarmic/frontend/A32/translate/impl/coprocessor.cpp"
#include "dynarmic/frontend/A32/translate/impl/data_processing.cpp"
#include "dynarmic/frontend/A32/translate/impl/divide.cpp"
#include "dynarmic/frontend/A32/translate/impl/extension.cpp"
#include "dynarmic/frontend/A32/translate/impl/hint.cpp"
#include "dynarmic/frontend/A32/translate/impl/load_store.cpp"
#include "dynarmic/frontend/A32/translate/impl/misc.cpp"
#include "dynarmic/frontend/A32/translate/impl/multiply.cpp"
#include "dynarmic/frontend/A32/translate/impl/packing.cpp"
#include "dynarmic/frontend/A32/translate/impl/parallel.cpp"
#include "dynarmic/frontend/A32/translate/impl/reversal.cpp"
#include "dynarmic/frontend/A32/translate/impl/saturated.cpp"
#include "dynarmic/frontend/A32/translate/impl/status_register_access.cpp"
#include "dynarmic/frontend/A32/translate/impl/synchronization.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb16.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_branch.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_control.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_coprocessor.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_data_processing_modified_immediate.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_data_processing_plain_binary_immediate.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_data_processing_register.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_data_processing_shifted_register.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_load_byte.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_load_halfword.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_load_store_dual.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_load_store_multiple.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_load_word.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_long_multiply.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_misc.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_multiply.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_parallel.cpp"
#include "dynarmic/frontend/A32/translate/impl/thumb32_store_single_data_item.cpp"
#include "dynarmic/frontend/A32/translate/impl/vfp.cpp"
