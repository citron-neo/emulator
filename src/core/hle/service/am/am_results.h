// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/result.h"

namespace Service::AM {

constexpr Result ResultNoDataInChannel{ErrorModule::AM, 2};
constexpr Result ResultNoMessages{ErrorModule::AM, 3};
constexpr Result ResultLibraryAppletTerminated{ErrorModule::AM, 22};
constexpr Result ResultInvalidOffset{ErrorModule::AM, 503};
constexpr Result ResultInvalidStorageType{ErrorModule::AM, 511};
constexpr Result ResultFatalSectionCountImbalance{ErrorModule::AM, 512};

} // namespace Service::AM
