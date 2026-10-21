// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include <cstddef>

namespace oaknut {
struct CodeGenerator;
struct WReg;
}  // namespace oaknut

namespace Dynarmic::Backend::Arm64 {

class FpsrManager {
public:
    explicit FpsrManager(oaknut::CodeGenerator& code, std::size_t state_fpsr_offset);

    void Spill();
    void Load();
    void Overwrite() { fpsr_loaded = false; }

    void GetFpsr(oaknut::WReg);

private:
    oaknut::CodeGenerator& code;
    std::size_t state_fpsr_offset;
    bool fpsr_loaded = false;
};

}  // namespace Dynarmic::Backend::Arm64
