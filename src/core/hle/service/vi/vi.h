// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/polyfill_thread.h"

namespace Core {
class System;
}

namespace Service::VI {

void LoopProcess(Core::System& system, std::stop_token token);

} // namespace Service::VI
