// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/result.h"

namespace Service::Nex {

constexpr Result ResultNexNotAvailable{ErrorModule::Nex, 520};
constexpr Result ResultNexConnectionFailed{ErrorModule::Nex, 1};
constexpr Result ResultNexTimeout{ErrorModule::Nex, 2};
constexpr Result ResultNexInvalidState{ErrorModule::Nex, 3};
constexpr Result ResultNexNotInitialized{ErrorModule::Nex, 4};

} // namespace Service::Nex
