// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <iterator>
#include <type_traits>

namespace Common {

// Check if type satisfies the ContiguousContainer named requirement.
template <typename T>
concept IsContiguousContainer = std::contiguous_iterator<typename T::iterator>;

// No equivalent in the stdlib
template <typename T>
concept IsArithmetic = std::is_arithmetic_v<T>;

} // namespace Common
