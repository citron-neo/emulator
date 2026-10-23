// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include <cstring>

#include "dynarmic/common/assert.h"

#include "dynarmic/backend/x64/block_of_code.h"
#include "dynarmic/backend/x64/constant_pool.h"

namespace Dynarmic::Backend::X64 {

ConstantPool::ConstantPool(BlockOfCode& code, size_t size)
    : insertion_point(0)
{
    code.EnsureMemoryCommitted(align_size + size);
    code.int3();
    code.align(align_size);
    pool = std::span<ConstantT>(reinterpret_cast<ConstantT*>(code.AllocateFromCodeSpace(size)), size / align_size);
}

Xbyak::Address ConstantPool::GetConstant(BlockOfCode& code, const Xbyak::AddressFrame& frame, u64 lower, u64 upper) {
    const auto constant = ConstantT(lower, upper);
    auto it = constant_info.find(constant);
    if (it == constant_info.end()) {
        ASSERT(insertion_point < pool.size());
        ConstantT& target_constant = pool[insertion_point];
        target_constant = constant;
        it = constant_info.insert({constant, &target_constant}).first;
        ++insertion_point;
    }
    return frame[code.rip + it->second];
}

}  // namespace Dynarmic::Backend::X64
