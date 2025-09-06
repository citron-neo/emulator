/* SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project */
/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#define __ASSEMBLY__

#include <asm-generic/signal.h>
#include <asm-generic/unistd.h>

#define ReturnToRunCodeByExceptionLevelChangeSignal SIGUSR2
#define BreakFromRunCodeSignal SIGURG
#define GuestAccessFaultSignal SIGSEGV
#define GuestAlignmentFaultSignal SIGBUS

#define GuestContextSp 0xF8
#define GuestContextHostContext 0x320

#define HostContextSpTpidrEl0 0xE0
#define HostContextTpidrEl0 0xE8
#define HostContextRegs 0x0
#define HostContextVregs 0x60

#define TpidrEl0NativeContext 0x10
#define TpidrEl0Lock 0x18
#define TpidrEl0TlsMagic 0x20
#define TlsMagic 0x555a5559

#define SpinLockLocked 0
#define SpinLockUnlocked 1
