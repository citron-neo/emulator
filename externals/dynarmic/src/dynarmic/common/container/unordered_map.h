// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/container_hash.h"

#include <boost/unordered/unordered_flat_map.hpp>

namespace Common {

template <class Key, class T, class Hash = std::hash<Key>, class Pred = std::equal_to<Key>,
          class Allocator = std::allocator<std::pair<const Key, T>>>
using unordered_map = boost::unordered::unordered_flat_map<Key, T, Hash, Pred, Allocator>;

}
