// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/result.h"

namespace Service::BCAT {

constexpr Result ResultInvalidArgument{ErrorModule::BCAT, 1};
constexpr Result ResultFailedOpenEntity{ErrorModule::BCAT, 2};
constexpr Result ResultEntityAlreadyOpen{ErrorModule::BCAT, 6};
constexpr Result ResultNoOpenEntry{ErrorModule::BCAT, 7};

} // namespace Service::BCAT
