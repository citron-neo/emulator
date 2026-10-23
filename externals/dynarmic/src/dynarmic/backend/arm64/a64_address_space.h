// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#include "dynarmic/backend/arm64/address_space.h"
#include "dynarmic/backend/block_range_information.h"
#include "dynarmic/interface/A64/config.h"

namespace Dynarmic::Backend::Arm64 {

struct EmittedBlockInfo;

class A64AddressSpace final : public AddressSpace {
public:
    explicit A64AddressSpace(const A64::UserConfig& conf);

    void GenerateIR(IR::Block& ir_block, IR::LocationDescriptor) const override;

    void InvalidateCacheRanges(const boost::icl::interval_set<u64>& ranges);

protected:
    friend class A64Core;

    void EmitPrelude();
    EmitConfig GetEmitConfig() override;
    void RegisterNewBasicBlock(const IR::Block& block, const EmittedBlockInfo& block_info) override;

    const A64::UserConfig conf;
    BlockRangeInformation<u64> block_ranges;
};

}  // namespace Dynarmic::Backend::Arm64
