// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/ssl/ssl_types.h"

namespace Core {
class System;
}

namespace Service::SSL {

void LoopProcess(Core::System& system);

} // namespace Service::SSL
