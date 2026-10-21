// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef __APPLE__
#    include <signal.h>
#    include <sys/ucontext.h>
#else
#    include <signal.h>
#    ifndef __OpenBSD__
#        include <ucontext.h>
#    endif
#    ifdef __sun__
// Thanks C macros for exisitng in Solaris headers, thanks a lot
// We really needed to define FOR EVERY SINGLE REGISTER didn't we?
#        include <sys/regset.h>
#        undef EAX
#        undef EBX
#        undef ECX
#        undef EDX
#        undef ESP
#        undef EBP
#        undef ESI
#        undef EDI
#        undef ERR
#        undef SS
#        undef CS
#        undef ES
#        undef DS
#    endif
#    ifdef __linux__
#       include <sys/syscall.h>
#    endif
#endif

#ifdef ARCHITECTURE_arm64
#   ifdef __OpenBSD__
#       define CTX_DECLARE(raw_context) ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(raw_context);
#   else
#       define CTX_DECLARE(raw_context) ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(raw_context);  \
        [[maybe_unused]] auto& mctx = ucontext->uc_mcontext; \
        [[maybe_unused]] const auto fpctx = GetFloatingPointState(mctx);
#   endif
#else
#   ifdef __OpenBSD__
#       define CTX_DECLARE(raw_context) ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(raw_context);
#   else
#       define CTX_DECLARE(raw_context) ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(raw_context);  \
        [[maybe_unused]] auto& mctx = ucontext->uc_mcontext;
#   endif
#endif

// Some OSes define generic helper macros for all architectures
#if defined(__NetBSD__)
// NetBSD always has useful macros for everything don't they?
// Basically this special path means that for every arch that NetBSD supports
// it atleast has the macro for _UC_MACHINE defined, thus it will be avoided on the
// other macro branches!
// https://github.com/NetBSD/src/blob/trunk/sys/arch/powerpc/include/mcontext.h
// https://github.com/NetBSD/src/blob/trunk/sys/arch/mips/include/mcontext.h
#   define CTX_PC (_UC_MACHINE_PC(ucontext))
// NetBSD defines a redzone for SP but we don't require nor want that
// ((uc)->uc_mcontext.__gregs[_REG_RSP] - 128) -> this is not desirable due to calcs
#if defined(ARCHITECTURE_x86_64)
#   define CTX_SP (mctx.__gregs[_REG_RSP])
#else
#   define CTX_SP (_UC_MACHINE_SP(ucontext))
#endif
#endif

