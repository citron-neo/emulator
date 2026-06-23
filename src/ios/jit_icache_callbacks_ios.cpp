// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Dedicated TU so Oaknut JIT pthread allowlist symbols always link into libcitron_ios.dylib
// (avoids missing symbols if native.cpp is skipped or preprocessor differs in the IDE).

#include <TargetConditionals.h>

#if !TARGET_OS_IPHONE
#    error "jit_icache_callbacks_ios.cpp must only be compiled for iOS (libcitron_ios)."
#endif

#include <cstddef>
#include <libkern/OSCacheControl.h>
#include <pthread/pthread.h>

// Layout must match oaknut::CodeBlock::protect local ctx type.
struct CitronOaknutJitCallbackCtx {
    void* ptr;
    std::size_t nbytes;
};

__attribute__((used)) __attribute__((visibility("default"))) extern "C" int
citron_oaknut_jit_icache_clear(void* ctx)
{
    auto* c = static_cast<CitronOaknutJitCallbackCtx*>(ctx);
    sys_icache_invalidate(c->ptr, c->nbytes);
    return 0;
}

// Legacy name; link dynarmic/oaknut object files and the allowlist may reference either symbol.
__attribute__((used)) __attribute__((visibility("default"))) extern "C" int
citron_oaknut_jit_icache_callback(void* ctx)
{
    auto* c = static_cast<CitronOaknutJitCallbackCtx*>(ctx);
    sys_icache_invalidate(c->ptr, c->nbytes);
    return 0;
}

PTHREAD_JIT_WRITE_ALLOW_CALLBACKS_NP(citron_oaknut_jit_icache_clear, citron_oaknut_jit_icache_callback);
