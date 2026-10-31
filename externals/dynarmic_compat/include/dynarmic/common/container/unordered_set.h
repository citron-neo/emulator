// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <boost/unordered/unordered_flat_set.hpp>
#include "dynarmic/common/container/default_hash.h"

// Build-only adapter for the restored pinned submodule. Explicit caller hashers
// are unchanged; all consumers inherit the same header priority from its target.
namespace Common {
template <class Key, class Hash = Dynarmic::Compat::DefaultHash<Key>,
          class Pred = std::equal_to<Key>, class Allocator = std::allocator<Key>>
using unordered_set = boost::unordered::unordered_flat_set<Key, Hash, Pred, Allocator>;
} // namespace Common
