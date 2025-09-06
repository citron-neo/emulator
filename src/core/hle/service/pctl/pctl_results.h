// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/result.h"

namespace Service::PCTL {

constexpr Result ResultNoFreeCommunication{ErrorModule::PCTL, 101};
constexpr Result ResultStereoVisionRestricted{ErrorModule::PCTL, 104};
constexpr Result ResultNoCapability{ErrorModule::PCTL, 131};
constexpr Result ResultNoRestrictionEnabled{ErrorModule::PCTL, 181};

} // namespace Service::PCTL
