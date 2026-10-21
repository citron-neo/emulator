// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <variant>
#include "dynarmic/common/common_types.h"

#include "dynarmic/ir/cond.h"
#include "dynarmic/ir/location_descriptor.h"

namespace Dynarmic::IR {
namespace Term {

/// This terminal instruction returns control to the dispatcher.
/// The dispatcher will use the current cpu state to determine what comes next.
struct ReturnToDispatch {};

/// This terminal instruction jumps to the basic block described by `next` if we have enough
/// cycles remaining. If we do not have enough cycles remaining, we return to the
/// dispatcher, which will return control to the host.
struct LinkBlock {
    explicit LinkBlock(const LocationDescriptor& next_) : next(next_) {}
    LocationDescriptor next;  ///< Location descriptor for next block.
};

/// This terminal instruction jumps to the basic block described by `next` unconditionally.
/// This is an optimization and MUST only be emitted when this is guaranteed not to result
/// in hanging, even in the face of other optimizations. (In practice, this means that only
/// forward jumps to short-ish blocks would use this instruction.)
/// A backend that doesn't support this optimization may choose to implement this exactly
/// as LinkBlock.
struct LinkBlockFast {
    explicit LinkBlockFast(const LocationDescriptor& next_) : next(next_) {}
    LocationDescriptor next;  ///< Location descriptor for next block.
};

/// This terminal instruction checks the top of the Return Stack Buffer against the current
/// location descriptor. If RSB lookup fails, control is returned to the dispatcher.
/// This is an optimization for faster function calls. A backend that doesn't support
/// this optimization or doesn't have a RSB may choose to implement this exactly as
/// ReturnToDispatch.
struct PopRSBHint {};

/// This terminal instruction performs a lookup of the current location descriptor in the
/// fast dispatch lookup table. A backend that doesn't support this optimization may choose
/// to implement this exactly as ReturnToDispatch.
struct FastDispatchHint {};

struct If;
struct CheckBit;
struct CheckHalt;

/// Non recursive kind of terminal
using LeafTerminal = std::variant<
    std::monostate,
    ReturnToDispatch,
    LinkBlock,
    LinkBlockFast,
    PopRSBHint,
    FastDispatchHint
>;

/// A Terminal is the terminal instruction in a MicroBlock.
using Terminal = std::variant<
    std::monostate,
    LeafTerminal,
    If,
    CheckBit,
    CheckHalt
>;

/// This terminal instruction conditionally executes one terminal or another depending
/// on the run-time state of the ARM flags.
struct If {
    explicit If(Cond if_, LeafTerminal then_, LeafTerminal else_) : if_(if_), then_(std::move(then_)), else_(std::move(else_)) {}
    Cond if_;
    LeafTerminal then_;
    LeafTerminal else_;
};

/// This terminal instruction conditionally executes one terminal or another depending
/// on the run-time state of the check bit.
/// then_ is executed if the check bit is non-zero, otherwise else_ is executed.
struct CheckBit {
    explicit CheckBit(LeafTerminal then_, LeafTerminal else_) : then_(std::move(then_)), else_(std::move(else_)) {}
    LeafTerminal then_;
    LeafTerminal else_;
};

/// This terminal instruction checks if a halt was requested. If it wasn't, else_ is
/// executed.
struct CheckHalt {
    explicit CheckHalt(LeafTerminal else_) : else_(std::move(else_)) {}
    LeafTerminal else_;
};

}  // namespace Term

}  // namespace Dynarmic::IR
