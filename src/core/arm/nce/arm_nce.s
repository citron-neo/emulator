/* SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "core/arm/nce/arm_nce_asm_definitions.h"

#define LOAD_IMMEDIATE_32(reg, val)                     \
    mov     reg, #(((val) >> 0x00) & 0xFFFF);           \
    movk    reg, #(((val) >> 0x10) & 0xFFFF), lsl #16


/* static HaltReason Core::ArmNce::ReturnToRunCodeByTrampoline(void* tpidr, Core::GuestContext* ctx, u64 trampoline_addr) */
.section    .text._ZN4Core6ArmNce27ReturnToRunCodeByTrampolineEPvPNS_12GuestContextEm, "ax", %progbits
.global     _ZN4Core6ArmNce27ReturnToRunCodeByTrampolineEPvPNS_12GuestContextEm
.type       _ZN4Core6ArmNce27ReturnToRunCodeByTrampolineEPvPNS_12GuestContextEm, %function
_ZN4Core6ArmNce27ReturnToRunCodeByTrampolineEPvPNS_12GuestContextEm:
    /* Back up host sp to x3. */
    /* Back up host tpidr_el0 to x4. */
    mov     x3, sp
    mrs     x4, tpidr_el0

    /* Load guest sp. x5 is used as a scratch register. */
    ldr     x5, [x1, #(GuestContextSp)]
    mov     sp, x5

    /* Offset GuestContext pointer to the host member. */
    add     x5, x1, #(GuestContextHostContext)

    /* Save original host sp and tpidr_el0 (x3, x4) to host context. */
    stp     x3, x4, [x5, #(HostContextSpTpidrEl0)]

    /* Save all callee-saved host GPRs. */
    stp     x19, x20, [x5, #(HostContextRegs+0x0)]
    stp     x21, x22, [x5, #(HostContextRegs+0x10)]
    stp     x23, x24, [x5, #(HostContextRegs+0x20)]
    stp     x25, x26, [x5, #(HostContextRegs+0x30)]
    stp     x27, x28, [x5, #(HostContextRegs+0x40)]
    stp     x29, x30, [x5, #(HostContextRegs+0x50)]

    /* Save all callee-saved host FPRs. */
    stp     q8, q9,   [x5, #(HostContextVregs+0x0)]
    stp     q10, q11, [x5, #(HostContextVregs+0x20)]
    stp     q12, q13, [x5, #(HostContextVregs+0x40)]
    stp     q14, q15, [x5, #(HostContextVregs+0x60)]

    /* Load guest tpidr_el0 from argument. */
    msr     tpidr_el0, x0

    /* Tail call the trampoline to restore guest state. */
    br      x2


/* static HaltReason Core::ArmNce::ReturnToRunCodeByExceptionLevelChange(int tid, void* tpidr) */
.section    .text._ZN4Core6ArmNce37ReturnToRunCodeByExceptionLevelChangeEiPv, "ax", %progbits
.global     _ZN4Core6ArmNce37ReturnToRunCodeByExceptionLevelChangeEiPv
.type       _ZN4Core6ArmNce37ReturnToRunCodeByExceptionLevelChangeEiPv, %function
_ZN4Core6ArmNce37ReturnToRunCodeByExceptionLevelChangeEiPv:
    /* This jumps to the signal handler, which will restore the entire context. */
    /* On entry, x0 = thread id, which is already in the right place. */

    /* Move tpidr to x9 so it is not trampled. */
    mov     x9, x1

    /* Set up arguments. */
    mov     x8, #(__NR_tkill)
    mov     x1, #(ReturnToRunCodeByExceptionLevelChangeSignal)

    /* Tail call the signal handler. */
    svc     #0

    /* Block execution from flowing here. */
    brk     #1000


/* static void Core::ArmNce::ReturnToRunCodeByExceptionLevelChangeSignalHandler(int sig, void* info, void* raw_context) */
.section    .text._ZN4Core6ArmNce50ReturnToRunCodeByExceptionLevelChangeSignalHandlerEiPvS1_, "ax", %progbits
.global     _ZN4Core6ArmNce50ReturnToRunCodeByExceptionLevelChangeSignalHandlerEiPvS1_
.type       _ZN4Core6ArmNce50ReturnToRunCodeByExceptionLevelChangeSignalHandlerEiPvS1_, %function
_ZN4Core6ArmNce50ReturnToRunCodeByExceptionLevelChangeSignalHandlerEiPvS1_:
    stp     x29, x30, [sp, #-0x10]!
    mov     x29, sp

    /* Call the context restorer with the raw context. */
    mov     x0, x2
    bl      _ZN4Core6ArmNce19RestoreGuestContextEPv

    /* Save the old value of tpidr_el0. */
    mrs     x8, tpidr_el0
    ldr     x9, [x0, #(TpidrEl0NativeContext)]
    str     x8, [x9, #(GuestContextHostContext + HostContextTpidrEl0)]

    /* Set our new tpidr_el0. */
    msr     tpidr_el0, x0

    /* Unlock the context. */
    bl      _ZN4Core6ArmNce22UnlockThreadParametersEPv

    /* Returning from here will enter the guest. */
    ldp     x29, x30, [sp], #0x10
    ret


/* static void Core::ArmNce::BreakFromRunCodeSignalHandler(int sig, void* info, void* raw_context) */
.section    .text._ZN4Core6ArmNce29BreakFromRunCodeSignalHandlerEiPvS1_, "ax", %progbits
.global     _ZN4Core6ArmNce29BreakFromRunCodeSignalHandlerEiPvS1_
.type       _ZN4Core6ArmNce29BreakFromRunCodeSignalHandlerEiPvS1_, %function
_ZN4Core6ArmNce29BreakFromRunCodeSignalHandlerEiPvS1_:
    /* Check to see if we have the correct TLS magic. */
    mrs     x8, tpidr_el0
    ldr     w9, [x8, #(TpidrEl0TlsMagic)]

    LOAD_IMMEDIATE_32(w10, TlsMagic)

    cmp     w9, w10
    b.ne    1f

    /* Correct TLS magic, so this is a guest interrupt. */
    /* Restore host tpidr_el0. */
    ldr     x0, [x8, #(TpidrEl0NativeContext)]
    ldr     x3, [x0, #(GuestContextHostContext + HostContextTpidrEl0)]
    msr     tpidr_el0, x3

    /* Tail call the restorer. */
    mov     x1, x2
    b       _ZN4Core6ArmNce16SaveGuestContextEPNS_12GuestContextEPv

    /* Returning from here will enter host code. */

1:
    /* Incorrect TLS magic, so this is a spurious signal. */
    ret


/* static void Core::ArmNce::GuestAlignmentFaultSignalHandler(int sig, void* info, void* raw_context) */
.section    .text._ZN4Core6ArmNce32GuestAlignmentFaultSignalHandlerEiPvS1_, "ax", %progbits
.global     _ZN4Core6ArmNce32GuestAlignmentFaultSignalHandlerEiPvS1_
.type       _ZN4Core6ArmNce32GuestAlignmentFaultSignalHandlerEiPvS1_, %function
_ZN4Core6ArmNce32GuestAlignmentFaultSignalHandlerEiPvS1_:
    /* Check to see if we have the correct TLS magic. */
    mrs     x8, tpidr_el0
    ldr     w9, [x8, #(TpidrEl0TlsMagic)]

    LOAD_IMMEDIATE_32(w10, TlsMagic)

    cmp     w9, w10
    b.eq    1f

    /* Incorrect TLS magic, so this is a host fault. */
    /* Tail call the handler. */
    b       _ZN4Core6ArmNce24HandleHostAlignmentFaultEiPvS1_

1:
    /* Correct TLS magic, so this is a guest fault. */
    stp     x29, x30, [sp, #-0x20]!
    str     x19, [sp, #0x10]
    mov     x29, sp

    /* Save the old tpidr_el0. */
    mov     x19, x8

    /* Restore host tpidr_el0. */
    ldr     x0, [x8, #(TpidrEl0NativeContext)]
    ldr     x3, [x0, #(GuestContextHostContext + HostContextTpidrEl0)]
    msr     tpidr_el0, x3

    /* Call the handler. */
    bl       _ZN4Core6ArmNce25HandleGuestAlignmentFaultEPNS_12GuestContextEPvS3_

    /* If the handler returned false, we want to preserve the host tpidr_el0. */
    cbz     x0, 2f

    /* Otherwise, restore guest tpidr_el0. */
    msr     tpidr_el0, x19

2:
    ldr     x19, [sp, #0x10]
    ldp     x29, x30, [sp], #0x20
    ret

/* static void Core::ArmNce::GuestAccessFaultSignalHandler(int sig, void* info, void* raw_context) */
.section    .text._ZN4Core6ArmNce29GuestAccessFaultSignalHandlerEiPvS1_, "ax", %progbits
.global     _ZN4Core6ArmNce29GuestAccessFaultSignalHandlerEiPvS1_
.type       _ZN4Core6ArmNce29GuestAccessFaultSignalHandlerEiPvS1_, %function
_ZN4Core6ArmNce29GuestAccessFaultSignalHandlerEiPvS1_:
    /* Check to see if we have the correct TLS magic. */
    mrs     x8, tpidr_el0
    ldr     w9, [x8, #(TpidrEl0TlsMagic)]

    LOAD_IMMEDIATE_32(w10, TlsMagic)

    cmp     w9, w10
    b.eq    1f

    /* Incorrect TLS magic, so this is a host fault. */
    /* Tail call the handler. */
    b       _ZN4Core6ArmNce21HandleHostAccessFaultEiPvS1_

1:
    /* Correct TLS magic, so this is a guest fault. */
    stp     x29, x30, [sp, #-0x20]!
    str     x19, [sp, #0x10]
    mov     x29, sp

    /* Save the old tpidr_el0. */
    mov     x19, x8

    /* Restore host tpidr_el0. */
    ldr     x0, [x8, #(TpidrEl0NativeContext)]
    ldr     x3, [x0, #(GuestContextHostContext + HostContextTpidrEl0)]
    msr     tpidr_el0, x3

    /* Call the handler. */
    bl       _ZN4Core6ArmNce22HandleGuestAccessFaultEPNS_12GuestContextEPvS3_

    /* If the handler returned false, we want to preserve the host tpidr_el0. */
    cbz     x0, 2f

    /* Otherwise, restore guest tpidr_el0. */
    msr     tpidr_el0, x19

2:
    ldr     x19, [sp, #0x10]
    ldp     x29, x30, [sp], #0x20
    ret


/* static void Core::ArmNce::LockThreadParameters(void* tpidr) */
.section    .text._ZN4Core6ArmNce20LockThreadParametersEPv, "ax", %progbits
.global     _ZN4Core6ArmNce20LockThreadParametersEPv
.type       _ZN4Core6ArmNce20LockThreadParametersEPv, %function
_ZN4Core6ArmNce20LockThreadParametersEPv:
    /* Offset to lock member. */
    add     x0, x0, #(TpidrEl0Lock)

1:
    /* Clear the monitor. */
    clrex

2:
    /* Load-linked with acquire ordering. */
    ldaxr   w1, [x0]

    /* If the value was SpinLockLocked, clear monitor and retry. */
    cbz     w1, 1b

    /* Store-conditional SpinLockLocked with relaxed ordering. */
    stxr    w1, wzr, [x0]

    /* If we failed to store, retry. */
    cbnz    w1, 2b

    ret


/* static void Core::ArmNce::UnlockThreadParameters(void* tpidr) */
.section    .text._ZN4Core6ArmNce22UnlockThreadParametersEPv, "ax", %progbits
.global     _ZN4Core6ArmNce22UnlockThreadParametersEPv
.type       _ZN4Core6ArmNce22UnlockThreadParametersEPv, %function
_ZN4Core6ArmNce22UnlockThreadParametersEPv:
    /* Offset to lock member. */
    add     x0, x0, #(TpidrEl0Lock)

    /* Load SpinLockUnlocked. */
    mov     w1, #(SpinLockUnlocked)

    /* Store value with release ordering. */
    stlr    w1, [x0]

    ret
