// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace FileSys {

enum class OperationId : s64 {
    FillZero = 0,
    DestroySignature = 1,
    Invalidate = 2,
    QueryRange = 3,
    QueryUnpreparedRange = 4,
    QueryLazyLoadCompletionRate = 5,
    SetLazyLoadPriority = 6,

    ReadLazyLoadFileForciblyForDebug = 10001,
};

} // namespace FileSys
