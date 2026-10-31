// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>
#include <tuple>
#include <utility>
#include "common/container_hash.h"

namespace Dynarmic::Compat {
// Preserve std::hash specializations (notably IR::LocationDescriptor). Standard
// tuples/pairs have no default std::hash; do not specialize std for those types.
template <class Key>
struct DefaultHash : std::hash<Key> {};
template <class... Ts>
struct DefaultHash<std::tuple<Ts...>> : ::Common::TupleHash {};
template <class First, class Second>
struct DefaultHash<std::pair<First, Second>> : ::Common::TupleHash {};
} // namespace Dynarmic::Compat
