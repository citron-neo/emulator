// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <boost/unordered/unordered_flat_map.hpp>
#include "dynarmic/common/container/default_hash.h"

// Build-only adapter for the restored pinned submodule. Explicit caller hashers
// are unchanged; all consumers inherit the same header priority from its target.
namespace Common {
template <class Key, class T, class Hash = Dynarmic::Compat::DefaultHash<Key>,
          class Pred = std::equal_to<Key>, class Allocator = std::allocator<std::pair<const Key, T>>>
using unordered_map = boost::unordered::unordered_flat_map<Key, T, Hash, Pred, Allocator>;
} // namespace Common
