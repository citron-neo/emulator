// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core {
class System;
}

namespace Service::SM {
class ServiceManager;
}

namespace Service::TMA {

/// Registers all TMA services with the specified service manager.
void LoopProcess(Core::System& system);

} // namespace Service::TMA