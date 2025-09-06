// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/kernel/k_page_table_base.h"

namespace Kernel {

class KPageTable final : public KPageTableBase {
public:
    explicit KPageTable(KernelCore& kernel) : KPageTableBase(kernel) {}
    ~KPageTable() = default;
};

} // namespace Kernel
