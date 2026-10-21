// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cassert>
#include <utility>

#include "common/logging.h"

// Assertions are the standard assert(): fatal in a build without NDEBUG, compiled out with it.
// Anything the condition has to do for its side effect is done before the assert, in a
// [[maybe_unused]] local, so it still happens in a release build. A message is written as
// assert(condition && "message {}" && (void(arg), true)): the text and the arguments are in the
// failure output and visible in a debugger, and nothing is formatted or printed.
// Code that cannot be reached is std::unreachable().

// Not yet implemented: logged, then execution continues. These mark missing functionality,
// not invariants, so they do not assert.
#define UNIMPLEMENTED() LOG_CRITICAL(Debug, "Unimplemented code!")
#define UNIMPLEMENTED_MSG(...) LOG_CRITICAL(Debug, "Unimplemented code!\n" __VA_ARGS__)

#define UNIMPLEMENTED_IF(cond)                                                                     \
    do {                                                                                           \
        if ((cond)) [[unlikely]] {                                                                 \
            LOG_CRITICAL(Debug, "Unimplemented code!");                                            \
        }                                                                                          \
    } while (0)

#define UNIMPLEMENTED_IF_MSG(cond, ...)                                                            \
    do {                                                                                           \
        if ((cond)) [[unlikely]] {                                                                 \
            LOG_CRITICAL(Debug, "Unimplemented code!\n" __VA_ARGS__);                              \
        }                                                                                          \
    } while (0)

// Assert _a_. When the assert is compiled out and _a_ is false, execute _b_ instead.
#define ASSERT_OR_EXECUTE(_a_, _b_)                                                                \
    do {                                                                                           \
        if (!(_a_)) [[unlikely]] {                                                                 \
            assert(false && #_a_);                                                                 \
            _b_                                                                                    \
        }                                                                                          \
    } while (0)

// As ASSERT_OR_EXECUTE; the message stays in the source as documentation of the fallback.
#define ASSERT_OR_EXECUTE_MSG(_a_, _b_, ...)                                                       \
    do {                                                                                           \
        if (!(_a_)) [[unlikely]] {                                                                 \
            assert(false && #_a_);                                                                 \
            _b_                                                                                    \
        }                                                                                          \
    } while (0)
