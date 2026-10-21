// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2020 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/x64/abi.h"

#include <algorithm>

#include "dynarmic/common/common_types.h"
#include "dynarmic/backend/x64/xbyak.h"

#include "dynarmic/backend/x64/block_of_code.h"

namespace Dynarmic::Backend::X64 {

constexpr size_t XMM_SIZE = 16;

struct FrameInfo {
    std::size_t stack_subtraction;
    std::size_t xmm_offset;
    std::size_t frame_offset;
};
static_assert(ABI_SHADOW_SPACE <= 32);

static FrameInfo CalculateFrameInfo(const size_t num_gprs, const size_t num_xmms, size_t frame_size) noexcept {
    // We are initially 8 byte aligned because the return value is pushed onto an aligned stack after a call.
    const size_t rsp_alignment = (num_gprs % 2 == 0) ? 8 : 0;
    const size_t total_xmm_size = num_xmms * XMM_SIZE;
    if (frame_size & 0xF) {
        frame_size += 0x10 - (frame_size & 0xF);
    }
    return {
        rsp_alignment + total_xmm_size + frame_size + ABI_SHADOW_SPACE,
        frame_size + ABI_SHADOW_SPACE,
        ABI_SHADOW_SPACE,
    };
}

static void ABI_PushRegistersAndAdjustStack(BlockOfCode& code, const size_t frame_size, std::bitset<32> regs) noexcept {
    using namespace Xbyak::util;

    const size_t num_gprs = (ABI_ALL_GPRS & regs).count();
    const size_t num_xmms = (ABI_ALL_XMMS & regs).count();
    const FrameInfo frame_info = CalculateFrameInfo(num_gprs, num_xmms, frame_size);

    for (size_t i = 0; i < regs.size(); ++i)
        if (regs[i] && HostLocIsGPR(HostLoc(i)))
            code.push(HostLocToReg64(HostLoc(i)));
    if (frame_info.stack_subtraction != 0)
        code.sub(rsp, u32(frame_info.stack_subtraction));
    size_t xmm_offset = frame_info.xmm_offset;
    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i] && HostLocIsXMM(HostLoc(i))) {
            if (code.HasHostFeature(HostFeature::AVX)) {
                code.vmovaps(code.xword[rsp + xmm_offset], HostLocToXmm(HostLoc(i)));
            } else {
                code.movaps(code.xword[rsp + xmm_offset], HostLocToXmm(HostLoc(i)));
            }
            xmm_offset += XMM_SIZE;
        }
    }
}

static void ABI_PopRegistersAndAdjustStack(BlockOfCode& code, const size_t frame_size, std::bitset<32> regs) noexcept {
    using namespace Xbyak::util;

    const size_t num_gprs = (ABI_ALL_GPRS & regs).count();
    const size_t num_xmms = (ABI_ALL_XMMS & regs).count();
    const FrameInfo frame_info = CalculateFrameInfo(num_gprs, num_xmms, frame_size);

    size_t xmm_offset = frame_info.xmm_offset + (num_xmms * XMM_SIZE);
    for (int32_t i = regs.size() - 1; i >= 0; --i)
        if (regs[i] && HostLocIsXMM(HostLoc(i))) {
            xmm_offset -= XMM_SIZE;
            if (code.HasHostFeature(HostFeature::AVX)) {
                code.vmovaps(HostLocToXmm(HostLoc(i)), code.xword[rsp + xmm_offset]);
            } else {
                code.movaps(HostLocToXmm(HostLoc(i)), code.xword[rsp + xmm_offset]);
            }
        }
    if (frame_info.stack_subtraction != 0)
        code.add(rsp, u32(frame_info.stack_subtraction));
    for (int32_t i = regs.size() - 1; i >= 0; --i)
        if (regs[i] && HostLocIsGPR(HostLoc(i)))
            code.pop(HostLocToReg64(HostLoc(i)));
}

void ABI_PushCalleeSaveRegistersAndAdjustStack(BlockOfCode& code, const std::size_t frame_size) {
    ABI_PushRegistersAndAdjustStack(code, frame_size, ABI_ALL_CALLEE_SAVE);
}

void ABI_PopCalleeSaveRegistersAndAdjustStack(BlockOfCode& code, const std::size_t frame_size) {
    ABI_PopRegistersAndAdjustStack(code, frame_size, ABI_ALL_CALLEE_SAVE);
}

void ABI_PushCallerSaveRegistersAndAdjustStack(BlockOfCode& code, const std::size_t frame_size) {
    ABI_PushRegistersAndAdjustStack(code, frame_size, ABI_ALL_CALLER_SAVE);
}

void ABI_PopCallerSaveRegistersAndAdjustStack(BlockOfCode& code, const std::size_t frame_size) {
    ABI_PopRegistersAndAdjustStack(code, frame_size, ABI_ALL_CALLER_SAVE);
}

// Windows ABI registers are not in the same allocation algorithm as unix's
void ABI_PushCallerSaveRegistersAndAdjustStackExcept(BlockOfCode& code, const HostLoc exception) {
    auto regs = ABI_ALL_CALLER_SAVE;
    regs.reset(size_t(exception));
    ABI_PushRegistersAndAdjustStack(code, 0, regs);
}

void ABI_PopCallerSaveRegistersAndAdjustStackExcept(BlockOfCode& code, const HostLoc exception) {
    auto regs = ABI_ALL_CALLER_SAVE;
    regs.reset(size_t(exception));
    ABI_PopRegistersAndAdjustStack(code, 0, regs);
}

}  // namespace Dynarmic::Backend::X64
