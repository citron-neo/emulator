// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2013 Dolphin Emulator Project
// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cassert>
#include <cstdlib>

#define ASSERT(_a_) assert(_a_)
#define DEBUG_ASSERT(_a_) assert(_a_)
#define UNREACHABLE() std::abort()
