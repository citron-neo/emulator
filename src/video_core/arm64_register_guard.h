// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "common/common_types.h"

#if defined(ANDROID) && defined(ARCHITECTURE_arm64) && defined(__clang__)
#define CITRON_ARM64_REGISTER_GUARD_SUPPORTED 1
#else
#define CITRON_ARM64_REGISTER_GUARD_SUPPORTED 0
#endif

#if CITRON_ARM64_REGISTER_GUARD_SUPPORTED
#define CITRON_ARM64_PRESERVE_REGISTERS(call_target)                                           \
    asm volatile("sub sp, sp, #272\n"                                                          \
                 "movz x9, #0x4752\n"                                                         \
                 "movk x9, #0x4f4e, lsl #16\n"                                                \
                 "movk x9, #0x5452, lsl #32\n"                                                \
                 "movk x9, #0x4349, lsl #48\n"                                                \
                 "str x9, [sp, #0]\n"                                                         \
                 "str x18, [sp, #8]\n"                                                        \
                 "stp x19, x20, [sp, #16]\n"                                                  \
                 "stp x21, x22, [sp, #32]\n"                                                  \
                 "stp x23, x24, [sp, #48]\n"                                                  \
                 "stp x25, x26, [sp, #64]\n"                                                  \
                 "stp x27, x28, [sp, #80]\n"                                                  \
                 "stp x29, x30, [sp, #96]\n"                                                  \
                 "str x30, [sp, #120]\n"                                                       \
                 "stp q8, q9, [sp, #128]\n"                                                   \
                 "stp q10, q11, [sp, #160]\n"                                                 \
                 "stp q12, q13, [sp, #192]\n"                                                 \
                 "stp q14, q15, [sp, #224]\n"                                                 \
                 "str x9, [sp, #256]\n"                                                       \
                 "str x30, [sp, #264]\n"                                                       \
                 "bl " #call_target "\n"                                                      \
                 "mov w0, wzr\n"                                                              \
                 "ldr x9, [sp, #16]\n"                                                        \
                 "cmp x19, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10\n"                                                         \
                 "ldr x9, [sp, #24]\n"                                                        \
                 "cmp x20, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #1\n"                                                 \
                 "ldr x9, [sp, #32]\n"                                                        \
                 "cmp x21, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #2\n"                                                 \
                 "ldr x9, [sp, #40]\n"                                                        \
                 "cmp x22, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #3\n"                                                 \
                 "ldr x9, [sp, #48]\n"                                                        \
                 "cmp x23, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #4\n"                                                 \
                 "ldr x9, [sp, #56]\n"                                                        \
                 "cmp x24, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #5\n"                                                 \
                 "ldr x9, [sp, #64]\n"                                                        \
                 "cmp x25, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #6\n"                                                 \
                 "ldr x9, [sp, #72]\n"                                                        \
                 "cmp x26, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #7\n"                                                 \
                 "ldr x9, [sp, #80]\n"                                                        \
                 "cmp x27, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #8\n"                                                 \
                 "ldr x9, [sp, #88]\n"                                                        \
                 "cmp x28, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #9\n"                                                 \
                 "ldr x9, [sp, #96]\n"                                                        \
                 "cmp x29, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #12\n"                                                \
                 "movz x9, #0x4752\n"                                                         \
                 "movk x9, #0x4f4e, lsl #16\n"                                                \
                 "movk x9, #0x5452, lsl #32\n"                                                \
                 "movk x9, #0x4349, lsl #48\n"                                                \
                 "ldr x10, [sp, #0]\n"                                                        \
                 "cmp x10, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #10\n"                                                \
                 "ldr x10, [sp, #256]\n"                                                      \
                 "cmp x10, x9\n"                                                              \
                 "cset w10, ne\n"                                                             \
                 "orr w0, w0, w10, lsl #11\n"                                                \
                 "ldr x9, [sp, #104]\n"                                                       \
                 "ldr x10, [sp, #120]\n"                                                      \
                 "ldr x11, [sp, #264]\n"                                                      \
                 "cmp x9, x10\n"                                                             \
                 "cset w12, ne\n"                                                            \
                 "cmp x9, x11\n"                                                             \
                 "cset w13, ne\n"                                                            \
                 "orr w12, w12, w13\n"                                                       \
                 "orr w0, w0, w12, lsl #13\n"                                                \
                 "cmp x9, x10\n"                                                             \
                 "b.eq 1f\n"                                                                 \
                 "cmp x9, x11\n"                                                             \
                 "b.eq 1f\n"                                                                 \
                 "cmp x10, x11\n"                                                            \
                 "b.eq 2f\n"                                                                 \
                 "mov w12, #1\n"                                                             \
                 "orr w0, w0, w12, lsl #14\n"                                                \
                 "b 1f\n"                                                                    \
                 "2:\n"                                                                      \
                 "mov x9, x10\n"                                                             \
                 "1:\n"                                                                      \
                 "str x9, [sp, #104]\n"                                                       \
                 "ldp q14, q15, [sp, #224]\n"                                                 \
                 "ldp q12, q13, [sp, #192]\n"                                                 \
                 "ldp q10, q11, [sp, #160]\n"                                                 \
                 "ldp q8, q9, [sp, #128]\n"                                                   \
                 "ldp x29, x30, [sp, #96]\n"                                                  \
                 "ldp x27, x28, [sp, #80]\n"                                                  \
                 "ldp x25, x26, [sp, #64]\n"                                                  \
                 "ldp x23, x24, [sp, #48]\n"                                                  \
                 "ldp x21, x22, [sp, #32]\n"                                                  \
                 "ldp x19, x20, [sp, #16]\n"                                                  \
                 "ldr x18, [sp, #8]\n"                                                        \
                 "add sp, sp, #272\n"                                                         \
                 "ret\n")
#endif

namespace VideoCore {

template <size_t MaskBits, typename Tag>
[[nodiscard]] bool IsFirstArm64RegisterCorruption(u32 mask) noexcept {
    static std::array<std::atomic_bool, 1ULL << MaskBits> reported_masks{};
    return mask < reported_masks.size() &&
           !reported_masks[mask].exchange(true, std::memory_order_relaxed);
}

} // namespace VideoCore
