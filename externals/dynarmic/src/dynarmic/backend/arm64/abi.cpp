// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#include "dynarmic/backend/arm64/abi.h"

#include <vector>

#include "dynarmic/mcl/bit.hpp"
#include "dynarmic/common/common_types.h"
#include <oaknut/oaknut.hpp>

namespace Dynarmic::Backend::Arm64 {

using namespace oaknut::util;

static constexpr std::size_t gpr_size = 8;
static constexpr std::size_t fpr_size = 16;

struct FrameInfo {
    std::vector<int> gprs;
    std::vector<int> fprs;
    std::size_t frame_size;
    std::size_t gprs_size;
    std::size_t fprs_size;
};

static std::vector<int> ListToIndexes(u32 list) {
    std::vector<int> indexes;
    for (int i = 0; i < 32; i++) {
        if (mcl::bit::get_bit(i, list)) {
            indexes.push_back(i);
        }
    }
    return indexes;
}

static FrameInfo CalculateFrameInfo(RegisterList rl, std::size_t frame_size) {
    const auto gprs = ListToIndexes(static_cast<u32>(rl));
    const auto fprs = ListToIndexes(static_cast<u32>(rl >> 32));

    const std::size_t num_gprs = gprs.size();
    const std::size_t num_fprs = fprs.size();

    const std::size_t gprs_size = (num_gprs + 1) / 2 * 16;
    const std::size_t fprs_size = num_fprs * 16;

    return {
        gprs,
        fprs,
        frame_size,
        gprs_size,
        fprs_size,
    };
}

#define DO_IT(TYPE, REG_TYPE, PAIR_OP, SINGLE_OP, OFFSET)                                                                                       \
    if (frame_info.TYPE##s.size() > 0) {                                                                                                        \
        for (std::size_t i = 0; i < frame_info.TYPE##s.size() - 1; i += 2) {                                                                         \
            code.PAIR_OP(oaknut::REG_TYPE{frame_info.TYPE##s[i]}, oaknut::REG_TYPE{frame_info.TYPE##s[i + 1]}, SP, (OFFSET) + i * TYPE##_size); \
        }                                                                                                                                       \
        if (frame_info.TYPE##s.size() % 2 == 1) {                                                                                               \
            const std::size_t i = frame_info.TYPE##s.size() - 1;                                                                                     \
            code.SINGLE_OP(oaknut::REG_TYPE{frame_info.TYPE##s[i]}, SP, (OFFSET) + i * TYPE##_size);                                            \
        }                                                                                                                                       \
    }

void ABI_PushRegisters(oaknut::CodeGenerator& code, RegisterList rl, std::size_t frame_size) {
    const FrameInfo frame_info = CalculateFrameInfo(rl, frame_size);

    code.SUB(SP, SP, frame_info.gprs_size + frame_info.fprs_size);

    DO_IT(gpr, XReg, STP, STR, 0)
    DO_IT(fpr, QReg, STP, STR, frame_info.gprs_size)

    code.SUB(SP, SP, frame_info.frame_size);
}

void ABI_PopRegisters(oaknut::CodeGenerator& code, RegisterList rl, std::size_t frame_size) {
    const FrameInfo frame_info = CalculateFrameInfo(rl, frame_size);

    code.ADD(SP, SP, frame_info.frame_size);

    DO_IT(gpr, XReg, LDP, LDR, 0)
    DO_IT(fpr, QReg, LDP, LDR, frame_info.gprs_size)

    code.ADD(SP, SP, frame_info.gprs_size + frame_info.fprs_size);
}

}  // namespace Dynarmic::Backend::Arm64