#if defined(ARCHITECTURE_arm64)
#    if defined(__APPLE__)
#        define CTX_PC (mctx->__ss.__pc)
#        define CTX_SP (mctx->__ss.__sp)
#        define CTX_LR (mctx->__ss.__lr)
#        define CTX_PSTATE (mctx->__ss.__cpsr)
#        define CTX_X(i) (mctx->__ss.__x[i])
#        define CTX_Q(i) (mctx->__ns.__v[i])
#        define CTX_FPSR (mctx->__ns.__fpsr)
#        define CTX_FPCR (mctx->__ns.__fpcr)
#    elif defined(__linux__)
#        define CTX_PC (mctx.pc)
#        define CTX_SP (mctx.sp)
#        define CTX_LR (mctx.regs[30])
#        define CTX_PSTATE (mctx.pstate)
#        define CTX_X(i) (mctx.regs[i])
#        define CTX_Q(i) (fpctx->vregs[i])
#        define CTX_FPSR (fpctx->fpsr)
#        define CTX_FPCR (fpctx->fpcr)
#    elif defined(__FreeBSD__)
#        define CTX_PC (mctx.mc_gpregs.gp_elr)
#        define CTX_SP (mctx.mc_gpregs.gp_sp)
#        define CTX_LR (mctx.mc_gpregs.gp_lr)
#        define CTX_X(i) (mctx.mc_gpregs.gp_x[i])
#        define CTX_Q(i) (mctx.mc_fpregs.fp_q[i])
#    elif defined(__NetBSD__) // SP + PC defined above
#        define CTX_LR (mctx.mc_gpregs.gp_lr)
#        define CTX_X(i) (mctx.mc_gpregs.gp_x[i])
#        define CTX_Q(i) (mctx.mc_fpregs.fp_q[i])
#    elif defined(__OpenBSD__)
// https://github.com/openbsd/src/blob/master/sys/arch/arm64/include/signal.h
#        define CTX_PC (ucontext->sc_elr)
#        define CTX_SP (ucontext->sc_sp)
#        define CTX_LR (ucontext->sc_lr)
#        define CTX_X(i) (ucontext->sc_x[i])
#        define CTX_Q(i) (ucontext->sc_q[i])
#    else
#        error "unknown platform"
#    endif
#elif defined(ARCHITECTURE_x86_64)
#    if defined(__APPLE__)
#        define CTX_PC (mctx->__ss.__rip)
#        define CTX_SP (mctx->__ss.__rsp)
#    elif defined(__linux__)
#        define CTX_PC (mctx.gregs[REG_RIP])
#        define CTX_SP (mctx.gregs[REG_RSP])
#    elif defined(__FreeBSD__)
#        define CTX_PC (mctx.mc_rip)
#        define CTX_SP (mctx.mc_rsp)
#    elif defined(__OpenBSD__)
// https://github.com/openbsd/src/blob/master/sys/arch/x86_64/include/signal.h
#        define CTX_PC (ucontext->sc_rip)
#        define CTX_SP (ucontext->sc_rsp)
#    elif defined(__sun__)
#        define CTX_PC (mctx.gregs[REG_RIP])
#        define CTX_SP (mctx.gregs[REG_RSP])
#    elif defined(__DragonFly__)
#        define CTX_PC (mctx.mc_rip)
#        define CTX_SP (mctx.mc_rsp)
#    elif defined(__managarm__)
#        define CTX_PC (mctx.__pollution(gregs)[REG_RIP])
#        define CTX_SP (mctx.__pollution(gregs)[REG_RSP])
#    else
#        error "unknown platform"
#    endif
#elif defined(ARCHITECTURE_riscv64)
#   if defined(__FreeBSD__)
#       define CTX_PC (mctx.mc_gpregs.gp_sepc)
#       define CTX_SP (mctx.mc_gpregs.gp_sp)
#   elif defined(__linux__)
#       define CTX_PC (mctx.__gregs[REG_PC])
#       define CTX_SP (mctx.__gregs[REG_SP])
#   elif defined(__OpenBSD__)
// https://github.com/openbsd/src/blob/master/sys/arch/riscv64/include/signal.h
#        define CTX_PC (ucontext->sc_sepc)
#        define CTX_SP (ucontext->sc_sp)
#    else
#        error "unknown platform"
#   endif
#elif defined(ARCHITECTURE_powerpc64)
#   if defined(__FreeBSD__)
#       define CTX_PC (mctx.mc_srr0)
#       define CTX_SP (mctx.mc_gpr[1])
#   elif defined(__linux__)
// https://github.com/lattera/glibc/blob/master/sysdeps/unix/sysv/linux/powerpc/sys/ucontext.h
#       define CTX_PC (mctx.__regs[REG_PC])
#       define CTX_SP (mctx.__regs[REG_SP])
#   elif defined(__OpenBSD__)
// https://github.com/openbsd/src/blob/master/sys/arch/powerpc64/include/signal.h
#        define CTX_PC (ucontext->sc_pc)
#        define CTX_SP (ucontext->sc_reg[1])
#    else
#        error "unknown platform"
#   endif
#elif defined(ARCHITECTURE_mips64)
#   if defined(__linux__)
// https://github.com/lattera/glibc/blob/master/sysdeps/unix/sysv/linux/mips/sys/ucontext.h
#       define CTX_PC (mctx.__pc)
#       define CTX_SP (mctx.__gregs[29])
#   elif defined(__OpenBSD__)
// https://github.com/openbsd/src/blob/master/sys/arch/mips64/include/signal.h
#        define CTX_PC (ucontext->sc_pc)
#        define CTX_SP (ucontext->sc_regs[29])
#    else
#        error "unknown platform"
#   endif
#elif defined(ARCHITECTURE_loongarch64)
#   if defined(__linux__)
// https://github.com/bminor/glibc/blob/04e750e75b73957cf1c791535a3f4319534a52fc/sysdeps/unix/sysv/linux/loongarch/sys/ucontext.h
#       define CTX_PC (mctx.__pc)
#       define CTX_SP (mctx.__gregs[LARCH_REG_SP])
#   elif defined(__OpenBSD__)
// Literally MIPS64 copypaste?
// https://github.com/openbsd/src/blob/master/sys/arch/loongson/include/signal.h
#        define CTX_PC (ucontext->sc_pc)
#        define CTX_SP (ucontext->sc_regs[29])
#    else
#        error "unknown platform"
#   endif
#elif defined(ARCHITECTURE_sparc64)
#   if defined(__linux__)
// https://github.com/lattera/glibc/blob/master/sysdeps/unix/sysv/linux/mips/sys/ucontext.h
#       define CTX_PC (mctx.__gregs[1]) //%pc
#       define CTX_SP (mctx.__gregs[17]) //%o6
#   elif defined(__OpenBSD__)
// https://github.com/openbsd/src/blob/master/sys/arch/sparc64/include/signal.h
#        define CTX_PC (ucontext->sc_pc)
#        define CTX_SP (ucontext->sc_sp)
#    else
#        error "unknown platform"
#   endif
#else
#    error "unimplemented"
#endif

#ifdef ARCHITECTURE_arm64
#ifdef __APPLE__
inline _STRUCT_ARM_NEON_STATE64* GetFloatingPointState(mcontext_t& host_ctx) {
    return &(host_ctx->__ns);
}
#elif defined(__linux__)
inline fpsimd_context* GetFloatingPointState(mcontext_t& host_ctx) {
    _aarch64_ctx* header = reinterpret_cast<_aarch64_ctx*>(&host_ctx.__reserved);
    while (header->magic != FPSIMD_MAGIC)
        header = reinterpret_cast<_aarch64_ctx*>(reinterpret_cast<char*>(header) + header->size);
    return reinterpret_cast<fpsimd_context*>(header);
}
#endif
#endif
