// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2022 merryhime
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <type_traits>

namespace mcl {

/// Is type T an instance of template class C?
template<template<class...> class, class>
struct is_instance_of_template : std::false_type {};

template<template<class...> class C, class... As>
struct is_instance_of_template<C, C<As...>> : std::true_type {};

/// Is type T an instance of template class C?
template<template<class...> class C, class T>
constexpr bool is_instance_of_template_v = is_instance_of_template<C, T>::value;

}  // namespace mcl
